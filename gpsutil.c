/*! compilation : gcc -c gpsutil.c -lgps */
#include <pthread.h>
#include <gps.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include "rtypes.h"
#include "rutil.h"

/*! Thread for GPS management */
struct ThreadData {
   struct gps_data_t gps_data;
   MyGpsData my_gps_data;  
};

struct ThreadData thread_data;
pthread_t gps_thread;

MyGpsData my_gps_data;
pthread_mutex_t gps_data_mutex = PTHREAD_MUTEX_INITIALIZER;

/*! write gps information in buffer */
bool gpsToStr (char *buffer) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   if (isnan (my_gps_data.lon) || isnan (my_gps_data.lat)) {
      strcpy (buffer, "No GPS data available\n");
      return false;
   }
   sprintf (buffer, "Position: %s %s\n", \
      latToStr (my_gps_data.lat, par.dispDms, strLat), lonToStr (my_gps_data.lon, par.dispDms, strLon));
   sprintf (line, "Altitude: %.2f\n", my_gps_data.alt);
   strcat (buffer, line);
   sprintf (line, "Status: %d\n", my_gps_data.status);
   strcat (buffer, line);
   sprintf (line, "Number of satellites: %d\n", my_gps_data.nSat);
   strcat (buffer, line);
   struct tm *timeinfo = gmtime (&my_gps_data.timestamp.tv_sec);

   sprintf(line, "GPS Time: %d-%02d-%02d %02d:%02d:%02d UTC\n", 
      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
   strcat (buffer, line);
   return true;
}

/*! Fonction de lecture du GPS exécutée dans un thread */
void *gps_thread_function(void *data) {
   struct ThreadData *thread_data = (struct ThreadData *)data;

   while (true) {
      if (gps_waiting (&thread_data->gps_data, MILLION)) {
         my_gps_data.ret = false;
         if (gps_read (&thread_data->gps_data, NULL, 0) == -1) {
            fprintf(stderr, "Error: GPS read failed.\n");
         } else if (thread_data->gps_data.set & PACKET_SET) {
            // Verrouillez la structure MyGpsData avant la mise à jour
            pthread_mutex_lock(&gps_data_mutex);

               // Mise à jour des données GPS
            my_gps_data.lat = thread_data->gps_data.fix.latitude;
            my_gps_data.lon = thread_data->gps_data.fix.longitude;
            my_gps_data.alt = thread_data->gps_data.fix.altitude;
            my_gps_data.status = thread_data->gps_data.fix.status;
            my_gps_data.nSat = thread_data->gps_data.satellites_visible;
            my_gps_data.timestamp = thread_data->gps_data.fix.time;
            my_gps_data.ret = true;
               // Déverrouillez la structure MyGpsData après la mise à jour
            pthread_mutex_unlock(&gps_data_mutex);
         }
      }
      // Ajoutez un délai ou utilisez une approche plus avancée
      usleep (MILLION); // Délai de 0.5 seconde
   }
   return NULL;
}

/*! GPS init */
bool initGPS () {
   // Initialisez les données du thread
   memset(&thread_data, 0, sizeof(struct ThreadData));
   thread_data.my_gps_data = my_gps_data;

   // Ouvrez la connexion GPS
   if (gps_open("localhost", GPSD_TCP_PORT, &thread_data.gps_data) == -1) {
      fprintf(stderr, "Error: Unable to connect to GPSD.\n");
      return false;
   }

   gps_stream(&thread_data.gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

   // Créez un thread pour la lecture du GPS
   if (pthread_create(&gps_thread, NULL, gps_thread_function, &thread_data) != 0) {
      fprintf(stderr, "Error: Unable to create GPS thread.\n");
      return false;
   }
   return true;
}

/*! GPS close */
void closeGPS () {
   // Fermez la connexion GPS et terminez le thread GPS lors de la fermeture de l'application
   gps_stream(&thread_data.gps_data, WATCH_DISABLE, NULL);
   gps_close(&thread_data.gps_data);
   pthread_cancel(gps_thread);
   pthread_join(gps_thread, NULL);
}
