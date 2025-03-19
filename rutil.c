/*! compilation: gcc -c rutil.c `pkg-config --cflags glib-2.0` */
#define _POSIX_C_SOURCE 200809L // to avoid warning with -std=c11 when using popen function (Posix)
#define MIN_NAME_LENGTH 3       // for poi. Minimul length to select pomei

#include <glib.h>
#include <float.h>   
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <locale.h>
#include "rtypes.h"
#include "inline.h"
#include "r3util.h"
struct tm;

/*! see aisgps file */
MyGpsData my_gps_data; 


const struct MailService mailServiceTab [N_MAIL_SERVICES] = {
   {"query@saildocs.com", "Saildocs Wind GFS",      "gfs",    6, "WIND,GUST,WAVES,RAIN,PRMSL", "With 1 (resp 3, 6, 12) hours time step, max forecast is 1, (resp 8, 10, 16) days"},
   {"query@saildocs.com", "Saildocs Wind ECWMF",    "ECMWF",  4, "WIND,MSLP,WAVES", "Min time step: 3 hours with 6 days max forecast. Time step 6 hours and above allow 10 days max forecast."},
   {"query@saildocs.com", "Saildocs Wind ICON",     "ICON",   4, "WIND,MSLP,WAVES", "Min time step: 3 hours, 7.5 days max forecast for 00Z and 12Z, 5 days for 6Z and 18Z"},
   {"query@saildocs.com", "Saildocs Wind ARPEGE",   "ARPEGE", 0, "", "Not available yet"},
   {"query@saildocs.com", "Saildocs Wind Arome",    "AROME",  0, "", "Not available yet"}, 
   {"query@saildocs.com", "Saildocs Current RTOFS", "RTOFS",  2, "CURRENT", "Min time step: 3 hours with 3 days forecast, 6 hours for 8 days max forecast."},
   {"weather@mailasail.com","MailaSail Wind GFS",    "",      2, "GRD,WAVE", "Resolution allways 1 degree. Min time step: 3 hours with 8 days max forecast. Time Step: 12 hours for 10 days forcast" },
   {""                     ,"Not Applicable"    ,    "",      0, "NONE", ""}
   // {"gmngrib@globalmarinenet.net", "GlobalMarinet GFS", "", ""} // Does not work well
};

const struct GribService serviceTab [N_WEB_SERVICES] = {
   {6, "Time Step 1 hours requires 0.25 Resolution with 5 days max forecast. Time step 3 hours allows 16 days forecast."}, // NOAA
   {3, "Resolution allways 0.25. Time Step min 3 hours with 6 days max forecast. Time step 6 hours and above allow 10 days forecast."}, // ECMWF
   {3, "Resolution allways 0.25. Time Step allways 3 hours. 5 days max => 4.25 days (102 hours) max."}, // ARPEGE
   {6, "Resolution allways 0.01. Time Step 1 hour recommanded. 2 days (48 hours) max forecast."} // AROME
};

/*! poi sata strutures */ 
Poi tPoi [MAX_N_POI];
int nPoi = 0;

const char *METEO_CONSULT_WIND_URL [N_METEO_CONSULT_WIND_URL * 2] = {
   "Atlantic North",   "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Nord_Atlantique.grb",
   "Atlantic Center",  "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Centre_Atlantique.grb",
   "Antilles",         "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Antilles.grb",
   "Europe",           "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Europe.grb",
   "Manche",           "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Manche.grb",
   "Gascogne",         "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Gascogne.grb"
};

const char *METEO_CONSULT_CURRENT_URL [N_METEO_CONSULT_CURRENT_URL * 2] = {
   "Atlantic North",    "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Nord_Atlantique.grb",
   "Atlantic Center",   "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Centre_Atlantique.grb",
   "Antilles",          "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Antilles.grb",
   "Europe",            "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Europe.grb",
   "Manche",            "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Manche.grb",
   "Gascogne",          "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Gascogne.grb"
};

/*! reinit zone describing geographic meta data */
void initZone (Zone *zone) {
   zone->latMin = -85;
   zone->latMax = 85;
   zone->lonRight = -179;
   zone->lonLeft = 180;
   zone->latStep = 5;
   zone->lonStep = 5;
   zone->nbLat = 0;
   zone->nbLon = 0;
}

/*! return maxTimeRange based on timeStep and resolution */
int maxTimeRange (int service, int mailService) {
   switch (service) {
   case  NOAA_WIND:
      return 384;
      // return (par.gribTimeStep == 1) ? 120 : 384;
   case ECMWF_WIND:
      return (par.gribTimeStep < 6) ? 144 : 240;
   case ARPEGE_WIND:
      return 102;
   case AROME_WIND:
      return 48;
   case MAIL:
      switch (mailService) {
      case SAILDOCS_GFS:
         return (par.gribTimeStep == 1) ? 24 : (par.gribTimeStep < 6) ? 192 : (par.gribTimeStep < 12) ? 240 : 384;
      case SAILDOCS_ECMWF:
         return (par.gribTimeStep < 6) ? 144 : 240;
      case SAILDOCS_ICON:
         return 180;
      case SAILDOCS_ARPEGE:
         return 96;
      case SAILDOCS_AROME:
         return 48;
      case SAILDOCS_CURR:
         return 192;
      case MAILASAIL:
         return (par.gribTimeStep < 12) ? 192 : 240;
      case NOT_MAIL:
         return 0;
      default:
         fprintf (stderr, "In maxTimeRange: provider not supported: %d\n", mailService);
         return 120;
     }
   default:
      fprintf (stderr, "In maxTimeRange: service not supported: %d\n", service);
      return 120;
   }
}

