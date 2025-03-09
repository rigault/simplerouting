#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "eccodes.h"
#include <glib.h>
#include <locale.h>
#include "rtypes.h"
#include "rutil.h"
#include "engine.h"
#include "grib.h"
#include "polar.h"
#include "inline.h"
#include <sys/stat.h>
#include <fcntl.h>

#define SYNOPSYS               "<port> [<parameter file>]"
#define MAX_SIZE_REQUEST       2048        // Max size from request client
#define MAX_SIZE_RESOURCE_NAME 256         // Max size polar or grib name

const char *filter[] = {".csv", ".pol", ".grb", ".grb2", NULL}; // global fiter for REQ_DIR request

enum {REQ_TEST, REQ_ROUTING, REQ_BEST_DEP, REQ_RACE, REQ_POLAR, REQ_GRIB, REQ_DIR, REQ_PAR, REQ_INIT}; // type of request

char parameterFileName [MAX_SIZE_FILE_NAME];

/*! Client Request description */
typedef struct {
   int type;                                 // type of request
   int cogStep;                              // step of cog in degrees
   int rangeCog;                             // range of cog from x - RANGE_GOG, x + RAGE_COG+1
   int jFactor;                              // factor for target point distance used in sectorOptimize
   int kFactor;                              // factor for target point distance used in sectorOptimize
   int nSectors;                             // number of sector for optimization by sector
   int penalty0;                             // penalty in minutes for tack
   int penalty1;                             // penalty in minutes fot Gybe
   int penalty2;                             // penalty in minutes for sail change
   int timeStep;                             // isoc time step in seconds
   int timeInterval;                         // for REQ_BEST_DEP time interval in seconds between each try
   time_t epochStart;                        // epoch time to start routing
   time_t timeWindow;                        // time window in seconds for bestTimeDeparture
   bool isoc;                                // true if isochrones requested
   bool sortByName;                          // true if directory should be sorted by name, false if sorted by modif date
   bool forbid;                              // true if forbid zone (polygons or Earth) are considered
   double staminaVR;                         // Init stamina
   double motorSpeed;                        // motor speed if used
   double threshold;                         // threshold for motor use
   double nightEfficiency;                   // efficiency of team at night
   double dayEfficiency;                     // efficiency of team at day
   double xWind;                             // multiply factor for wind
   double maxWind;                           // max Wind supported
   int nBoats;                               // number of boats
   struct {
      char name [MAX_SIZE_NAME];             // name of the boat
      double lat;                            // latitude
      double lon;                            // longitude
   } boats [MAX_N_COMPETITORS];              // boats
   int nWp;                                  // number of waypoints
   struct {
      double lat;                            // latitude
      double lon;                            // longitude
   } wp [MAX_N_WAY_POINT];                   // way points
   char polarName [MAX_SIZE_RESOURCE_NAME];  // polar file name
   char gribName  [MAX_SIZE_RESOURCE_NAME];  // grib file name
   char dirName   [MAX_SIZE_RESOURCE_NAME];  // remote directory name
} ClientRequest;

/*! Structure to store file information. */
typedef struct {
   char *name;
   off_t size;
   time_t mtime;
} FileInfo;

/*! Comparator to sort by name (ascending order) */
static gint compareByName (gconstpointer a, gconstpointer b) {
   const FileInfo *fa = a;
   const FileInfo *fb = b;
   return g_strcmp0(fa->name, fb->name);
}

/*! Comparator to sort by modification date (most recent first) */
static gint compareByMtime (gconstpointer a, gconstpointer b) {
   const FileInfo *fa = a;
   const FileInfo *fb = b;
   if (fa->mtime < fb->mtime)
      return 1;
   else if (fa->mtime > fb->mtime)
      return -1;
   else
      return 0;
}

/*! Checks if the filename matches one of the suffixes in the filter */
static gboolean matchFilter (const char *filename, const char **filter) {
   if (filter == NULL)
      return TRUE;
   for (int i = 0; filter[i] != NULL; i++) {
      if (g_str_has_suffix (filename, filter[i]))
         return TRUE;
   }
   return FALSE;
}

/*!
 * Function: listDirToJson
 * -----------------------
 * Lists the regular files in the directory constructed from root/dir,
 * applies a suffix filter if provided, sorts the list either by name or by
 * modification date (most recent first), and generates a JSON string containing
 * an array of arrays in the format [filename, size, modification date].
 *
 * In case of an error (e.g., unable to open the directory), the error is printed
 * to the console and a corresponding JSON error response is returned.
 */
