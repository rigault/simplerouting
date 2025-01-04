/*! compilation: gcc -c aisgps.c `pkg-config --cflags glib-2.0` */

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#endif

#include <glib.h>
#include <stdio.h>
#include <float.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include "rtypes.h"  
#include "rutil.h"
#include "aisgps.h"

#define AIS_BAUD_RATE_UNIX     B38400       // for AIS NMEA USB configuration Unix
#define GPS_BAUD_RATE_UNIX     B9600        // for GPS NMEA USB configuration Unix
#define AIS_BAUD_RATE_WINDOWS  CBR_38400    // for AIS NMEA USB configuration Windows
#define GPS_BAUD_RATE_WINDOWS  CBR_9600     // for GPS NMEA USB configuration Windows

#define AIS_CHAR_OFFSET       48            // for AIS decoding character
#define T_SHIP_MAX            (30 * 60)     // 30 minutes

#define SIZE_DATE_TIME        10            // Date and time in NMEA GPS frame
#define MAX_SIZE_NMEA         1024          // for NMEA Frame buffer size

MyGpsData my_gps_data; 
GHashTable *aisTable;

/*! value in NMEA GPS frame*/
struct {
   char time [SIZE_DATE_TIME];
   char date [SIZE_DATE_TIME];
   char status;
   double lat;
   char NS;
   double lon;
   char EW;
   double sog;
   double cog;
   int quality;
   int numSV;
   double hdop;
   double alt;
   char uAlt;
} gpsRecord;

/*! gps information for helpInfo */
char *nmeaInfo (char *strGps, int maxLen) {
   char str [MAX_SIZE_LINE];
   strGps [0] = '\0';
   for (int i = 0; i < par.nNmea; i++) {
      snprintf (str, MAX_SIZE_LINE, "NMEA input: %s, Index Speed: %d %s", 
         par.nmea [i].portName, 
         par.nmea [i].speed,
         (par.nmea [i].open) ? "Open\n" : "Closed\n");
      g_strlcat (strGps, str, maxLen);
   }
   return strGps;
}

/* return country name associated to mid, NULL if not found */
static char* midToCountry (const char *fileName, int mid, char *country) {
   FILE *f;
   int n = -1;
   char line [MAX_SIZE_LINE];
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In midToCountry, Error file not found: %s\n", fileName);
      return NULL;
   }
   while (fgets (line, MAX_SIZE_LINE, f) != NULL) {
      if ((sscanf (line, "%d %32[^\n]", &n, country) > 1) && (n == mid))
         break;
   }
   fclose (f);
   return (mid == n) ? country : NULL;
}

/*! Function to calculate the new position after moving a distance at a given bearing */
static void movePosition (double lat, double lon, double sog, double cog, double t, double *newLat, double *newLon) {
   double d = sog * t; // distance traveled
   double bearing_rad = cog * DEG_TO_RAD;

   double lat1 = lat * DEG_TO_RAD;
   double lon1 = lon * DEG_TO_RAD;

   double new_lat = lat1 + (d * cos(bearing_rad)) / EARTH_RADIUS;
   double new_lon = lon1 + (d * sin(bearing_rad)) / (EARTH_RADIUS * cos(lat1));

   *newLat = new_lat * RAD_TO_DEG;
   *newLon = new_lon * RAD_TO_DEG;
}