/*! evaluate number of shortnames according to service and id mail, mailService */
int howManyShortnames (int service, int mailService) { 
   if ((service > N_WEB_SERVICES) || ((service == MAIL) && (mailService > N_MAIL_SERVICES)))
      return 0;
   return (service == MAIL) ? mailServiceTab [mailService].nShortNames : serviceTab [service].nShortNames; 
}

/*!remove all .tmp file with prefix */
void removeAllTmpFilesWithPrefix (const char *prefix) {
   char *directory = g_path_get_dirname (prefix); 
   char *base_prefix = g_path_get_basename (prefix);

   GDir *dir = g_dir_open (directory, 0, NULL);
   if (!dir) {
      fprintf (stderr, "In removeAllTmpFilesWithPrefix: Fail opening directory: %s", directory);
      g_free (directory);
      g_free (base_prefix);
      return;
   }

   const char *filename = NULL;
   while ((filename = g_dir_read_name (dir))) {
      if (g_str_has_prefix (filename, base_prefix) && g_str_has_suffix(filename, ".tmp")) {
         char *filepath = g_build_filename (directory, filename, NULL);
         remove (filepath);
         g_free (filepath);
     }
   }

   g_dir_close(dir);
   g_free(directory);
   g_free(base_prefix);
}

/*! launch system command in a thread */
void *commandRun (void *data) {
   FILE *fs;
   char *line = (char *) data;
   // printf ("commandRun Line: %s\n", line);
   if ((fs = popen (line, "r")) == NULL)
      fprintf (stderr, "In commandRun, Error popen call: %s\n", line);
   else
      pclose (fs);
   free (line); 
   return NULL;
}

/*! treatment for ECMWF or ARPEGE or AROME files. Select shortnames and subregion defined by lon and lat values and produce output file
   return true if all OK */
bool compact (const char *dir, const char *inFile, const char *shortNames, double lonLeft, double lonRight, double latMin, double latMax, const char *outFile) {
   FILE *fs;
   const char* tempCompact = "compacted.tmp";
   char command [MAX_SIZE_LINE];
   char fullFileName [MAX_SIZE_LINE];
   snprintf (command, MAX_SIZE_LINE, "grib_copy -w shortName=%s %s %s%s", shortNames, inFile, dir, tempCompact);
   if ((fs = popen (command, "r")) == NULL) {
      fprintf (stderr, "In compact, Error popen call: %s\n", command);
      return false;
   }
   pclose (fs);

   snprintf (fullFileName, MAX_SIZE_LINE, "%s%s", dir, tempCompact);
   if (access (fullFileName, F_OK) != 0) {
      fprintf (stderr, "In compact, Error: file does no exist: %s\n", fullFileName);
      return false;
   }

   snprintf (command, MAX_SIZE_LINE, "wgrib2 %s%s -small_grib %.0lf:%.0lf %.0lf:%.0lf %s >/dev/null", 
      dir, tempCompact, lonLeft, lonRight, latMin, latMax, outFile);
   if ((fs = popen (command, "r")) == NULL) {
      fprintf (stderr, "In compact, Error popen call: %s\n", command);
      return false;
   }
   pclose (fs);
   return true;
}

/*! concat all files prefixed by prefix and suffixed 0..limit0 (with step0 the limit0..max with step1) in fileRes */
bool concat (const char *prefix, const char *suffix, int limit0, int step0, int step1, int max, const char *fileRes) {
   FILE *fRes = NULL;
   if ((fRes = fopen (fileRes, "w")) == NULL) {
      fprintf (stderr, "In concat, Error Cannot write: %s\n", fileRes);
      return false;
   }
   for (int i = 0; i <= max; i += (i < limit0) ? step0 : step1) {
      char fileName [MAX_SIZE_FILE_NAME];
      snprintf (fileName, sizeof (fileName), "%s%03d%s", prefix, i, suffix);
      FILE *f = fopen (fileName, "rb");
      if (f == NULL) {
         fprintf (stderr, "In concat, Error opening impossible %s\n", fileName);
         fclose (fRes);
         return false;
      }
      char buffer [MAX_SIZE_STD];
      size_t n;
      while ((n = fread (buffer, 1, sizeof(buffer), f)) > 0) {
         fwrite (buffer, 1, n, fRes);
      }
      fclose (f);
   }
   fclose (fRes);
   return true;
}

