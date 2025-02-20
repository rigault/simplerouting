#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <glib.h>
#include <locale.h>
#include "rtypes.h"
#include "rutil.h"
#include "engine.h"
#include "grib.h"
#include "polar.h"
#include "inline.h"

#define PORT             8080       // HTTP Request
#define MAX_N_WP         10         // Max number of Way points
#define MAX_SIZE_REQUEST 2048       // Max size from request client
#define LOG_FILE_NAME    "rcube.log" // Fime to log connexions

enum {REQ_TEST, REQ_ROUTING, REQ_BEST_DEP, REQ_RACE}; // type of request

char parameterFileName [MAX_SIZE_FILE_NAME];

typedef struct {
   int type;
   time_t epochStart;
   int timeStep;
   int nWp;
   Point wp [MAX_N_WP];
} ClientRequest;

/*! Make initialization  */
static void initRouting () {
   char directory [MAX_SIZE_DIR_NAME];
   char str [MAX_SIZE_LINE];
   char errMessage [MAX_SIZE_TEXT] = "";
   int readGribRet;
   int ret = readParam (parameterFileName);
   printf ("param File: %s\n", parameterFileName);
   if (par.mostRecentGrib) {  // most recent grib will replace existing grib
      snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir); 
      mostRecentFile (directory, ".gr", par.gribFileName, sizeof (par.gribFileName));
   }
   printf ("grib File: %s\n", par.gribFileName);
   if (par.gribFileName [0] != '\0') {
      readGribRet = readGribAll (par.gribFileName, &zone, WIND);
      if (readGribRet == 0) {
         fprintf (stderr, "In initScenarioOption, Error: Unable to read grib file: %s\n ", par.gribFileName);
         return;
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
   
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
}

/*! date for logging */
static const char* getCurrentDate () {
   static char date_buffer[100];
   time_t now = time(NULL);
   struct tm *tm_info = gmtime(&now);
   strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d %H:%M:%S UTC", tm_info);
   return date_buffer;
}

// Fonction pour écrire dans le fichier log
static void logRequest (const char *remote_addr, const char *user_agent) {
   const char *date = getCurrentDate();
    
   // Ouvrir le fichier log en mode append
   FILE *logFile = fopen (LOG_FILE_NAME, "a");
   if (logFile == NULL) {
      perror("Erreur d'ouverture du fichier log");
      return;
   }

   // Écrire la ligne de log
   fprintf(logFile, "%s; %s; %s\n", date, remote_addr, user_agent);
   fclose(logFile);
}

/*! decode request from client and fill ClientRequest structure 
   return true if correct false if impossible to decode */
static bool decodeHttpReq (const char *req, ClientRequest *clientReq, int maxNWp) {
   clientReq->nWp = 0;
   char **parts = g_strsplit(req, "&", -1);

   for (int i = 0; parts[i]; i++) {
      // type extraction
      if (g_str_has_prefix(parts[i], "type=")) {
         char *type_part = parts[i] + strlen("type=");
         if (type_part && sscanf(type_part, "%d", &clientReq->type) != 1) {
            g_strfreev(parts);
            return false;  // Erreur de parsing de timeStep
         }
      }
      
      else if (g_str_has_prefix(parts[i], "waypoints=")) {
         char *waypoints_part = parts[i] + strlen("waypoints="); // Partie après "waypoints="
         if (!waypoints_part) {
            g_strfreev(parts);
            return false;  // Paramètre "waypoints" manquant
         }
         // waypoints parsing
         char **wp_coords = g_strsplit(waypoints_part, ";", -1);
         for (int i = 0; wp_coords[i] && clientReq->nWp < maxNWp; i++) {
            double lat, lon;
            if (sscanf(wp_coords[i], "%lf,%lf", &lat, &lon) == 2) {
               clientReq->wp[clientReq->nWp].lat = lat;
               clientReq->wp[clientReq->nWp].lon = lon;
               clientReq->nWp += 1;
            }
         }
         g_strfreev(wp_coords);
      }

      // timeStep extraction
      else if (g_str_has_prefix(parts[i], "timeStep=")) {
         char *timeStep_part = parts[i] + strlen("timeStep=");
         if (timeStep_part && sscanf(timeStep_part, "%d", &clientReq->timeStep) != 1) {
            g_strfreev(parts);
            return false;  // Erreur de parsing de timeStep
         }
      }

      // timeStart extraction
      else if (g_str_has_prefix(parts[i], "timeStart=")) {
         char *timeStart_part = parts[i] + strlen("timeStart=");
         if (timeStart_part && sscanf(timeStart_part, "%ld", &clientReq->epochStart) != 1) {
            return false;  // Erreur de parsing de timeStart
         }
      }
   }
   g_strfreev(parts);
   
   return true;  
}

/*! check validity of parameters */
static bool checkParamAndUpdate (ClientRequest *clientReq, char *checkMessage, size_t maxLen) {
   // printf ("startInfo after: %s, startTime: %lf\n", asctime (&startInfo), par.startTimeInHours);
   if ((par.startTimeInHours < 0) || (par.startTimeInHours > zone.timeStamp [zone.nTimeStamp -1])) {
      snprintf (checkMessage, maxLen, "1: start Time not in Grib time window.");
      return false;
   }
   
   if (! isInZone (par.pOr.lat, par.pOr.lon, &zone) && (par.constWindTws == 0)) { 
      snprintf (checkMessage, maxLen, "2: Origin point not in Grib wind zone.");
      return false;
   }
   if (! isInZone (par.pDest.lat, par.pDest.lon, &zone) && (par.constWindTws == 0)) {
      snprintf (checkMessage, maxLen, "3: Destination point not Grib in wind zone.");
      return false;
   }
   par.pOr.lat = clientReq->wp [0].lat;
   par.pOr.lon = clientReq->wp [0].lon;
   par.pDest.lat = clientReq->wp [clientReq->nWp-1].lat;
   par.pDest.lon = clientReq->wp [clientReq->nWp-1].lon;
   for (int i = 1; i < clientReq->nWp -1; i += 1) {
      wayPoints.t[i-1].lat = clientReq->wp [i].lat; 
      wayPoints.t[i-1].lon = clientReq->wp [i].lon; 
   }
   wayPoints.n = clientReq->nWp - 2;
   par.tStep = clientReq->timeStep / 3600.0;
   
   return true;
}


/*! Handle client connection and launch actions */
static void handleClient(int client_fd, struct sockaddr_in *client_addr) {
   char buffer[MAX_SIZE_REQUEST];
   char *user_agent = "Unknown";
   ClientRequest clientReq = {0};
   clientReq.type = 1; // default: routingLaunch

   // Lire la requête HTTP
   int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
   if (bytes_read <= 0) {
      return;
   }
   buffer[bytes_read] = '\0'; // Ajouter un terminator de chaîne
   printf("Client Request:\n%s\n", buffer);

   // Extraire le corps de la requête
   char *post_data = strstr(buffer, "\r\n\r\n");
   if (post_data == NULL) {
      return;
   }
   // Extraire l'agent utilisateur de la requête
   char *user_agent_start = strstr(buffer, "User-Agent: ");
   if (user_agent_start) {
      user_agent_start += strlen("User-Agent: ");
      char *user_agent_end = strstr(user_agent_start, "\r\n");
      if (user_agent_end) {
         *user_agent_end = '\0';
         user_agent = user_agent_start;
      }
   }

   // Obtenir l'adresse IP du client
   char remote_addr[INET_ADDRSTRLEN];
   inet_ntop(AF_INET, &(client_addr->sin_addr), remote_addr, INET_ADDRSTRLEN);

   // Loguer l'information de la requête
   logRequest(remote_addr, user_agent);

   post_data += 4; // Ignorer les séparateurs de la requête HTTP

   bool ok = decodeHttpReq(post_data, &clientReq, MAX_N_WP);
   printf ("Found: %d, timeStep=%d, timeStart: %ld\n", clientReq.nWp, clientReq.timeStep, clientReq.epochStart);
   for (int i = 0; i < clientReq.nWp; i += 1)
      printf ("WP: %.2lf, %.2lf\n", clientReq.wp [i].lat, clientReq.wp[i].lon);

   if (! ok) {
      const char *error_response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nError";
      printf ("Error: %s\n", error_response);
      send(client_fd, error_response, strlen(error_response), 0);
      return;
   }
   GString *res = g_string_new ("");
   char checkMessage [MAX_SIZE_LINE];
   
   switch (clientReq.type) {
      case REQ_ROUTING:
         time_t theTime0 = gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]);
         par.startTimeInHours = (clientReq.epochStart - theTime0) / 3600.0;
         printf ("Start Time Epoch: %ld, theTime0: %ld\n", clientReq.epochStart, theTime0);
         printf ("Start Time in Hours after Grib: %.2lf\n", par.startTimeInHours);
         if (checkParamAndUpdate (&clientReq, checkMessage, sizeof (checkMessage))) {
            routingLaunch ();
            GString *jsonRoute = routeToJson (0);
            g_string_append_printf (res, "%s", jsonRoute->str);
            g_string_free (jsonRoute, TRUE);
         }
         else {
            g_string_append_printf (res,"{\"_checkId\":\"%s\"}", checkMessage);
         }
         break;
      case REQ_TEST:
         g_string_append_printf (res,"{\"_test\":\"OK\"}"); 
         break;
      case REQ_BEST_DEP:
         bestTimeDeparture (); // ATT not terminated
         break;
      case REQ_RACE:
         allCompetitors (); // ATT not terminated
         break;
      default:;
   }
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
   send(client_fd, response->str, strlen(response->str), 0);
   g_string_free (response, TRUE);
}

