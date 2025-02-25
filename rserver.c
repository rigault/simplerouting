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

#define SERVER_PORT            8080        // default HTTP Request may be changed in routing.par
#define MAX_SIZE_REQUEST       2048        // Max size from request client
#define MAX_SIZE_RESOURCE_NAME 256         // Max size polar or grib name

const char *filter[] = { ".csv", ".pol", ".grb", ".grb2", NULL }; // global fiter for REQ_DIR request

enum {REQ_TEST, REQ_ROUTING, REQ_BEST_DEP, REQ_RACE, REQ_POLAR, REQ_GRIB, REQ_DIR}; // type of request

char parameterFileName [MAX_SIZE_FILE_NAME];

/*! Client Request description */
typedef struct {
   int type;                                 // type of request
   time_t epochStart;                        // epoch time to start routing
   time_t timeWindow;                        // time window in seconds for bestTimeDeparture
   int timeStep;                             // isoc time step in seconds
   int timeInterval;                         // for REQ_BEST_DEP time interval in seconds between each try
   bool isoc;                                // true if isochrones requested
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
   bool sortByName;                          // true id directory should be sorted by name, false if sorted by modif date
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
   int readGribRet;

   if (! readParam (parameterFileName)) {
      fprintf (stderr, "In iniRouting, Error readParam: %s\n", parameterFileName);
      return false;
   }
   printf ("param File: %s\n", parameterFileName);
   if (par.mostRecentGrib) {  // most recent grib will replace existing grib
      snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir); 
      mostRecentFile (directory, ".gr", par.gribFileName, sizeof (par.gribFileName));
   }
   printf ("grib File: %s\n", par.gribFileName);
   if (par.gribFileName [0] != '\0') {
      readGribRet = readGribAll (par.gribFileName, &zone, WIND);
      if (readGribRet == 0) {
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
   if (readPolar (true, par.polarFileName, &polMat, errMessage, sizeof (errMessage)))
      printf ("Polar loaded   : %s\n", par.polarFileName);
   else
      fprintf (stderr, "In initScenarioOption, Error readPolar: %s\n", errMessage);
      
   if (readPolar (true, par.wavePolFileName, &wavePolMat, errMessage, sizeof (errMessage)))
      printf ("Polar loaded   : %s\n", par.wavePolFileName);
   else
      fprintf (stderr, "In initScenatioOption, Error readPolar: %s\n", errMessage);
  
   printf ("par.web        : %s\n", par.web);
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
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

   char **parts = g_strsplit(req, "&", -1);

   for (int i = 0; parts[i]; i++) {
      printf ("part: %s\n", parts [i]);
      // type extraction
      if (g_str_has_prefix (parts[i], "type=")) {
         char *type_part = parts[i] + strlen ("type=");
         if (type_part && sscanf (type_part, "%d", &clientReq->type) != 1) {
            g_strfreev (parts);
            return false;  // type  parsing error
         }
      }
      
      else if (g_str_has_prefix(parts[i], "boat=")) {
         char *boats_part = parts[i] + strlen ("boat="); // After boats=="
         if (!boats_part) {
            g_strfreev(parts);
            return false;  // parsing error
         }
         // waypoints parsing
         char **boats_coords = g_strsplit (boats_part, ";", -1);
            printf ("att boat: %s\n", boats_coords [i]);
         for (int i = 0; boats_coords[i] && clientReq->nBoats < MAX_N_COMPETITORS; i++) {
            char name [MAX_SIZE_NAME];
            printf ("att 1 _ i: %d\n", i);
            double lat, lon;
            if (sscanf(boats_coords[i], "%63[^,], %lf, %lf", name, &lat, &lon) == 3) {
               printf ("att 2 _ i: %d\n", i);
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
         if (!waypoints_part) {
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

      // timeStep extraction
      else if (g_str_has_prefix(parts[i], "timeStep=")) {
         char *timeStep_part = parts[i] + strlen ("timeStep=");
         if (timeStep_part && sscanf (timeStep_part, "%d", &clientReq->timeStep) != 1) {
            g_strfreev(parts);
            return false;  // timeStep parsing error
         }
      }

      // timeStart extraction
      else if (g_str_has_prefix(parts[i], "timeStart=")) {
         char *timeStart_part = parts[i] + strlen ("timeStart=");
         if (timeStart_part && sscanf (timeStart_part, "%ld", &clientReq->epochStart) != 1) {
            g_strfreev(parts);
            return false;  // timeStart parsing error
         }
      }
      // timeStop extraction
      else if (g_str_has_prefix(parts[i], "timeWindow=")) {
         char *timeWindow_part = parts[i] + strlen ("timeWindow=");
         if (timeWindow_part && sscanf (timeWindow_part, "%ld", &clientReq->timeWindow) != 1) {
            g_strfreev(parts);
            return false;  // timeStop parsing error
         }
      }
      // timeInterval extraction
      else if (g_str_has_prefix(parts[i], "timeInterval=")) {
         char *timeInterval_part = parts[i] + strlen ("timeInterval=");
         if (timeInterval_part && sscanf (timeInterval_part, "%d", &clientReq->timeInterval) != 1) {
            g_strfreev(parts);
            return false;  // timeStop parsing error
         }
      }
      // Isoc true or false
      else if (g_str_has_prefix (parts[i], "isoc=")) {
         char *isoc_part = parts[i] + strlen("isoc=");
         if (g_strstr_len (isoc_part, 4, "true") != NULL)
            clientReq->isoc = true;
      }
      // polar Name
      else if (g_str_has_prefix (parts[i], "polar=")) {
         char *polar_part = parts[i] + strlen ("polar=");
         if (polar_part && sscanf (polar_part, "%255s;", clientReq->polarName) != 1) {
            g_strfreev(parts);
            return false;  // parsing error
         }
         printf ("polar found: %s\n", clientReq->polarName);
      }
      // grib Name
      else if (g_str_has_prefix (parts[i], "grib=")) {
         char *grib_part = parts[i] + strlen ("grib=");
         if (grib_part && sscanf (grib_part, "%255s;", clientReq->gribName) != 1) {
            g_strfreev(parts);
            return false;  // parsing error
         }
         printf ("grib found: %s\n", clientReq->gribName);
      }
      // dir Name
      else if (g_str_has_prefix (parts[i], "dir=")) {
         char *dir_part = parts[i] + strlen ("dir=");
         if (dir_part && sscanf (dir_part, "%255s;", clientReq->dirName) != 1) {
            g_strfreev(parts);
            return false;  // parsing error
         }
         printf ("dir found: %s\n", clientReq->dirName);
      }
      // sortbyname true or false
      else if (g_str_has_prefix (parts[i], "sortByName=")) {
         char *sort_part = parts[i] + strlen("sortByName=");
         if (g_strstr_len (sort_part, 4, "true") != NULL)
            clientReq->sortByName = true;
      }
   }
   g_strfreev(parts);
   
   return true;  
}

/*! check validity of parameters */
static bool checkParamAndUpdate (ClientRequest *clientReq, char *checkMessage, size_t maxLen) {
   char strPolar [MAX_SIZE_FILE_NAME];
   // printf ("startInfo after: %s, startTime: %lf\n", asctime (&startInfo), par.startTimeInHours);
   if ((clientReq->nBoats == 0) || (clientReq->nWp == 0)) {
      snprintf (checkMessage, maxLen, "1: No boats or no Waypoints");
      return false;
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
      snprintf (checkMessage, maxLen, "2: start Time not in Grib time window.");
      return false;
   }
   
   if (! isInZone (par.pOr.lat, par.pOr.lon, &zone) && (par.constWindTws == 0)) { 
      snprintf (checkMessage, maxLen, "3: Origin point not in Grib wind zone.");
      return false;
   }
   if (! isInZone (par.pDest.lat, par.pDest.lon, &zone) && (par.constWindTws == 0)) {
      snprintf (checkMessage, maxLen, "4: Destination point not Grib in wind zone.");
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

   // change polar if requested
   if (clientReq->polarName [0] != '\0') {
      buildRootName (clientReq->polarName, strPolar, sizeof (strPolar));
      printf ("polar found: %s\n", strPolar);
      if (strncmp (par.polarFileName, strPolar, strlen (strPolar)) != 0) {
         printf ("readPolar: %s\n", strPolar);
         if (readPolar (false, strPolar, &polMat, checkMessage, maxLen)) {
            g_strlcpy (par.polarFileName, strPolar, sizeof (par.polarFileName));
            printf ("Polar loaded   : %s\n", strPolar);
            return true;
         }
         return false;
      }  
   }
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
   char checkMessage [MAX_SIZE_TEXT];
   printf ("client.req = %d\n", clientReq->type);
   switch (clientReq->type) {
   case REQ_ROUTING:
      if (checkParamAndUpdate (clientReq, checkMessage, sizeof (checkMessage))) {
         routingLaunch ();
         GString *jsonRoute = allCompetitorsToJson (0, clientReq->isoc);
         g_string_append_printf (res, "%s", jsonRoute->str);
         g_string_free (jsonRoute, TRUE);
      }
      else {
         g_string_append_printf (res,"{\"_checkId\":\"%s\"}\n", checkMessage);
      }
      break;
   case REQ_TEST:
      g_string_append_printf (res, "{\"Prog-version\": \"%s, %s, %s\",\n", PROG_NAME, PROG_VERSION, PROG_AUTHOR);
      g_string_append_printf (res, "\" Compilation-date\": \"%s\",\n", __DATE__);
      g_string_append_printf (res, "\" GLIB-version\": \"%d.%d.%d\", \n \"ECCODES-version\": \"%s\"\n}\n",
            GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION, ECCODES_VERSION_STR);
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
         g_string_append_printf (res,"{\"_checkId\":\"%s\"}\n", checkMessage);
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
         g_string_append_printf (res,"{\"_checkId\":\"%s\"}\n", checkMessage);
      }
      break;
   case REQ_POLAR:
      if (clientReq->polarName [0] != '\0')
         res = polToJson (clientReq->polarName);
      else
         res = polToJson (par.polarFileName);
      break;
   case REQ_GRIB:
      res = gribToJson (clientReq->gribName);
      break;
   case REQ_DIR:
      res = listDirToJson (par.workingDir, clientReq->dirName, clientReq->sortByName, filter);
      break;
      // g_string_append_printf (res, "%s", polar->str);
      break;
   default:;
   }
   return res;
}

/*! Handle client connection and launch actions */
static void handleClient (int client_fd, struct sockaddr_in *client_addr) {
   char saveBuffer [MAX_SIZE_REQUEST];
   char buffer [MAX_SIZE_REQUEST];
   const char *user_agent = "Unknown";
   ClientRequest clientReq;

   // read HTTP request
   int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
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

      serveStaticFile (client_fd, requested_path);
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
      send (client_fd, error_response, strlen(error_response), 0);
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
   send (client_fd, response->str, strlen(response->str), 0);
   g_string_free (response, TRUE);
}

int main (int argc, char *argv[]) {
   int server_fd, client_fd;
   struct sockaddr_in address;
   int addrlen = sizeof(address);

   if (setlocale (LC_ALL, "C") == NULL) {                // very important for printf decimal numbers
      fprintf (stderr, "Server Error: setlocale failed");
      return EXIT_FAILURE;
      exit (EXIT_FAILURE);
   }

   if (argc > 1)
      g_strlcpy (parameterFileName, argv [1], sizeof (parameterFileName));
   else 
      g_strlcpy (parameterFileName, PARAMETERS_FILE, sizeof (parameterFileName));

   if (! initRouting (parameterFileName))
      return EXIT_FAILURE;

   // Socket 
   if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
      perror ("In main, Error creating socket");
      return EXIT_FAILURE;
   }

   // Define server parameters address port
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   if (par.serverPort == 0) par.serverPort = SERVER_PORT;
   address.sin_port = htons (par.serverPort);

   // Bind socket with port
   if (bind (server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
      perror ("In main, Error socket bind"); 
      return EXIT_FAILURE;
   }

   // Listen connexions
   if (listen (server_fd, 3) < 0) {
      perror ("In main: error listening");
      return EXIT_FAILURE;
   }
   printf ("Server listen on port: %d\n", par.serverPort);

   while (true) {
      // Accept connexion
      if ((client_fd = accept (server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
         perror ("In main: Error accept");
         exit (EXIT_FAILURE);
      }
      handleClient (client_fd, &address);

      // Close connexion
      close (client_fd);
   }
   return EXIT_SUCCESS;
}

