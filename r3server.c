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
#include "r3util.h"
#include "engine.h"
#include "grib.h"
#include "polar.h"
#include "inline.h"
#include "mailutil.h"
#include <curl/curl.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SYNOPSYS               "<port> [<parameter file>]"
#define MAX_SIZE_REQUEST       2048        // Max size from request client
#define MAX_SIZE_RESOURCE_NAME 256         // Max size polar or grib name
#define PATTERN                "GFS"
#define MAX_SIZE_FEED_BACK     1024
#define FEED_BACK_FILE_NAME    "feedback.log"
#define FEED_BACK_OBJECT       "rCubeFeedBack"

const char *filter[] = {".csv", ".pol", ".grb", ".grb2", ".log", ".txt", ".par", NULL}; // global filter for REQ_DIR request

enum {REQ_KILL = -1793, REQ_TEST = 0, REQ_ROUTING = 1, REQ_BEST_DEP = 2, REQ_RACE = 3, REQ_POLAR = 4, 
      REQ_GRIB = 5, REQ_DIR = 6, REQ_PAR_RAW = 7, REQ_PAR_JSON = 8, 
      REQ_INIT = 9, REQ_FEEDBACK = 10, REQ_DUMP_FILE = 11}; // type of request

char parameterFileName [MAX_SIZE_FILE_NAME];

/*! Client Request description */
typedef struct {
   int type;                                 // type of request
   int cogStep;                              // step of cog in degrees
   int rangeCog;                             // range of cog from x - RANGE_GOG, x + RAGE_COG+1
   int jFactor;                              // factor for target point distance used in sectorOptimize
   int kFactor;                              // factor for target point distance used in sectorOptimize
   int nSectors;                             // number of sector for optimization by sector
   int penalty0;                             // penalty in seconds for tack
   int penalty1;                             // penalty in seconds fot Gybe
   int penalty2;                             // penalty in seconds for sail change
   int timeStep;                             // isoc time step in seconds
   int timeInterval;                         // for REQ_BEST_DEP time interval in seconds between each try
   time_t epochStart;                        // epoch time to start routing
   time_t timeWindow;                        // time window in seconds for bestTimeDeparture
   bool isoc;                                // true if isochrones requested
   bool isoDesc;                             // true if isochrone decriptor requested
   bool sortByName;                          // true if directory should be sorted by name, false if sorted by modif date
   bool forbid;                              // true if forbid zone (polygons or Earth) are considered
   bool withWaves;                           // true if waves specified in wavePolName file are considered
   bool withCurrent;                         // true if current specified in currentGribName is considered
   double staminaVR;                         // Init stamina
   double motorSpeed;                        // motor speed if used
   double threshold;                         // threshold for motor use
   double nightEfficiency;                   // efficiency of team at night
   double dayEfficiency;                     // efficiency of team at day
   double xWind;                             // multiply factor for wind
   double maxWind;                           // max Wind supported
   double constWindTws;                      // if not equal 0, constant wind used in place of grib file
   double constWindTwd;                      // the direction of constant wind if used
   double constWave;                         // constant wave height if used
   double constCurrentS;                     // if not equal 0, contant current speed Knots
   double constCurrentD;                     // the direction of constant current if used
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
   char dirName   [MAX_SIZE_RESOURCE_NAME];        // remote directory name
   char wavePolName [MAX_SIZE_RESOURCE_NAME];      // polar file name
   char polarName [MAX_SIZE_RESOURCE_NAME];        // polar file name
   char gribName  [MAX_SIZE_RESOURCE_NAME];        // grib file name
   char fileName  [MAX_SIZE_RESOURCE_NAME];        // grib file name
   char currentGribName [MAX_SIZE_RESOURCE_NAME];  // grib file name
   char feedback [MAX_SIZE_FEED_BACK];             // for feed back info
} ClientRequest; 

ClientRequest clientReq;

/*! Structure to store file information. */
typedef struct {
   char *name;
   off_t size;
   time_t mtime;
} FileInfo;

/**
 * @brief Retrieves the real client IP address from HTTP headers.
 * 
 * @param headers A string containing the HTTP headers.
 * @param clientAddress Buffer to store the client's IP address.
 * @param bufferSize Size of the clientAddress buffer.
 * @return true if the IP address was successfully found and stored, false if not found or error.
 */