int main() {
   int server_fd, client_fd;
   struct sockaddr_in address;
   int addrlen = sizeof(address);

   if (setlocale (LC_ALL, "C") == NULL) {                // very important for printf decimal numbers
      fprintf (stderr, "Server Error: setlocale failed");
      exit (EXIT_FAILURE);
   }

   strncpy (parameterFileName, PARAMETERS_FILE, sizeof (parameterFileName) - 1);

   // Création du socket
   if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
      perror("Échec de la création du socket");
      exit (EXIT_FAILURE);
   }

   // Définir les paramètres du serveur (adresse et port)
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons(PORT);

   // Lier le socket au port
   if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
      perror("Échec de la liaison du socket");
      exit(EXIT_FAILURE);
   }

   // Écouter les connexions entrantes
   if (listen(server_fd, 3) < 0) {
      perror("Échec de l'écoute sur le port");
      exit(EXIT_FAILURE);
   }

   printf("Serveur en écoute sur le port %d...\n", PORT);
   initRouting ();

   while (true) {
      // Accepter une connexion
      if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
         perror("Échec de l'acceptation");
         exit(EXIT_FAILURE);
      }
      handleClient(client_fd, &address);

      // Fermer la connexion
      close(client_fd);
   }
   return EXIT_SUCCESS;
}