/*! copy str in res with adding newline every n character DEPRECATED */ 
char *strCpyMaxWidth (const char *str, int n, char *res, size_t maxLen) {
    if (!str || n <= 0 || !res) {
        return NULL;
    }
    int len = strlen (str);
    int i = 0, j = 0;

    while ((i < len) && (j < (int) maxLen)) {
        res[j++] = str[i++];
        if (((i % n) == 0) && (i < len)) {  // Insert '\n' every n characters
            res[j++] = '\n';
        }
        
    }
    if (j == (int) maxLen) j -= 1;
    res[j] = '\0';
    return res;
}

/*! get file size */
long getFileSize (const char *fileName) {
    struct stat st;
    if (stat (fileName, &st) == 0) {
        return st.st_size;
    } else {
        fprintf (stderr, "In getFileSize, Error: %s\n", fileName);
        return -1;
    }
}

/*! return tm struct equivalent to date hours found in grib (UTC time) */
/*! Read poi file and fill internal data structure. Return number of poi */
int readPoi (const char *fileName) {
   char buffer [MAX_SIZE_LINE];
   char *pLine = &buffer [0];
   char *pt = NULL;
	FILE *f = NULL;
   int i = nPoi;

	if ((f = fopen (fileName, "r")) == NULL) {
		fprintf (stderr, "In readPoi, Error impossible to read: %s\n", fileName);
		return 0;
	}
   while ((fgets (pLine, MAX_SIZE_LINE, f) != NULL )) {
      if ((pt = strchr (pLine, '#')) != NULL)               // comment elimination
         *pt = '\0';
      g_strstrip (pLine);
      char **tokens = g_strsplit (pLine, ";", 6); // Maximum 6 tokens

      if (!tokens[0] || !tokens[1] || !tokens[2] || !tokens[3] || !tokens[4]) {
         g_strfreev(tokens);
         continue;  // Invalid line, skip it
      }

      tPoi [i].lat = getCoord (tokens [0], MIN_LAT, MAX_LAT);
      tPoi [i].lon = getCoord (tokens [1], MIN_LON, MAX_LON);
      tPoi [i].type = strtol (tokens [2], NULL, 10);
      tPoi [i].level = strtol (tokens [3], NULL, 10);
      g_strlcpy (tPoi [i].name, tokens [4], MAX_SIZE_POI_NAME);
      g_strstrip (tPoi [i].name);
      if (tokens [5]) { // optionnal country code
         g_strlcpy (tPoi [i].cc, g_strstrip (tokens[5]), MAX_SIZE_COUNTRY_CODE);
      }
      i += 1;
      if (i >= MAX_N_POI) {
         fprintf (stderr, "In readPoi, Error Exceed MAX_N_POI : %d\n", i);
	      fclose (f);
         return 0;
      }
	}
	fclose (f);
   // printf ("nPoi : %d\n", i);
   return i;
}

/*! write in a file poi data structure except ports */
bool writePoi (const char *fileName) {
   FILE *f = NULL;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "In writePoi, Error cannot write: %s\n", fileName);
      return false;
   }
   fprintf (f, "LATITUDE; LONGITUDE; Type; Level; PORT_NAME\n");
   for (int i = 0; i < nPoi; i++) {
      if (tPoi[i].type != PORT && tPoi [i].type != UNVISIBLE)
         fprintf (f, "%.2lf; %.2lf; %d; %d; %s\n", tPoi [i].lat, tPoi [i].lon, tPoi [i].type, tPoi [i].level, tPoi [i].name);
   }
   fclose (f);
   return true;
}

/*! give the point refered by its name. return index found, -1 if not found */
int findPoiByName (const char *name, double *lat, double *lon) {
    if (!name || !lat || !lon) {
        return -1; // Invalid arguments
    }

    // Normalize the input name for case-insensitive comparison
    char *normalized_name = g_utf8_casefold (name, -1);
    g_strstrip (normalized_name);

    if (strlen(normalized_name) < MIN_NAME_LENGTH) {
        g_free(normalized_name);
        return -1; // Name too short
    }

    for (int i = 0; i < nPoi; i++) {
        // Normalize the POI name
        char *normalized_poi = g_utf8_casefold (tPoi[i].name, -1);

        // Check if the input name is a substring of the POI name (case-insensitive)
        if (g_strstr_len (normalized_poi, -1, normalized_name)) {
            *lat = tPoi[i].lat;
            *lon = tPoi[i].lon;
            g_free(normalized_poi);
            g_free(normalized_name);
            return i; // Found
        }

        g_free(normalized_poi);
    }

    g_free(normalized_name);
    return -1; // Not found
}

/*! printf poi table */ 
void poiPrint () {
   char strLat [MAX_SIZE_NAME] = "";
   char strLon [MAX_SIZE_NAME] = "";
   printf ("Lat         Lon         type level Country Name\n");
   for (int i = 0; i < nPoi; i++) {
      if (tPoi [i].type > UNVISIBLE) {
         printf ("%-12s %-12s %4d %5d %7s %s\n",\
            latToStr (tPoi[i].lat, par.dispDms, strLat, sizeof (strLat)),\
            lonToStr (tPoi[i].lon, par.dispDms, strLon, sizeof (strLon)),\
            tPoi [i].type, tPoi [i].level, tPoi [i].cc, tPoi[i].name);
      }
   }
   printf ("\nNumber of Points Of Interest: %d\n", nPoi);
}