/*! collision detection function 
* Let ShipA at position lat1, lonB moving at speed sogA with course cogA
* Let ShipB at position latB, lonB moving at speed sogB with course cogB
the function returns the position where the 2 ships are at closest point
returns -1 if ships are moving away, the minimal distance otherwise */
static double collisionDetection (double latA, double lonA, double sogA, double cogA,
                           double latB, double lonB, double sogB, double cogB,
                           double *latC, double *lonC) {

   //convert lat/lon to Cartesian coordinates assuming a flat Earth
   double xA = lonA * DEG_TO_RAD * EARTH_RADIUS * cos (latA * DEG_TO_RAD);
   double yA = latA * DEG_TO_RAD * EARTH_RADIUS;
   double xB = lonB * DEG_TO_RAD * EARTH_RADIUS * cos (latB * DEG_TO_RAD);
   double yB = latB * DEG_TO_RAD * EARTH_RADIUS;

   double vAx = sogA * cos(cogA * DEG_TO_RAD);
   double vAy = sogA * sin(cogA * DEG_TO_RAD);
   double vBx = sogB * cos(cogB * DEG_TO_RAD);
   double vBy = sogB * sin(cogB * DEG_TO_RAD);

   double dx = xB - xA;
   double dy = yB - yA;
   double dvx = vBx - vAx;
   double dvy = vBy - vAy;

   // solve equation a*(t*t) + b*t + c = 0
   double a = dvx * dvx + dvy * dvy;
   double b = 2 * (dx * dvx + dy * dvy);
   double c = dx * dx + dy * dy;

   double discriminant = b * b - 4 * a * c;
   if (discriminant < 0) {
      return -1; // Ships are moving away
   }
   double t1 = (-b - sqrt(discriminant)) / (2 * a);
   double t2 = (-b + sqrt(discriminant)) / (2 * a);
   double t = (t1 >= 0 && t1 <= t2) ? t1 : t2;
   if (t < 0) {
      return -1; // Ships are moving away
   }

   double latC_A, lonC_A, latC_B, lonC_B;
   movePosition(latA, lonA, sogA, cogA, t, &latC_A, &lonC_A);
   movePosition(latB, lonB, sogB, cogB, t, &latC_B, &lonC_B);

   *latC = (latC_A + latC_B) / 2;
   *lonC = (lonC_A + lonC_B) / 2;

   double minDistance = sqrt((dx + dvx * t) * (dx + dvx * t) + (dy + dvy * t) * (dy + dvy * t));
   return minDistance; // nautical miles
}

/*! Hash function for MMSI keys */
static guint mmsiHash (gconstpointer key) {
   return g_direct_hash (key);
}

/*! Equality function for  MMSI Kets */
static gboolean mmsiEqual (gconstpointer a, gconstpointer b) {
   return g_direct_equal (a, b);
}

/*! free ais ata */
static void freeAisSgpt (gpointer data) {
   free (data);
}

/*! AIS hash table init */
bool aisTableInit (void) {
   if ((aisTable = g_hash_table_new_full (mmsiHash, mmsiEqual, NULL, freeAisSgpt)) == NULL) {
      fprintf (stderr, "In aisTableInit, Error: Init Hash table for AIS \n");
      return false;
   }
   else
      return true;
}

/*! just for testing */
bool testAisTable () {
   AisRecord *ship = calloc (1, sizeof(AisRecord));
   if (!ship) {
      fprintf (stderr, "In testAisTable, Erreur d'allocation mémoire pour ship1\n");
      return false;
   }
    
   ship->mmsi = 227191400;
   ship->lat = 48.858844;
   ship->lon = 2.294351;
   g_strlcpy (ship->name, "hello", 21);
   ship->lat = 45.2;
   ship->lon = -2.5;
   ship->cog = 45;
   ship->lastUpdate = time (NULL) - 60;
   g_hash_table_insert(aisTable, GINT_TO_POINTER(ship->mmsi), ship);

   ship = calloc (1, sizeof(AisRecord));
   ship->mmsi = 607191800;
   g_strlcpy (ship->name, "bobo", 21);
   ship->lat = 45.3;
   ship->lon = -2.2;
   ship->cog = -45;
   ship->lastUpdate = time (NULL) - 3600;
   g_hash_table_insert(aisTable, GINT_TO_POINTER(ship->mmsi), ship);

   ship = calloc (1, sizeof(AisRecord));
   ship->mmsi = 999193900;
   g_strlcpy (ship->name, "coco", 21);
   ship->lat = 45.4;
   ship->lon = -3;
   ship->cog = 180;
   ship->minDist = 140;
   ship->lastUpdate = time (NULL) ;
   g_hash_table_insert(aisTable, GINT_TO_POINTER(ship->mmsi), ship);

   return true;
}

