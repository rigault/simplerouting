/*! compilation: gcc -c option.c `pkg-config --cflags glib-2.0` */
#include <glib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include "rtypes.h"
#include "rutil.h"
#include "inline.h"
#include "engine.h"
#include "grib.h"
#include "polar.h"

/*! Make initialization following parameter file load */
static void initScenarioOption (void) {
   char str [MAX_SIZE_LINE];
   char errMessage [MAX_SIZE_TEXT] = "";
   int readGribRet;
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
   if (readPolar (par.polarFileName, &polMat, errMessage, sizeof (errMessage)))
      printf ("Polar loaded   : %s\n", par.polarFileName);
   else
      fprintf (stderr, "In initScenarioOption, Error readPolar: %s\n", errMessage);
      
   if (readPolar (par.wavePolFileName, &wavePolMat, errMessage, sizeof (errMessage)))
      printf ("Polar loaded   : %s\n", par.wavePolFileName);
   else
      fprintf (stderr, "In initScenatioOption, Error readPolar: %s\n", errMessage);
   
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   // initWayPoints ();
}

/*! Manage command line option reduced to one character */
void optionManage (char option) {
	FILE *f = NULL;
   char directory [MAX_SIZE_DIR_NAME];
   char buffer [MAX_SIZE_BUFFER] = "";
   double w, twa, tws, lon, lat, lat2, lon2, cog;
   char errMessage [MAX_SIZE_TEXT] = "";
   char str [MAX_SIZE_LINE] = "";
   switch (option) {
   case 'a':
      snprintf (str, sizeof (str), "%sgrib/inter-", par.workingDir);
      removeAllTmpFilesWithPrefix (str); // we suppress the temp files in order to get clean
      printf ("All .tmp files with prefix:%s are removed\n", str); 
      break;
   case 'c': // cap
      printf ("Lon1 = ");
      if (scanf ("%lf", &lon) < 1) break;
      printf ("Lat1 = ");
      if (scanf ("%lf", &lat) < 1) break;
      printf ("Lon2 = ");
      if (scanf ("%lf", &lon2) < 1) break;
      printf ("Lat2 = ");
      if (scanf ("%lf", &lat2) < 1) break;
      printf ("Direct cap1: %.2lf°,  Direct cap2: %.2lf°\n", directCap (lat, lon, lat2, lon2), directCap (lat2, lon2, lat, lon));
      printf ("Orthodist1 : %.2lf,   Orthodist2: %.2lf\n", orthoDist (lat, lon, lat2, lon2), orthoDist (lat2, lon2, lat, lon));
      printf ("Loxodist1  : %.2lf,   Loxodist2 : %.2lf\n", loxoDist(lat, lon, lat2, lon2), loxoDist (lat2, lon2, lat, lon));
      break;
   case 'd': // distance
      while (true) {
         double latA, lonA, latB, lonB, latX, lonX, res;
         printf ("Lat A: "); 
         if (scanf ("%lf", &latA) < 1) break;
         printf ("Lon A: "); 
         if (scanf ("%lf", &lonA) < 1) break;
         printf ("Lat B: "); 
         if (scanf ("%lf", &latB) < 1) break;
         printf ("Lon B: "); 
         if (scanf ("%lf", &lonB) < 1) break;
         printf ("Lat X: "); 
         if (scanf ("%lf", &latX) < 1) break;
         printf ("Lon X: "); 
         if (scanf ("%lf", &lonX) < 1) break;
         res =  distSegment (latX, lonX, latA, lonA, latB, lonB);
         printf ("Distance from X to [AB]: %.2lf\n", res);
      }
      break;
   case 'g': // grib
      if (par.mostRecentGrib) {// most recent grib will replace existing grib
         snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir); 
         mostRecentFile (directory, ".csv", par.gribFileName, sizeof (par.gribFileName));
      }
      printf ("Grib File Name: %s\n", par.gribFileName);
      readGribAll (par.gribFileName, &zone, WIND);
      gribToStr (&zone, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      printf ("grib print...\n");
      printGrib (&zone, tGribData [WIND]);
      printf ("\n\nFollowing lines are suspects info...\n");
      checkGribToStr (buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      break;
   case 'G': // grib
      readGribAll (par.currentGribFileName, &currentZone, CURRENT);
      gribToStr (&currentZone, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      printf ("grib print...\n");
      printGrib (&currentZone, tGribData [CURRENT]);
      printf ("\n\nFollowing lines are suspects info...\n");
      checkGribToStr (buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      break;
   case 'h': // help
	   if ((f = fopen (par.cliHelpFileName, "r")) == NULL) {
		   fprintf (stderr, "In optionManage, Error help: Impossible to read: %s\n", par.cliHelpFileName);
		   break;
	   }
      while ((fgets (str, MAX_SIZE_LINE, f) != NULL ))
         printf ("%s", str);
      fclose (f);
      break;
   case 'i': // Interest points
      nPoi = 0;
      if (par.poiFileName [0] != '\0')
         nPoi += readPoi (par.poiFileName);
      if (par.portFileName [0] != '\0')
         nPoi += readPoi (par.portFileName);
   
      poiPrint ();
      break;
   case 'n': // network
      if (isServerAccessible ("http://www.google.com"))
         printf ("Network OK\n");
      else printf ("No network\n");
      break;
   case 'p': // polar
      readPolar (par.polarFileName, &polMat, errMessage, sizeof (errMessage));
      polToStr (&polMat, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      while (true) {
         printf ("twa true wind angle = ");
         if (scanf ("%lf", &twa) < 1) break;
         printf ("tws true wind speed = ");
         if (scanf ("%lf", &tws) < 1) break;
         printf ("speed over ground: %.2lf\n", findPolar (twa, tws, polMat));
      }
      break;
   case 'P': // Wave polar
      readPolar (par.wavePolFileName, &wavePolMat, errMessage, sizeof (errMessage));
      polToStr (&wavePolMat, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      while (true) {
         printf ("angle = " );
         if (scanf ("%lf", &twa) < 1) break;
         printf ("w = ");
         if (scanf ("%lf", &w) < 1) break;
         printf ("coeff: %.2lf\n", findPolar (twa, w, wavePolMat)/100.0);
      }
      break;
   case 'r': // routing
      initZone (&zone);
      if (par.mostRecentGrib) {// most recent grib will replace existing grib
         snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir); 
         mostRecentFile (directory, ".csv", par.gribFileName, sizeof (par.gribFileName));
      }
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
         if (scanf ("%lf", &lat) < 1) break;
         printf ("Lon = ");
         if (scanf ("%lf", &lon) < 1) break;
         if (isSea (tIsSea, lat, lon))
            printf ("Sea\n");
         else printf ("Earth\n");
      }
      break;
   /*case 't': // test
      while (1) {
         double lon1, lon2;
         printf ("Lon1 = ");
         if (scanf ("%lf", &lon1) < 1) break;
         printf ("Lon2 = ");
         if (scanf ("%lf", &lon2) < 1) break;
         etc
      }
      break;*/
   case 'T': // test
      while (1) {
         double lon;
         printf ("lon = ");
         if (scanf ("%lf", &lon) < 1) break;
         printf ("fMod (lon)= %.2lf, lonCanonize (lon) = %.2lf\n", fmod (lon, 360.0), lonCanonize (lon));
      }
      break;
   case 'v': // version
      printf ("Prog version: %s, %s, %s\n", PROG_NAME, PROG_VERSION, PROG_AUTHOR);
      printf ("Compilation-date: %s\n", __DATE__);
      break;
   case 'w': // twa
      while (true) {
         printf ("COG = ");
         if (scanf ("%lf", &cog) < 1) break;
         printf ("TWS = ");
         if (scanf ("%lf", &twa) < 1) break;
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