GString *listDirToJson (char *root, char *dir, bool sortByName, const char **filter) {
   GString *json = g_string_new("");
   GError *error = NULL;

   // Build the full directory path.
   gchar *full_path = g_build_filename(root, dir, NULL);
   GDir *gdir = g_dir_open(full_path, 0, &error);
   if (!gdir) {
      fprintf (stderr, "In listDirToJson Error opening directory '%s': %s", full_path, error->message);
      g_string_assign (json, "{\"error\": \"Error opening directory\"}");
      g_error_free (error);
      g_free (full_path);
      return json;
   }

   // List to store each file's information.
   GList *files_list = NULL;
   const gchar *filename;
   while ((filename = g_dir_read_name(gdir)) != NULL) {
      // Apply the suffix filter.
      if (!matchFilter (filename, filter))
         continue;

      // Build the full file path.
      gchar *file_path = g_build_filename (full_path, filename, NULL);
      struct stat st;
      if (stat (file_path, &st) != 0) {
         fprintf (stderr, "In listDirToJson Error retrieving information for '%s'", file_path);
         g_free (file_path);
         continue;
      }
      // Process only regular files.
      if (!S_ISREG (st.st_mode)) {
         g_free (file_path);
         continue;
      }
      // Allocate and fill the FileInfo structure.
      FileInfo *info = g_new (FileInfo, 1);
      info->name = g_strdup (filename);
      info->size = st.st_size;
      info->mtime = st.st_mtime;
      files_list = g_list_append (files_list, info);
      g_free (file_path);
   }
   g_dir_close (gdir);
   g_free (full_path);

   // Sort the list based on the requested criteria.
   if (sortByName)
      files_list = g_list_sort (files_list, compareByName);
   else
      files_list = g_list_sort (files_list, compareByMtime);

   // Construct the JSON string.
   g_string_assign (json, "[\n");
   for (GList *l = files_list; l != NULL; l = l->next) {
      FileInfo *info = (FileInfo *)l->data;
      // Convert the modification date to a string (format "YYYY-MM-DD HH:MM:SS").
      struct tm *tm_info = localtime (&(info->mtime));
      char time_str [20];
      strftime (time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
      // Escape the filename for JSON.
      char *escaped_name = g_strescape (info->name, NULL);
      // Add a JSON array for this file.
      g_string_append_printf (json, "   [\"%s\", %ld, \"%s\"]", escaped_name, info->size, time_str);
      g_free (escaped_name);
      if (l->next != NULL)
         g_string_append (json, ",\n");
   }
   g_string_append(json, "\n]\n");

   // Free allocated memory.
   for (GList *l = files_list; l != NULL; l = l->next) {
      FileInfo *info = (FileInfo *)l->data;
      g_free (info->name);
      g_free (info);
   }
   g_list_free (files_list);

   return json;
}

/*! Make initialization  
   return false if readParam or readGribAll fail */
static bool initRouting (char *parameterFileName) {
   char directory [MAX_SIZE_DIR_NAME];
   char str [MAX_SIZE_LINE];
   char errMessage [MAX_SIZE_TEXT] = "";
   bool readGribRet;
   char sailPolFileName [MAX_SIZE_NAME] = "";

   if (! readParam (parameterFileName)) {
      fprintf (stderr, "In initRouting, Error readParam: %s\n", parameterFileName);
      return false;
   }
   printf ("Parameters File: %s\n", parameterFileName);
   if (par.mostRecentGrib) {  // most recent grib will replace existing grib
      snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir); 
      mostRecentFile (directory, ".gr", par.gribFileName, sizeof (par.gribFileName));
   }
   if (par.gribFileName [0] != '\0') {
      readGribRet = readGribAll (par.gribFileName, &zone, WIND);
      if (! readGribRet) {
         fprintf (stderr, "In initRouting, Error: Unable to read grib file: %s\n ", par.gribFileName);
         return false;
      }
      printf ("Grib loaded    : %s\n", par.gribFileName);
      printf ("Grib DateTime0 : %s\n", gribDateTimeToStr (zone.dataDate [0], zone.dataTime [0], str, sizeof (str)));
   }

   if (par.currentGribFileName [0] != '\0') {
      readGribRet = readGribAll (par.currentGribFileName, &zone, WIND);
      printf ("Cur grib loaded: %s\n", par.currentGribFileName);
      printf ("Grib DateTime0 : %s\n", gribDateTimeToStr (currentZone.dataDate [0], currentZone.dataTime [0], str, sizeof (str)));
   }
   if (readPolar (true, par.polarFileName, &polMat, errMessage, sizeof (errMessage))) {
      printf ("Polar loaded   : %s\n", par.polarFileName);
      newFileNameSuffix (par.polarFileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
      if (readPolar (false, sailPolFileName, &sailPolMat, errMessage, sizeof (errMessage)))
         printf ("Sail Pol.loaded: %s\n", sailPolFileName);
   }
   else
      fprintf (stderr, "In initRouting, Error readPolar: %s\n", errMessage);
      
   if (readPolar (true, par.wavePolFileName, &wavePolMat, errMessage, sizeof (errMessage)))
      printf ("Polar loaded   : %s\n", par.wavePolFileName);
   else
      fprintf (stderr, "In initRouting, Error readPolar: %s\n", errMessage);
  
   printf ("par.web        : %s\n", par.web);
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   if (par.isSeaFileName [0] != '\0')
      readIsSea (par.isSeaFileName);
   updateIsSeaWithForbiddenAreas ();
   return true;
}

/*! date for logging */
static const char* getCurrentDate () {
   static char date_buffer[100];
   time_t now = time (NULL);
   struct tm *tm_info = gmtime(&now);
   strftime (date_buffer, sizeof(date_buffer), "%Y-%m-%d %H:%M:%S UTC", tm_info);
   return date_buffer;
}

/*! log client Request */
static void logRequest (const char* fileName, const char *remote_addr, const char *user_agent, ClientRequest *client) {
   const char *date = getCurrentDate();
   FILE *logFile = fopen (fileName, "a");
   if (logFile == NULL) {
      fprintf (stderr, "In logRequest, Error opening log file: %s\n", fileName);
      return;
   }
   fprintf (logFile, "%s; %s; %s; %d\n", date, remote_addr, user_agent, client->type);
   fclose (logFile);
}

/*! decode request from client and fill ClientRequest structure 
   return true if correct false if impossible to decode */
static bool decodeHttpReq (const char *req, ClientRequest *clientReq) {
   memset (clientReq, 0, sizeof (ClientRequest));
   clientReq->type = 1;              // default: routingLaunch   
   clientReq->timeStep = 3600;   
   clientReq->timeInterval = 3600;
   clientReq->cogStep = 5;
   clientReq->rangeCog = 90;
   clientReq->kFactor = 1;
   clientReq->nSectors = 720;
   clientReq->staminaVR = 100.0;
   clientReq->motorSpeed = 6.0;
   clientReq->nightEfficiency = 1.0;
   clientReq->dayEfficiency = 1.0;
   clientReq->xWind = 1.0;
   clientReq->maxWind = 100.0;

   char **parts = g_strsplit (req, "&", -1);

   for (int i = 0; parts[i]; i++) {
      printf ("part: %s\n", parts [i]);
      if (sscanf(parts[i], "type=%d", &clientReq->type) == 1);// type extraction
      else if (g_str_has_prefix(parts[i], "boat=")) {
         char *boats_part = parts[i] + strlen ("boat="); // After boats=="
         if (*boats_part == '\0') { 
            g_strfreev(parts);
            return false;  // parsing error
         }
         // waypoints parsing
         char **boats_coords = g_strsplit (boats_part, ";", -1);
         for (int i = 0; boats_coords[i] && clientReq->nBoats < MAX_N_COMPETITORS; i++) {
            char name [MAX_SIZE_NAME];
            double lat, lon;
            if (sscanf(boats_coords[i], "%63[^,], %lf, %lf", name, &lat, &lon) == 3) {
               g_strlcpy (clientReq->boats [clientReq->nBoats].name, name, MAX_SIZE_NAME);
               clientReq->boats [clientReq->nBoats].lat = lat;
               clientReq->boats [clientReq->nBoats].lon = lon;
               clientReq->nBoats += 1;
            }
         }
         g_strfreev(boats_coords);
      }
      else if (g_str_has_prefix(parts[i], "waypoints=")) {
         char *waypoints_part = parts[i] + strlen ("waypoints="); // Partie après "waypoints="
         if (*waypoints_part == '\0') {
            g_strfreev(parts);
            return false;  // parsing error
         }
         // waypoints parsing
         char **wp_coords = g_strsplit (waypoints_part, ";", -1);
         for (int i = 0; wp_coords[i] && clientReq->nWp < MAX_N_WAY_POINT; i++) {
            double lat, lon;
            if (sscanf(wp_coords[i], "%lf,%lf", &lat, &lon) == 2) {
               clientReq->wp [clientReq->nWp].lat = lat;
               clientReq->wp [clientReq->nWp].lon = lon;
               clientReq->nWp += 1;
            }
         }
         g_strfreev(wp_coords);
      }

      else if (sscanf (parts[i], "timeStep=%d", &clientReq->timeStep) == 1);                    // time step extraction
      else if (sscanf (parts[i], "cogStep=%d",  &clientReq->cogStep) == 1);                     // cog step extraction
      else if (sscanf (parts[i], "rangeCog=%d", &clientReq->rangeCog) == 1);                    // range sog  extraction
      else if (sscanf (parts[i], "jFactor=%d",  &clientReq->jFactor) == 1);                     // jFactor extraction
      else if (sscanf (parts[i], "kFactor=%d",  &clientReq->kFactor) == 1);                     // kFactor extraction
      else if (sscanf (parts[i], "nSectors=%d", &clientReq->nSectors) == 1);                    // nSectors extraction
      else if (sscanf (parts[i], "penalty0=%d", &clientReq->penalty0) == 1);                    // penalty0 extraction
      else if (sscanf (parts[i], "penalty1=%d", &clientReq->penalty1) == 1);                    // penalty1 extraction
      else if (sscanf (parts[i], "penalty2=%d", &clientReq->penalty2) == 1);                    // penalty2 (sail change)  extraction
      else if (sscanf (parts[i], "timeInterval=%d", &clientReq->timeInterval) == 1);            // time Window extraction
      else if (sscanf (parts[i], "timeStart=%ld",  &clientReq->epochStart) == 1);               // time startextraction
      else if (sscanf (parts[i], "timeWindow=%ld", &clientReq->timeWindow) == 1);               // time Window extraction
      else if (sscanf (parts[i], "polar=%255s", clientReq->polarName) == 1);                    // polar name
      else if (sscanf (parts[i], "grib=%255s",  clientReq->gribName) == 1);                     // grib name
      else if (sscanf (parts[i], "dir=%255s",   clientReq->dirName) == 1);                      // directory name
      else if (g_str_has_prefix (parts[i], "isoc=true")) clientReq->isoc = true;                // Default false
      else if (g_str_has_prefix (parts[i], "isoc=false")) clientReq->isoc = false;              // Default false
      else if (g_str_has_prefix (parts[i], "forbid=true")) clientReq->forbid = true;            // Default false
      else if (g_str_has_prefix (parts[i], "forbid=false")) clientReq->forbid = false;          // Default false
      else if (g_str_has_prefix (parts[i], "sortByName=true")) clientReq->sortByName = true;    // Default false
      else if (g_str_has_prefix (parts[i], "sortByName=false")) clientReq->sortByName = false;  // Default false
      else if (sscanf (parts[i], "staminaVR=%lf",        &clientReq->staminaVR) == 1);          // stamina Virtual Regatta
      else if (sscanf (parts[i], "motorSpeed=%lf",       &clientReq->motorSpeed) == 1);         // motor speed
      else if (sscanf (parts[i], "threshold=%lf",         &clientReq->threshold) == 1);         // threshold for motoe
      else if (sscanf (parts[i], "nightEfficiency=%lf",  &clientReq->nightEfficiency) == 1);    // efficiency at night
      else if (sscanf (parts[i], "dayEfficiency=%lf",    &clientReq->dayEfficiency) == 1);      // efficiency daylight
      else if (sscanf (parts[i], "xWind=%lf",            &clientReq->xWind) == 1);              // xWind factor
      else if (sscanf (parts[i], "maxWind=%lf",          &clientReq->maxWind) == 1);            // max Wind
      else printf ("Unknown value: %s\n", parts [i]);
   }
   g_strfreev(parts);
   
   return true;  
}


/*! check validity of parameters */
static bool checkParamAndUpdate (ClientRequest *clientReq, char *checkMessage, size_t maxLen) {
   char strPolar [MAX_SIZE_FILE_NAME];
   char strGrib [MAX_SIZE_FILE_NAME];
   char sailPolFileName [MAX_SIZE_NAME] = "";
   char errMessage [MAX_SIZE_TEXT] = "";
   // printf ("startInfo after: %s, startTime: %lf\n", asctime (&startInfo), par.startTimeInHours);
   if ((clientReq->nBoats == 0) || (clientReq->nWp == 0)) {
      snprintf (checkMessage, maxLen, "\"1: No boats or no Waypoints\"");
      return false;
   }
   par.allwaysSea = !clientReq->forbid;
   par.cogStep = clientReq->cogStep;
   par.rangeCog = clientReq->rangeCog;
   par.jFactor = clientReq->jFactor; 
   par.kFactor = clientReq->kFactor; 
   par.nSectors = clientReq->nSectors; 
   par.penalty0 = clientReq->penalty0; 
   par.penalty1 = clientReq->penalty1; 
   par.penalty2 = clientReq->penalty2; 
   par.motorSpeed = clientReq->motorSpeed;
   par.threshold = clientReq->threshold;
   par.nightEfficiency = clientReq->nightEfficiency;
   par.dayEfficiency = clientReq->dayEfficiency;
   par.xWind = clientReq->xWind;
   par.maxWind = clientReq->maxWind;

   // change polar if requested
   if (clientReq->polarName [0] != '\0') {
      buildRootName (clientReq->polarName, strPolar, sizeof (strPolar));
      printf ("polar found: %s\n", strPolar);
      if (strncmp (par.polarFileName, strPolar, strlen (strPolar)) != 0) {
         printf ("readPolar: %s\n", strPolar);
         if (readPolar (false, strPolar, &polMat, checkMessage, maxLen)) {
            g_strlcpy (par.polarFileName, strPolar, sizeof (par.polarFileName));
            printf ("Polar loaded   : %s\n", strPolar);
            newFileNameSuffix (par.polarFileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
            if (readPolar (false, sailPolFileName, &sailPolMat, errMessage, sizeof (errMessage)))
               printf ("Sail Pol.loaded: %s\n", sailPolFileName);
         }
         else {
            snprintf (checkMessage, maxLen, "\"2: Error reading Polar: %s\"", clientReq->polarName);
            return false;
         }
      }  
   }
   // change grib if requested MAY TAKE TIME !!!!
   if (clientReq->gribName [0] != '\0') {
      buildRootName (clientReq->gribName, strGrib, sizeof (strGrib));
      printf ("grib found: %s\n", strGrib);
      if (strncmp (par.gribFileName, strGrib, strlen (strGrib)) != 0) {
         printf ("readGrib: %s\n", strGrib);
         if (readGribAll (strGrib, &zone, WIND)) {
            g_strlcpy (par.gribFileName, strGrib, sizeof (par.gribFileName));
            printf ("Grib loaded   : %s\n", strGrib);
         }
         else {
            snprintf (checkMessage, maxLen, "\"3: Error reading Grib: %s\"", clientReq->gribName);
            return false;
         }
      }  
   }

   if (clientReq->epochStart <= 0)
       clientReq->epochStart = time (NULL); // default value if empty is now
   time_t theTime0 = gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]);
   par.startTimeInHours = (clientReq->epochStart - theTime0) / 3600.0;
   printf ("Start Time Epoch: %ld, theTime0: %ld\n", clientReq->epochStart, theTime0);
   printf ("Start Time in Hours after Grib: %.2lf\n", par.startTimeInHours);

   competitors.n = clientReq->nBoats;
   for (int i = 0; i < clientReq->nBoats; i += 1) {
      g_strlcpy (competitors.t [i].name, clientReq -> boats [i].name, MAX_SIZE_NAME);
      printf ("competitor name: %s\n", competitors.t [i].name);
      competitors.t [i].lat = clientReq -> boats [i].lat;
      competitors.t [i].lon = clientReq -> boats [i].lon;
   }
 
   par.pOr.lat = clientReq->boats [0].lat;
   par.pOr.lon = clientReq->boats [0].lon;
   par.pDest.lat = clientReq->wp [clientReq->nWp-1].lat;
   par.pDest.lon = clientReq->wp [clientReq->nWp-1].lon;

   for (int i = 0; i < clientReq->nWp -1; i += 1) {
      wayPoints.t[i].lat = clientReq->wp [i].lat; 
      wayPoints.t[i].lon = clientReq->wp [i].lon; 
   }
   wayPoints.n = clientReq->nWp - 1;

   if ((par.startTimeInHours < 0) || (par.startTimeInHours > zone.timeStamp [zone.nTimeStamp -1])) {
         snprintf (checkMessage, maxLen, "\"4: start Time not in Grib time window\"");
      return false;
   }
   
   gchar *gribBaseName = g_path_get_basename (par.gribFileName);

   if (! isInZone (par.pOr.lat, par.pOr.lon, &zone) && (par.constWindTws == 0)) { 
      snprintf (checkMessage, maxLen, "\"5: Origin point not in Grib wind zone.\",\n\
\"grib\": \"%s\", \"bottomLat\": %.2lf, \"leftLon\": %.2lf, \"topLat\": %.2lf, \"rightLon\": %.2lf\n",
               gribBaseName, zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight);
      g_free (gribBaseName);
      return false;
   }
   if (! isInZone (par.pDest.lat, par.pDest.lon, &zone) && (par.constWindTws == 0)) {
      snprintf (checkMessage, maxLen, "\"6: Destination point not Grib in wind zone.\",\n\
\"grib\": \"%s\", \"bottomLat\": %.2lf, \"leftLon\": %.2lf, \"topLat\": %.2lf, \"rightLon\": %.2lf\n",
               gribBaseName, zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight);
      g_free (gribBaseName);
      return false;
   }

   par.tStep = clientReq->timeStep / 3600.0;

   // specific for bestTimeDeparture
   chooseDeparture.count = 0;
   chooseDeparture.tInterval = clientReq->timeInterval / 3600.0;
   chooseDeparture.tBegin = par.startTimeInHours;
   if (clientReq->timeWindow > 0)
      chooseDeparture.tEnd = chooseDeparture.tBegin + (clientReq->timeWindow / 3600.0);
   else
      chooseDeparture.tEnd = INT_MAX; // all grib window Grib time will be used.

   return true;
}