/*! ais information to string */
int aisToStr (char *str, size_t maxLen) {
   char strLat [MAX_SIZE_NAME] = "", strLon [MAX_SIZE_NAME] = "";
   char line [MAX_SIZE_LINE];
   char strDate [MAX_SIZE_DATE];
   char country [32];
   int count = 0;

   // use GHashTableIter to iterare on all elements
   GHashTableIter iter;
   gpointer key, value;
   g_hash_table_iter_init (&iter, aisTable);

   snprintf (str, MAX_SIZE_LINE, \
      "Name                  Country       MinDist      MMSI        Lat          Lon    SOG  COG LastUpdate                 \n");
   while (g_hash_table_iter_next(&iter, &key, &value)) {
      AisRecord *ship = (AisRecord *)value;
      // printf ("Ship MMSI=%d, Lat=%f, Lon=%f\n", ship->mmsi, ship->lat, ship->lon);
      if ((midToCountry (par.midFileName, ship->mmsi / 1000000, country)) == NULL)
         g_strlcpy (country, "NA", 3);
      country [12] = '\0'; // truncated before snprint
      
      snprintf (line, MAX_SIZE_LINE, "%-21s %-12s %8.0d %9d %-12s %-12s %6.2lf %4d %-s\n",
         ship->name,
         country,
         ship->minDist,
         ship->mmsi,
         latToStr (ship->lat, par.dispDms, strLat, sizeof (strLat)), 
         lonToStr (ship->lon, par.dispDms, strLon, sizeof (strLon)),
         ship->sog,
         ship->cog,
         epochToStr (ship->lastUpdate, false, strDate, MAX_SIZE_DATE));

      g_strlcat (str, line, maxLen);
      count += 1;
   }
   return count;
}

/*! remove ship not updated since tMax */
static void removeOldShips (time_t tMax) {
   GHashTableIter iter;
   gpointer key, value;
   time_t current_time = time (NULL);

   g_hash_table_iter_init (&iter, aisTable);
   while (g_hash_table_iter_next (&iter, &key, &value)) {
      AisRecord *ship = (AisRecord *) value;
      // printf ("%d %ld\n", ship->mmsi, ship->lastUpdate);
      if (difftime (current_time, ship->lastUpdate) > tMax) {
         g_hash_table_iter_remove (&iter); // Supprime l'élément de la table
      }
   }
}

