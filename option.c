/*! compilation: gcc -c option.c `pkg-config --cflags glib-2.0` */
#include <math.h>
#include <glib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "rtypes.h"
#include "r3util.h"
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
   if (readPolar (true, par.polarFileName, &polMat, errMessage, sizeof (errMessage)))
      printf ("Polar loaded   : %s\n", par.polarFileName);
   else
      fprintf (stderr, "In initScenarioOption, Error readPolar: %s\n", errMessage);
      
   if (readPolar (true, par.wavePolFileName, &wavePolMat, errMessage, sizeof (errMessage)))
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
   char sailPolFileName [MAX_SIZE_NAME] = "";
   char directory [MAX_SIZE_DIR_NAME];
   char *buffer = NULL;
   char footer [MAX_SIZE_LINE] = "";
   double w, twa, tws, lon, lat, lat2, lon2, cog, t;
   char errMessage [MAX_SIZE_TEXT] = "";
   char str [MAX_SIZE_LINE] = "";
   clock_t start, end;
   double result;
   const long iterations = 100000;
   int sail;

   struct tm tm0;
   int intRes;


   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      fprintf (stderr, "In optionManage, Error Malloc %d\n", MAX_SIZE_BUFFER); 
      return;
   }
   
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
      printf ("Ortho cap1: %.2lf°,   Ortho cap2: %.2lf°\n", orthoCap (lat, lon, lat2, lon2), orthoCap (lat2, lon2, lat, lon));
      printf ("Ortho2 cap1: %.2lf°,  Ortho2 cap2: %.2lf°\n", orthoCap2 (lat, lon, lat2, lon2), orthoCap2 (lat2, lon2, lat, lon));
      printf ("Orthodist1 : %.2lf,   Orthodist2: %.2lf\n", orthoDist (lat, lon, lat2, lon2), orthoDist (lat2, lon2, lat, lon));
      printf ("Orthodist1 : %.2lf,   Orthodist2: %.2lf\n", orthoDist2 (lat, lon, lat2, lon2), orthoDist (lat2, lon2, lat, lon));
      printf ("Loxodist1  : %.2lf,   Loxodist2 : %.2lf\n", loxoDist(lat, lon, lat2, lon2), loxoDist (lat2, lon2, lat, lon));
      break;
   case 'C': // Perf cap dist
      lat = 48.8566, lon = 2.3522;   // Paris
      lat2 = 40.7128, lon2 = -74.0060; // New York

      // Test direct
      start = clock();
      for (long i = 0; i < iterations; i++) {
        result = directCap (lat, lon, lat2, lon2);
      }
      end = clock();
      printf("direct Cap:      %.2f ms, last result = %.2f NM\n", 
           (double)(end - start) * 1000.0 / CLOCKS_PER_SEC, result);

      // Test orthoCap givry
      start = clock();
      for (long i = 0; i < iterations; i++) {
        result = orthoCap (lat, lon, lat2, lon2);
      }
      end = clock();
      printf("orthoCap givry:      %.2f ms, last result = %.2f NM\n", 
           (double)(end - start) * 1000.0 / CLOCKS_PER_SEC, result);


      // Test orthoCap 
      start = clock();
      for (long i = 0; i < iterations; i++) {
        result = orthoCap2 (lat, lon, lat2, lon2);
      }
      end = clock();
      printf("orthoCap2: %.2f ms, last result = %.2f NM\n", 
           (double)(end - start) * 1000.0 / CLOCKS_PER_SEC, result);
      break;

   case 'g': // grib
      if (par.mostRecentGrib) {// most recent grib will replace existing grib
         snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir); 
         mostRecentFile (directory, ".gr", "", par.gribFileName, sizeof (par.gribFileName));
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
      printf ("Size of size_t : %zu bytes\n", sizeof (size_t));
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
   case 'p': // polar
      readPolar (true, par.polarFileName, &polMat, errMessage, sizeof (errMessage));
      newFileNameSuffix (par.polarFileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
      readPolar (false, sailPolFileName, &sailPolMat, errMessage, sizeof (errMessage));
      polToStr (&polMat, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      polToStr (&sailPolMat, buffer, MAX_SIZE_BUFFER);
      // printf ("%s\n", buffer);
      while (true) {
         printf ("twa true wind angle = ");
         if (scanf ("%lf", &twa) < 1) break;
         printf ("tws true wind speed = ");
         if (scanf ("%lf", &tws) < 1) break;
         printf ("Old Speed over ground: %.2lf\n", oldFindPolar (twa, tws, &polMat));
         printf ("Speed over ground: %.2lf\n", findPolar (twa, tws, &polMat, &sailPolMat, &sail));
         printf ("Sail: %d, Name: %s\n", sail, sailName [sail % MAX_N_SAIL]); 
      }
      break;
   case 'P': // Wave polar
      readPolar (true, par.wavePolFileName, &wavePolMat, errMessage, sizeof (errMessage));
      polToStr (&wavePolMat, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      while (true) {
         printf ("angle = " );
         if (scanf ("%lf", &twa) < 1) break;
         printf ("w = ");
         if (scanf ("%lf", &w) < 1) break;
         printf ("coeff: %.2lf\n", findPolar (twa, w, &wavePolMat, NULL, &sail) / 100.0);
      }
      break;
   case 'q':
      readPolar (true, par.polarFileName, &polMat, errMessage, sizeof (errMessage));
      polToStr (&polMat, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      while (true) {
         printf ("tws = " );
         if (scanf ("%lf", &tws) < 1) break;
         printf ("oldMaxSpeedInPolarAt   : %.4lf\n", oldMaxSpeedInPolarAt (tws, &polMat));
         printf ("newMaxSpeedInPolarAt: %.4lf\n", maxSpeedInPolarAt (tws, &polMat));
      }
      break;
   case 'r': // routing
      if (par.mostRecentGrib) {// most recent grib will replace existing grib
         snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir); 
         mostRecentFile (directory, ".gr", "", par.gribFileName, sizeof (par.gribFileName));
      }
      initScenarioOption ();
      routingLaunch ();
      routeToStr (&route, buffer, sizeof (buffer), footer, sizeof (footer));
      printf ("%s\n", buffer);
      printf ("%s\n", footer);
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
   case 't': // test
      readGribAll (par.gribFileName, &zone, WIND);
      tm0 = gribDateToTm (zone.dataDate [0], zone.dataTime [0] / 100);
      printf ("Grib Time: %s\n", asctime (&tm0));
      printf ("Lat = ");
      if (scanf ("%lf", &lat) < 1) break;
      printf ("Lon = ");
      if (scanf ("%lf", &lon) < 1) break;
      printf ("t = ");
      if (scanf ("%lf", &t) < 1) break;
      start = clock();
      for (long i = 0; i < iterations; i++) {
        struct tm localTm = tm0;
        intRes = isDayLight (&localTm, t, lat, lon);;
      }
      end = clock();
      printf("isDayLight:      %.2f, last result = %d\n", 
           (double)(end - start) * 1000.0 / CLOCKS_PER_SEC, intRes);

      break;
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
      // printf ("Password %s\n", dollarSubstitute (par.mailPw, buffer, strlen (par.mailPw)));
      break;
   default:
      printf ("Option unknown: -%c\n", option);
      break;
   }
   free (buffer);
}