bool getRealIPAddress (const char* headers, char* clientAddress, size_t bufferSize) {
   const char* headerName = "X-Real-IP: ";
   const char* headerStart = g_strstr_len (headers, -1, headerName);

   if (headerStart) {
      headerStart += strlen (headerName);
      while (*headerStart == ' ') headerStart++;  // Skip any whitepaces
        
      const char* headerEnd = g_strstr_len(headerStart, -1, "\r\n");
      if (headerEnd) {
         size_t ipLength = headerEnd - headerStart;
         if (ipLength < bufferSize) {
            g_strlcpy (clientAddress, headerStart, ipLength + 1);
            g_strstrip (clientAddress); 
            return true;
         } else {
            fprintf (stderr, "In getRealIPAddress: IP address length exceeds buffer size.");
            return false;
         }
      }
   }
   clientAddress [0] = '\0';
   return false;
}

/*! extract user agent */
char* extractUserAgent (const char* saveBuffer) {
   const char* headerName = "User-Agent: ";
   const char* userAgentStart = g_strstr_len(saveBuffer, -1, headerName);
   if (userAgentStart) {
      userAgentStart += strlen (headerName);
      const char* userAgentEnd = g_strstr_len (userAgentStart, -1, "\r\n");
       if (userAgentEnd) {
          return g_strndup(userAgentStart, userAgentEnd - userAgentStart);
       }
   }
   return NULL;  // No user agent gound
}

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
   GList *filesList = NULL;
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
      filesList = g_list_append (filesList, info);
      g_free (file_path);
   }
   g_dir_close (gdir);
   g_free (full_path);

   // Sort the list based on the requested criteria.
   if (sortByName)
      filesList = g_list_sort (filesList, compareByName);
   else
      filesList = g_list_sort (filesList, compareByMtime);

   // Construct the JSON string.
   g_string_assign (json, "[\n");
   for (GList *l = filesList; l != NULL; l = l->next) {
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
   g_string_append (json, "\n]\n");

   // Free allocated memory.
   for (GList *l = filesList; l != NULL; l = l->next) {
      FileInfo *info = (FileInfo *)l->data;
      g_free (info->name);
      g_free (info);
   }
   g_list_free (filesList);

   return json;
}

/*! Make initialization  
   return false if readParam or readGribAll fail */