/*! copy s to d and replace ",," occurence by ",-1," */
static void strcpymod (char *d, const char *s) {
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

/*! convert AIS charaecter in bits */
static int aisCharToBits (char c) {
   int value = c - AIS_CHAR_OFFSET;
   if (value > 40) value -= 8;
   return value;
}

/*! bits extraction from frame */
static void extract_bits (const char *payload, char *bits) {
   int len = strlen (payload);
   bits[0] = '\0';
   for (int i = 0; i < len; i++) {
      int value = aisCharToBits (payload[i]);
      for (int j = 5; j >= 0; j--) {
         bits [6 * i + (5 - j)] = ((value >> j) & 1) ? '1' : '0';
      }
   }
   bits [6 * len] = '\0';
}

/*! extract integer value within a bit string */
static int getIntFromBits (const char *bits, int start, int length) {
   char buf [33];
   g_strlcpy (buf, bits + start, length);
   buf [length] = '\0';
   return (int) strtol(buf, NULL, 2);
}

/*! extract signed integer value within a bit string */
static int getSignedIntFromBits (const char *bits, int start, int length) {
   int value = getIntFromBits(bits, start, length);
   if (value & (1 << (length - 1))) {
      value -= (1 << length);          // signed value conversion
   }
   return value;
}

/*! extract string within a bit string */
static void getStringFromBits (const char *bits, int start, int length, char *result) {
   int byteLen = length / 6;
   for (int i = 0; i < byteLen; i++) {
      int value = getIntFromBits (bits, start + i * 6, 6);
      result[i] = (value < 32) ? value + 64 : value; // ASCII conversion
   }
   result[byteLen] = '\0';
}

/*! get ship with mmsi or create the ship if it does nos exist yet */
static AisRecord *getRecord (int mmsi) {
   // check if ship exist
   AisRecord *ship = g_hash_table_lookup (aisTable, GINT_TO_POINTER (mmsi));
   if (ship == NULL) { // ship does not exist
      if ((ship = calloc (1, sizeof (AisRecord))) == NULL) {
         fprintf (stderr, "In getRecord, Error in getRecord Calloc Ais recod fail\n");
         return NULL;
      }
      ship->mmsi = mmsi; // new mmsi because ship does not exist
      g_hash_table_insert (aisTable, GINT_TO_POINTER(ship->mmsi), ship);
   }
   return ship;
}

/*! convert to Epoch time date and time found in NMEA frame */
static time_t strToEpoch (char date [], char time []) {
   struct tm tm0 = {0};
   //time_t t;
   tm0.tm_year = (date [4] - '0') * 10 + (date [5] - '0') + 2000 - 1900;
   tm0.tm_mon = ((date [2] - '0') * 10 + date [3] - '0') - 1;
   tm0.tm_mday = (date [0] - '0') * 10 + date [1] - '0';
   tm0.tm_hour = (time [0] - '0') * 10 + time [1] - '0';
   tm0.tm_min = (time [2] - '0') * 10 + time [3] - '0';
   tm0.tm_sec = (time [4] - '0') * 10 + time [5] - '0';
   tm0.tm_sec += offsetLocalUTC ();
   tm0.tm_isdst = -1;                     // avoid adjustments with hours summer winter
   return mktime (&tm0);
}

/*! copy gps data in structure */
static void copyGpsData () {
   // printResult (true, true);
   if (isfinite(gpsRecord.lat) && isfinite(gpsRecord.lon) && ((gpsRecord.lat != 0 || gpsRecord.lon != 0))) {
      my_gps_data.lat = (int) gpsRecord.lat/100 + fmod (gpsRecord.lat, 100) / 60;
		if (gpsRecord.NS == 'S')
		   my_gps_data.lat = - my_gps_data.lat;
      my_gps_data.lon = (int) gpsRecord.lon/100 + fmod (gpsRecord.lon, 100) / 60;
		if (gpsRecord.EW == 'W')
		   my_gps_data.lon = - my_gps_data.lon;
      my_gps_data.alt = gpsRecord.alt;
      my_gps_data.cog = gpsRecord.cog;
      my_gps_data.sog = MS_TO_KN * gpsRecord.sog;
      my_gps_data.status = gpsRecord.status;
      my_gps_data.nSat = gpsRecord.numSV;
      my_gps_data.time = strToEpoch (gpsRecord.date, gpsRecord.time);
      my_gps_data.OK = true;
   }
   else 
      my_gps_data.OK = false;
}

/* decode AIS Paylod fields and fill aisRecord hash table */
static void decodeAisPayload (const char *payload) {
   char bits [1000] = {0};
   const int MAX_SOG = 1000; 
   
   extract_bits (payload, bits);
   printf ("Bits: %s\n", bits);

   int messageId = getIntFromBits(bits, 0, 6); // message ID conversion
   int mmsi = getIntFromBits(bits, 8, 30);      // MMSI extraction

   char shipName [MAX_SIZE_SHIP_NAME] = {0};
   int latitude = NIL;
   int longitude = NIL;
   int partNumber = NIL;       // for message with ID = 24
   int speed = NIL;
   int course = NIL;
   double latC, lonC;          // possible  collision point
   AisRecord *ship;

   switch (messageId) {
   case 1: case 2: case 3:    // Class A report position
      speed = getIntFromBits (bits, 50, 10);        
      longitude = getSignedIntFromBits (bits, 61, 28);
      latitude = getSignedIntFromBits (bits, 89, 27);
      course = getIntFromBits (bits, 116, 12);
      break;
   case 5:                    // Class A static information
      getStringFromBits (bits, 112, 120, shipName);      
      char *pt = strchr (shipName, '@');
      if (pt != NULL) *pt = '\0';                          
      break;
   case 18:                   // Class B report position
      speed = getIntFromBits (bits, 46, 10);
      longitude = getSignedIntFromBits (bits, 57, 28);
      latitude = getSignedIntFromBits (bits, 85, 27);
      course = getIntFromBits (bits, 112, 12);
      break;
   case 24:                   // Class B static information
      partNumber = getSignedIntFromBits (bits, 38, 2);
      if (partNumber == 0) {   // part A
         getStringFromBits (bits, 40, 120, shipName);
         char *pt = strchr (shipName, '@');
         if (pt != NULL) *pt = '\0';                         // end name
      }
      else {                  // part B
         printf ("Unsupported part B in message: %d\n\n", messageId); 
      }
      break;
   default:
      printf ("Unsupported message type: %d\n\n", messageId); 
      return;
   }

   ship = getRecord (mmsi);   
   if (ship == NULL) return;

   if ((speed >= 0) && (speed < MAX_SOG)) ship->sog = ((double) speed) / 10.0;
   if (latitude != NIL) ship->lat = ((double) latitude) / 600000.0;
   if (longitude != NIL) ship->lon = ((double) longitude) / 600000.0;
   if ((course >= 0) && (course <= 3600)) ship->cog = ((double) course) / 10.0;
   if (shipName [0] != '\0') g_strlcpy (ship->name, shipName, MAX_SIZE_SHIP_NAME);
   ship->lastUpdate = time (NULL);

   if (my_gps_data.OK) {
      int x = collisionDetection (my_gps_data.lat, my_gps_data.lon, my_gps_data.sog, my_gps_data.cog,
                               ship->lat, ship->lon, ship->sog, ship->cog, &latC, &lonC);
      if (x < 0) 
         ship->minDist = x;
      else if (ship->minDist >= MILLION)
         ship->minDist = -2;
      else ship -> minDist = 1852 * x; // in meters
   }
   else
      ship->minDist = -3;

   printf("Message ID: %d %s\n", messageId, (partNumber == 0) ? "A" : (partNumber == 1) ? "B" : "");
   printf("MMSI: %d\n", mmsi);
   if (shipName [0] != '\0') printf ("Ship Name: %s\n", shipName);
   if (latitude != NIL) printf ("Latitude: %.2lf\n", (double)latitude/600000.0);
   if (longitude != NIL) printf ("Longitude: %.2lf\n", (double)longitude/600000.0);
   if (speed != NIL) printf ("Speed Over Ground (SOG): %.1f knots\n", ((double) speed) / 10.0);
   if (course != NIL) printf ("Course Over Ground (COG): %.1f degrees\n", ((double) course) / 10.0);
   printf ("\n");
}

/*!GPPS decoding  
 * GPRMC : Recommended Minimum data 
 * GPGGA : Global Positioning System Fix Data
 * GPGLL : Latitude Longitude 
 * return true if frame is decoded 
 * AIS frames 
 * AIVDM :   
 * AIVDO :  
 * $AIVDM,1,1,,B,15Muq30003wtPj8MrbQ@bDwt2<0b,0*34 
  * return true if frame decoded */
static bool decodeNMEA (const char *line) {
   char lig [MAX_SIZE_NMEA];
   //fields found in NMEA AIS Frame*/
   struct {
      char charType;       // M or O because AIVDM or AIDDO, not used
      int nb;              // total number of AIS segments (1 or 2 in general), not used
      int i;               // indice of fragment, only indice 1 is supported)
      int x;               // not used
      char channel;        // A or B, not used
      char data64  [128];  // AIS data coded as base 64.
      int pad;             // not used
   } aisFrame;

   if (line [0] == '!')
      printf ("%s\n", line);

   strcpymod (lig, line);
   if ((sscanf (lig, "!AIVD%c, %d, %d, %d, %c, %128s, %d,", &aisFrame.charType, &aisFrame.nb, &aisFrame.i, &aisFrame.x,\
         &aisFrame.channel, aisFrame.data64, &aisFrame.pad) >= 1) && (aisFrame.i == 1)) { // only segment 1 supported
      decodeAisPayload (aisFrame.data64);
      return true;
   }
   if (sscanf (lig, "$GPRMC, %64[0-9.], %c, %lf, %c, %lf, %c, %lf, %lf, %64[0-9],", gpsRecord.time, &gpsRecord.status, \
         &gpsRecord.lat, &gpsRecord.NS, &gpsRecord.lon, &gpsRecord.EW, &gpsRecord.sog, &gpsRecord.cog, gpsRecord.date) >= 1) {
      copyGpsData ();
      return true;
   }
   if (sscanf (lig, "$GPGGA, %64[0-9.], %lf, %c, %lf, %c, %d, %d, %lf, %lf, %c", gpsRecord.time , &gpsRecord.lat, &gpsRecord.NS, \
         &gpsRecord.lon, &gpsRecord.EW, &gpsRecord.quality, &gpsRecord.numSV, &gpsRecord.hdop, &gpsRecord.alt, &gpsRecord.uAlt) >= 1) {
      copyGpsData ();
      return true; 
   }
   if (sscanf (lig, "$GPGLL, %lf, %c, %lf, %c, %64[0-9.], %c", &gpsRecord.lat, &gpsRecord.NS, &gpsRecord.lon, &gpsRecord.EW, \
         gpsRecord.time, &gpsRecord.status ) >= 1) {
      copyGpsData ();
      return true; 
   }

   return false;
}

/*! return frame checksum de la trame (XOR of all between '$' or '!' and '*') */
static unsigned int checksum (char *str) {
   unsigned char check = 0;
   char *s = str;
   if ((*s == '$') || (*s == '!')) s++; // elimine $
   while (*s && *s != '*')
      check ^= *s++;
   return check;
}

/*! check read checksum at end of frame equals calculated checksumm  */
static bool checksumOK (char *s) {
   char *ptCheck ;
   if ((ptCheck = strrchr (s, '*')) == NULL)
      return false;
   else {
      return (strtoul (ptCheck +1, NULL, 16) == checksum (s));
   }
}

#ifdef _WIN32
static HANDLE configureSerialPort (const char *portName, int baudRate) {
   HANDLE hSerial = CreateFile (portName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
   if (hSerial == INVALID_HANDLE_VALUE) {
      fprintf (stderr, "In configureSerialPort: cannot open input flow:\n");
      return INVALID_HANDLE_VALUE;
   }

   DCB dcbSerialParams = {0};
   dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
   if (!GetCommState (hSerial, &dcbSerialParams)) {
      fprintf (stderr, "In configureSerialPort: Error getting state: \n");
      CloseHandle(hSerial);
      return INVALID_HANDLE_VALUE;
   }

   dcbSerialParams.BaudRate = baudRate;
   dcbSerialParams.ByteSize = 8;
   dcbSerialParams.StopBits = ONESTOPBIT;
   dcbSerialParams.Parity = NOPARITY;
   if (!SetCommState(hSerial, &dcbSerialParams)) {
      fprintf (stderr, "In configureSerialPort: Error setting state: \n");
      CloseHandle(hSerial);
      return INVALID_HANDLE_VALUE;
   }

   COMMTIMEOUTS timeouts = {0};
   timeouts.ReadIntervalTimeout = 50;
   timeouts.ReadTotalTimeoutConstant = 50;
   timeouts.ReadTotalTimeoutMultiplier = 10;
   timeouts.WriteTotalTimeoutConstant = 50;
   timeouts.WriteTotalTimeoutMultiplier = 10;

   if (!SetCommTimeouts(hSerial, &timeouts)) {
      fprintf (stderr, "In configureSerialPort: Error setting timeouts: \n");
      CloseHandle(hSerial);
      return INVALID_HANDLE_VALUE;
   }

   return hSerial;
}

/*! read serial USB port to get NMEA GPS AIS info, Window environment */
void *getNmea (gpointer x) {
   int index = GPOINTER_TO_INT (x);
   char buffer[MAX_SIZE_NMEA];
   char tempBuffer[MAX_SIZE_NMEA * 2] = {0};
   DWORD bytesRead;
   size_t tempBufferPos = 0;

   HANDLE hSerial = configureSerialPort (par.nmea [index].portName, par.nmea [index].speed);

   if (hSerial == INVALID_HANDLE_VALUE) {
      fprintf (stderr, "In getNmea : Error, configuration serial port: %s\n", par.nmea [index].portName);
      return NULL;
   }

   printf ("In getNmea     : %s open with speed: %d\n", par.nmea [index].portName, par.nmea [index].speed);
   par.nmea [index].open = true;
   while (true) {
      if (index == AIS_INDEX) removeOldShips (T_SHIP_MAX);
      if (ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && (bytesRead > 0)) {
         buffer[bytesRead] = '\0';
	      // printf ("buffer: %s\n", buffer);

         // Ajoutez les nouvelles données dans le tampon temporaire
         g_strlcat (tempBuffer, buffer, bytesRead);
         tempBufferPos += bytesRead;

         // Recherchez des trames complètes dans le tampon temporaire
         char *start = tempBuffer;
         char *end = NULL;

         while ((end = strchr(start, '\n')) != NULL) {
            *end = '\0'; // Terminez la trame à la fin de ligne
            if (((start[0] == '!') || (start[0] == '$')) && checksumOK(start)) {
               decodeNMEA (start);
            }
            start = end + 1;
         }

         // Déplacez les données non traitées au début du tampon temporaire
         tempBufferPos = strlen(start);
         memmove(tempBuffer, start, tempBufferPos);
         tempBuffer[tempBufferPos] = '\0';
      }
   }

   CloseHandle(hSerial);
   return NULL;
}

#else
// UNIX part
/*! Serial port configuration, Unix */
static int configureSerialPort (int fd, int baudRate) {
   struct termios options;
   // get options of serial port
   tcgetattr(fd, &options);

   speed_t iSpeed = cfgetispeed(&options);
   printf ("old speed if   : %d\n", iSpeed);
   cfsetispeed(&options, baudRate);
   cfsetospeed(&options, baudRate);
   iSpeed = cfgetispeed(&options);
   printf ("new speed if   : %d\n", iSpeed);

   options.c_cflag |= (CLOCAL | CREAD);    
   options.c_cflag &= ~PARENB;             // No parity
   options.c_cflag &= ~CSTOPB;             // 1 stop bit
   options.c_cflag &= ~CSIZE;              // 
   options.c_cflag |= CS8;                 // 8 data bits

   // apply new option
   tcsetattr(fd, TCSANOW, &options);
   return 0;
}

/*! read serial USB port to get NMEA GPS AIS info, Unix environment */
void *getNmea (gpointer x) {
   int index = GPOINTER_TO_INT (x);
   char buffer [MAX_SIZE_NMEA];
   int bytes_read;

   int fd = open (par.nmea [index].portName, O_RDONLY | O_NOCTTY | O_NDELAY); // serial port

   if (fd == -1) {
      fprintf (stderr, "In getNmea     : cannot open input flow %s\n", par.nmea [index].portName);
      return NULL;
   }
   configureSerialPort (fd, par.nmea [index].speed);
   printf ("In getNmea     : %s open with speed index: %d\n", par.nmea [index].portName, par.nmea [index].speed);
   par.nmea [index].open = true;

   // read serial port
   while (true) {
      removeOldShips (T_SHIP_MAX);
      bytes_read = read (fd, buffer, sizeof(buffer) - 1);
      if (bytes_read > 0) {
         buffer[bytes_read] = '\0';       // terminate the buffer
         if (((buffer [0] == '!') || (buffer [0] == '$')) && (checksumOK (buffer))) {
            decodeNMEA (buffer);
         }
      }
   }
   close (fd);
   return NULL;
}
#endif

