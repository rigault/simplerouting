/*! compilation gcc -Wall -c option.c */
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include "rtypes.h"
#include "rutil.h"
#include "engine.h"

/*! Manage command line option reduced to one character */
void optionManage (char option) {
	FILE *f = NULL;
   char buffer [MAX_SIZE_BUFFER] = "";
   double u, v, g, w, twa, tws, time, lon, lat, lat2, lon2;
   char str [MAX_SIZE_LINE] = "";
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
      printf ("cap1: %.2lf°,  cap2: %.2lf°\n", directCap (lat, lon, lat2, lon2), directCap (lat2, lon2, lat, lon));
      printf ("orthodist1: %.2lf,  orthodist2: %.2lf\n", orthoDist (lat, lon, lat2, lon2), orthoDist (lat2, lon2, lat, lon));
      printf ("loxodist1 : %.2lf,  loxodist2 : %.2lf\n", loxoDist(lat, lon, lat2, lon2), loxoDist (lat2, lon2, lat, lon));
      break;
   case 'g': // grib
      printf ("grib read: %s\n", par.gribFileName);
      readGribAll (par.gribFileName, &zone, WIND);
      gribToStr (buffer, zone);
      printf ("%s\n", buffer);
      printf ("grib print...\n");
      printGrib (zone, tGribData [WIND]);
      printf ("\n\nFollowing lines are suspects info...\n");
      checkGribToStr (buffer);
      printf ("%s\n", buffer);
      break;
   case 'G': // grib
      printf ("grib current read: %s\n", par.currentGribFileName);
      readGribAll (par.currentGribFileName, &currentZone, CURRENT);
      gribToStr (buffer, currentZone);
      printf ("%s\n", buffer);
      printf ("grib print...\n");
      printGrib (currentZone, tGribData [CURRENT]);
      printf ("\n\nFollowing lines are suspects info...\n");
      checkGribToStr (buffer);
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
   case 'n': //network
      if (isServerAccessible ("http://www.google.com"))
         printf ("Network OK\n");
      else printf ("No network\n");
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
         printf ("speed over ground: %.2lf\n", findPolar (twa, tws, polMat));
      }
      break;
   case 'P': // Wave polar
      readPolar (par.wavePolFileName, &wavePolMat);
      polToStr (buffer, wavePolMat);
      printf ("%s\n", buffer);
      while (true) {
         printf ("angle = " );
         scanf ("%lf", &twa);
         printf ("w = ");
         scanf ("%lf", &w);
         printf ("coeff: %.2lf\n", findPolar (twa, w, wavePolMat)/100.0);
      }
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
         if (isSea (lon, lat))
            printf ("Sea\n");
         else printf ("Earth\n");
      }
      break;
   case 'v': // version
      printf ("Prog version: %s, %s, %s\n", PROG_NAME, PROG_VERSION, PROG_AUTHOR);
      printf ("Compilation-date: %s\n", __DATE__);
      break;
   case 'w': // findflow wind
      readGrib (NULL);
      printf ("Time = ");
      scanf ("%lf", &time);
      printf ("Lat = ");
      scanf ("%lf", &pt.lat);
      printf ("Lon = ");
      scanf ("%lf", &pt.lon);
      findFlow (pt, time, &u, &v, &g, &w, zone, tGribData [WIND]);
      printf ("u: %.2f m/s v: %.2f m/s g: %.2f m/s w: %2.f m\n", u, v, g, w);
      break;
   case 'y':
      readGrib (NULL);
      printf ("Time now minus Time0 Grib in hours %.2lf\n", diffNowGribTime0 (zone)/3600.0);
      break;
   case 'z': //
      printf ("str: ");
      scanf ("%10s", str);
      strClean (str);
      printf ("%s\n", str);
      break;
   default:
      printf ("Option unknown: -%c\n", option);
      break;
   }
}