/*! Translate extension in MIME type */
static const char *getMimeType (const char *path) {
   const char *ext = strrchr(path, '.');
   if (!ext) return "application/octet-stream";
   if (strcmp(ext, ".html") == 0) return "text/html";
   if (strcmp(ext, ".css") == 0) return "text/css";
   if (strcmp(ext, ".js") == 0) return "application/javascript";
   if (strcmp(ext, ".png") == 0) return "image/png";
   if (strcmp(ext, ".jpg") == 0) return "image/jpeg";
   if (strcmp(ext, ".gif") == 0) return "image/gif";
   if (strcmp(ext, ".txt") == 0) return "text/plain";
   return "application/octet-stream";
}

/*! serve static file */
static void serveStaticFile (int client_socket, const char *requested_path) {
   char filepath [512];
   snprintf (filepath, sizeof(filepath), "%s%s", par.web, requested_path);
   printf ("File Path: %s\n", filepath);

   // Check if file exist
   struct stat st;
   if (stat (filepath, &st) == -1 || S_ISDIR(st.st_mode)) {
      const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
      send(client_socket, not_found, strlen(not_found), 0);
      return;
   }

   // File open
   int file = open (filepath, O_RDONLY);
   if (file == -1) {
      const char *error = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 21\r\n\r\n500 Internal Server Error";
      send (client_socket, error, strlen(error), 0);
      return;
   }

   // send HTTP header and MIME type
   char header [256];
   snprintf (header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n",
          getMimeType(filepath), st.st_size);
   send (client_socket, header, strlen(header), 0);

   // Read end send file
   char buffer [1024];
   ssize_t bytes_read;
   while ((bytes_read = read(file, buffer, sizeof(buffer))) > 0) {
      send(client_socket, buffer, bytes_read, 0);
   }
   close(file);
}

