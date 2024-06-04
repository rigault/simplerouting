/*! compilation gcc -Wall -c option.c */
#include <glib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include "rtypes.h"
#include "rutil.h"
#include "inline.h"
#include "engine.h"

/*! Make initialization following parameter file load */
static void initScenarioOption () {
   char str [MAX_SIZE_LINE];
   int iFlow;
   if (par.gribFileName [0] != '\0') {
      iFlow = WIND;
      readGrib (&iFlow);
      if (readGribRet == 0) {
         fprintf (stderr, "Error in initScenario: Unable to read grib file: %s\n ", par.gribFileName);
         return;
      }
      printf ("Grib loaded    : %s\n", par.gribFileName);
      printf ("Grib DateTime0 : %s\n", gribDateTimeToStr (zone.dataDate [0], zone.dataTime [0], str, sizeof (str)));
   }

   if (par.currentGribFileName [0] != '\0') {
      iFlow = CURRENT;
      readGrib (&iFlow);
      printf ("Cur grib loaded: %s\n", par.currentGribFileName);
      printf ("Grib DateTime0 : %s\n", gribDateTimeToStr (currentZone.dataDate [0], currentZone.dataTime [0], str, sizeof (str)));
   }
   if (readPolar (par.polarFileName, &polMat))
      printf ("Polar loaded   : %s\n", par.polarFileName);
      
   if (readPolar (par.wavePolFileName, &wavePolMat))
      printf ("Polar loaded   : %s\n", par.wavePolFileName);
   
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   initWayPoints ();
}

/*! Manage command line option reduced to one character */
void optionManage (char option) {
	FILE *f = NULL;
   char buffer [MAX_SIZE_BUFFER] = "";
   double w, twa, tws, lon, lat, lat2, lon2, cog;
   char str [MAX_SIZE_LINE] = "";
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
      printf ("Direct cap1: %.2lf°,  Direct cap2: %.2lf°\n", directCap (lat, lon, lat2, lon2), directCap (lat2, lon2, lat, lon));
      printf ("Orthodist1 : %.2lf,   Orthodist2: %.2lf\n", orthoDist (lat, lon, lat2, lon2), orthoDist (lat2, lon2, lat, lon));
      printf ("Loxodist1  : %.2lf,   Loxodist2 : %.2lf\n", loxoDist(lat, lon, lat2, lon2), loxoDist (lat2, lon2, lat, lon));
      break;
   case 'g': // grib
      printf ("Grib File Name: %s\n", par.gribFileName);
      readGribAll (par.gribFileName, &zone, WIND);
      gribToStr (buffer, &zone, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      printf ("grib print...\n");
      printGribFull (&zone, tGribData [WIND]);
      printf ("\n\nFollowing lines are suspects info...\n");
      checkGribToStr (buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      break;
   case 'G': // grib
      readGribAll (par.currentGribFileName, &currentZone, CURRENT);
      gribToStr (buffer, &currentZone, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      printf ("grib print...\n");
      printGrib (&currentZone, tGribData [CURRENT]);
      printf ("\n\nFollowing lines are suspects info...\n");
      checkGribToStr (buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      break;
   case 'h': // help
	   if ((f = fopen (par.cliHelpFileName, "r")) == NULL) {
		   fprintf (stderr, "Error in option help: Impossible to read: %s\n", par.cliHelpFileName);
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
      polToStr (buffer, &polMat, MAX_SIZE_BUFFER);
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
      polToStr (buffer, &wavePolMat, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      while (true) {
         printf ("angle = " );
         scanf ("%lf", &twa);
         printf ("w = ");
         scanf ("%lf", &w);
         printf ("coeff: %.2lf\n", findPolar (twa, w, wavePolMat)/100.0);
      }
      break;
   case 'r': // routing
      initZone (&zone);
      if (par.mostRecentGrib) // most recent grib will replace existing grib
         initWithMostRecentGrib ();
      initScenarioOption ();
      routingLaunch ();
      routeToStr (&route, buffer, sizeof (buffer));
      printf ("%s\n", buffer);
      break;
   case 's': // isIsea
      readIsSea (par.isSeaFileName);
      if (tIsSea == NULL) printf ("in readIsSea : bizarre\n");
      // dumpIsSea ();
      while (1) {
         printf ("Lat = ");
         scanf ("%lf", &lat);
         printf ("Lon = ");
         scanf ("%lf", &lon);
         if (isSea (lat, lon))
            printf ("Sea\n");
         else printf ("Earth\n");
      }
      break;
   case 'v': // version
      printf ("Prog version: %s, %s, %s\n", PROG_NAME, PROG_VERSION, PROG_AUTHOR);
      printf ("Compilation-date: %s\n", __DATE__);
      break;
   case 'w': // twa
      while (true) {
         printf ("COG = ");
         scanf ("%lf", &cog);
         printf ("TWS = ");
         scanf ("%lf", &twa);
         printf ("fTwa = %.2lf\n",fTwa (cog, twa));
      }
   break;
   case 'z': //
      printf ("Password %s\n", par.mailPw);
      printf ("Password %s\n", dollarSubstitute (par.mailPw, buffer, strlen (par.mailPw)));
      break;
   default:
      printf ("Option unknown: -%c\n", option);
      break;
   }
}
