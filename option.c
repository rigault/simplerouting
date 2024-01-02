/*! compilation gcc -Wall -c option.c */
#include <stdbool.h>
#include <stdio.h>
#include "eccodes.h"
#include "rtypes.h"
#include "rutil.h"
#include <gtk/gtk.h>

void optionManage (char option) {
	FILE *f;
   char buffer [MAX_SIZE_BUFFER];
   double u, v, w, twa, tws, time, lon, lat, lat2, lon2;
   char str [MAX_SIZE_LINE];
   Pp pt;
   switch (option) {
   case 'c': // cap
      printf ("Lon1 = ");
      scanf ("%lf", &lon);
      printf ("Lat1 = ");
      scanf ("%lf", &lat);
      printf ("Lon2 = ");
      scanf ("%lf", &lon2);
      printf ("Lat2 = ");
      scanf ("%lf", &lat2);
      printf ("cap1: %.2lf°,  cap2: %.2lf°\n", loxCap (lat, lon, lat2, lon2), loxCap (lat2, lon2, lat, lon));
      break;
   case 'g': // grib
      printf ("grib read: %s\n", par.gribFileName);
      readGrib (NULL);
      printf ("grib print...\n");
      printGrib (zone, gribData);
      gribToStr (buffer, zone);
      printf ("%s\n", buffer);
      printf ("\n\n Following lines are suspects info...\n");
      checkGribToStr (buffer, zone, gribData);
      printf ("%s\n", buffer);
      break;
   case 'h': // help
	   if ((f = fopen (par.cliHelpFileName, "r")) == NULL) {
		   fprintf (stderr, "Error in option help: impossible to read: %s\n", par.cliHelpFileName);
		   break;
	   }
      while ((fgets (str, MAX_SIZE_LINE, f) != NULL ))
         printf ("%s", str);
      fclose (f);
      break;
   case 'p': //polar
      readPolar (par.polarFileName, &polMat);
      polToStr (buffer, polMat);
      printf ("%s\n", buffer);
      while (true) {
         printf ("twa true wind angle = ");
         scanf ("%lf", &twa);
         printf ("tws true wind speed = ");
         scanf ("%lf", &tws);
         printf ("speed over ground: %.2lf\n", estimateSog (twa, tws, polMat));
      }
      break;
   case 'P': // Wave polar
      readPolar (par.wavePolFileName, &wavePolMat);
      polToStr (buffer, wavePolMat);
      printf ("%s\n", buffer);
      break;
   case 's': // isIsea
      readIsSea (par.isSeaFileName);
      if (tIsSea == NULL) printf ("in readIsSea : bizarre\n");
      // dumpIsSea ();
      while (1) {
         printf ("Lon = ");
         scanf ("%lf", &lon);
         printf ("Lat = ");
         scanf ("%lf", &lat);
         if (extIsSea (lon, lat))
            printf ("Sea\n");
         else printf ("Earth\n");
      }
      break;
   case 'v': // version
      int major = gtk_get_major_version();
      int minor = gtk_get_minor_version();
      int micro = gtk_get_micro_version();
      printf ("GTK version : %d.%d.%d\n", major, minor, micro);
      printf ("Prog version: %s, %s, %s\n", PROG_NAME, PROG_VERSION, PROG_AUTHOR);
      break;
   case 'w': // findflow wind
      readGrib (NULL);
      printf ("Time = ");
      scanf ("%lf", &time);
      printf ("Lat = ");
      scanf ("%lf", &pt.lat);
      printf ("Lon = ");
      scanf ("%lf", &pt.lon);
      findFlow (pt, time, &u, &v, &w, zone, gribData);
      printf ("u: %.2f m/s v: %.2f m/s w: %2.f m\n", u, v, w);
      break;
   break;
   default:
      printf ("Option unknown: -%c\n", option);
      break;
   }
}