/*! launch action and retuen GString after execution */
static GString *launchAction (ClientRequest *clientReq) {
   GString *res = g_string_new ("");
   GString *polString;
   GString *legendString = g_string_new ("");
   char tempFileName [MAX_SIZE_FILE_NAME];
   char checkMessage [MAX_SIZE_TEXT];
   char sailPolFileName [MAX_SIZE_NAME] = "";
   printf ("client.req = %d\n", clientReq->type);
   switch (clientReq->type) {
   case REQ_TEST:
      g_string_append_printf (res, "{\"Prog-version\": \"%s, %s, %s\",\n", PROG_NAME, PROG_VERSION, PROG_AUTHOR);
      g_string_append_printf (res, "\" Compilation-date\": \"%s\",\n", __DATE__);
      g_string_append_printf (res, "\" GLIB-version\": \"%d.%d.%d\", \n \"ECCODES-version\": \"%s\"\n}\n",
            GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION, ECCODES_VERSION_STR);
      break;
   case REQ_ROUTING:
      if (checkParamAndUpdate (clientReq, checkMessage, sizeof (checkMessage))) {
         routingLaunch ();
         GString *jsonRoute = routeToJson (&route, 0, clientReq->isoc); // only most recent route with isochrones 

   
         //GString *jsonRoute = allCompetitorsToJson (0, clientReq->isoc);
         g_string_append_printf (res, "{\n%s}\n", jsonRoute->str);
         g_string_free (jsonRoute, TRUE);
      }
      else {
         g_string_append_printf (res,"{\"_Error\":\n%s\n}\n", checkMessage);
      }
      break;
   case REQ_BEST_DEP:
      if (checkParamAndUpdate (clientReq, checkMessage, sizeof (checkMessage))) {
         printf ("Launch bestTimeDesparture\n");
         printf ("begin: %d, end: %d\n", chooseDeparture.tBegin, chooseDeparture.tEnd);
         bestTimeDeparture (); // ATT not terminated
         GString *bestTimeReport = bestTimeReportToJson (&chooseDeparture, clientReq->isoc);
         g_string_append_printf (res, "%s", bestTimeReport->str);
         g_string_free (bestTimeReport, TRUE);
      }
      else {
         g_string_append_printf (res,"{\"_Error\":\n%s\n}\n", checkMessage);
      }
      break;
   case REQ_RACE:
      if (checkParamAndUpdate (clientReq, checkMessage, sizeof (checkMessage))) {
         printf ("Launch AllCompetitors\n");
         allCompetitors (); // ATT not terminated
         GString *jsonRoutes = allCompetitorsToJson (competitors.n, clientReq->isoc);
         g_string_append_printf (res, "%s", jsonRoutes->str);
         g_string_free (jsonRoutes, TRUE);
      }
      else {
         g_string_append_printf (res,"{\"_Error\":\n%s\n}\n", checkMessage);
      }
      break;
   case REQ_POLAR:
      if (clientReq->polarName [0] != '\0') {
         polString = polToJson (clientReq->polarName, "polarName");
         newFileNameSuffix (clientReq->polarName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
      }
      else {
         polString = polToJson (par.polarFileName, "polarName");
         newFileNameSuffix (par.polarFileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
      }
      GString *sailString = polToJson (sailPolFileName, "sailName");
      if (g_str_has_prefix (sailString->str, "{}"))
         g_string_assign (legendString, "{}");
      else 
         legendString = sailLegendToJson (sailName, colorStr, MAX_N_SAIL);
      g_string_append_printf (res, "[%s, %s, %s]\n", polString->str, sailString->str, legendString->str);
      g_string_free (legendString, TRUE);
      g_string_free (sailString, TRUE);
      g_string_free (polString, TRUE);
      break;
   case REQ_GRIB:
      res = gribToJson (clientReq->gribName);
      break;
   case REQ_DIR:
      res = listDirToJson (par.workingDir, clientReq->dirName, clientReq->sortByName, filter);
      break;
   case REQ_PAR:
      writeParam (buildRootName (TEMP_FILE_NAME, tempFileName, sizeof (tempFileName)), false, false);
      GError *error = NULL;
      char *content = NULL;
      gsize length;
      if (g_file_get_contents (tempFileName, &content, &length, &error)) {
         g_string_append_printf (res, "%s", content);
         g_free (content);
      }
      else {
         g_string_append_printf (res, "{\"_Error\": \"%s\"}\n", error->message);
         g_clear_error (&error);
      }
      break;
   case REQ_INIT:
      if (! initRouting (parameterFileName))
         g_string_append_printf (res, "{\"_Error\": \"%s\"}\n", error->message);
      else
         g_string_append_printf (res, "{\"_Message\": \"%s\"}\n", "Init done");
      break;
   default:;
   }
   return res;
}

/*! Handle client connection and launch actions */
static void handleClient (int clientFd, struct sockaddr_in *client_addr) {
   char saveBuffer [MAX_SIZE_REQUEST];
   char buffer [MAX_SIZE_REQUEST];
   const char *user_agent = "Unknown";
   ClientRequest clientReq;

   // read HTTP request
   int bytes_read = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
   if (bytes_read <= 0) {
     return;
   }
   buffer [bytes_read] = '\0'; // terminate string
   printf ("Client Request: %s\n", buffer);
   g_strlcpy (saveBuffer, buffer, MAX_SIZE_BUFFER);

   // Extract HTTP first line request
   char *request_line = strtok(buffer, "\r\n");
   if (!request_line) {
     return;
   }
   printf ("request line: %s\n", request_line);

   // check if Rest API (POST) or static file (GET)
   if (strncmp(request_line, "POST", 4) != 0) {
      printf ("Static file\n");
      // static file
      const char *requested_path = strchr (request_line, ' '); // space after "GET"
      if (!requested_path) {
        return;
      }
      requested_path++; // Pass space

      char *end_path = strchr (requested_path, ' ');
        if (end_path) {
      *end_path = '\0'; // Terminate string
      }

      if (strcmp(requested_path, "/") == 0) {
         requested_path = "/index.html"; // Default page
      }

      serveStaticFile (clientFd, requested_path);
      return; // stop
   }
   printf ("Rest API\n");

   // Extract request body
   char *post_data = strstr(saveBuffer, "\r\n\r\n");
   if (post_data == NULL) {
      return;
   }
   // Extract user agent
   char *user_agent_start = strstr (saveBuffer, "User-Agent: ");
   if (user_agent_start) {
      user_agent_start += strlen ("User-Agent: ");
      char *user_agent_end = strstr (user_agent_start, "\r\n");
      if (user_agent_end) {
         *user_agent_end = '\0';
         user_agent = user_agent_start;
      }
   }

   // Get client IP address
   char remote_addr [INET_ADDRSTRLEN];
   inet_ntop (AF_INET, &(client_addr->sin_addr), remote_addr, INET_ADDRSTRLEN);

   post_data += 4; // Ignore HTTP request separators

   bool ok = decodeHttpReq (post_data, &clientReq);
   printf ("Found: %d, timeStep=%d, timeStart: %ld\n", clientReq.nWp, clientReq.timeStep, clientReq.epochStart);
   for (int i = 0; i < clientReq.nWp; i += 1)
      printf ("WP: %.2lf, %.2lf\n", clientReq.wp [i].lat, clientReq.wp[i].lon);

   // Log req info
   logRequest (par.logFileName, remote_addr, user_agent, &clientReq);

   if (! ok) {
      const char *error_response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nError";
      fprintf (stderr, "In handleClient, Error: %s\n", error_response);
      send (clientFd, error_response, strlen(error_response), 0);
      return;
   }
   GString *res = launchAction (&clientReq);
   const char *cors_headers = "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n";
   GString *response = g_string_new ("");

   g_string_append_printf (response,
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "%s"
      "Content-Length: %zu\r\n"
      "\r\n"
      "%s",
      cors_headers, strlen(res->str), res->str);
   g_string_free (res, TRUE);
   printf ("response; %s\n", response->str);
   send (clientFd, response->str, strlen(response->str), 0);
   g_string_free (response, TRUE);
}

int main (int argc, char *argv[]) {
   int serverFd, clientFd;
   struct sockaddr_in address;
   int addrlen = sizeof (address);
   int serverPort;
   gint64 start = g_get_monotonic_time (); 

   if (setlocale (LC_ALL, "C") == NULL) {                // very important for printf decimal numbers
      fprintf (stderr, "Server Error: setlocale failed");
      return EXIT_FAILURE;
   }

   if (argc <= 1 || argc > 3) {
      fprintf (stderr, "Synopsys: %s %s\n", argv [0], SYNOPSYS);
      return EXIT_FAILURE;
   }
   serverPort = atoi (argv [1]);
   if (serverPort < 80 || serverPort > 9000) {
      fprintf (stderr, "Error: port server not in range\n");
      return EXIT_FAILURE;
   }

   if (argc > 2)
      g_strlcpy (parameterFileName, argv [2], sizeof (parameterFileName));
   else 
      g_strlcpy (parameterFileName, PARAMETERS_FILE, sizeof (parameterFileName));

   if (! initRouting (parameterFileName))
      return EXIT_FAILURE;

   // Socket 
   if ((serverFd = socket (AF_INET, SOCK_STREAM, 0)) == 0) {
      perror ("In main, Error creating socket");
      return EXIT_FAILURE;
   }

   // Define server parameters address port
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons (serverPort);

   // Bind socket with port
   if (bind (serverFd, (struct sockaddr *)&address, sizeof(address)) < 0) {
      perror ("In main, Error socket bind"); 
      return EXIT_FAILURE;
   }

   // Listen connexions
   if (listen (serverFd, 3) < 0) {
      perror ("In main: error listening");
      return EXIT_FAILURE;
   }
   double elapsed = (g_get_monotonic_time () - start) / 1e6; 
   printf ("✅ Loaded in...: %.2lf seconds. Server listen on port: %d\n", elapsed, serverPort);

   while (true) {
      // Accept connexion
      if ((clientFd = accept (serverFd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
         perror ("In main: Error accept");
         exit (EXIT_FAILURE);
      }
      handleClient (clientFd, &address);

      // Close connexion
      close (clientFd);
   }
   return EXIT_SUCCESS;
}