/*! translate poi table into a string, return number of visible poi */
int poiToStr (bool portCheck, char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE] = "";
   char strLat [MAX_SIZE_NAME] = "";
   char strLon [MAX_SIZE_NAME] = "";
   int count = 0;
   snprintf (str, MAX_SIZE_LINE, "Lat         Lon           Type  Level Country Name\n");
   
   
   for (int i = 0; i < nPoi; i++) {
      if ((tPoi [i].type > UNVISIBLE) && ((tPoi [i].type != PORT) || portCheck)) {
         snprintf (line, MAX_SIZE_LINE, "%-12s;%-12s;%6d;%6d;%7s;%s\n",\
            latToStr (tPoi[i].lat, par.dispDms, strLat, sizeof (strLat)),\
            lonToStr (tPoi[i].lon, par.dispDms, strLon, sizeof (strLon)),\
            tPoi [i].type, tPoi [i].level, tPoi [i].cc, tPoi[i].name);
	      g_strlcat (str, line, maxLen);
         count += 1;
      }
   }
   return count;
}

/*! return name of nearest port found in file fileName from lat, lon. return empty string if not found */
char *nearestPort (double lat, double lon, const char *fileName, char *res, size_t maxLen) {
   const double minShomLat = 43.0;
   const double maxShomLat = 51.0;
   const double minShomLon = -4.0;
   const double maxShomLon = 3.0;
   double minDist = DBL_MAX;
   FILE *f;
   char str [MAX_SIZE_LINE];
   double latPort, lonPort;
   char portName [MAX_SIZE_NAME];
   res [0] = '\0';
   if ((lat < minShomLat) || (lat > maxShomLat) || (lon < minShomLon) || (lon > maxShomLon))
      return res;

   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In nearestPort, Error cannot open: %s\n", fileName);
      return NULL;
   }
   while (fgets (str, MAX_SIZE_LINE, f) != NULL ) {
      if (sscanf (str, "%lf,%lf,%63s", &latPort, &lonPort, portName) > 2) {
         double d = orthoDist (lat, lon, latPort, lonPort);
         if (d < minDist) {
            minDist = d;
            g_strlcpy (res, portName, maxLen);
               // printf ("%s %.2lf %.2lf %.2lf %.2lf %.2lf \n", portName, d, lat, lon, latPort, lonPort);
         }
      }
   }
   fclose (f);
   return res;
}

/*! time management init at begining of Grib */
void initStart (struct tm *start) {
   long intDate = zone.dataDate [0];
   long intTime = zone.dataTime [0];
   start->tm_year = (intDate / 10000) - 1900;
   start->tm_mon = ((intDate % 10000) / 100) - 1;
   start->tm_mday = intDate % 100;
   start->tm_hour = intTime / 100;
   start->tm_min = intTime % 100;
   start->tm_isdst = -1;                                       // avoid adjustments with hours summer winter
   start->tm_sec += 3600 * par.startTimeInHours;               // add seconds
   mktime (start);
}

/* return number of hours between now minus beginning of the grib */
double diffTimeBetweenNowAndGribOrigin (long intDate, double nHours) {
   time_t now = time (NULL);
   struct tm tm0 = {0};
   tm0.tm_year = (intDate / 10000) - 1900;
   tm0.tm_mon = ((intDate % 10000) / 100) - 1;
   tm0.tm_mday = intDate % 100;
   tm0.tm_hour = (int ) nHours;
   tm0.tm_sec += offsetLocalUTC ();       // make sure we use UTC
   tm0.tm_isdst = -1;                     // avoid adjustments with hours summer winter
    // Convertir struct tm en timestamp (secondes depuis Epoch)
   time_t timeTm0 = mktime (&tm0);
   if ((timeTm0 != -1) && (now -= -1) && (now > timeTm0)) 
      return difftime (now, timeTm0) / 3600;
   else {
      fprintf (stderr, "In diffTimeBetweenNowAndGribOrigin, Error\n");
      return -1;
   }
}

/*! convert epoch time to string with or without seconds */
char *epochToStr (time_t t, bool seconds, char *str, size_t maxLen) {
   struct tm *utc = gmtime (&t);
   if (seconds)
      snprintf (str, maxLen, "%d-%02d-%02d %02d:%02d:%02d", 
         utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
         utc->tm_hour, utc->tm_min, utc->tm_sec);
   else
      snprintf (str, maxLen, "%d-%02d-%02d %02d:%02d", 
         utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
         utc->tm_hour, utc->tm_min);
      
   return str;
}