static bool initContext (const char *parameterFileName, const char *pattern) {
   char directory [MAX_SIZE_DIR_NAME];
   char str [MAX_SIZE_LINE];
   char errMessage [MAX_SIZE_TEXT] = "";
   bool readGribRet;
   char sailPolFileName [MAX_SIZE_NAME] = "";

   if (! readParam (parameterFileName)) {
      fprintf (stderr, "In initContext, Error readParam: %s\n", parameterFileName);
      return false;
   }
   printf ("Parameters File: %s\n", parameterFileName);
   if (par.mostRecentGrib) {  // most recent grib will replace existing grib
      snprintf (directory, sizeof (directory), "%sgrib", par.workingDir); 
      mostRecentFile (directory, ".gr", pattern, par.gribFileName, sizeof (par.gribFileName));
   }
   if (par.gribFileName [0] != '\0') {
      readGribRet = readGribAll (par.gribFileName, &zone, WIND);
      if (! readGribRet) {
         fprintf (stderr, "In initContext, Error: Unable to read grib file: %s\n ", par.gribFileName);
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
      fprintf (stderr, "In initContext, Error readPolar: %s\n", errMessage);
      
   if (readPolar (true, par.wavePolFileName, &wavePolMat, errMessage, sizeof (errMessage)))
      printf ("Polar loaded   : %s\n", par.wavePolFileName);
   else
      fprintf (stderr, "In initContext, Error readPolar: %s\n", errMessage);
  
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
   static char dateBuffer [100];
   time_t now = time (NULL);
   struct tm *tm_info = gmtime(&now);
   strftime (dateBuffer, sizeof(dateBuffer), "%Y-%m-%d %H:%M:%S UTC", tm_info);
   return dateBuffer;
}

/*! store feeed back information */
static void handleFeedbackRequest (const char *fileName, const char *date, const char *clientIPAddress, const char *string) {
   FILE *file = fopen (fileName, "a");
   if (file == NULL) {
      fprintf (stderr, "handleFeedbackRequest, Error opening file: %s\n", fileName);
      return;
   }
   fprintf (file, "%s; %s; \n%s\n\n", date, clientIPAddress, string);
   fclose (file);
}

/*! log client Request 
    side effect: dataReq is modified */
static void logRequest (const char* fileName, const char *date, int serverPort, const char *remote_addr, \
   char *dataReq, const char *userAgent, ClientRequest *client, double duration) {

   FILE *logFile = fopen (fileName, "a");
   if (logFile == NULL) {
      fprintf (stderr, "In logRequest, Error opening log file: %s\n", fileName);
      return;
   }
   g_strstrip (dataReq);
   g_strdelimit (dataReq, "\r\n", ' ');
   
   fprintf (logFile, "%s; %d; %-16.16s; %-30.30s; %2d; %6.2lf, %.50s\n", 
      date, serverPort, remote_addr, userAgent, client->type, duration, dataReq);
   fclose (logFile);
}

/*! decode request from client and fill ClientRequest structure 
   return true if correct false if impossible to decode */
static bool decodeHttpReq (const char *req, ClientRequest *clientReq) {
   memset (clientReq, 0, sizeof (ClientRequest));
   clientReq->type = -1;              // default unknown
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
      // printf ("part %d %s\n", i, parts [i]);
      g_strstrip (parts [i]);      
      if (sscanf(parts[i], "type=%d", &clientReq->type) == 1);// type extraction
      else if (g_str_has_prefix (parts[i], "boat=")) {
         char *boatsPart = parts[i] + strlen ("boat="); // After boats=="
         if (*boatsPart == '\0') { 
            g_strfreev(parts);
            return false;  // parsing error
         }
         // waypoints parsing
         char **boatsCoords = g_strsplit (boatsPart, ";", -1);
         for (int i = 0; boatsCoords[i] && clientReq->nBoats < MAX_N_COMPETITORS; i++) {
            char name [MAX_SIZE_NAME];
            double lat, lon;
            if (sscanf(boatsCoords[i], "%63[^,], %lf, %lf", name, &lat, &lon) == 3) {
               g_strlcpy (clientReq->boats [clientReq->nBoats].name, name, MAX_SIZE_NAME);
               clientReq->boats [clientReq->nBoats].lat = lat;
               clientReq->boats [clientReq->nBoats].lon = lon;
               clientReq->nBoats += 1;
            }
         }
         g_strfreev(boatsCoords);
      }
      else if (g_str_has_prefix (parts[i], "waypoints=")) {
         char *wpPart = parts[i] + strlen ("waypoints="); // Partie après "waypoints="
         if (*wpPart == '\0') {
            g_strfreev(parts);
            return false;  // parsing error
         }
         // waypoints parsing
         char **wpCoords = g_strsplit (wpPart, ";", -1);
         for (int i = 0; wpCoords[i] && clientReq->nWp < MAX_N_WAY_POINT; i++) {
            double lat, lon;
            if (sscanf(wpCoords[i], "%lf,%lf", &lat, &lon) == 2) {
               clientReq->wp [clientReq->nWp].lat = lat;
               clientReq->wp [clientReq->nWp].lon = lon;
               clientReq->nWp += 1;
            }
         }
         g_strfreev(wpCoords);
      }

      else if (sscanf (parts[i], "timeStep=%d", &clientReq->timeStep) == 1);                    // time step extraction
      else if (sscanf (parts[i], "cogStep=%d",  &clientReq->cogStep) == 1);                     // cog step extraction
      else if (sscanf (parts[i], "cogRange=%d", &clientReq->rangeCog) == 1);                    // range sog  extraction
      else if (sscanf (parts[i], "jFactor=%d",  &clientReq->jFactor) == 1);                     // jFactor extraction
      else if (sscanf (parts[i], "kFactor=%d",  &clientReq->kFactor) == 1);                     // kFactor extraction
      else if (sscanf (parts[i], "nSectors=%d", &clientReq->nSectors) == 1);                    // nSectors extraction
      else if (sscanf (parts[i], "penalty0=%d", &clientReq->penalty0) == 1);                    // penalty0 extraction
      else if (sscanf (parts[i], "penalty1=%d", &clientReq->penalty1) == 1);                    // penalty1 extraction
      else if (sscanf (parts[i], "penalty2=%d", &clientReq->penalty2) == 1);                    // penalty2 (sail change)  extraction
      else if (sscanf (parts[i], "timeInterval=%d", &clientReq->timeInterval) == 1);            // time Window extraction
      else if (sscanf (parts[i], "epochStart=%ld",  &clientReq->epochStart) == 1);              // time startextraction
      else if (sscanf (parts[i], "timeWindow=%ld", &clientReq->timeWindow) == 1);               // time Window extraction
      else if (sscanf (parts[i], "polar=%255s", clientReq->polarName) == 1);                    // polar name
      else if (sscanf (parts[i], "wavePolar=%255s", clientReq->wavePolName) == 1);              // wave polar name
      else if (sscanf (parts[i], "file=%255s",  clientReq->fileName) == 1);                     // file name
      else if (sscanf (parts[i], "grib=%255s",  clientReq->gribName) == 1);                     // grib name
      else if (sscanf (parts[i], "currentGrib=%255s", clientReq->currentGribName) == 1);        // current grib name
      else if (sscanf (parts[i], "dir=%255s",   clientReq->dirName) == 1);                      // directory name
      else if (g_str_has_prefix (parts[i], "dir=")) 
         g_strlcpy (clientReq->dirName, parts [i] + strlen ("dir="), sizeof (clientReq->dirName)); // defaulr empty works
      else if (g_str_has_prefix (parts[i], "feedback=")) 
         g_strlcpy (clientReq->feedback, parts [i] + strlen ("feedback="), sizeof (clientReq->feedback));
      else if (g_str_has_prefix (parts[i], "isoc=true")) clientReq->isoc = true;                // Default false
      else if (g_str_has_prefix (parts[i], "isoc=false")) clientReq->isoc = false;              // Default false
      else if (g_str_has_prefix (parts[i], "isodesc=true")) clientReq->isoDesc = true;          // Default false
      else if (g_str_has_prefix (parts[i], "isodesc=false")) clientReq->isoDesc = false;         // Default false
      else if (g_str_has_prefix (parts[i], "forbid=true")) clientReq->forbid = true;            // Default false
      else if (g_str_has_prefix (parts[i], "forbid=false")) clientReq->forbid = false;          // Default false
      else if (g_str_has_prefix (parts[i], "withWaves=true")) clientReq->withWaves = true;      // Default false
      else if (g_str_has_prefix (parts[i], "withWaves=false")) clientReq->withWaves = false;    // Default false
      else if (g_str_has_prefix (parts[i], "withCurrent=true")) clientReq->withCurrent = true;  // Default false
      else if (g_str_has_prefix (parts[i], "withCurrent=false")) clientReq->withCurrent = false;// Default false
      else if (g_str_has_prefix (parts[i], "sortByName=true")) clientReq->sortByName = true;    // Default false
      else if (g_str_has_prefix (parts[i], "sortByName=false")) clientReq->sortByName = false;  // Default false
      else if (sscanf (parts[i], "staminaVR=%lf",        &clientReq->staminaVR) == 1);          // stamina Virtual Regatta
      else if (sscanf (parts[i], "motorSpeed=%lf",       &clientReq->motorSpeed) == 1);         // motor speed
      else if (sscanf (parts[i], "threshold=%lf",         &clientReq->threshold) == 1);         // threshold for motoe
      else if (sscanf (parts[i], "nightEfficiency=%lf",  &clientReq->nightEfficiency) == 1);    // efficiency at night
      else if (sscanf (parts[i], "dayEfficiency=%lf",    &clientReq->dayEfficiency) == 1);      // efficiency daylight
      else if (sscanf (parts[i], "xWind=%lf",            &clientReq->xWind) == 1);              // xWind factor
      else if (sscanf (parts[i], "maxWind=%lf",          &clientReq->maxWind) == 1);            // max Wind
      else if (sscanf (parts[i], "constWindTws=%lf",     &clientReq->constWindTws) == 1);       // const Wind TWS
      else if (sscanf (parts[i], "constWindTwd=%lf",     &clientReq->constWindTwd) == 1);       // const Wind TWD
      else if (sscanf (parts[i], "constWave=%lf",        &clientReq->constWave) == 1);          // const Waves
      else if (sscanf (parts[i], "constCurrentS=%lf",     &clientReq->constCurrentS) == 1);     // const current speed
      else if (sscanf (parts[i], "constCurrentD=%lf",     &clientReq->constCurrentD) == 1);     // const current direction
      else fprintf (stderr, "In decodeHttpReq Unknown value: %s\n", parts [i]);
   }
   g_strfreev (parts);
   
   return clientReq->type != -1;  
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
   par.cogStep = MAX (1, clientReq->cogStep);
   par.rangeCog = clientReq->rangeCog;
   par.jFactor = clientReq->jFactor; 
   par.kFactor = clientReq->kFactor; 
   par.nSectors = clientReq->nSectors; 
   par.penalty0 = clientReq->penalty0;  // seconds
   par.penalty1 = clientReq->penalty1;  // seconds 
   par.penalty2 = clientReq->penalty2;  // seconds
   par.motorSpeed = clientReq->motorSpeed;
   par.threshold = clientReq->threshold;
   par.nightEfficiency = clientReq->nightEfficiency;
   par.dayEfficiency = clientReq->dayEfficiency;
   par.xWind = clientReq->xWind;
   par.maxWind = clientReq->maxWind;
   par.withWaves = clientReq->withWaves;
   par.withCurrent = clientReq->withCurrent;
   par.constWindTws = clientReq->constWindTws;
   par.constWindTwd = clientReq->constWindTwd;
   par.constWave = clientReq->constWave;
   par.constCurrentS = clientReq->constCurrentS;
   par.constCurrentD = clientReq->constCurrentD;

   // change polar if requested
   if (clientReq->polarName [0] != '\0') {
      buildRootName (clientReq->polarName, strPolar, sizeof (strPolar));
      printf ("polar found: %s\n", strPolar);
      if (strncmp (par.polarFileName, strPolar, strlen (strPolar)) != 0) {
         printf ("read polar: %s\n", strPolar);
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
   if (clientReq->wavePolName [0] != '\0') {
      buildRootName (clientReq->wavePolName, strPolar, sizeof (strPolar));
      printf ("wave polar found: %s\n", strPolar);
      if (strncmp (par.wavePolFileName, strPolar, strlen (strPolar)) != 0) {
         printf ("read wave polar: %s\n", strPolar);
         if (readPolar (false, strPolar, &wavePolMat, checkMessage, maxLen)) {
            g_strlcpy (par.wavePolFileName, strPolar, sizeof (par.wavePolFileName));
            printf ("Wave Polar loaded : %s\n", strPolar);
         }
         else {
            snprintf (checkMessage, maxLen, "\"2: Error reading Wave Polar: %s\"", clientReq->wavePolName);
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

   if (clientReq->currentGribName [0] != '\0') {
      buildRootName (clientReq->currentGribName, strGrib, sizeof (strGrib));
      printf ("current grib found: %s\n", strGrib);
      if (strncmp (par.currentGribFileName, strGrib, strlen (strGrib)) != 0) {
         printf ("current readGrib: %s\n", strGrib);
         if (readGribAll (strGrib, &currentZone, CURRENT)) {
            g_strlcpy (par.currentGribFileName, strGrib, sizeof (par.currentGribFileName));
            printf ("Current Grib loaded   : %s\n", strGrib);
         }
         else {
            snprintf (checkMessage, maxLen, "\"3: Error reading Current Grib: %s\"", clientReq->currentGribName);
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
   gchar *gribBaseName = g_path_get_basename (par.gribFileName);

   competitors.n = clientReq->nBoats;
   for (int i = 0; i < clientReq->nBoats; i += 1) {
      if (! par.allwaysSea && ! isSea (tIsSea,  clientReq -> boats [i].lat,  clientReq -> boats [i].lon)) {
         snprintf (checkMessage, maxLen, 
            "\"5: Competitor not in sea.\",\n\"name\": \"%s\", \"lat\": %.2lf, \"lon\": %.2lf\n",
            clientReq -> boats [i].name, clientReq -> boats [i].lat, clientReq -> boats [i].lon);
         return false;
      }
      if (! isInZone (clientReq -> boats [i].lat, clientReq -> boats [i].lon, &zone) && (par.constWindTws == 0)) { 
         snprintf (checkMessage, maxLen, 
            "\"6: Competitor not in Grib wind zone.\",\n\"grib\": \"%s\", \"bottomLat\": %.2lf, \"leftLon\": %.2lf, \"topLat\": %.2lf, \"rightLon\": %.2lf\n",
            gribBaseName, zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight);
         g_free (gribBaseName);
         return false;
      }
      g_strlcpy (competitors.t [i].name, clientReq -> boats [i].name, MAX_SIZE_NAME);
      printf ("competitor name: %s\n", competitors.t [i].name);
      competitors.t [i].lat = clientReq -> boats [i].lat;
      competitors.t [i].lon = clientReq -> boats [i].lon;
   }
 
   for (int i = 0; i < clientReq->nWp; i += 1) {
      if (! par.allwaysSea && ! isSea (tIsSea, clientReq->wp [i].lat, clientReq->wp [i].lon)) {
         snprintf (checkMessage, maxLen, 
            "\"7: WP or Dest. not in sea.\",\n\"lat\": %.2lf, \"lon\": %.2lf\n",
            clientReq->wp [i].lat, clientReq->wp [i].lon);
         return false;
      }
      if (! isInZone (clientReq->wp [i].lat , clientReq->wp [i].lon , &zone) && (par.constWindTws == 0)) {
         snprintf (checkMessage, maxLen, 
            "\"8: WP or Dest. not in Grib wind zone.\",\n\"grib\": \"%s\", \"bottomLat\": %.2lf, \"leftLon\": %.2lf, \"topLat\": %.2lf, \"rightLon\": %.2lf\n",
            gribBaseName, zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight);
         g_free (gribBaseName);
         return false;
      }
   }
   for (int i = 0; i < clientReq->nWp -1; i += 1) {
      wayPoints.t[i].lat = clientReq->wp [i].lat; 
      wayPoints.t[i].lon = clientReq->wp [i].lon; 
   }
   wayPoints.n = clientReq->nWp - 1;

   if ((par.startTimeInHours < 0) || (par.startTimeInHours > zone.timeStamp [zone.nTimeStamp -1])) {
         snprintf (checkMessage, maxLen, "\"4: start Time not in Grib time window\"");
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

   par.pOr.lat = clientReq->boats [0].lat;
   par.pOr.lon = clientReq->boats [0].lon;
   par.pDest.lat = clientReq->wp [clientReq->nWp-1].lat;
   par.pDest.lon = clientReq->wp [clientReq->nWp-1].lon;
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
   if (strcmp(ext, ".par") == 0) return "text/plain";
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
      send (client_socket, not_found, strlen(not_found), 0);
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
   ssize_t bytesRead;
   while ((bytesRead = read(file, buffer, sizeof(buffer))) > 0) {
      send (client_socket, buffer, bytesRead, 0);
   }
   close (file);
}

/*!
 * @brief Return the current memory usage (resident set size) in kilobytes.
 * @return int Memory usage in KB, or -1 on error.
 */
int memoryUsage (void) {
   FILE *fp = fopen("/proc/self/status", "r");
   if (!fp) return -1;
   char line[256];
   int mem = -1;

   while (fgets (line, sizeof(line), fp)) {
      if (strncmp (line, "VmRSS:", 6) == 0) {
         // Example line: "VmRSS:      12345 kB"
         sscanf (line + 6, "%d", &mem);  // skip "VmRSS:"
         break;
      }
   }

   fclose(fp);
   return mem;  // in KB
}

/*! Raw dump of file fileName */
GString *dumpFile (const char * fileName) {
   GString *res = g_string_new ("");
   char fullFileName [MAX_SIZE_FILE_NAME];
   buildRootName (fileName, fullFileName, sizeof (fullFileName));
   GError *error = NULL;
   char *content = NULL;
   gsize length;
   if (g_file_get_contents (fullFileName, &content, &length, &error)) {
      g_string_append_printf (res, "%s", content);
      g_free (content);
   }
   else {
      g_string_append_printf (res, "{\"_Error\": \"%s\"}\n", error->message);
      g_clear_error (&error);
   }
   return res;
}  

/*! launch action and returns GString after execution */
static GString *launchAction (int serverPort, ClientRequest *clientReq, const char *date, const char *clientIPAddress) {
   char tempFileName [MAX_SIZE_FILE_NAME];
   GString *res = g_string_new ("");
   GString *polString;
   char checkMessage [MAX_SIZE_TEXT];
   char sailPolFileName [MAX_SIZE_NAME] = "";
   char body [2048] = "";
   // printf ("client.req = %d\n", clientReq->type);
   switch (clientReq->type) {
   case REQ_KILL:
      printf ("Killed on port: %d, At: %s, By: %s\n", serverPort, date, clientIPAddress);
      g_string_append_printf (res, "{\n   \"killed_on_port\": %d, \"date\": %s, \"by\": %s\"\n}\n", serverPort, date, clientIPAddress);
      break;
   case REQ_TEST:
      g_string_append_printf (res, "{\n   \"Prog-version\": \"%s, %s, %s\",\n", PROG_NAME, PROG_VERSION, PROG_AUTHOR);
      g_string_append_printf (res, "   \"API server port\": %d,\n", serverPort);
      g_string_append_printf (res, "   \"Compilation-date\": \"%s\",\n", __DATE__);
      g_string_append_printf (res, "   \"GLIB-version\": \"%d.%d.%d\",\n   \"ECCODES-version\": \"%s\",\n   \"CURL-version\": \"%s\",\n",
            GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION, ECCODES_VERSION_STR, LIBCURL_VERSION);
      g_string_append_printf (res, "   \"PID\": %d,\n", getpid ());
      g_string_append_printf (res, "   \"Memory usage in KB\": %d\n}\n", memoryUsage ());
      break;
   case REQ_ROUTING:
      if (checkParamAndUpdate (clientReq, checkMessage, sizeof (checkMessage))) {
         competitors.runIndex = 0;
         routingLaunch ();
         GString *jsonRoute = routeToJson (&route, 0, clientReq->isoc, clientReq->isoDesc); // only most recent route with isochrones 
         g_string_append_printf (res, "{\n%s}\n", jsonRoute->str);
         g_string_free (jsonRoute, TRUE);
      }
      else {
         g_string_append_printf (res,"{\"_Error\":\n%s\n}\n", checkMessage);
      }
      break;
   case REQ_BEST_DEP:
      if (checkParamAndUpdate (clientReq, checkMessage, sizeof (checkMessage))) {
         competitors.runIndex = 0;
         printf ("Launch bestTimeDesparture\n");
         printf ("begin: %d, end: %d\n", chooseDeparture.tBegin, chooseDeparture.tEnd);
         bestTimeDeparture ();
         GString *bestTimeReport = bestTimeReportToJson (&chooseDeparture, clientReq->isoc, clientReq->isoDesc);
         g_string_append_printf (res, "%s", bestTimeReport->str);
         g_string_free (bestTimeReport, TRUE);
      }
      else {
         g_string_append_printf (res,"{\"_Error\":\n%s\n}\n", checkMessage);
      }
      break;
   case REQ_RACE:
      historyRoute.n = 0;
      if (checkParamAndUpdate (clientReq, checkMessage, sizeof (checkMessage))) {
         printf ("Launch AllCompetitors\n");
         allCompetitors ();
         GString *jsonRoutes = allCompetitorsToJson (competitors.n, clientReq->isoc, clientReq->isoDesc);
         g_string_append_printf (res, "%s", jsonRoutes->str);
         g_string_free (jsonRoutes, TRUE);
      }
      else {
         g_string_append_printf (res,"{\"_Error\":\n%s\n}\n", checkMessage);
      }
      break;
   case REQ_POLAR:
      printf ("polarName: %s\n", clientReq->polarName);
      if (strstr (clientReq->polarName, "wavepol") != NULL) {
         polString = polToJson (clientReq->polarName, "wavePolarName");
         g_string_append_printf (res, "[%s, %s, %s]\n", polString->str, "{}", "{}");
         g_string_free (polString, TRUE);
         break;
      }

      if (clientReq->polarName [0] != '\0') {
         polString = polToJson (clientReq->polarName, "polarName");
         newFileNameSuffix (clientReq->polarName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
      }
      else {
         polString = polToJson (par.polarFileName, "polarName");
         newFileNameSuffix (par.polarFileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
      }
      GString *sailString = polToJson (sailPolFileName, "sailName");
      GString *legendString = g_string_new ("");
      if (g_str_has_prefix (sailString->str, "{}"))
         g_string_assign (legendString, "{}");
      else 
         legendString = sailLegendToJson (sailName, MAX_N_SAIL);
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
   case REQ_PAR_RAW:
      writeParam (buildRootName (TEMP_FILE_NAME, tempFileName, sizeof (tempFileName)), true, false);
      res = dumpFile (TEMP_FILE_NAME);
      break;
   case REQ_PAR_JSON:
      res = paramToJson (&par);
      break;
   case REQ_INIT:
      if (! initContext (parameterFileName, PATTERN))
         g_string_append_printf (res, "{\"_Error\": \"%s\"}\n", "Init Routing failed");
      else
         g_string_append_printf (res, "{\"_Message\": \"%s\"}\n", "Init done");
      break;
   case REQ_FEEDBACK:
         snprintf (body, sizeof (body), "%s; %s\n%s\n", date, clientIPAddress, clientReq->feedback);
         handleFeedbackRequest (FEED_BACK_FILE_NAME, date, clientIPAddress, clientReq->feedback);
         if (smtpSend (par.smtpTo, FEED_BACK_OBJECT, body))
            g_string_append_printf (res, "{\"_Feedback\": \"%s\"}\n", "OK");
         else
            g_string_append_printf (res, "{\"_Feedback\": \"%s\"}\n", "KO");
      break;
   case REQ_DUMP_FILE:  // raw dump
      res = dumpFile (clientReq->fileName);
      break;
   default:;
   }
   return res;
}

/*! Handle client connection and launch actions */
static void handleClient (int serverPort, int clientFd, struct sockaddr_in *client_addr) {
   char saveBuffer [MAX_SIZE_REQUEST];
   char buffer [MAX_SIZE_REQUEST] = "";
   char clientIPAddress [MAX_SIZE_LINE];

   // read HTTP request
   int bytes_read = recv (clientFd, buffer, sizeof(buffer) - 1, 0);
   if (bytes_read <= 0) {
     return;
   }
   buffer [bytes_read] = '\0'; // terminate string
   //printf ("Client Request: %s\n", buffer);
   g_strlcpy (saveBuffer, buffer, MAX_SIZE_BUFFER);

   if (! getRealIPAddress (buffer, clientIPAddress, sizeof (clientIPAddress))) { // try if proxy 
      // Get client IP address if IP address not found with proxy
      char remoteAddr [INET_ADDRSTRLEN];
      inet_ntop (AF_INET, &(client_addr->sin_addr), remoteAddr, INET_ADDRSTRLEN); // not used
      g_strlcpy (clientIPAddress, remoteAddr, INET_ADDRSTRLEN);
   }

   // Extract HTTP first line request
   char *requestLine = strtok (buffer, "\r\n");
   if (!requestLine) {
     return;
   }
   printf ("Request line: %s\n", requestLine);

   // check if Rest API (POST) or static file (GET)
   if (strncmp (requestLine, "POST", 4) != 0) {
      printf ("GET Request, static file\n");
      // static file
      const char *requested_path = strchr (requestLine, ' '); // space after "GET"
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

   // Extract request body
   char *postData = strstr (saveBuffer, "\r\n\r\n");
   if (postData == NULL) {
      return;
   }

   char* userAgent = extractUserAgent (saveBuffer);

   postData += 4; // Ignore HTTP request separators
   printf ("POST Request:\n%s\n", postData);
   bool ok = decodeHttpReq (postData, &clientReq);
   
   // data for log
   gint64 start = g_get_monotonic_time (); 
   const char *date = getCurrentDate ();

   if (! ok) {
      const char *errorResponse = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nError";
      fprintf (stderr, "In handleClient, Error: %s\n", errorResponse);
      send (clientFd, errorResponse, strlen(errorResponse), 0);
      return;
   }
   GString *res = launchAction (serverPort, &clientReq, date, clientIPAddress);
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
   //printf ("response on port %d: %s\n", serverPort, response->str);
   send (clientFd, response->str, strlen(response->str), 0);
   printf ("Response sent to client\n\n");
   g_string_free (response, TRUE);
   double duration = (g_get_monotonic_time () - start) / 1e6; 
   logRequest (par.logFileName, date, serverPort, clientIPAddress, postData, userAgent, &clientReq, duration);
   if (userAgent) g_free (userAgent);
}

int main (int argc, char *argv[]) {
   int serverFd, clientFd;
   struct sockaddr_in address;
   int addrlen = sizeof (address);
   int serverPort, opt = 1;
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

   if (! initContext (parameterFileName, ""))
      return EXIT_FAILURE;

   // Socket 
   serverFd = socket (AF_INET, SOCK_STREAM, 0);
   if (serverFd < 0) {
      perror ("socket failed");
      exit (EXIT_FAILURE);
   }

   // Allow adress reuse juste after closing
   if (setsockopt (serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      perror ("setsockopt");
      close (serverFd);
      exit (EXIT_FAILURE);
   }

   // Define server parameters address port
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons (serverPort);

   // Bind socket with port
   if (bind (serverFd, (struct sockaddr *)&address, sizeof(address)) < 0) {
      perror ("In main, Error socket bind"); 
      close (serverFd);
      return EXIT_FAILURE;
   }

   // Listen connexions
   if (listen (serverFd, 3) < 0) {
      perror ("In main: error listening");
      close (serverFd);
      return EXIT_FAILURE;
   }
   double elapsed = (g_get_monotonic_time () - start) / 1e6; 
   printf ("✅ Loaded in...: %.2lf seconds. Server listen on port: %d, Pid: %d\n", elapsed, serverPort, getpid ());

   while (clientReq.type != REQ_KILL) {
      if ((clientFd = accept (serverFd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
         perror ("In main: Error accept");
         close (serverFd);
         exit (EXIT_FAILURE);
      }
      handleClient (serverPort, clientFd, &address);
      fflush (stdout);
      fflush (stderr);
      close (clientFd);
   }
   close (serverFd);
   free (tIsSea);
   free (isoDesc);
   free (isocArray);
   free (route.t);
   freeHistoryRoute ();
   free (tGribData [WIND]); 
   free (tGribData [CURRENT]); 
   curl_global_cleanup();
   return EXIT_SUCCESS;
}

