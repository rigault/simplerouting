/*! \mainpage  GPS server
* \section Compilation
* sudo gcc -Wall -pedantic -std=c99 gpserver.c -o gpsserver
* \section Usage
* \subsection Synopsys
* sudo ./gpsserver [port]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_COUNT0      100
#define MAX_COUNT1      3
#define BUFFER_SIZE     8192
#define HELP            "Synopsys : sudo ./gpsserver <port>"
#define VERSION         "V. 2025-04"
#define DESCRIPTION     "GPS Server by Rene Rigault"
#define FLOW_INPUT      "/dev/ttyACM0" // convertisseur NMEA USB
//#define FLOW_INPUT    "/dev/ttyUSB0" // convertisseur AIS
#define MAX_ROWS        128
#define MAX_SIZE_OUTPUT 4096
#define SIZE_BUFFER     20
#define SIZE_DATE_TIME  10

// (à remplacer par la vraie)
bool testGpsToJson(char *buffer, size_t size) {
   snprintf(buffer, size,
      "{\n"
      "  \"time\": \"2025-03-24 14:36:41 UTC\",\n"
      "  \"lat\": -0.016667,\n"
      "  \"lon\": -2.347016,\n"
      "  \"alt M\": 24.80,\n"
      "  \"sog\": 1.24,\n"
      "  \"cog\": -1.00,\n"
      "  \"numSat\": 4,\n"
      "  \"status\": \"V\",\n"
      "  \"quality\": 1,\n"
      "  \"hdop\": 3.40\n"
      "}");
   return true;
}
FILE *flowInput;
/*! buffer de SIZE_BUFFER trames 
struct {
   int iNext;
   int nFrame;
   char t [SIZE_BUFFER][MAX_ROWS];
} frameBuff = {0, 0};*/
    
/*! valeurs trouvée dans les trames NMEA */
struct {
   char time [SIZE_DATE_TIME] ;
   char date [SIZE_DATE_TIME] ;
   char status;
   float lat;
   char NS;
   float lon;
   char EW;
   float sog;
   float cog;
   int quality;
   int numSV;
   float hdop;
   float alt;
   char uAlt;
} gpsRecord;

/*! renvoie le checksum de la trame (ou exclusif de tout entre '$' et '*') */
unsigned int checksum (char *s) {
   unsigned int check = 0;
   if (*s == '$') s++; // elimine $
   while (*s && *s != '*')
      check ^= *s++;
   return check;
}

/*! Verifie que le checksum lu en fin de trame est égal au checksum calcule */
bool checksumOK (char *s) {
   return (strtol (s + strlen (s) - 3, NULL, 16) == checksum (s));
}

/*! ajoute une trame dans le buffer de trames 
void addFrameBuff (char *line) { 
   strcpy (frameBuff.t [frameBuff.iNext], line);
   frameBuff.iNext = (frameBuff.iNext >= SIZE_BUFFER - 1) ? 0 : frameBuff.iNext + 1;
   if (frameBuff.nFrame < SIZE_BUFFER) 
      frameBuff.nFrame += 1;
}*/

/*! Produit dans str la trame decodee au format JSON */
void toJson (char * str, size_t len) {
   char d [SIZE_DATE_TIME];
   char t [SIZE_DATE_TIME];
   strncpy (d, gpsRecord.date, 6);
   strncpy (t, gpsRecord.time, 6);
   char strTime [100];
   double lat, lon;
   lat = ((int) gpsRecord.lat / 100) + (gpsRecord.lat - ((int) gpsRecord.lat / 100) * 100) / 60.0;
   if (gpsRecord.NS == 'S') lat = -lat;
   lon = ((int) gpsRecord.lon / 100) + (gpsRecord.lon - ((int) gpsRecord.lon / 100) * 100) / 60.0;
   if (gpsRecord.EW == 'W') lon = -lon;
   char uAlt = gpsRecord.uAlt;
   if (! isalnum (uAlt)) uAlt = ' ';
   char status = gpsRecord.status; 
   if (! isalnum (status)) status = '-';

   snprintf (strTime, sizeof (strTime), "20%c%c-%c%c-%c%c %c%c:%c%c:%c%c UTC", \
      d[4], d[5], d[2], d[3], d[0], d[1], t[0], t[1], t[2], t[3], t[4], t[5]);
   if (strcmp (strTime, "20") == 0)
      snprintf (strTime, sizeof (strTime), "NA");
   
   snprintf (str, len,
        "{\n"
        "  \"time\": \"%s\",\n"
        "  \"lat\": %.6lf,\n"
        "  \"lon\": %.6lf,\n"
        "  \"alt %c\": %.2f,\n"
        "  \"sog\": %.2lf,\n"
        "  \"cog\": %.2lf,\n"
        "  \"numSat\": %d,\n"
        "  \"status\": \"%c\",\n"
        "  \"quality\": %d,\n"
        "  \"hdop\": %.2lf\n"
        "}\n",
        strTime, lat, lon, uAlt, gpsRecord.alt, gpsRecord.sog, gpsRecord.cog,
        gpsRecord.numSV, status, gpsRecord.quality, gpsRecord.hdop
   );
}