/*! fill str with polygon information */
void polygonToStr (char *buffer, size_t maxLen) {
   char strLat[MAX_SIZE_NAME], strLon[MAX_SIZE_NAME];
   buffer [0] = '\0';
   char *line =  g_strdup_printf ("Number of polygons: %d\n", par.nForbidZone);
   g_strlcat (buffer, line, maxLen);
   g_free (line);

   for (int i = 0; i < par.nForbidZone; i++) {
      char *line = g_strdup_printf ("Polygon %2d:\n", i);
      g_strlcat (buffer, line, maxLen);
      g_free (line);
      for (int j = 0; j < forbidZones[i].n; j++) {
         latToStr(forbidZones[i].points[j].lat, par.dispDms, strLat, sizeof(strLat));
         lonToStr(forbidZones[i].points[j].lon, par.dispDms, strLon, sizeof(strLon));
         char *coord = g_strdup_printf("%12s; %12s\n", strLat, strLon);
         g_strlcat (buffer, coord, maxLen);
         g_free (coord);
        }
    }
}

/*! build object and body of grib mail */
bool buildGribMail (int type, double lat1, double lon1, double lat2, double lon2, char *object,  char *body, size_t maxLen) {
   int i;
   char temp [MAX_SIZE_LINE];
   
   lon1 = lonCanonize (lon1);
   lon2 = lonCanonize (lon2);
   
   if ((lon1 > 0) && (lon2 < 0)) { // zone crossing antimeridien
      printf ("In buildGribMail: Tentative to cross antemeridian !\n");
      // lon2 = 179.0;
   }
   if (setlocale (LC_ALL, "C") == NULL) {                // very important for printf decimal numbers
      fprintf (stderr, "In buildGribMail, Error: setlocale failed");
      return false;
   }
   
   if (type != MAILASAIL) {
      snprintf (body, maxLen, "send %s:%d%c,%d%c,%d%c,%d%c|%.2lf,%.2lf|0,%d..%d|%s",\

         mailServiceTab [type].service,\
         (int) fabs (round(lat1)), (lat1 > 0) ? 'N':'S', (int) fabs (round(lat2)), (lat2 > 0) ? 'N':'S',\
		   (int) fabs (round(lon1)), (lon1 > 0) ? 'E':'W', (int) fabs (round(lon2)), (lon2 > 0) ? 'E':'W',\
		   par.gribResolution, par.gribResolution, par.gribTimeStep, par.gribTimeMax,\
         mailServiceTab [type].suffix);
      snprintf (object, maxLen, "grib");
   }
   else { // MAILASAIL:
      //printf ("smtp mailasail python\n");
      snprintf (object, maxLen, "grib gfs %d%c:%d%c:%d%c:%d%c ",
         (int) fabs (round(lat1)), (lat1 > 0) ? 'N':'S', (int) fabs (round(lon1)), (lon1 > 0) ? 'E':'W',\
         (int) fabs (round(lat2)), (lat2 > 0) ? 'N':'S', (int) fabs (round(lon2)), (lon2 > 0) ? 'E':'W');
      for (i = 0; i < par.gribTimeMax; i+= par.gribTimeStep) {
         snprintf (temp, MAX_SIZE_LINE, "%d,", i);
	      g_strlcat (object, temp, maxLen);
      }
      snprintf (temp, MAX_SIZE_LINE, "%d %s", i, mailServiceTab [type].suffix);
      g_strlcat (object, temp, maxLen);
      snprintf (body, maxLen, "grib");
   }
   return true;
}

/*! URL for METEO Consult Wind delay hours before now at closest time run 0Z, 6Z, 12Z, or 18Z 
	return time run  type : WIND or CURRENT, i : zone index, delay: nb of hours to get a run */
int buildMeteoConsultUrl (int type, int i, int delay, char *url, size_t maxLen) {
   time_t meteoTime = time (NULL) - (3600 * delay);
   struct tm * timeInfos = gmtime (&meteoTime);  // time 4 or 5 or 6 hours ago
   int mm = timeInfos->tm_mon + 1;
   int dd = timeInfos->tm_mday;
   int hh = (type == WIND) ? (timeInfos->tm_hour / 6) * 6 : 0;    // select 0 or 6 or 12 or 18 for WIND, allways 0 for current
   if (type == CURRENT) {
      snprintf (url, maxLen, METEO_CONSULT_CURRENT_URL [i*2 + 1], METEO_CONSULT_ROOT_GRIB_URL, hh, mm, dd); // allways 0
   }
   else {
      snprintf (url, maxLen, METEO_CONSULT_WIND_URL [i*2 + 1], METEO_CONSULT_ROOT_GRIB_URL, hh, mm, dd);
   }
   return hh;
}

/*! URL for NOAA Wind or ECMWF delay hours before now at closest time run 0Z, 6Z, 12Z, or 18Z 
   return time run or -1 if error*/
