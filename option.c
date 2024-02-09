/*! compilation gcc -Wall -c option.c */
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include "rtypes.h"
#include "rutil.h"
#include "engine.h"

void optionManage (char option) {
	FILE *f;
   char buffer [MAX_SIZE_BUFFER];
   double u, v, g, w, twa, tws, time, lon, lat, lat2, lon2;
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
         printf ("speed over ground: %.2lf\n", extFindPolar (twa, tws, polMat));
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
         printf ("coeff: %.2lf\n", extFindPolar (twa, w, wavePolMat)/100.0);
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
         if (extIsSea (lon, lat))
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
      findFlow (pt, time, &u, &v, &g, &w, zone, gribData);
      printf ("u: %.2f m/s v: %.2f m/s g: %.2f m/s w: %2.f m\n", u, v, g, w);
      break;
   case 'y':
      readGrib (NULL);
      printf ("Time now minus Time0 Grib in hours %.2lf\n", diffNowGribTime0 (zone)/3600.0);
      break;
   case 'z': // conversion deg min sec
      while (true) {
         printf ("str: ");
         fgets(buffer, sizeof(buffer), stdin);
         printf ("len: %d\n", (int) strlen (buffer));
         printf ("res: %.2lf\n", getCoord (buffer));
      }
   break;
   default:
      printf ("Option unknown: -%c\n", option);
      break;
   }
}
