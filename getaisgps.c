#include <gps.h>
#include <glib.h>
#include <stdio.h>
#include <float.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>

/*! provide GPS and AIS information using gpsd gps.h. Work only in Unix environment. AIS part does NOR work */
void *getAisGps () {
   const int MAX_SOG = 1023; 
   struct gps_data_t gps_data;
   double latC, lonC;
   // double latC, lonC; // possible  collision point
   memset (&my_gps_data, 0, sizeof (my_gps_data));
   my_gps_data.OK = false;
   if (gps_open ("localhost", GPSD_TCP_PORT, &gps_data) == -1) {
      fprintf(stderr, "In getAisGps    : Error, unable to connect to GPSD.\n");
      return NULL;
   }
   printf ("In GetAisGps   : GPSD open\n");
   gps_stream (&gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

   while (true) {
      removeOldShips (T_SHIP_MAX);
      if (gps_waiting(&gps_data, GPS_TIME_OUT)) {
         if (gps_read(&gps_data, NULL, 0) == -1) {
            fprintf (stderr, "In getAisGps   : Error, gps_read\n");
            continue;
         } 
         if (gps_data.set & PACKET_SET) {
            if (isfinite(gps_data.fix.latitude) && isfinite(gps_data.fix.longitude) &&
               ((gps_data.fix.latitude != 0 || gps_data.fix.longitude != 0))) {
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
         }
         if (gps_data.set & AIS_SET) {
            struct ais_t *ais = &gps_data.ais;
            AisRecord *ship = getRecord (ais-> mmsi);
            
            printf ("\nMMSI: %u\n", ais->mmsi);
            printf ("AIS message : %u\n", ais->type);
            switch (ais->type) {
            case 1: case 2: case 3:
               if (ais->type1.speed < MAX_SOG) ship->sog = ((double) ais->type1.speed) / 10.0;
               if (ais->type1.course <= 3600) ship->cog = ais->type1.course / 10.0;
               ship->lat = ((double) ais->type1.lat) / 600000.0;
               ship->lon = ((double) ais->type1.lon) / 600000.0;
               printf ("Sog: %.1lf\n", ((double) ais->type1.speed) / 10.0);
               printf ("Cog: %d\n", ais->type1.course / 10);
               printf ("Lat: %.2lf\n", ((double) ais->type1.lat) / 600000.0);
               printf ("Lon: %.2lf\n", ((double) ais->type1.lon) / 600000.0);
               break;
            case 18:
               if (ais->type18.speed < MAX_SOG) ship->sog = ((double) ais->type18.speed) / 10.0;
               if (ais->type18.course <= 3600) ship->cog = ais->type18.course / 10.0;
               ship->lat = ((double) ais->type18.lat) / 600000.0;
               ship->lon = ((double) ais->type18.lon) / 600000.0;
               printf ("Sog: %.1lf\n", ((double) ais->type18.speed) / 10.0);
               printf ("Cog: %d\n", ais->type18.course / 10);
               printf ("Lat: %.2f\n", ((double) ais->type18.lat) / 600000.0);
               printf ("Lon: %.2lf\n", ((double) ais->type18.lon) / 600000.0);
               break;
            case 5:
               if (ais->type5.shipname [0] != '\0')
                  strncpy (ship->name, ais->type5.shipname, MAX_SIZE_SHIP_NAME);
               printf ("Ship Name: %s\n", ais->type5.shipname);
               break;
            case 24:
               if (ais->type24.shipname [0] != '\0')
                  strncpy (ship->name, ais->type24.shipname, MAX_SIZE_SHIP_NAME);
               printf ("Ship Name: %s\n", ais->type24.shipname);
               break;
            default:;
            }

            if (my_gps_data.OK) {
               int x = collisionDetection (my_gps_data.lat, my_gps_data.lon, my_gps_data.sog, my_gps_data.cog,
                                           ship->lat, ship->lon, ship->sog, ship->cog, &latC, &lonC);
               if (x < 0) 
                  ship->minDist = x;
               else if (ship->minDist >= MILLION)
                  ship->minDist = -2;
               else ship -> minDist = 1852 * x;
            }
            else
               ship->minDist = -3;
         }
      }
   }
   gps_stream (&gps_data, WATCH_DISABLE, NULL);
   gps_close (&gps_data);
   return NULL;
}