int buildGribUrl (int typeWeb, int topLat, int leftLon, int bottomLat, int rightLon, int step, int step2, char *url, size_t maxLen) {
   char fileName [MAX_SIZE_FILE_NAME];
   time_t meteoTime = time (NULL) - (3600 * ((typeWeb == NOAA_WIND) ? NOAA_DELAY : 
                                             (typeWeb == ECMWF_WIND) ? ECMWF_DELAY : METEO_FRANCE_DELAY));
   struct tm * timeInfos = gmtime (&meteoTime);  // time x hours ago
   // printf ("Delay: %d hours ago: %s\n", delay [type], asctime (timeInfos));
   int yy = timeInfos->tm_year + 1900;
   int mm = timeInfos->tm_mon + 1;
   int dd = timeInfos->tm_mday;
   int hh;
   char startUrl [MAX_SIZE_LINE];
   int resolution;
   const int rightLonNew = ((leftLon > 0) && (rightLon < 0)) ? rightLon + 360 : rightLon;

   switch (typeWeb) {
   case NOAA_WIND:
      resolution = (par.gribResolution == 0.25) ? 25 : 50;
      hh = (timeInfos->tm_hour / 6) * 6;    // select 0 or 6 or 12 or 18
      snprintf (startUrl, sizeof (startUrl), "%sfilter_gfs_0p%d.pl?", NOAA_ROOT_GRIB_URL, resolution);

      if (resolution == 25) // resolution = 0.25
         snprintf (fileName, sizeof (fileName), "gfs.t%02dz.pgrb2.0p25.f%03d", hh, step);
      else                  // resolution = 0.50
         snprintf (fileName, sizeof (fileName), "gfs.t%02dz.pgrb2full.0p50.f%03d", hh, step);

      snprintf (url, maxLen, "%sdir=/gfs.%4d%02d%02d/%02d/atmos&file=%s&%s&subregion=&toplat=%d&leftlon=%d&rightlon=%d&bottomlat=%d", 
                startUrl, yy, mm, dd, hh, fileName, NOAA_GENERAL_PARAM_GRIB_URL, topLat, 
                leftLon, rightLonNew, bottomLat);
      break;
   case ECMWF_WIND:
      resolution = (par.gribResolution == 0.25) ? 25 : 50;
      hh = (timeInfos->tm_hour / 12) * 12;    // select 0 or 12
      // Ex: https://data.ecmwf.int/forecasts/20240806/00z/ifs/0p25/oper/20240806000000-102h-oper-fc.grib2
      //     https://data.ecmwf.int/forecasts/20250128/00z/ifs/0p25/oper/20250128000000-1h-oper-fc.grib2
      snprintf (fileName, sizeof (fileName), "%4d%02d%02d%02d0000-%dh-oper-fc.grib2", yy, mm, dd, hh, step);
      snprintf (url, maxLen, "%s%4d%02d%02d/%02dz/ifs/0p25/oper/%s", ECMWF_ROOT_GRIB_URL, yy, mm, dd, hh, fileName); 
      break;
   case ARPEGE_WIND:
      resolution = 25; // 0.25 degree
      hh = (timeInfos->tm_hour / 6) * 6;    // select 0 or 6 or 12 or 18
      // Ex: https://object.data.gouv.fr/meteofrance-pnt/pnt/2024-12-24T12:00:00Z/arpege/025/SP1/arpege__025__SP1__000H024H__2024-12-24T12:00:00Z.grib
      snprintf (fileName, sizeof (fileName), "arpege__%03d__SP1__%03dH%03dH__%4d-%02d-%02dT%02d:00:00Z.grib2", resolution, step, step2, yy, mm, dd, hh);
      snprintf (url, maxLen, "%s%4d-%02d-%02dT%02d:00:00Z/arpege/%03d/SP1/%s", METEO_FRANCE_ROOT_GRIB_URL, yy, mm, dd, hh, resolution, fileName);
      break;
   case AROME_WIND:
      resolution = 1; // here this means 0.1 degree
      hh = (timeInfos->tm_hour / 6) * 6;    // select 0 or 6 or 12 or 18
      // Ex: https://object.data.gouv.fr/meteofrance-pnt/pnt/2025-01-28T12:00:00Z/arome/001/HP1/arome__001__HP1__00H__2025-01-28T12:00:00Z.grib2
      snprintf (fileName, sizeof (fileName), "arome__%03d__HP1__%02dH__%4d-%02d-%02dT%02d:00:00Z.grib2", resolution, step, yy, mm, dd, hh);
      snprintf (url, maxLen, "%s%4d-%02d-%02dT%02d:00:00Z/arome/%03d/HP1/%s", METEO_FRANCE_ROOT_GRIB_URL, yy, mm, dd, hh, resolution, fileName);
      break;
   default:
      fprintf (stderr, "In buildGribUrl Error typeWeb not found: %d\n", typeWeb);
      return -1;
   }
   return hh;
}

