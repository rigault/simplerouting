/*! compilation gcc -Wall -Wextra -pedantic -Werror -std=c11 -O2 -c gpsutil.c */

#ifdef _WIN32 
#include <windows.h>
#else
#include <gps.h>
#endif

#include <stdio.h>
#include <float.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "rtypes.h"  
#define MAX_ROWS 1024
#define SIZE_DATE_TIME 10
#define SLEEP_TIME 1                   // seconds
#define FLOW_INPUT_UNIX "/dev/ttyACM0" // NMEA USB converter
#define FLOW_INPUT_WINDOWS "com3"      // NMEA USB converter

MyGpsData my_gps_data; 

/*! valeurs trouvée dans les trames NMEA */
struct {
   char time [SIZE_DATE_TIME];
   char date [SIZE_DATE_TIME];
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

/*! return offset Local UTC in seconds */
double offsetLocalUTC (void) {
   time_t now;
   time (&now);                           
   struct tm local_tm = *localtime(&now);
   struct tm utc_tm = *gmtime(&now);
   time_t local_time = mktime(&local_tm);
   time_t utc_time = mktime(&utc_tm);
   double offsetSeconds = difftime(local_time, utc_time);
   if (local_tm.tm_isdst > 0)
        offsetSeconds += 3600;  // add one hour if summer time
   return offsetSeconds;
}
      
/*! gps information for helpInfo */
char *gpsInfo (int type, char *strGps, int len) {
#ifdef _WIN32
   snprintf (strGps, len, "NMEA input: %s", FLOW_INPUT_WINDOWS);
#else
   if (type == API_GPSD)
      snprintf (strGps, len, "API GPSD version: %d.%d", GPSD_API_MAJOR_VERSION, GPSD_API_MINOR_VERSION);
   else
      snprintf (strGps, len, "NMEA input: %s", FLOW_INPUT_UNIX);
#endif
   return strGps;
}

/*! renvoie le checksum de la trame (ou exclusif de tout entre '$' et '*') */
static unsigned int checksum (char *s) {
   unsigned int check = 0;
   if (*s == '$') s++; // elimine $
   while (*s && *s != '*')
      check ^= *s++;
   return check;
}

/*! Verifie que le checksum lu en fin de trame est égal au checksum calcule */
static bool checksumOK (char *s) {
   return (strtol (s + strlen (s) - 3, NULL, 16) == checksum (s));
}

/*! recopie s dans d en replaçant les occurences de ,, par ,-1, */
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

/*! renvoie le checksum de la trame (ou exclusif de tout entre '$' et '*') */
/*! decode les trames */
/* GPRMC : Recommended Minimum data */ 
/* GPGGA : Global Positioning System Fix Data */
/* GPGLL : Latitude Longitude */
/* vraie si a reussit a decoder la trame */
static bool decode (char *line) {
   char lig [MAX_ROWS];
   strcpymod (lig, line);
   // printf ("decode: %s\n", lig);
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

/*! Ecrit sur la sortie standart la trame decodee */
static void printResult (bool clear, bool verbose) {
   char d [SIZE_DATE_TIME];
   char t [SIZE_DATE_TIME];
   strncpy (d, gpsRecord.date, 6);
   strncpy (t, gpsRecord.time, 6);

   // if (clear) printf (CLEAR);
   printf ("Date&Time...............: 20%c%c/%c%c/%c%c %c%c:%c%c:%c%c UTC\n", \
      d[4], d[5], d[2], d[3], d[0], d[1], t[0], t[1], t[2], t[3], t[4], t[5]);
   printf ("Latitude................: %02dD%7.4f' %c\n", \
      (int) gpsRecord.lat/100, gpsRecord.lat - ((int) gpsRecord.lat / 100) * 100, gpsRecord.NS);
   printf ("Longitude...............: %02dD%7.4f' %c\n", \
      (int) gpsRecord.lon/100, gpsRecord.lon - ((int) gpsRecord.lon / 100) * 100,  gpsRecord.EW);
   printf ("Altitude................: %7.2f %c\n", gpsRecord.alt, gpsRecord.uAlt);
   printf ("Speed Over Ground (SOG).: %7.2f Knots\n", gpsRecord.sog);
   printf ("Course Over Ground (COG): %7.2fD\n", gpsRecord.cog);
   printf ("Number of Satellites....: %d\n", gpsRecord.numSV);
   if (verbose) {
      printf ("Status..................: %c\n", gpsRecord.status);
      printf ("Quality.................: %d\n", gpsRecord.quality);
      printf ("Ho. Di. Precision (HDOP): %7.2f\n\n\n", gpsRecord.hdop);
      // displayFrameBuff ();
   }
   printf ("\n");
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
   //printf ("Year, Month Day: %d-%d-%d\n", tm0.tm_year + 1900, tm0.tm_mon + 1, tm0.tm_mday);
   //printf ("Hour, Min sec: %d:%d:%d\n", tm0.tm_hour, tm0.tm_min, tm0.tm_sec);
   //t = mktime (&tm0);
   //struct tm *utc = gmtime (&t);
   //utc->tm_isdst = -1;                     // avoid adjustments with hours summer winter
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

#ifdef _WIN32 
/* get GPS information with NMEA decoding */
static void getGpsNmea () {
   HANDLE hSerial;
   DCB dcbSerialParams = {0};
   COMMTIMEOUTS timeouts = {0};
   char buffer[1024];
   DWORD bytesRead;
   printf ("GPS with NMEA USB Port Windows\n");

   // Open the serial port
   hSerial = CreateFile(FLOW_INPUT_WINDOWS, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
   if (hSerial == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "Error opening serial port\n");
      return;
   }

   // Set device parameters (115200 baud, 1 start bit, 1 stop bit, no parity)
   dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
   if (!GetCommState(hSerial, &dcbSerialParams)) {
      fprintf(stderr, "Error getting device state\n");
      CloseHandle(hSerial);
      return;
   }
   dcbSerialParams.BaudRate = CBR_115200;
   dcbSerialParams.ByteSize = 8;
   dcbSerialParams.StopBits = ONESTOPBIT;
   dcbSerialParams.Parity = NOPARITY;
   if (!SetCommState(hSerial, &dcbSerialParams)) {
      fprintf(stderr, "Error setting device parameters\n");
      CloseHandle(hSerial);
      return;
   }

   // Set COM port timeout settings
   timeouts.ReadIntervalTimeout = 50;
   timeouts.ReadTotalTimeoutConstant = 50;
   timeouts.ReadTotalTimeoutMultiplier = 10;
   timeouts.WriteTotalTimeoutConstant = 50;
   timeouts.WriteTotalTimeoutMultiplier = 10;
   if (!SetCommTimeouts(hSerial, &timeouts)) {
      fprintf(stderr, "Error setting timeouts\n");
      CloseHandle(hSerial);
      return;
   }
   // Read data
   while (true) {
      if ((ReadFile(hSerial, buffer, sizeof(buffer), &bytesRead, NULL)) && (bytesRead > 0)) {
         buffer[bytesRead] = '\0';
         // printf("%s", buffer);
         if (decode (buffer)) {
            copyGpsData ();
         }
      }
   }
   CloseHandle(hSerial);
}

#else
/* get GPS information with NMEA decoding Unix like */
static void getGpsNmea () {
   FILE *flowInput;
   char row [MAX_ROWS];
   char *line = &row [0];
   if ((flowInput = fopen (FLOW_INPUT_UNIX, "r")) == NULL) {
      fprintf (stderr, "In GetGpsNmeaUnix: cannot open input flow\n");
      exit (EXIT_FAILURE);
   }
   while (true) {
      if (((line = fgets (line, MAX_ROWS, flowInput)) != NULL) && (*line == '$') && (checksumOK (row))) {
         // printf ("%s\n", line);
         if (decode (line)) {
            copyGpsData ();
         }
      }
   }
   fclose (flowInput);
}
#endif

/*! provide GPS information with gpsd */
static void getGpsApi () {
#ifdef _WIN32
   return;
#else
   struct gps_data_t gps_data;
   my_gps_data.OK = false;
   if (gps_open("localhost", GPSD_TCP_PORT, &gps_data) == -1) {
      fprintf(stderr, "Error: Unable to connect to GPSD.\n");
      return;
   }
   gps_stream(&gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

   while (true) {
      if (gps_waiting(&gps_data, GPS_TIME_OUT)) {
         if (gps_read(&gps_data, NULL, 0) == -1) {
            fprintf (stderr, "Error in getGPS: gps_read\n");
         } else {
            if (gps_data.set & PACKET_SET) {
   /*            
               printf("Latitude   : %lf, Longitude: %lf\n", gps_data.fix.latitude, gps_data.fix.longitude);
               printf("Altitude   : %lf meters\n", gps_data.fix.altitude);
               printf("Fix Quality: %d\n", gps_data.fix.status);
               printf("Satellites : %d\n", gps_data.satellites_visible);
               printf("SOG        : %.2lfKn\n", MS_TO_KN * gps_data.fix.speed);
               printf("COG        : %.0lf°\n", gps_data.fix.track);
               printf("Time       : %ld\n\n", gps_data.fix.time.tv_sec);
   */            
               if (isfinite(gps_data.fix.latitude) && isfinite(gps_data.fix.longitude) &&
                  ((gps_data.fix.latitude != 0 || gps_data.fix.longitude != 0))) {
                  // printf("Lat: %.2f, Lon: %.2f\n", gps_data.fix.latitude, gps_data.fix.longitude);
                  // update GPS data
                  my_gps_data.lat = gps_data.fix.latitude;
                  my_gps_data.lon = gps_data.fix.longitude;
                  my_gps_data.alt = gps_data.fix.altitude;
                  my_gps_data.cog = gps_data.fix.track;
                  my_gps_data.sog = MS_TO_KN * gps_data.fix.speed;
                  my_gps_data.status = gps_data.fix.status;
                  my_gps_data.nSat = gps_data.satellites_visible;
                  my_gps_data.time = gps_data.fix.time.tv_sec;
                  my_gps_data.OK = true;
               }
               else 
                  my_gps_data.OK = false;
            } else 
               fprintf(stderr, "Error in getGPS: No fix available.\n");
         }
      }
   }
   gps_stream(&gps_data, WATCH_DISABLE, NULL);
   gps_close(&gps_data);
#endif
}

/*! provide GPS information */
void *getGPS (void *type) {
   int *ptr = (int *) type;
   int gpsType = *ptr;
   memset (&my_gps_data, 0, sizeof (my_gps_data));
#ifdef _WIN32
   gpsType = NMEA_USB;
#endif
   printf ("GPS type       : %d\n", gpsType);
   if (gpsType == API_GPSD)
      getGpsApi ();  // Unix GPSD API
   else
      getGpsNmea (); // GPS USB port (at least for Windows)
   return NULL;
}