/*! recopie s dans d en replaçant les occurences de ,, par ,-1, */
void strcpymod (char *d, const char *s) {
   char previous = '\0';
   while (*s) {
      if ((*s == ',') && (previous == ',')) {
         *d++ = '-';
         *d++ = '1';
      }
      previous = *s;
      *d++ = *s++;
   }
   *d = '\0';
}

/*! decode les trames */
/* GPRMC : Recommended Minimum data */ 
/* GPGGA : Global Positioning System Fix Data */
/* GPGLL : Latitude Longitude */
/* vraie si a reussit a decoder la trame */
bool decode (char *line) {
   char lig [MAX_ROWS];
   strcpymod (lig, line);
   if (sscanf (lig, "$GPRMC, %[0-9.], %c, %f, %c, %f, %c, %f, %f, %[0-9],", gpsRecord.time, &gpsRecord.status, \
         &gpsRecord.lat, &gpsRecord.NS, &gpsRecord.lon, &gpsRecord.EW, &gpsRecord.sog, &gpsRecord.cog, gpsRecord.date) >= 1) 
      return true;
   if (sscanf (lig, "$GPGGA, %[0-9.], %f, %c, %f, %c, %d, %d, %f, %f, %c", gpsRecord.time , &gpsRecord.lat, &gpsRecord.NS, \
         &gpsRecord.lon, &gpsRecord.EW, &gpsRecord.quality, &gpsRecord.numSV, &gpsRecord.hdop, &gpsRecord.alt, &gpsRecord.uAlt) >= 1) 
      return true; 
   if (sscanf (lig, "$GPGLL, %f, %c, %f, %c, %[0-9.], %c", &gpsRecord.lat, &gpsRecord.NS, &gpsRecord.lon, &gpsRecord.EW, \
         gpsRecord.time, &gpsRecord.status ) >= 1)
      return true; 
   return false;
}

/*! produit une sortie JSON de la trame analysée */
bool gpsToJson (char *strJson, size_t len) {
   char row [MAX_ROWS];
   strJson [0] = '\0';
   char *line = &row [0];
   int count0 = 0;
   int count1 = 0;
   
   while (count0 < MAX_COUNT0) {
      if (((line = fgets (line, MAX_ROWS, flowInput)) != NULL) && (*line == '$') && (checksumOK (row))) {
         //addFrameBuff (line);
         if (decode (line)) {
            toJson (strJson, len);
            printf ("gpsToJson: \n%s", strJson);
            if (count1 >= MAX_COUNT1) return true;
            count1 += 1;
         }
      }
      count0 += 1;
   }
   return false;
}

void handleClient (int client_fd) {
   char buffer [BUFFER_SIZE];
   ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
   if (received < 0) {
      perror("recv");
      close(client_fd);
      return;
   }
   buffer[received] = '\0';
   printf ("received: %s\n", buffer);
   if (strncmp(buffer, "OPTIONS", 7) == 0) {
      printf ("Error Options\n");
      const char *optionsResponse =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
      send(client_fd, optionsResponse, strlen(optionsResponse), 0);
      close(client_fd);
      return;
   }
   // search "gps" in request
   if (strstr(buffer, "gps") != NULL) {
      if ((flowInput = fopen (FLOW_INPUT, "r")) == NULL) {
         printf ("cannot open input flow\n");
         const char *err = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
         send(client_fd, err, strlen(err), 0);
         close(client_fd);
         return;
      }

      printf ("gps found\n");
      char json[MAX_SIZE_OUTPUT];
      if (gpsToJson(json, sizeof(json))) {
         char header[256];
         snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n", strlen(json));

         send(client_fd, header, strlen(header), 0);
         send(client_fd, json, strlen(json), 0);
      } else {
         printf ("No gps data found\n");
         const char *err = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
         send(client_fd, err, strlen(err), 0);
      }
      fclose (flowInput);
   } else {
      const char *not_found = "HTTP/1.1 404 Not Found\r\n\r\n";
      send(client_fd, not_found, strlen(not_found), 0);
   }
   close(client_fd);
}

int main(int argc, char *argv[]) {
   if ((argc != 2) || ((argc == 2) && strcmp (argv [1], "-h") == 0)) {
      printf ("%s\n%s\n%s\n", HELP, VERSION, DESCRIPTION);
      exit (EXIT_SUCCESS);
   }

   int port = atoi(argv[1]);

   int server_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (server_fd < 0) {
      perror("socket");
      return EXIT_FAILURE;
   }

   int opt = 1;
   setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

   struct sockaddr_in addr = {0};
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(port);

   if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      perror("bind");
      close(server_fd);
      return EXIT_FAILURE;
   }
   if (listen(server_fd, 10) < 0) {
      perror("listen");
      close(server_fd);
      return EXIT_FAILURE;
   }

   printf("Listening on port %d...\n", port);

   while (true) {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
      if (client_fd < 0) {
         perror("accept");
         continue;
      }

      handleClient (client_fd);
   }

   close(server_fd);
   return EXIT_SUCCESS;
}