/*! export Waypoints to GPX */
bool exportWpToGpx (const char *gpxFileName) {
   FILE *f;
   if (! gpxFileName) 
      return false;
   if ((f = fopen (gpxFileName, "w")) == NULL) {   
      fprintf (stderr, "In exportWpToGpx, Impossible to write open: %s\n", gpxFileName);
      return false;
   }

   fprintf (f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
   fprintf (f, "<gpx version=\"1.1\" creator=\"%s\">\n", PROG_NAME);
   fprintf (f, "   <wpt lat=\"%.4lf\" lon=\"%.4lf\">\n", par.pOr.lat, par.pOr.lon);
   fprintf (f, "      <name>Origin</name>\n");
   fprintf (f, "   </wpt>\n");
   for (int i = 0; i <  wayPoints.n; i++) {
      fprintf (f, "   <wpt lat=\"%.4lf\" lon=\"%.4lf\">\n", wayPoints.t [i].lat, wayPoints.t[i].lon);
      fprintf (f, "      <name>WP: %d</name>\n", i);
      fprintf (f, "   </wpt>\n");
   }
   fprintf (f, "   <wpt lat=\"%.4lf\" lon=\"%.4lf\">\n", par.pDest.lat, par.pDest.lon);
   fprintf (f, "      <name>Destination</name>\n");
   fprintf (f, "   </wpt>\n");

   fprintf (f, "</gpx>\n");
   fclose (f);
   return true;
}

/*! export CSV trace file toGPX file */
bool exportTraceToGpx (const char *fileName, const char *gpxFileName) {
   FILE *csvFile = fopen (fileName, "r");
   FILE *gpxFile = fopen (gpxFileName, "w");
   
   if (!csvFile || !gpxFile) {
      if (csvFile) fclose(csvFile);
      if (gpxFile) fclose(gpxFile);
      return false;
   }
   
   gchar *line = NULL;
   size_t len = 0;
   ssize_t read;           // signed size t to authorize -1
   bool firstLine = true;
   
   fprintf (gpxFile, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
   fprintf (gpxFile, "<gpx version=\"1.1\" creator=\"%s\">\n", PROG_NAME);
   fprintf (gpxFile, "  <trk>\n");
   fprintf (gpxFile, "   <name>Track %s</name>\n", gpxFileName);
   fprintf (gpxFile, "   <trkseg>\n");
   
   while ((read = getline (&line, &len, csvFile)) != -1) {
      if (firstLine) { 
         firstLine = false;
         continue;
      }
      
      char **fields = g_strsplit (line, ";", -1);
      size_t nFields = g_strv_length(fields);
      if (nFields < 4) {
         g_strfreev (fields);
         continue;
      }
      for (size_t i = 0; i < nFields; i += 1)
         g_strstrip (fields [i]);

     fprintf (gpxFile, "     <trkpt lat=\"%s\" lon=\"%s\">\n", fields [0], fields [1]);
     g_strdelimit (fields [3], "/", '-');
     fprintf (gpxFile, "       <time>%.19sZ</time>\n",  g_strdelimit (fields [3], " ", 'T'));

     if (nFields > 3 && fields [4] != NULL && fields [4][0] != '\0')
         fprintf (gpxFile, "       <course>%s</course>\n", fields [4]);
     if (nFields > 4 && fields [5] != NULL && fields [5][0] != '\0')
         fprintf (gpxFile, "       <speed>%s</speed>\n", fields [5]);
     fprintf (gpxFile, "     </trkpt>\n");
      
     g_strfreev (fields);
   }
   
   fprintf (gpxFile, "   </trkseg>\n");
   fprintf (gpxFile, "  </trk>\n");
   fprintf (gpxFile, "</gpx>\n");
   
   fclose (csvFile);
   fclose (gpxFile);
   if (line) free(line);
   return true;
}

/*! add GPS trace point to trace file */
bool addTraceGPS (const char *fileName) {
   FILE *f = NULL;
   char str [MAX_SIZE_LINE];
   if (!my_gps_data.OK) 
      return false;
   if (my_gps_data.lon == 0 && my_gps_data.lat == 0)
      return false;
   if ((f = fopen (fileName, "a")) == NULL) {
      fprintf (stderr, "In addTraceGPS, Error cannot write: %s\n", fileName);
      return false;
   }
   fprintf (f, "%7.2lf; %7.2lf; %10ld; %15s; %4.0lf; %6.2lf\n", my_gps_data.lat, my_gps_data.lon, \
      (long int) my_gps_data.time, epochToStr (my_gps_data.time, true, str, sizeof (str)),\
      my_gps_data.cog, my_gps_data.sog);
   fclose (f);
   return true;
}

/*! add trace point to trace file */
bool addTracePt (const char *fileName, double lat, double lon) {
   FILE *f = NULL;
   char str [MAX_SIZE_LINE];
   time_t t = time (NULL);
   if ((f = fopen (fileName, "a")) == NULL) {
      fprintf (stderr, "In addTracePt: cannot write: %s\n", fileName);
      return false;
   }
   fprintf (f, "%7.2lf; %7.2lf; %10ld; %15s;\n", lat, lon, (long int) t, epochToStr (t, true, str, sizeof (str)));
   fclose (f);
   return true;
}

/*! find last point in trace */
bool findLastTracePoint (const char *fileName, double *lat, double *lon, double *time) {
   FILE *f = NULL;
   char line [MAX_SIZE_LINE];
   char lastLine [MAX_SIZE_LINE];
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In findLastTracePoint, Error cannot read: %s\n", fileName);
      return false;
   }
   while (fgets (line, MAX_SIZE_LINE, f) != NULL ) {
      g_strlcpy (lastLine, line, MAX_SIZE_LINE);
   }
   fclose (f);
   return (sscanf (lastLine, "%lf;%lf;%lf", lat, lon, time) >= 3);
}

/*! calculate distance in number of miles 
   od: orthodromic, ld: loxodromic, rd: real*/
bool distanceTraceDone (const char *fileName, double *od, double *ld, double *rd, double *sog) {
   FILE *f = NULL;
   bool first = true;
   char line [MAX_SIZE_LINE];
   double lat0 = 0.0, lon0 = 0.0, time0 = 0.0, time = 0.0, lat = 0.0, lon = 0.0;
   double latLast, lonLast, delta;
   *od = 0.0, *ld = 0.0, *rd = 0.0;
   
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In distanceTraceDone, Error cannot read: %s\n", fileName);
      return false;
   }
   if (fgets (line, MAX_SIZE_LINE, f) == NULL) { // head line contain libelles and is ignored
      fprintf (stderr, "In distanceTraceDone, Error no header: %s\n", fileName);
      fclose (f);
      return false;
   }
   while (fgets (line, MAX_SIZE_LINE, f) != NULL ) {
      if (sscanf (line, "%lf;%lf;%lf", &lat, &lon, &time) < 3) continue;
      if (first) {
         lat0 = lat, lon0 = lon, time0 = time;
         first = false;
      }
      else {
         delta = orthoDist (lat, lon, latLast, lonLast);
         if (!isnan (delta)) 
            *rd += delta;
         // printf ("lat: %.2lf lon: %.2lf rd: %.2lf\n", lat, lon, *rd);
      }
      latLast = lat, lonLast = lon;
   }
   *od = orthoDist (lat, lon, lat0, lon0);
   *ld = loxoDist (lat, lon, lat0, lon0);
   *sog = *rd / ((time - time0) / 3600.0);
   fclose (f);
   return true;
}

/*! Last info digest 
   idem infoTrace but take into account last GPS pos if available */ 
bool infoDigest (const char *fileName, double *od, double *ld, double *rd, double *sog) {
   FILE *f = NULL;
   bool first = true;
   char line [MAX_SIZE_LINE];
   double lat0 = 0.0, lon0 = 0.0, time0 = 0, latLast = 0.0, lonLast = 0.0, lat, lon, time, delta;
   *rd = 0.0;
   
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In infoDigest, Error cannot read: %s\n", fileName);
      return false;
   }
   while (fgets (line, MAX_SIZE_LINE, f) != NULL ) {
      if (sscanf (line, "%lf;%lf;%lf", &lat, &lon, &time) < 3) continue;
      if (first) {
         lat0 = lat, lon0 = lon, time0 = time;
         first = false;
      }
      else {
         delta = orthoDist (lat, lon, latLast, lonLast);
         if (!isnan (delta)) 
            *rd += delta;
         // printf ("lat: %.2lf lon: %.2lf rd: %.2lf\n", lat, lon, *rd);
      }
      latLast = lat, lonLast = lon;
   }
   fclose (f);
   if (my_gps_data.OK) {
      lat = my_gps_data.lat;
      lon = my_gps_data.lon;
      delta = orthoDist (lat, lon, latLast, lonLast);
      if (!isnan (delta))
         *rd += delta;
      time = my_gps_data.time;
   }
   *od = orthoDist (lat, lon, lat0, lon0);
   *ld = loxoDist (lat, lon, lat0, lon0);
   *sog = *rd / ((time - time0) / 3600.0);
   return true;
}

/*! true if day light, false if night 
// libnova required for this
bool OisDayLight (time_t t, double lat, double lon) {
   struct ln_lnlat_posn observer;
   struct ln_rst_time rst;
   double julianDay;https://data.ecmwf.int/forecasts
   int ret;
   observer.lat = lat; 
   observer.lng = lon; 
        
   julianDay = ln_get_julian_from_timet (&t);
   // printf ("julianDay: %lf\n", julianDay);
   if ((ret = ln_get_solar_rst (julianDay, &observer, &rst)) != 0) {
      //printf ("Sun is circumpolar: %d\n", ret);
      return (ret == 1); // 1 if day, 0 if night
   }
   else {
      //printf ("julianDay Rise: %lf\n", rst.rise);
      //printf ("julianDay Set : %lf\n", rst.set);
      return (rst.set < rst.rise);
   }
}
*/

/*! reset way route with only pDest */
void initWayPoints (void) {
   wayPoints.n = 0;
   wayPoints.totOrthoDist = 0;
   wayPoints.totLoxoDist = 0;
   wayPoints.t [0].lat = par.pDest.lat;
   wayPoints.t [0].lon = par.pDest.lon;
}

