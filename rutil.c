/*! compilation: gcc -c rutil.c `pkg-config --cflags glib-2.0` */
#define _POSIX_C_SOURCE 200809L // to avoid warning with -std=c11 when using popen function (Posix)
#include <glib.h>
#include <curl/curl.h>
#include <float.h>   
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <locale.h>
#include "eccodes.h"
#include "rtypes.h"
#include "shapefil.h"
#include "aisgps.h"
#include "inline.h"
#include "mailutil.h"

/*! forbid zones is a set of polygons */
MyPolygon forbidZones [MAX_N_FORBID_ZONE];

/*! dictionnary of meteo services */
const struct MeteoElmt meteoTab [N_METEO_ADMIN] = {{7, "Weather service US"}, {78, "DWD Germany"}, {85, "Meteo France"}, {98,"ECMWF European"}};

const struct MailService mailServiceTab [N_MAIL_SERVICES] = {
   {"query@saildocs.com", "Saildocs Wind GFS",      "gfs",    6, "WIND,GUST,WAVES,RAIN,PRMSL", "With 1 (resp 3, 6, 12) hours time step, max forecast is 1, (resp 8, 10, 16) days"},
   {"query@saildocs.com", "Saildocs Wind ECWMF",    "ECMWF",  4, "WIND,MSLP,WAVES", "Min time step: 3 hours with 6 days max forecast. Time step 6 hours and above allow 10 days max forecast."},
   {"query@saildocs.com", "Saildocs Wind ICON",     "ICON",   4, "WIND,MSLP,WAVES", "Min time step: 3 hours, 7.5 days max forecast for 00Z and 12Z, 5 days for 6Z and 18Z"},
   {"query@saildocs.com", "Saildocs Wind ARPEGE",   "ARPEGE", 0, "", "Not available yet"},
   {"query@saildocs.com", "Saildocs Wind Arome",    "AROME",  0, "", "Not available yet"}, 
   {"query@saildocs.com", "Saildocs Current RTOFS", "RTOFS",  2, "CURRENT", "Min time step: 6 hours. 8 days max forecast."},
   {"weather@mailasail.com","MailaSail Wind GFS",    "",      2, "GRD,WAVE", "Resolution allways 1 degree. Min time step: 3 hours with 8 days max forecast. Time Step: 12 hours for 10 days forcast" },
   {""                     ,"Not Applicable"    ,    "",      0, "NONE", ""}
   // {"gmngrib@globalmarinenet.net", "GlobalMarinet GFS", "", ""} // Does not work well
};

const struct GribService serviceTab [N_WEB_SERVICES] = {
   {6, "Time Step 1 hours requires 0.25 Resolution with 5 days max forecast. Time step 3 hours allows max 8 days forecast. Timestep 6 hours for 16 days forecast."}, // NOAA
   {3, "Resolution allways 0.25. Time Step min 3 hours with 6 days max forecast. Time step 6 hours and above allow 10 days forecast."}, // ECMWF
   {3, "Resolution allways 0.25. Time Step allways 3 hours. 5 days max => 4.25 days (102 hours) max."} // ARPEGE
};

/*! list of wayPoint */
WayPointList wayPoints;

/*! list of competitors */
CompetitorsList competitors;

/*! polar matrix description */
PolMat polMat;

/*! polar matrix for waves */
PolMat wavePolMat;

/*! parameter */
Par par;

/*! geographic zone covered by grib file */
Zone zone;                          // wind
Zone currentZone;                   // current

/*! table describing if sea or earth */
char *tIsSea = NULL; 

/*! grib data description */
FlowP *tGribData [2]= {NULL, NULL};  // wind, current

/*! poi sata strutures */ 
Poi tPoi [MAX_N_POI];
int nPoi = 0;

/*! for shp file */
int  nTotEntities = 0;              // cumulated number of entities
Entity *entities = NULL;

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

/*!remove all .tmp file with prefix */
void removeAllTmpFilesWithPrefix (const char *prefix) {
   gchar *directory = g_path_get_dirname (prefix); 
   gchar *base_prefix = g_path_get_basename (prefix);

   GDir *dir = g_dir_open (directory, 0, NULL);
   if (!dir) {
      fprintf (stderr, "In removeAllTmpFilesWithPrefix: Fail opening directory: %s", directory);
      g_free (directory);
      g_free (base_prefix);
      return;
   }

   const gchar *filename = NULL;
   while ((filename = g_dir_read_name(dir))) {
      if (g_str_has_prefix (filename, base_prefix) && g_str_has_suffix(filename, ".tmp")) {
         gchar *filepath = g_build_filename (directory, filename, NULL);
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

/*! treatment for NOAA or ECMWF files. Select shortnames and subregion defined by lon and lat values and produce output file
   return true if all OK */
bool compact (bool full, const char *dir, const char *inFile, const char *shortNames, double lonLeft, double lonRight, double latMin, double latMax, const char *outFile) {
   FILE *fs;
   char command [MAX_SIZE_LINE];
   char fullFileName [MAX_SIZE_LINE];
   snprintf (command, MAX_SIZE_LINE, "grib_copy -w shortName=%s %s %s%s", shortNames, inFile, dir, "inter0.tmp");
   if ((fs = popen (command, "r")) == NULL) {
      fprintf (stderr, "In compact, Error popen call: %s\n", command);
      return false;
   }
   pclose (fs);

   snprintf (fullFileName, MAX_SIZE_LINE, "%s%s", dir, "inter0.tmp");
   if (access (fullFileName, F_OK) != 0) {
      fprintf (stderr, "In compact, Error: file does no exist: %s\n", fullFileName);
      return false;
   }

   if (! full) {
      if (rename (fullFileName, outFile) != 0) {
         fprintf (stderr, "In compact, Error rename to: %s\n", outFile);
         return false;
      }
      return true;
   }
   else {
      snprintf (command, MAX_SIZE_LINE, "wgrib2 %s%s -small_grib %.0lf:%.0lf %.0lf:%.0lf %s >/dev/null", 
         dir, "inter0.tmp", lonLeft, lonRight, latMin, latMax, outFile);
      if ((fs = popen (command, "r")) == NULL) {
         fprintf (stderr, "In compact, Error popen call: %s\n", command);
         return false;
      }
      pclose (fs);
      return true;
   }
}

/*! concat all files prefixed by prefix and suffixed 0..max (with step) in fileRes */
bool concat (const char *prefix, const char *suffix, int step, int max, const char *fileRes) {
   FILE *fRes = NULL;
   if ((fRes = fopen (fileRes, "w")) == NULL) {
      fprintf (stderr, "In concat, Error Cannot write: %s\n", fileRes);
      return false;
   }
   for (int i = 0; i <= max; i += step) {
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

/*! return true if str is empty */
bool isEmpty (const char *str) {
   if (str == NULL)
      return true;
   while (*str) {
      if (!g_ascii_isspace(*str)) {
         return false;
      }
      str++;
   }
   return true;
}

/*! format big number with thousand sep. Example 1000000 formated as 1 000 000 */
char *formatThousandSep (char *buffer, size_t maxLen, long value) {
   char str [MAX_SIZE_LINE];
   snprintf (str, MAX_SIZE_LINE, "%ld", value);
   int length = strlen (str);
   int nSep = length / 3;
   int mod = length % 3;
   int i = 0, j = 0;
   for (i = 0; i < mod; i++)
      buffer [j++] = str [i];
   if ((nSep > 0) && (mod > 0)) buffer [j++] = ' '; 
   for (int k = 0; k < nSep; k++) {
      buffer [j++] = str [i++];
      buffer [j++] = str [i++];
      buffer [j++] = str [i++];
      buffer [j++] = ' ';
      if (j >= (int) maxLen) {
         j = maxLen - 1;
         break;
      }
   }
   buffer [j] = '\0';
   return buffer;
}

/*! build entities based on .shp file */
bool initSHP (const char* nameFile) {
   int nEntities = 0;         // number of entities in current shp file
   int nShapeType;
   int k = 0;
   int nMaxPart = 0;
   double minBound[4], maxBound[4];
   SHPHandle hSHP = SHPOpen (nameFile, "rb");
   if (hSHP == NULL) {
      fprintf (stderr, "In InitSHP, Error: Cannot open Shapefile: %s\n", nameFile);
      return false;
   }

   SHPGetInfo(hSHP, &nEntities, &nShapeType, minBound, maxBound);
   printf ("Geo nEntities  : %d, nShapeType: %d\n", nEntities, nShapeType);
   printf ("Geo limits     : %.2lf, %.2lf, %.2lf, %.2lf\n", minBound[0], minBound[1], maxBound[0], maxBound[1]);

   Entity *temp = (Entity *) realloc (entities, sizeof(Entity) * (nEntities + nTotEntities));
   if (temp == NULL) {
      fprintf (stderr, "In initSHP, Error realloc failed\n");
      free (entities); // Optionnal if entities is NULL at beginning, but sure..
      return false;
   } else {
      entities = temp;
   }

   for (int i = 0; i < nEntities; i++) {
      SHPObject *psShape = SHPReadObject(hSHP, i);
      if (psShape == NULL) {
         fprintf (stderr, "In initSHP, Error Reading entity: %d\n", i);
         continue;
      }
      k = nTotEntities + i;

      entities[k].numPoints = psShape->nVertices;
      entities[k].nSHPType = psShape->nSHPType;
      if ((psShape->nSHPType == SHPT_POLYGON) || (psShape->nSHPType == SHPT_POLYGONZ) || (psShape->nSHPType == SHPT_POLYGONM) ||
          (psShape->nSHPType == SHPT_ARC) || (psShape->nSHPType == SHPT_ARCZ) || (psShape->nSHPType == SHPT_ARCM)) {
         entities[k].latMin = psShape->dfYMin;
         entities[k].latMax = psShape->dfYMax;
         entities[k].lonMin = psShape->dfXMin;
         entities[k].lonMax = psShape->dfXMax;  
      }    

      // printf ("Bornes: %d, LatMin: %.2lf, LatMax: %.2lf, LonMin: %.2lf, LonMax: %.2lf\n", k, entities [i].latMin, entities [i].latMax, entities [i].lonMin, entities [i].lonMax);
      if ((entities[k].points = malloc (sizeof(Point) * entities[k].numPoints)) == NULL) {
         fprintf (stderr, "In initSHP, Error: malloc entity [k].numPoints failed with k = %d\n", k);
         free (entities);
         return false;
      }
      for (int j = 0; (j < psShape->nParts) && (j < MAX_INDEX_ENTITY); j++) {
         if (j < MAX_INDEX_ENTITY)
            entities[k].index [j] = psShape->panPartStart[j];
         else {
            fprintf (stderr, "In InitSHP, Error MAX_INDEX_ENTITY reached: %d\n", j);
            break;
         } 
      }
      entities [k].maxIndex = psShape->nParts;
      if (psShape->nParts > nMaxPart) 
         nMaxPart = psShape->nParts;

      for (int j = 0; j < psShape->nVertices; j++) {
         // mercatorToLatLon (psShape->padfY[j], psShape->padfX[j], &entities[k].points[j].lat, &entities[k].points[j].lon);
         entities[k].points[j].lon = psShape->padfX[j];
         entities[k].points[j].lat = psShape->padfY[j];
      }
         
      SHPDestroyObject(psShape);
   }
   SHPClose(hSHP);
   nTotEntities += nEntities;
   // printf ("nMaxPart = %d\n", nMaxPart);
   return true;
}

/*! free memory allocated */
void freeSHP (void) {
   for (int i = 0; i < nTotEntities; i++) {
      free (entities[i].points);
   }
   free(entities);
}

/*! select most recent file in "directory" that contains "pattern" in name 
  return true if found with name of selected file */
static bool mostRecentFile (const char *directory, const char *pattern, char *name, size_t maxLen) {
   DIR *dir = opendir(directory);
   if (dir == NULL) {
      fprintf (stderr, "In mostRecentFile, Error opening: %s\n", directory);
      return false;
   }

   struct dirent *entry;
   struct stat statbuf;
   time_t latestTime = 0;
   char filepath [MAX_SIZE_DIR_NAME + MAX_SIZE_FILE_NAME];

   while ((entry = readdir(dir)) != NULL) {
      snprintf (filepath, sizeof (filepath), "%s/%s", directory, entry->d_name);
      if ((strstr (entry->d_name, pattern) != NULL)
         && (stat(filepath, &statbuf) == 0) 
         && (S_ISREG(statbuf.st_mode) && statbuf.st_mtime > latestTime)) {
         latestTime = statbuf.st_mtime;
         if (strlen (entry->d_name) < maxLen)
            g_strlcpy (name, entry->d_name, maxLen);
         else {
            fprintf (stderr, "In mostRecentFile, Error File name size is: %zu and exceed Max Size: %zu\n", \
               strlen (entry->d_name), maxLen);
            break;
         }
      }
   }
   closedir(dir);
   return (latestTime > 0);
}


/*! true if name contains a number */
bool isNumber (const char *name) {
    return strpbrk (name, "0123456789") != NULL;
}

/*! replace $ by sequence 
   can be simplified with g_string_replace(gstr, "$", "\\$", TRUE); */
char *dollarSubstitute (const char* str, char *res, size_t maxLen) {
#ifdef _WIN32
   g_strlcpy (res, str, maxLen); // no issue with $ with Windows 
   return res;
#else  
   res [0] = '\0';
   if (str == NULL) return NULL;
   int len = 0;
   for (size_t i = 0; (i < maxLen) && (str[i] != '\0'); ++i) {
      if (str[i] == '$') {
         res[len++] = '\\';
         res[len++] = '$';
      } else {
         res[len++] = str[i];
      }
   }
   res[len] = '\0';
   return res;
#endif
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

/*! translate str in double for latitude longitude */
double getCoord (const char *str, double minLimit, double maxLimit) {
   double deg = 0.0, min = 0.0, sec = 0.0;
   bool minFound = false;
   const char *neg = "SsWwOo";            // value returned is negative if south or West
   int sign = 1;
   
   sign = (strpbrk (str, neg) == NULL) ? 1 : -1; // -1 if neg found
   char *pt = NULL;
   // find degrees
   while (*str && (! (isdigit (*str) || (*str == '-') || (*str == '+')))) 
      str++;
   deg  = strtod (str, NULL);
   if (deg < 0) sign = -1;                // force sign if negative value found 
   deg = fabs (deg);
   // find minutes
   if ((strchr (str, '\'') != NULL)) {
      minFound = true;
      if ((pt = strstr (str, "°")) != NULL) // degree ° is UTF8 coded with 2 bytes
         pt += 2;
      else 
         if ((pt = strpbrk (str, neg)) != NULL)
            pt += 1;
            
      if (pt != NULL)
         min = strtod (pt, NULL);
   }
   // find seconds
   if (minFound && (strchr (str, '"') != NULL)) {
      sec = strtod (strchr (str, '\'') + 1, NULL);
   }
   
   return CLAMP (sign * (deg + min/60.0 + sec/3600.0), minLimit, maxLimit);
}

/*! build root name if not already a root name */
char *buildRootName (const char *fileName, char *rootName, size_t maxLen) {
   if (fileName [0] == '/') 
      g_strlcpy (rootName, fileName, maxLen);
   else if (par.workingDir [0] != '\0')
      snprintf (rootName, maxLen, "%s%s", par.workingDir, fileName);
   else snprintf (rootName, maxLen, "%s%s", WORKING_DIR, fileName);
   return (rootName);
}

/*! get file size */
long getFileSize (const char *fileName) {
    struct stat st;
    if (stat(fileName, &st) == 0) {
        return st.st_size;
    } else {
        fprintf (stderr, "In getFileSize, Error: %s\n", fileName);
        return -1;
    }
}

/*! return date and time using ISO notation after adding myTime (hours) to the Date */
char *newDate (long intDate, double nHours, char *res, size_t maxLen) {
   struct tm tm0 = {0};
   tm0.tm_year = (intDate / 10000) - 1900;
   tm0.tm_mon = ((intDate % 10000) / 100) - 1;
   tm0.tm_mday = intDate % 100;
   tm0.tm_isdst = -1;                     // avoid adjustments with hours summer winter
   int totalMinutes = (int)(nHours * 60);
   tm0.tm_min += totalMinutes;
   mktime (&tm0);                         // adjust tm0 struct (manage day, mon year overflow)
   snprintf (res, maxLen, "%4d/%02d/%02d %02d:%02d", tm0.tm_year + 1900, tm0.tm_mon + 1, tm0.tm_mday,\
      tm0.tm_hour, tm0.tm_min);
   return res;
}

/*! return date and time using week day after adding myTime (hours) to the Date */
char *newDateWeekDay (long intDate, double nHours, char *res, size_t maxLen) {
   struct tm tm0 = {0};
   tm0.tm_year = (intDate / 10000) - 1900;
   tm0.tm_mon = ((intDate % 10000) / 100) - 1;
   tm0.tm_mday = intDate % 100;
   tm0.tm_isdst = -1;                     // avoid adjustments with hours summer winter
   int totalMinutes = (int)(nHours * 60);
   tm0.tm_min += totalMinutes;
   mktime (&tm0);                         // adjust tm0 struct (manage day, mon year overflow)
   strftime (res, maxLen, "%a %H:%M", &tm0);
   return res;
}

/*! return date and time using week day after adding myTime (hours) to the Date */
char *newDateWeekDayVerbose (long intDate, double nHours, char *res, size_t maxLen) {
   struct tm tm0 = {0};
   tm0.tm_year = (intDate / 10000) - 1900;
   tm0.tm_mon = ((intDate % 10000) / 100) - 1;
   tm0.tm_mday = intDate % 100;
   tm0.tm_isdst = -1;                     // avoid adjustments with hours summer winter
   int totalMinutes = (int)(nHours * 60);
   tm0.tm_min += totalMinutes;
   mktime (&tm0);                         // adjust tm0 struct (manage day, mon year overflow)
   strftime (res, maxLen, "%A, %b %d at %H:%M UTC", &tm0);
   return res;
}

/*! convert lat to str according to type */
char *latToStr (double lat, int type, char* str, size_t maxLen) {
   double mn = 60 * lat - 60 * (int) lat;
   double sec = 3600 * lat - (3600 * (int) lat) - (60 * (int) mn);
   char c = (lat > 0) ? 'N' : 'S';
   switch (type) {
   case BASIC: snprintf (str, maxLen, "%.2lf°", lat); break;
   case DD: snprintf (str, maxLen, "%06.2lf°%c", fabs (lat), c); break;
   case DM: snprintf (str, maxLen, "%02d°%05.2lf'%c", (int) fabs (lat), fabs(mn), c); break;
   case DMS: 
      snprintf (str, maxLen, "%02d°%02d'%02.0lf\"%c", (int) fabs (lat), (int) fabs(mn), fabs(sec), c);
      break;
   default:;
   }
   return str;
}

/*! convert lon to str according to type */
char *lonToStr (double lon, int type, char *str, size_t maxLen) {
   double mn = 60 * lon - 60 * (int) lon;
   double sec = 3600 * lon - (3600 * (int) lon) - (60 * (int) mn);
   char c = (lon > 0) ? 'E' : 'W';
   switch (type) {
   case BASIC: snprintf (str, maxLen, "%.2lf°", lon); break;
   case DD: snprintf (str, maxLen, "%06.2lf°%c", fabs (lon), c); break;
   case DM: snprintf (str, maxLen, "%03d°%05.2lf'%c", (int) fabs (lon), fabs(mn), c); break;
   case DMS:
      snprintf (str, maxLen, "%03d°%02d'%02.0lf\"%c", (int) fabs (lon), (int) fabs(mn), fabs(sec), c);
      break;
   default:;
   }
   return str;
}

/*! convert hours in string with days, hours, minutes */
char *durationToStr (double duration, char *res, size_t maxLen) {
   int nDays, nHours, nMin;
   nDays = duration / 24;
   nHours = fmod (duration, 24.0);
   nMin = 60 * fmod (duration, 1.0);
   if (nDays == 0)
      snprintf (res, maxLen, "%02d:%02d", nHours, nMin); 
   else
      snprintf (res, maxLen, "%d Days %02d:%02d", nDays, nHours, nMin);
   // printf ("Duration: %.2lf hours, equivalent to %d days, %02d:%02d\n", duration, nDays, nHours, nMin);
   return res;
} 

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
    const size_t MIN_NAME_LENGTH = 3;
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

/*! translate poi table into a string */
char *poiToStr (bool portCheck, char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE] = "";
   char strLat [MAX_SIZE_NAME] = "";
   char strLon [MAX_SIZE_LINE] = "";
   snprintf (str, MAX_SIZE_LINE, "Lat         Lon           Type  Level Country Name\n");
   
   for (int i = 0; i < nPoi; i++) {
      if ((tPoi [i].type > UNVISIBLE) && ((tPoi [i].type != PORT) || portCheck)) {
         snprintf (line, MAX_SIZE_LINE, "%-12s %-12s %6d %6d %7s %s\n",\
            latToStr (tPoi[i].lat, par.dispDms, strLat, sizeof (strLat)),\
            lonToStr (tPoi[i].lon, par.dispDms, strLon, sizeof (strLon)),\
            tPoi [i].type, tPoi [i].level, tPoi [i].cc, tPoi[i].name);
	      g_strlcat (str, line, maxLen);
      }
   }
   return str;
}

/*! return name of nearest port found in file fileName from lat, lon. return empty string if not found */
char *nearestPort (double lat, double lon, const char *fileName, char *res, size_t maxLen) {
   const double MIN_SHOM_LAT = 43.0;
   const double MAX_SHOM_LAT = 51.0;
   const double MIN_SHOM_LON = -4.0;
   const double MAX_SHOM_LON = 3.0;
   double minDist = DBL_MAX;
   FILE *f;
   char str [MAX_SIZE_LINE];
   double latPort, lonPort;
   char portName [MAX_SIZE_NAME];
   res [0] = '\0';
   if ((lat < MIN_SHOM_LAT) || (lat > MAX_SHOM_LAT) || (lon < MIN_SHOM_LON) || (lon > MAX_SHOM_LON))
      return res;

   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In nearestPort, Error cannot open: %s\n", fileName);
      return res;
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
   return res;
}

/*! read issea file and fill table tIsSea */
bool readIsSea (const char *fileName) {
   FILE *f = NULL;
   int i = 0;
   char c;
   int nSea = 0;
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In readIsSea, Error cannot open: %s\n", fileName);
      return false;
   }
	if ((tIsSea = (char *) malloc (SIZE_T_IS_SEA + 1)) == NULL) {
		fprintf (stderr, "In readIsSea, error Malloc");
      fclose (f);
		return false;
	}

   while (((c = fgetc (f)) != -1) && (i < SIZE_T_IS_SEA)) {
      if (c == '1') nSea += 1;
      tIsSea [i] = c - '0';
      i += 1;
   }
   fclose (f);
   printf ("isSea file     : %s, Size: %d, nIsea: %d, Proportion sea: %lf\n", fileName, i, nSea, (double) nSea/ (double) i); 
   return true;
} 

/*! reinit zone decribing geographic meta data */
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
      snprintf (str, maxLen, "%d/%02d/%02d %02d:%02d:%02d", 
         utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
         utc->tm_hour, utc->tm_min, utc->tm_sec);
   else
      snprintf (str, maxLen, "%d/%02d/%02d %02d:%02d", 
         utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
         utc->tm_hour, utc->tm_min);
      
   return str;
}

/*! return str representing grib date */
char *gribDateTimeToStr (long date, long time, char *str, size_t maxLen) {
   int year = date / 10000;
   int mon = (date % 10000) / 100;
   int day = date % 100;
   int hour = time / 100;
   int min = time % 100;
   snprintf (str, maxLen, "%4d/%02d/%02d %02d:%02d", year, mon, day, hour, min);
   return str;
}

/*! convert long date found in grib file in seconds time_t */
time_t gribDateTimeToEpoch (long date, long time) {
   struct tm tm0 = {0};
   tm0.tm_year = (date / 10000) - 1900;
   tm0.tm_mon = ((date % 10000) / 100) - 1;
   tm0.tm_mday = date % 100;
   tm0.tm_hour = time / 100;
   tm0.tm_min = time % 100;
   tm0.tm_isdst = -1;                        // avoid adjustments with hours summer winter
   return mktime (&tm0);                                 
}

/*! return difference in hours between two zones (current zone and Wind zone) */
double zoneTimeDiff (const Zone *zone1, const Zone *zone0) {
   if (zone1->wellDefined && zone0->wellDefined) {
      time_t time1 = gribDateTimeToEpoch (zone1->dataDate [0], zone1->dataTime [0]);
      time_t time0 = gribDateTimeToEpoch (zone0->dataDate [0], zone0->dataTime [0]);
      return (time1 - time0) / 3600.0;
   }
   else return 0;
} 

/*! print Grib u v ... for all lat lon time information */
void printGrib (const Zone *zone, const FlowP *gribData) {
   int iGrib;
   printf ("printGribAll\n");
   for (size_t  k = 0; k < zone->nTimeStamp; k++) {
      double t = zone->timeStamp [k];
      printf ("Time: %.0lf\n", t);
      printf ("lon   lat   u     v     g     w     msl     prate\n");
      for (int i = 0; i < zone->nbLat; i++) {
         for (int j = 0; j < zone->nbLon; j++) {
	         iGrib = (k * zone->nbLat * zone->nbLon) + (i * zone->nbLon) + j;
            printf (" %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f\n", \
            gribData [iGrib].lon, \
	         gribData [iGrib].lat, \
            gribData [iGrib].u, \
            gribData [iGrib].v, \
            gribData [iGrib].g, \
            gribData [iGrib].w, \
            gribData [iGrib].msl, \
            gribData [iGrib].prate);
         }
      }
     printf ("\n");
   }
}

/* true if all value seem OK */
static bool checkGrib (const Zone *zone, int iFlow, CheckGrib *check) {
   const int MAX_UV = 100;
   const int MAX_W = 20;
   memset (check, 0, sizeof (CheckGrib));
   for (size_t iGrib = 0; iGrib < zone->nTimeStamp * zone->nbLat * zone->nbLon; iGrib++) {
      if (tGribData [iFlow] [iGrib].u == MISSING) check->uMissing += 1;
	   else if (fabs (tGribData [iFlow] [iGrib].u) > MAX_UV) check->uStrange += 1;
   
      if (tGribData [iFlow] [iGrib].v == MISSING) check->vMissing += 1;
	   else if (fabs (tGribData [iFlow] [iGrib].v) > MAX_UV) check->vStrange += 1;

      if (tGribData [iFlow] [iGrib].w == MISSING) check->wMissing += 1;
	   else if ((tGribData [iFlow] [iGrib].w > MAX_W) ||  (tGribData [iFlow] [iGrib].w < 0)) check->wStrange += 1;

      if (tGribData [iFlow] [iGrib].g == MISSING) check->gMissing += 1;
	   else if ((tGribData [iFlow] [iGrib].g > MAX_UV) || (tGribData [iFlow] [iGrib].g < 0)) check->gStrange += 1;

      if ((tGribData [iFlow] [iGrib].lat > zone->latMax) || (tGribData [iFlow] [iGrib].lat < zone->latMin) ||
         (tGribData [iFlow] [iGrib].lon > zone->lonRight) || (tGribData [iFlow] [iGrib].lon < zone->lonLeft))
		   check->outZone += 1;
   }
   return (check->uMissing == 0) && (check->vMissing == 0) && (check->gMissing == 0) /* && (check->wMissing == 0)*/ && 
          (check->uStrange == 0) && (check->vStrange == 0) && (check->gStrange == 0) && (check->wStrange == 0) && 
          (check->outZone == 0);
}

/*! true if (wind) zone and currentZone intersect in geography */
static bool geoIntersectGrib (const Zone *zone1, const Zone *zone2) {
	return 
	  ((zone2->latMin < zone1 -> latMax) &&
	   (zone2->latMax > zone1 -> latMin) &&
	   (zone2->lonLeft < zone1 -> lonRight) &&
	   (zone2->lonRight > zone1 -> lonLeft));
}

/*! true if (wind) zone and zone 2 intersect in time */
static bool timeIntersectGrib (const Zone *zone1, const Zone *zone2) {
	time_t timeMin1 = gribDateTimeToEpoch (zone1->dataDate [0], zone1->dataTime [0]);
	time_t timeMin2 = gribDateTimeToEpoch (zone2->dataDate [0], zone2->dataTime [0]);
	time_t timeMax1 = timeMin1 + 3600 * (zone1-> timeStamp [zone1 -> nTimeStamp -1]);
	time_t timeMax2 = timeMin2 + 3600 * (zone2-> timeStamp [zone2 -> nTimeStamp -1]);
	
	return (timeMin2 < timeMax1) && (timeMax2 > timeMin1);
}

/*! check if time steps are regular */
static bool timeStepRegularGrib (const Zone *zone) {
   if (zone->nTimeStamp  < 2) return true;
   long timeStep = zone->timeStamp [1] - zone->timeStamp [0];
   for (size_t i = 1; i < zone->nTimeStamp - 1; i++) {
      if ((zone->timeStamp [i] - zone->timeStamp [i-1]) != timeStep) {
         return false;
      }
   }
   return true;
}

/*! true if u and v (or uCurr, vCurr) are in zone */
static bool uvPresentGrib (const Zone *zone) {
   bool uPresent = false, vPresent = false;
   for (size_t i = 0; i < zone->nShortName; i++) {
      if ((strcmp (zone->shortName [i], "10u") == 0) || (strcmp (zone->shortName [i], "u") == 0) ||
		   (strcmp (zone->shortName [i], "ucurr") == 0)) 
		uPresent = true;
      if ((strcmp (zone->shortName [i], "10v") == 0)  || (strcmp (zone->shortName [i], "v") == 0) || 
		   (strcmp (zone->shortName [i], "vcurr") == 0)) 
		vPresent = true;
   }
   return uPresent && vPresent;
}

/*! check lat, lon are consistent with indice 
	return number of values */
static int consistentGrib (const Zone *zone, int iFlow, double epsilon, int *nLatSuspects, int *nLonSuspects) {
   int n = 0, iGrib;
   double lat, lon;
   *nLatSuspects = 0;
   *nLonSuspects = 0;
   for (size_t k = 0; k < zone->nTimeStamp; k++) {
      for (int i = 0; i < zone->nbLat; i++) {
         for (int j = 0; j < zone->nbLon; j++) {
            n += 1,
	         iGrib = (k * zone->nbLat * zone->nbLon) + (i * zone->nbLon) + j;
            lat = zone->latMin + i * zone->latStep;  
            lon = zone->lonLeft + j * zone->lonStep;
            if (! zone->anteMeridian) 
               lon = lonCanonize (lon);
            if (fabs (lat - tGribData [iFlow][iGrib].lat) > epsilon) {
               *nLatSuspects += 1;
            }
            if (fabs (lon - tGribData [iFlow][iGrib].lon) > epsilon) {
               *nLonSuspects += 1;
            }
         }
      }
   }
   return n;
}

/*! check Grib information and write (add) report in the buffer
    return false if something wrong  */
bool checkGribInfoToStr (int type, Zone *zone, char *buffer, size_t maxLen) {
   const double WIND_EPSILON = 0.01;
   const double CURRENT_EPSILON = 0.1;
   CheckGrib sCheck;
   int nVal = 0, nLatSuspects, nLonSuspects;
   bool OK = true;
   char str [MAX_SIZE_LINE] = "";
   char *separator = g_strnfill (78, '-'); 
   
   snprintf (str, MAX_SIZE_LINE, "\n%s\nCheck Grib Info: %s\n%s\n", separator, (type == WIND) ? "Wind" : "Current", separator);
   g_free (separator);
   g_strlcat (buffer, str, maxLen);

   if (zone->nbLat <= 0) {
      snprintf (str, MAX_SIZE_LINE, "No %s grib available\n", (type == WIND) ? "Wind" : "Current");
      g_strlcat (buffer, str, maxLen);
      return true;
   }

   if ((zone->nDataDate != 1) || (zone->nDataTime != 1)) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected nDataDate = 1 and nDataTime = 1. nDataDate: %zu, nDataTime: %zu\n", zone->nDataDate, zone->nDataTime);
      g_strlcat (buffer, str, maxLen);
   }
   if (zone->stepUnits != 1) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected stepUnits = 1, stepUnits = %ld\n", zone->stepUnits);
      g_strlcat (buffer, str, maxLen);
   }
   if (zone->numberOfValues != zone->nbLon * zone->nbLat) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected numberofValues = nbLon x nbLat = %ld, but numberOfValues = %ld\n",\
         zone->nbLon * zone->nbLat, zone->numberOfValues);
      g_strlcat (buffer, str, maxLen);
   }
   if ((zone->lonRight - zone->lonLeft) != (zone->lonStep * (zone->nbLon - 1))) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected difference between lonLeft and lonRight is %.2lf, found: %.2lf\n",\
         (zone->lonStep * (zone->nbLon - 1)), zone->lonRight - zone->lonLeft);
      g_strlcat (buffer, str, maxLen);
   }
   if ((zone->latMax - zone->latMin) != (zone->latStep * (zone->nbLat - 1))) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected difference between latMax and latMin is %.2lf, found: %.2lf\n",\
         (zone->latStep * (zone->nbLat - 1)), zone->latMax - zone->latMin);
      g_strlcat (buffer, str, maxLen);
   }
   if ((zone->nTimeStamp) < 1 ) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected nTimeStamp >= 1, nTimeStamp =  %zu\n", zone->nTimeStamp);
      g_strlcat (buffer, str, maxLen);
   }

   nVal = consistentGrib (zone, type, (type == WIND) ? WIND_EPSILON : CURRENT_EPSILON, &nLatSuspects, &nLonSuspects);
   if ((nLatSuspects > 0) || (nLonSuspects > 0)) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "n Val suspect Lat: %d, ratio: %.2lf %% \n", nLatSuspects, 100 * (double)nLatSuspects/(double) (nVal));
      g_strlcat (buffer, str, maxLen);
      snprintf (str, MAX_SIZE_LINE, "n Val suspect Lon: %d, ratio: %.2lf %% \n", nLonSuspects, 100 * (double)nLonSuspects/(double) (nVal));
      g_strlcat (buffer, str, maxLen);
   }
      snprintf (str, MAX_SIZE_LINE, "n Val Values: %d\n", nVal); // at least this line
   g_strlcat (buffer, str, maxLen);
   snprintf (str, MAX_SIZE_LINE, "%s\n", (zone->wellDefined) ? (zone->allTimeStepOK) ? "Wind Zone Well defined" : "All Zone TimeSteps are not defined" : "Zone Undefined");
   g_strlcat (buffer, str, maxLen);
   
   if (!uvPresentGrib (zone)) { 
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "lack u or v\n");
      g_strlcat (buffer, str, maxLen);
   }
   
   if (! timeStepRegularGrib (zone)) {
      OK = false;
	   snprintf (str, MAX_SIZE_LINE, "timeStep is NOT REGULAR !!!\n");
      g_strlcat (buffer, str, maxLen);
   }
   
   // check grib missing or strange values and out of zone for wind
   if (! checkGrib (zone, type, &sCheck)) {
      OK = false;
      snprintf (str, MAX_SIZE_LINE, "out zone Values: %d, ratio: %.2lf %% \n", sCheck.outZone, 100 * (double)sCheck.outZone/(double) (nVal));
      g_strlcat (buffer, str, maxLen);

      snprintf (str, MAX_SIZE_LINE, "u missing Values: %d, ratio: %.2lf %% \n", sCheck.uMissing, 100 * (double)sCheck.uMissing/(double) (nVal));
      g_strlcat (buffer, str, maxLen);
      snprintf (str, MAX_SIZE_LINE, "u strange Values: %d, ratio: %.2lf %% \n", sCheck.uStrange, 100 * (double)sCheck.uStrange/(double) (nVal));
      g_strlcat (buffer, str, maxLen);

      snprintf (str, MAX_SIZE_LINE, "v missing Values: %d, ratio: %.2lf %% \n", sCheck.vMissing, 100 * (double)sCheck.vMissing/(double) (nVal));
      g_strlcat (buffer, str, maxLen);
      snprintf (str, MAX_SIZE_LINE, "v strange Values: %d, ratio: %.2lf %% \n", sCheck.vStrange, 100 * (double)sCheck.vStrange/(double) (nVal));
      g_strlcat (buffer, str, maxLen);

      if (type == WIND) {
         snprintf (str, MAX_SIZE_LINE, "w missing Values: %d, ratio: %.2lf %% \n", sCheck.wMissing, 100 * (double)sCheck.wMissing/(double) (nVal));
         g_strlcat (buffer, str, maxLen);
         
         snprintf (str, MAX_SIZE_LINE, "w strange Values: %d, ratio: %.2lf %% \n", sCheck.wStrange, 100 * (double)sCheck.wStrange/(double) (nVal));
         g_strlcat (buffer, str, maxLen);

         snprintf (str, MAX_SIZE_LINE, "g missing Values: %d, ratio: %.2lf %% \n", sCheck.gMissing, 100 * (double)sCheck.gMissing/(double) (nVal));
         g_strlcat (buffer, str, maxLen);
         snprintf (str, MAX_SIZE_LINE, "g strange Values: %d, ratio: %.2lf %% \n", sCheck.gStrange, 100 * (double)sCheck.gStrange/(double) (nVal));
         g_strlcat (buffer, str, maxLen);
      }
   } 
   return OK;
}

/*! check Grib information and write report in the buffer
    return false if something wrong  */
bool checkGribToStr (char *buffer, size_t maxLen) {
   char str [MAX_SIZE_LINE] = "";
   buffer [0] = '\0';

   bool OK = (checkGribInfoToStr (WIND, &zone, buffer, maxLen));
   if (OK)  
      buffer [0] = '\0';
   if (! checkGribInfoToStr (CURRENT, &currentZone, buffer, maxLen))
      OK = false;
   if (OK)  
      buffer [0] = '\0';
   if (currentZone.nbLat > 0) { // current exist
      // check geo intersection between current and wind
      if (! geoIntersectGrib (&zone, &currentZone)) {
         OK = false;
         snprintf (str, MAX_SIZE_LINE, "\nCurrent and wind grib have no common geo\n");
         g_strlcat (buffer, str, maxLen);
      }
      // check time intersection between current and wind
      if (! timeIntersectGrib (&zone, &currentZone)) {
         OK = false;
	      snprintf (str, MAX_SIZE_LINE, "\nCurrent and wind grib have no common time\n");
         g_strlcat (buffer, str, maxLen);
      }
   }
   return OK;
}

/*! find iTinf and iTsup in order t : zone.timeStamp [iTInf] <= t <= zone.timeStamp [iTSup] */
static inline void findTimeAround (double t, int *iTInf, int *iTSup, const Zone *zone) { 
   size_t k;
   if (t <= zone->timeStamp [0]) {
      *iTInf = 0;
      *iTSup = 0;
      return;
   }
   for (k = 0; k < zone->nTimeStamp; k++) {
      if (t == zone->timeStamp [k]) {
         *iTInf = k;
	      *iTSup = k;
	      return;
      }
      if (t < zone->timeStamp [k]) {
         *iTInf = k - 1;
         *iTSup = k;
         return;
      }
   }
   *iTInf = zone->nTimeStamp -1;
   *iTSup = zone->nTimeStamp -1;
}

/*! ex arrondiMin (46.4, 0.25) = 46.25 */
static inline double arrondiMin (double v, double step) {
   return ((double) floor (v/step)) * step;
} 
 
/*! ex arrondiMax (46.4, 0.25) = 46.50 */
static inline double arrondiMax (double v, double step) {
   return ((double) ceil (v/step)) * step;
}

/*! provide 4 wind points around p */
static inline void find4PointsAround (double lat, double lon,  double *latMin, 
   double *latMax, double *lonMin, double *lonMax, Zone *zone) {

   *latMin = arrondiMin (lat, zone->latStep);
   *latMax = arrondiMax (lat, zone->latStep);
   *lonMin = arrondiMin (lon, zone->lonStep);
   *lonMax = arrondiMax (lon, zone->lonStep);
  
   // check limits
   if (zone->latMin > *latMin) *latMin = zone->latMin;
   if (zone->latMax < *latMax) *latMax = zone->latMax; 
   if (zone->lonLeft > *lonMin) *lonMin = zone->lonLeft; 
   if (zone->lonRight < *lonMax) *lonMax = zone->lonRight;
   
   if (zone->latMax < *latMin) *latMin = zone->latMax; 
   if (zone->latMin > *latMax) *latMax = zone->latMin; 
   if (zone->lonRight < *lonMin) *lonMin = zone->lonRight; 
   if (zone->lonLeft > *lonMax) *lonMax = zone->lonLeft;
}

/*! return indice of lat in gribData wind or current  */
static inline int indLat (double lat, const Zone *zone) {
   return (int) round ((lat - zone->latMin)/zone->latStep);
}

/*! return indice of lon in gribData wind or current */
static inline int indLon (double lon, const Zone *zone) {
   if (lon < zone->lonLeft) lon += 360;
   return (int) round ((lon - zone->lonLeft)/zone->lonStep);
}

/*! interpolation to get u, v, g (gust), w (waves) at point (lat, lon)  and time t */
static bool findFlow (double lat, double lon, double t, double *rU, double *rV, \
   double *rG, double *rW, double *msl, double *prate, Zone *zone, const FlowP *gribData) {

   double t0,t1;
   double latMin, latMax, lonMin, lonMax;
   double a, b, u0, u1, v0, v1, g0, g1, w0, w1, msl0, msl1, prate0, prate1;
   int iT0, iT1;
   FlowP windP00, windP01, windP10, windP11;
   if ((!zone->wellDefined) || (zone->nbLat == 0) || (! isInZone (lat, lon, zone) && par.constWindTws == 0) || (t < 0)){
      *rU = 0, *rV = 0, *rG = 0; *rW = 0;
      return false;
   }
   
   findTimeAround (t, &iT0, &iT1, zone);
   find4PointsAround (lat, lon, &latMin, &latMax, &lonMin, &lonMax, zone);
   //printf ("lat %lf iT0 %d, iT1 %d, latMin %lf latMax %lf lonMin %lf lonMax %lf\n", p.lat, iT0, iT1, latMin, latMax, lonMin, lonMax);  
   // 4 points at time t0
   windP00 = gribData [iT0 * zone->nbLat * zone->nbLon + indLat(latMax, zone) * zone->nbLon + indLon(lonMin, zone)];  
   windP01 = gribData [iT0 * zone->nbLat * zone->nbLon + indLat(latMax, zone) * zone->nbLon + indLon(lonMax, zone)];
   windP10 = gribData [iT0 * zone->nbLat * zone->nbLon + indLat(latMin, zone) * zone->nbLon + indLon(lonMax, zone)];
   windP11 = gribData [iT0 * zone->nbLat * zone->nbLon + indLat(latMin, zone) * zone->nbLon + indLon(lonMin, zone)];

   // printf ("00lat %lf  01lat %lf  10lat %lf 11lat %lf\n", windP00.lat, windP01.lat,windP10.lat,windP11.lat); 
   // printf ("00tws %lf  01tws %lf  10tws %lf 11tws %lf\n", windP00.tws, windP01.tws,windP10.tws,windP11.tws); 
   // speed longitude interpolation at northern latitudes
  
   // interpolation  for u, v, w, g, msl, prate
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.u, windP01.u);
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.u, windP11.u); 
   u0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.v, windP01.v); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.v, windP11.v); 
   v0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.g, windP01.g); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.g, windP11.g); 
   g0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
    
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.w, windP01.w); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.w, windP11.w); 
   w0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
    
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.msl, windP01.msl); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.msl, windP11.msl); 
   msl0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
    
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.prate, windP01.prate); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.prate, windP11.prate); 
   prate0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
    
   //printf ("lat %lf a %lf  b %lf  00lat %lf 10lat %lf  %lf\n", p.lat, a, b, windP00.lat, windP10.lat, tws0); 

   // 4 points at time t1
   windP00 = gribData [iT1 * zone->nbLat * zone->nbLon + indLat(latMax, zone) * zone->nbLon + indLon(lonMin, zone)];  
   windP01 = gribData [iT1 * zone->nbLat * zone->nbLon + indLat(latMax, zone) * zone->nbLon + indLon(lonMax, zone)];
   windP10 = gribData [iT1 * zone->nbLat * zone->nbLon + indLat(latMin, zone) * zone->nbLon + indLon(lonMax, zone)];
   windP11 = gribData [iT1 * zone->nbLat * zone->nbLon + indLat(latMin, zone) * zone->nbLon + indLon(lonMin, zone)];

   // interpolatuon for  for u, v
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.u, windP01.u);
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.u, windP11.u); 
   u1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.v, windP01.v); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.v, windP11.v); 
   v1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
    
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.g, windP01.g); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.g, windP11.g); 
   g1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.w, windP01.w); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.w, windP11.w); 
   w1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.msl, windP01.msl); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.msl, windP11.msl); 
   msl1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.prate, windP01.prate); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.prate, windP11.prate); 
   prate1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   // finally, interpolation twd tws between t0 and t1
   t0 = zone->timeStamp [iT0];
   t1 = zone->timeStamp [iT1];
   *rU = interpolate (t, t0, t1, u0, u1);
   *rV = interpolate (t, t0, t1, v0, v1);
   *rG = interpolate (t, t0, t1, g0, g1);
   *rW = interpolate (t, t0, t1, w0, w1);
   *msl = interpolate (t, t0, t1, msl0, msl1);
   *prate = interpolate (t, t0, t1, prate0, prate1);

   return true;
}

/*! use findflow to get wind and waves */
void findWindGrib (double lat, double lon, double t, double *u, double *v, \
   double *gust, double *w, double *twd, double *tws ) {

   double msl, prate;
   if (par.constWindTws != 0) {
      *twd = par.constWindTwd;
      *tws = par.constWindTws;
	   *u = - KN_TO_MS * par.constWindTws * sin (DEG_TO_RAD * par.constWindTwd);
	   *v = - KN_TO_MS * par.constWindTws * cos (DEG_TO_RAD * par.constWindTwd);
      *w = 0;
   }
   else {
      findFlow (lat, lon, t, u, v, gust, w, &msl, &prate,  &zone, tGribData [WIND]);
      *twd = fTwd (*u, *v);
      *tws = fTws (*u, *v);
   }
   if (par.constWave < 0) *w = 0;
   else if (par.constWave != 0) *w = par.constWave;
}

/*! use findflow to get rain */
double findRainGrib (double lat, double lon, double t) {
   double u, v, g, w, msl, prate;
   findFlow (lat, lon, t, &u, &v, &g, &w, &msl, &prate, &zone, tGribData [WIND]);
   return prate;
}

/*! use findflow to get pressure */
double findPressureGrib (double lat, double lon, double t) {
   double u, v, g, w, msl, prate;
   findFlow (lat, lon, t, &u, &v, &g, &w, &msl, &prate, &zone, tGribData [WIND]);
   return msl;
}

/*! use findflow to get current */
void findCurrentGrib (double lat, double lon, double t, double *uCurr,\
   double *vCurr, double *tcd, double *tcs) {

   double gust, bidon, msl, prate;
   *uCurr = 0;
   *vCurr = 0;
   *tcd = 0;
   *tcs = 0;
   if (par.constCurrentS != 0) {
      *uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
      *vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
      *tcd = par.constCurrentD; // direction
      *tcs = par.constCurrentS; // speed

   }
   else {
      if (t <= currentZone.timeStamp [currentZone.nTimeStamp - 1]) {
         findFlow (lat, lon, t, uCurr, vCurr, &gust, &bidon, &msl, &prate, &currentZone, tGribData [CURRENT]);
         *tcd = fTwd (*uCurr, *vCurr);
         *tcs = fTws (*uCurr, *vCurr);
      }
   }
}

/*! Modify arrr with new value if not already in array. Return new array size*/
static long updateLong (long value, size_t n, size_t maxSize, long array []) {
   bool found = false;
   for (size_t i = 0; i < n; i++) {
      if (value == array [i]) {
         found = true;
         break;
      }
   } 
   if ((! found) && (n < maxSize)) { // new time stamp
      array [n] = value;
	   n += 1;
   }
   return n;
}

/*! Read lists zone.timeStamp, shortName, zone.dataDate, dataTime before full grib reading */
static bool readGribLists (const char *fileName, Zone *zone) {
   FILE* f = NULL;
   int err = 0;
   long timeStep, dataDate, dataTime;
   char shortName [MAX_SIZE_SHORT_NAME];
   size_t lenName;
   bool found = false;
   memset (zone, 0,  sizeof (Zone));
   // Message handle. Required in all the ecCodes calls acting on a message.
   codes_handle* h = NULL;

   if ((f = fopen (fileName, "rb")) == NULL) {
       fprintf (stderr, "In readGribLists, ErrorUnable to open file %s\n", fileName);
       return false;
   }

   // Loop on all the messages in a file
   while ((h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err)) != NULL) {
      if (err != CODES_SUCCESS) CODES_CHECK (err, 0);
      lenName = MAX_SIZE_SHORT_NAME;

      CODES_CHECK(codes_get_string(h, "shortName", shortName, &lenName), 0);
      CODES_CHECK(codes_get_long(h, "step", &timeStep), 0);
      CODES_CHECK(codes_get_long(h, "dataDate", &dataDate), 0);
      CODES_CHECK(codes_get_long(h, "dataTime", &dataTime), 0);
      codes_handle_delete (h);

      found = false;
      for (size_t i = 0; i < zone->nShortName; i++) {
         if (strcmp (shortName, zone->shortName [i]) == 0) {
            found = true;
            break;
         }
      } 
      if ((! found) && (zone->nShortName < MAX_N_SHORT_NAME)) { // new shortName
         g_strlcpy (zone->shortName [zone->nShortName], shortName, MAX_SIZE_SHORT_NAME); 
         zone->nShortName += 1;
      }
      zone->nTimeStamp = updateLong (timeStep, zone->nTimeStamp, MAX_N_TIME_STAMPS, zone->timeStamp); 
      zone->nDataDate = updateLong (dataDate, zone->nDataDate, MAX_N_DATA_DATE, zone->dataDate); 
      zone->nDataTime = updateLong (dataTime, zone->nDataTime, MAX_N_DATA_TIME, zone->dataTime); 
   }
 
   fclose (f);
   // Replace unknown by gust ? (GFS case where gust identified by specific indicatorOfParameter)
   for (size_t i = 0; i < zone->nShortName; i++) {
      if (strcmp (zone->shortName[i], "unknown") == 0)
         g_strlcpy (zone->shortName[i], "gust?", 6);
   }
   return true;
}

/*! Read grib parameters in zone before full grib reading */
static bool readGribParameters (const char *fileName, Zone *zone) {
   int err = 0;
   FILE* f = NULL;
   double lat1, lat2;
   codes_handle *h = NULL;
 
   if ((f = fopen (fileName, "rb")) == NULL) {
       fprintf (stderr, "In readGribParameters, Error unable to open file %s\n",fileName);
       return false;
   }
 
   // create new handle from the first message in the file
   if ((h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err)) == NULL) {
       fprintf (stderr, "In readGribParameters, Error code handle from file : %s Code error: %s\n",\
         fileName, codes_get_error_message(err)); 
       fclose (f);
       return false;
   }
   fclose (f);
 
   CODES_CHECK(codes_get_long (h, "centre", &zone->centreId),0);
   CODES_CHECK(codes_get_long (h, "editionNumber", &zone->editionNumber),0);
   CODES_CHECK(codes_get_long (h, "stepUnits", &zone->stepUnits),0);
   CODES_CHECK(codes_get_long (h, "numberOfValues", &zone->numberOfValues),0);
   CODES_CHECK(codes_get_long (h, "Ni", &zone->nbLon),0);
   CODES_CHECK(codes_get_long (h, "Nj", &zone->nbLat),0);

   CODES_CHECK(codes_get_double (h,"latitudeOfFirstGridPointInDegrees",&lat1),0);
   CODES_CHECK(codes_get_double (h,"longitudeOfFirstGridPointInDegrees",&zone->lonLeft),0);
   CODES_CHECK(codes_get_double (h,"latitudeOfLastGridPointInDegrees",&lat2),0);
   CODES_CHECK(codes_get_double (h,"longitudeOfLastGridPointInDegrees",&zone->lonRight),0);
   CODES_CHECK(codes_get_double (h,"iDirectionIncrementInDegrees",&zone->lonStep),0);
   CODES_CHECK(codes_get_double (h,"jDirectionIncrementInDegrees",&zone->latStep),0);

   if ((lonCanonize (zone->lonLeft) > 0.0) && (lonCanonize (zone->lonRight) < 0.0)) {
      zone -> anteMeridian = true;
   }
   else {
      zone -> anteMeridian = false;
      zone->lonLeft = lonCanonize (zone->lonLeft);
      zone->lonRight = lonCanonize (zone->lonRight);
   }
   zone->latMin = MIN (lat1, lat2);
   zone->latMax = MAX (lat1, lat2);

   codes_handle_delete (h);
   return true;
}

/*! find index in gribData table */
static inline int indexOf (int timeStep, double lat, double lon, const Zone *zone) {
   int iLat = indLat (lat, zone);
   int iLon = indLon (lon, zone);
   int iT = -1;
   for (iT = 0; iT < (int) zone->nTimeStamp; iT++) {
      if (timeStep == zone->timeStamp [iT])
         break;
   }
   if (iT == -1) {
      fprintf (stderr, "In indexOf, Error Cannot find index of time: %d\n", timeStep);
      return -1;
   }
   //printf ("iT: %d iLon :%d iLat: %d\n", iT, iLon, iLat); 
   return  (iT * zone->nbLat * zone->nbLon) + (iLat * zone->nbLon) + iLon;
}

/*! read grib file using eccodes C API 
   return true if OK */
bool readGribAll (const char *fileName, Zone *zone, int iFlow) {
   FILE* f = NULL;
   int err = 0, iGrib;
   long bitmapPresent  = 0, timeStep, oldTimeStep;
   double lat, lon, val, indicatorOfParameter;
   char shortName [MAX_SIZE_SHORT_NAME];
   size_t lenName;
   const long GUST_GFS = 180;
   long timeInterval = 0;
   char str [MAX_SIZE_LINE];
   //static int count;
   memset (zone, 0,  sizeof (Zone));
   zone->wellDefined = false;

   if (! readGribLists (fileName, zone)) {
      return false;
   }
   if (! readGribParameters (fileName, zone)) {
      return false;
   }
   if (zone -> nDataDate > 1) {
      fprintf (stderr, "In readGribAll, Error Grib file with more than 1 dataDate not supported nDataDate: %zu\n", 
         zone -> nDataDate);
      return false;
   }
   if (zone->nTimeStamp > 1) {
      timeInterval = zone->timeStamp [1] - zone->timeStamp [0]; 
   }
   else {
      timeInterval = 3;
      fprintf (stderr, "In readGribAll, Error nTimeStamp = %zu\n", zone->nTimeStamp);
      //return false;
   }


   if (tGribData [iFlow] != NULL) {
      free (tGribData [iFlow]); 
      tGribData [iFlow] = NULL;
   }
   if ((tGribData [iFlow] = calloc ((zone->nTimeStamp + 1) * zone->nbLat * zone->nbLon, sizeof (FlowP))) == NULL) { // nTimeStamp + 1
      fprintf (stderr, "In readGribAll, Errorcalloc tGribData [iFlow]\n");
      return false;
   }
   printf ("In readGribAll.: %s allocated\n", 
      formatThousandSep (str, sizeof (str), sizeof(FlowP) * (zone->nTimeStamp + 1) * zone->nbLat * zone->nbLon));
   
   // Message handle. Required in all the ecCodes calls acting on a message.
   codes_handle* h = NULL;
   // Iterator on lat/lon/values.
   codes_iterator* iter = NULL;
   if ((f = fopen (fileName, "rb")) == NULL) {
      free (tGribData [iFlow]); 
      tGribData [iFlow] = NULL;
      fprintf (stderr, "In readGribAll, Error Unable to open file %s\n", fileName);
      return false;
   }
   zone->nMessage = 0;
   zone->allTimeStepOK = true;
   timeStep = zone->timeStamp [0];
   oldTimeStep = timeStep;

   // Loop on all the messages in a file
   while ((h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err)) != NULL) {
      if (err != CODES_SUCCESS) CODES_CHECK (err, 0);
      //printf ("count ReadGribAll: %d\n", count++);

      // Check if a bitmap applies
      CODES_CHECK(codes_get_long(h, "bitmapPresent", &bitmapPresent), 0);
      if (bitmapPresent) {
          CODES_CHECK(codes_set_double(h, "missingValue", MISSING), 0);
      }

      lenName = MAX_SIZE_SHORT_NAME;
      CODES_CHECK(codes_get_string(h, "shortName", shortName, &lenName), 0);
      CODES_CHECK(codes_get_long(h, "step", &timeStep), 0);

      // check timeStep progress well 
      if ((timeStep != 0) && (timeStep != oldTimeStep) && ((timeStep - oldTimeStep) != timeInterval)) {// check timeStep progress well 
         zone->allTimeStepOK = false;
         fprintf (stderr, "In readGribAll: All time Step Are Not defined message: %d, timeStep: %ld shortName: %s\n", 
            zone->nMessage, timeStep, shortName);
         //free (tGribData [iFlow]); 
         //tGribData [iFlow] = NULL;
         //fclose (f);
         //return false;
      }
      oldTimeStep = timeStep;

      err = codes_get_double(h, "indicatorOfParameter", &indicatorOfParameter);
      if (err != CODES_SUCCESS) 
         indicatorOfParameter = -1;
 
      // A new iterator on lat/lon/values is created from the message handle h.
      iter = codes_grib_iterator_new(h, 0, &err);
      if (err != CODES_SUCCESS) CODES_CHECK(err, 0);
 
      // Loop on all the lat/lon/values.
      while (codes_grib_iterator_next(iter, &lat, &lon, &val)) {
         if (! (zone -> anteMeridian))
            lon = lonCanonize (lon);
         // printf ("lon : %.2lf\n", lon);
         iGrib = indexOf ((int) timeStep, lat, lon, zone);
   
         if (iGrib == -1) {
            fprintf (stderr, "In readGribAll: Error iGrib : %d\n", iGrib); 
            free (tGribData [iFlow]); 
            tGribData [iFlow] = NULL;
            codes_handle_delete (h);
            codes_grib_iterator_delete (iter);
            iter = NULL;
            h = NULL;
            fclose (f);
            return false;
         }
         //printf("%.2f %.2f %.2lf %ld %d %s\n", lat, lon, val, timeStep, iGrib, shortName);
         tGribData [iFlow] [iGrib].lat = lat; 
         tGribData [iFlow] [iGrib].lon = lon; 
         if ((strcmp (shortName, "10u") == 0) || (strcmp (shortName, "ucurr") == 0))
            tGribData [iFlow] [iGrib].u = val;
         else if ((strcmp (shortName, "10v") == 0) || (strcmp (shortName, "vcurr") == 0))
            tGribData [iFlow] [iGrib].v = val;
         else if (strcmp (shortName, "gust") == 0)
            tGribData [iFlow] [iGrib].g = val;
         else if ((strcmp (shortName, "msl") == 0) || (strcmp (shortName, "prmsl") == 0))  { // pressure
            tGribData [iFlow] [iGrib].msl = val;
            // printf ("pressure: %.2lf\n", val);
         }
         else if (strcmp (shortName, "prate") == 0) { // precipitation
            tGribData [iFlow] [iGrib].prate = val;
            // if (val > 0) printf ("prate: %lf\n", val);
         }
         else if (strcmp (shortName, "swh") == 0)     // waves
            tGribData [iFlow] [iGrib].w = val;
         else if (indicatorOfParameter == GUST_GFS)   // find gust in GFS file specific parameter = 180
            tGribData [iFlow] [iGrib].g = val;
      }
      codes_grib_iterator_delete (iter);
      codes_handle_delete (h);
      iter = NULL;
      h = NULL;
      zone->nMessage += 1;
   }
 
   fclose (f);
   zone->wellDefined = true;
   return true;
}

/*! write Grib information in string */
char *gribToStr (const Zone *zone, char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE] = "";
   char strTmp [MAX_SIZE_LINE] = "";
   char strLat0 [MAX_SIZE_NAME] = "", strLon0 [MAX_SIZE_NAME] = "";
   char strLat1 [MAX_SIZE_NAME] = "", strLon1 [MAX_SIZE_NAME] = "";
   char centreName [MAX_SIZE_NAME] = "";
   
   for (size_t i = 0; i < sizeof(meteoTab) / sizeof(meteoTab[0]); i++) // search name of center
     if (meteoTab [i].id == zone->centreId) 
        g_strlcpy (centreName, meteoTab [i].name, sizeof (centreName));
         
   newDate (zone->dataDate [0], zone->dataTime [0]/100, strTmp, sizeof (strTmp));
   snprintf (str, maxLen, "Centre ID: %ld %s   %s   Ed number: %ld\nnMessages: %d\nstepUnits: %ld\n# values : %ld\n",\
      zone->centreId, centreName, strTmp, zone->editionNumber, zone->nMessage, zone->stepUnits, zone->numberOfValues);
   snprintf (line, MAX_SIZE_LINE, "Zone From: %s, %s To: %s, %s\n",\
      latToStr (zone->latMin, par.dispDms, strLat0, sizeof (strLat0)),\
      lonToStr (zone->lonLeft, par.dispDms, strLon0, sizeof (strLon0)),\
      latToStr (zone->latMax, par.dispDms, strLat1, sizeof (strLat1)),\
      lonToStr (zone->lonRight, par.dispDms, strLon1, sizeof (strLon1)));
   g_strlcat (str, line, maxLen);   
   snprintf (line, MAX_SIZE_LINE, "LatStep  : %04.4f° LonStep: %04.4f°\n", zone->latStep, zone->lonStep);
   g_strlcat (str, line, maxLen);   
   snprintf (line, MAX_SIZE_LINE, "Nb Lat   : %ld      Nb Lon : %ld\n", zone->nbLat, zone->nbLon);
   g_strlcat (str, line, maxLen);   
   if (zone->nTimeStamp < 8) {
      snprintf (line, MAX_SIZE_LINE, "TimeStamp List of %zu : [ ", zone->nTimeStamp);
      g_strlcat (str, line, maxLen);   
      for (size_t k = 0; k < zone->nTimeStamp; k++) {
         snprintf (line, MAX_SIZE_LINE, "%ld ", zone->timeStamp [k]);
         g_strlcat (str, line, maxLen);   
      }
      g_strlcat (str, "]\n", maxLen);
   }
   else {
      snprintf (line, MAX_SIZE_LINE, "TimeStamp List of %zu : [%ld, %ld, ..%ld]\n", zone->nTimeStamp,\
         zone->timeStamp [0], zone->timeStamp [1], zone->timeStamp [zone->nTimeStamp - 1]);
      g_strlcat (str, line, maxLen);   
   }

   snprintf (line, MAX_SIZE_LINE, "Shortname List: [ ");
   g_strlcat (str, line, maxLen);   
   for (size_t k = 0; k < zone->nShortName; k++) {
      snprintf (line, MAX_SIZE_LINE, "%s ", zone->shortName [k]);
      g_strlcat (str, line, maxLen);   
   }
   g_strlcat (str, "]\n", maxLen);
   if ((zone->nDataDate > 1) || (zone->nDataTime > 1)) {
      snprintf (line, MAX_SIZE_LINE, "Warning number of Date: %zu, number of Time: %zu\n", zone->nDataDate, zone->nDataTime);
      g_strlcat (str, line, maxLen);   
   }
   snprintf (line, MAX_SIZE_LINE, "Zone is       :  %s\n", (zone->wellDefined) ? "Well defined" : "Undefined");
   g_strlcat (str, line, maxLen);   
   return str;
}

/* str to double accerpting both . or , as decimal separator */
bool strtodNew (const char *str, double *v) {
   if (str == NULL)
      return false;
   if (!isNumber (str))
      return false;
   char *mutableStr = g_strdup(str);
   g_strdelimit (mutableStr, ",", '.');

   char *endptr;
   *v = strtod (mutableStr, &endptr);
   if (endptr == mutableStr)
      return false;
   g_free(mutableStr);
   
   return true;
}

/*! Check polar and return false and a report if something to say */
static bool polarCheck (PolMat *mat, char *report, size_t maxLen) {
   double maxInRow, maxInCol;
   int cMax, rowMax;
   report [0] ='\0';
   for (int c = 1; c < mat->nCol; c += 1) {  // check values in line 0 progress
      if (mat->t [0][c] < mat->t[0][c-1]) {
         snprintf (report, maxLen, "Report: values in row 0 should progress, col: %d\n", c);
      }
   }
   for (int row = 1; row < mat->nLine; row += 1) {
      if ((mat->t [row][0] < mat->t[row-1] [0])) {
         snprintf (report, maxLen, "Report: values in col 0 should progress, row: %d\n", row);
      }
   }
   for (int row = 1; row < mat->nLine; row += 1) {
      maxInRow = -1.0;
      cMax = -1;
      for (int c = 1; c < mat->nCol; c += 1) { // find maxInRow
         if (mat->t[row][c] > maxInRow) {
            maxInRow = mat->t[row][c];
            cMax = c;
         }
      }
      for (int c = 2; c <= cMax; c += 1) {
         if (mat->t [row][c] < mat->t[row][c-1]) {
            snprintf (report, maxLen, "Report: values in row: %d should progress at col: %d up to maxInRow: %.2lf\n", row, c, maxInRow);
         }
      }
      for (int c = cMax + 1; c < mat->nCol; c += 1) {
         if ((mat->t [row][c] > mat->t[row][c-1])) {
            snprintf (report, maxLen, "Report: values in row: %d should regress at col: %d after maxInRow: %.2lf\n", row, c, maxInRow);
         }
      }
   }

   for (int c = 1; c < mat->nCol; c += 1) {
      maxInCol = -1.0;
      rowMax = -1;
      for (int row = 1; row < mat->nLine; row += 1) { // find maxInRow
         if (mat->t[row][c] > maxInCol) {
            maxInCol = mat->t[row][c];
            rowMax = row;
         }
      }
      for (int row = 2; row <= rowMax; row += 1) {
         if (mat->t [row][c] < mat->t[row-1][c]) {
            snprintf (report, maxLen, "Report: values in col: %d should progress at row: %d up to maxInCol: %.2lf\n", c, row, maxInCol);
         }
      }
      for (int row = rowMax + 1; row < mat->nLine; row += 1) {
         if ((mat->t [row][c] > mat->t[row-1][c])) {
            snprintf (report, maxLen, "Report: values in col: %d should regress at row: %d after maxInCol: %.2lf\n", c, row, maxInCol);
         }
      }
   }
   return (report [0] == '\0'); // True if no error found
}

/*! read polar file and fill poLMat matrix */
bool readPolar (const char *fileName, PolMat *mat, char *errMessage, size_t maxLen) {
   FILE *f = NULL;
   char buffer [MAX_SIZE_LINE];
   char *pLine = &buffer [0];
   char *strToken;
   double v = 0;
   int c;
      
   mat->nLine = 0;
   mat->nCol = 0;
   if ((f = fopen (fileName, "r")) == NULL) {
      snprintf (errMessage, maxLen,  "Error in readPolar: cannot open: %s\n", fileName);
      return false;
   }
   while (fgets (pLine, MAX_SIZE_LINE, f) != NULL) {
      c = 0;
      if (pLine [0] == '#') continue;                       // ignore if comment
      if (strpbrk (pLine, CSV_SEP_POLAR) == NULL) continue; // ignore if no separator
         
      strToken = strtok (pLine, CSV_SEP_POLAR);
      while ((strToken != NULL) && (c < MAX_N_POL_MAT_COLS)) {
         if (strtodNew (strToken, &v)) {
            mat->t [mat->nLine][c] = v;
            c += 1;
         }
         if ((mat->nLine == 0) && (c == 0))
            c += 1;
         strToken = strtok (NULL, CSV_SEP_POLAR);
      }
      if (c <= 2) continue; // line is not validated if not at least 2 columns
      if (c >= MAX_N_POL_MAT_COLS) {
         snprintf (errMessage, maxLen, "Error in readPolar: max number of colums: %d\n", MAX_N_POL_MAT_COLS);
         fclose (f);
         return false;
      }
      mat->nLine+= 1;
      
      if (mat->nLine >= MAX_N_POL_MAT_LINES) {
         snprintf (errMessage, maxLen, "Error in readPolar: max number of line: %d\n", MAX_N_POL_MAT_LINES);
         fclose (f);
         return false;
      }
      if (mat->nLine == 1) mat->nCol = c; // max
   }
   mat -> t [0][0] = -1;
   fclose (f);
   polarCheck (mat, errMessage, maxLen);
   return true;
}

/*! return the max value found in polar */
double maxValInPol (const PolMat *mat) {
   double max = 0.0;
   for (int i = 1; i < mat->nLine; i++)
      for (int j = 1; j < mat->nCol; j++) 
         if (mat->t [i][j] > max) max = mat->t [i][j];
   return max;
}

/*! return VMG: angle and speed at TWSi : pres */
void bestVmg (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed) {
   *vmgSpeed = -1;
   double vmg;
   for (int i = 1; i < mat->nLine; i++) {
      if (mat->t [i][0] > 90) break; // useless over 90°
      vmg = findPolar (mat->t [i][0], tws, *mat) * cos (DEG_TO_RAD * mat->t [i][0]);
      if (vmg > *vmgSpeed) {
         *vmgSpeed = vmg;
         *vmgAngle = mat->t [i][0];
      }
   }
}

/*! return VMG back: angle and speed at TWS : vent arriere */
void bestVmgBack (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed) {
   *vmgSpeed = -1;
   double vmg;
   for (int i = 1; i < mat->nLine; i++) {
      if (mat->t [i][0] < 90) continue; // useless under  90° for Vmg Back
      vmg = fabs (findPolar (mat->t [i][0], tws, *mat) * cos (DEG_TO_RAD * mat->t [i][0]));
      if (vmg > *vmgSpeed) {
         *vmgSpeed = vmg;
         *vmgAngle = mat->t [i][0];
      }
   }
}

/*! write polar information in string */
char *polToStr (const PolMat *mat, char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE] = "";
   str [0] = '\0';
   for (int i = 0; i < mat->nLine; i++) {
      for (int j = 0; j < mat->nCol; j++) {
         snprintf (line, MAX_SIZE_LINE, "%6.2f ", mat->t [i][j]);
         g_strlcat (str, line, maxLen);
      }
      g_strlcat (str, "\n", maxLen);
   }
   snprintf (line, MAX_SIZE_LINE, "Number of rows in polar : %d\n", mat->nCol);
   g_strlcat (str, line, maxLen);
   snprintf (line, MAX_SIZE_LINE, "Number of lines in polar: %d\n", mat->nLine);
   g_strlcat (str, line, maxLen);
   snprintf (line, MAX_SIZE_LINE, "Max                     : %.2lf\n", maxValInPol (mat));
   g_strlcat (str, line, maxLen);
   return str;
}

/*! find name, lat and lon */
bool analyseCoord (const char *strCoord, double *lat, double *lon) {
   char *pt = NULL;
   char *str = g_strdup (strCoord);
   g_strstrip (str);
   if (isNumber (str) && ((pt = strchr (str, ',')) || (pt = strchr (str, '-')))) {
      *lon = getCoord (pt + 1, MIN_LON, MAX_LON);
      *pt = '\0'; // cut str in two parts
      *lat = getCoord (str, MIN_LAT, MAX_LAT);
      g_free (str);
      return true;
   }
   g_free (str);
   return false;
}

/*! fill str with polygon information */
void polygonToStr (char *buffer, size_t maxLen) {
   char strLat[MAX_SIZE_NAME], strLon[MAX_SIZE_NAME];
   buffer [0] = '\0';
   char *line =  g_strdup_printf ( "Number of polygons: %d\n", par.nForbidZone);
   g_strlcat (buffer, line, maxLen);
   g_free (line);

   for (int i = 0; i < par.nForbidZone; i++) {
      char *line = g_strdup_printf("Polygon %2d:\n", i);
      g_strlcat (buffer, line, maxLen);
      g_free (line);
      for (int j = 0; j < forbidZones[i].n; j++) {
         latToStr(forbidZones[i].points[j].lat, par.dispDms, strLat, sizeof(strLat));
         lonToStr(forbidZones[i].points[j].lon, par.dispDms, strLon, sizeof(strLon));
         char *coord = g_strdup_printf("%12s; %12s\n", strLat, strLon);
         g_strlcat (buffer, coord, maxLen);
         g_free(coord);
        }
    }
}

/*! return true if p is in polygon po
  Ray casting algorithm */ 
static bool isInPolygon (double lat, double lon, const MyPolygon *po) {
   int i, j;
   bool inside = false;
   // printf ("isInPolygon lat:%.2lf lon: %.2lf\n", p.lat, p.lon);
   for (i = 0, j = po->n - 1; i < po->n; j = i++) {
      if ((po->points[i].lat > lat) != (po->points[j].lat > lat) &&
         (lon < (po->points[j].lon - po->points[i].lon) * (lat - po->points[i].lat) / (po->points[j].lat - po->points[i].lat) + po->points[i].lon)) {
         inside = !inside;
      }
   }
   return inside;
}

/*! return true if p is in forbid area */ 
static bool isInForbidArea (double lat, double lon) {
   for (int i = 0; i < par.nForbidZone; i++) {
      if (isInPolygon (lat, lon, &forbidZones [i]))
         return true;
   }
   return false;
}

/*! complement according to forbidden areas */
void updateIsSeaWithForbiddenAreas (void) {
   if (tIsSea == NULL) return;
   if (par.nForbidZone <= 0) return;
   for (int i = 0; i < SIZE_T_IS_SEA; i++) {
      double lon = (i % 3601) / 10 - 180.0;
      double lat = 90.0 - (i / (3601 *10));
      if (isInForbidArea (lat, lon))
         tIsSea [i] = 0;
   }
}

/*! read forbid zone */
static void forbidZoneAdd (char *line, int n) {
   char *latToken, *lonToken;
   printf ("Forbid Zone    : %d\n", n);

   Point *temp = (Point *) realloc (forbidZones [n].points, MAX_SIZE_FORBID_ZONE * sizeof(Point));
   if (temp == NULL) {
      fprintf (stderr, "In forbidZoneAdd, Error realloc with n = %d\n", n);
      return;
   }
   forbidZones [n].points = temp;

   if (((latToken = strtok (line, ",")) != NULL) && \
      ((lonToken = strtok (NULL, ";")) != NULL)) { // first point

      forbidZones [n].points[0].lat = getCoord (latToken, MIN_LAT, MAX_LAT);  
      forbidZones [n].points[0].lon = getCoord (lonToken, MIN_LON, MAX_LON);
      forbidZones [n].n = 1;
      while ((lonToken != NULL) && (latToken != NULL) && (forbidZones [n].n < MAX_SIZE_FORBID_ZONE)) {
         if ((latToken = strtok (NULL, ",")) != NULL) {         // lat
            forbidZones [n].points[forbidZones [n].n].lat = getCoord (latToken, MIN_LAT, MAX_LAT);  
            if ((lonToken = strtok (NULL, ";")) != NULL) {      // lon
               double lon = getCoord (lonToken, MIN_LON, MAX_LON);
               forbidZones [n].points[forbidZones [n].n].lon = lon;
               forbidZones [n].n += 1;
            }
         }
      }
   }
}

/*! read parameter file and build par struct */
bool readParam (const char *fileName) {
   FILE *f = NULL;
   char *pt = NULL;
   char str [MAX_SIZE_LINE];
   char buffer [MAX_SIZE_TEXT];
   char *pLine = &buffer [0];
   memset (&par, 0, sizeof (Par));
   par.opt = 1;
   par.tStep = 1.0;
   par.cogStep = 5;
   par.rangeCog = 90;
   par.dayEfficiency = 1;
   par.nightEfficiency = 1;
   par.kFactor = 1;
   par.jFactor = 300;
   par.nSectors = 720;
   par.style = 1;
   par.showColors =2;
   par.dispDms = 2;
   par.windDisp = 1;
   par.xWind = 1.0;
   par.maxWind = 50.0;
   wayPoints.n = 0;
   wayPoints.totOrthoDist = 0.0;
   wayPoints.totLoxoDist = 0.0;
   competitors.n = 0;
   competitors.runIndex = -1;
   
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In readParam, Error Cannot open: %s\n", fileName);
      return false;
   }

   while (fgets (pLine, sizeof (buffer), f) != NULL ) {
      str [0] = '\0';
      while (isspace (*pLine)) pLine++;
      // printf ("%s", pLine);
      if ((!*pLine) || *pLine == '#' || *pLine == '\n') continue;
      if ((pt = strchr (pLine, '#')) != NULL)               // comment elimination
         *pt = '\0';

      if (sscanf (pLine, "DESC:%255[^\n]s", par.description) > 0)
         g_strstrip (par.description);
      else if (sscanf (pLine, "ALLWAYS_SEA:%d", &par.allwaysSea) > 0);
      else if (sscanf (pLine, "WD:%255s", par.workingDir) > 0);  // should be first !!!
      else if (sscanf (pLine, "POI:%255s", str) > 0) 
         buildRootName (str, par.poiFileName, sizeof (par.poiFileName));
      else if (sscanf (pLine, "PORT:%255s", str) > 0)
         buildRootName (str, par.portFileName, sizeof (par.portFileName));
      else if (strstr (pLine, "POR:") != NULL) {
         if (analyseCoord (strchr (pLine, ':') + 1, &par.pOr.lat, &par.pOr.lon)) {
            //par.pOr.lon = lonCanonize (par.pOr.lon);
            par.pOr.id = -1;
            par.pOr.father = -1;
         }
      } 
      else if (strstr (pLine, "PDEST:") != NULL) {
         if (analyseCoord (strchr (pLine, ':') + 1, &par.pDest.lat, &par.pDest.lon)) {
            //par.pDest.lon = lonCanonize (par.pDest.lon);
            par.pDest.id = 0;
            par.pDest.father = 0;
         }
      }
      else if (strstr (pLine, "WP:") != NULL) {
         if (wayPoints.n > MAX_N_WAY_POINT)
            fprintf (stderr, "In readParam, Error: number of wayPoints exceeded; %d\n", MAX_N_WAY_POINT);
         else {
            if (analyseCoord (strchr (pLine, ':') + 1, &wayPoints.t [wayPoints.n].lat, &wayPoints.t [wayPoints.n].lon))
               wayPoints.n += 1;
         }
      }
      else if (strstr (pLine, "COMPETITOR:") != NULL) {
         if (competitors.n > MAX_N_COMPETITORS)
            fprintf (stderr, "In readParam, Error number of competitors exceeded; %d\n", MAX_N_COMPETITORS);
         else {
            if (sscanf (pLine, "COMPETITOR:%d", &competitors.t [competitors.n].colorIndex) > 0) {
               competitors.t [competitors.n].name [0] = '\0';
               if (((pt = strrchr (pLine, ';')) != NULL) && (sscanf (pt + 1, "%64s", competitors.t [competitors.n].name) > 0)) {
                  *pt = '\0'; // cut line
                  g_strstrip (competitors.t [competitors.n].name);
               }
               if (analyseCoord (strchr (pLine, ';') + 1, &competitors.t [competitors.n].lat, &competitors.t [competitors.n].lon)) {
               // printf ("i:%d, lat: %.2lf, lon: %.2lf, %s\n\n", competitors.n, competitors.t [competitors.n].lat, 
               //competitors.t [competitors.n].lon, competitors.t [competitors.n].name);
                  if (competitors.n == 0) {
                     par.pOr.lat = competitors.t [0].lat;
                     par.pOr.lon = competitors.t [0].lon;
                  }
                  competitors.n += 1;
               }
            }
         }
      }
      else if (sscanf (pLine, "POR_NAME:%255s", par.pOrName) > 0);
      else if (sscanf (pLine, "PDEST_NAME:%255s", par.pDestName) > 0);
      else if (sscanf (pLine, "GRIB_RESOLUTION:%lf", &par.gribResolution) > 0);
      else if (sscanf (pLine, "GRIB_TIME_STEP:%d", &par.gribTimeStep) > 0);
      else if (sscanf (pLine, "GRIB_TIME_MAX:%d", &par.gribTimeMax) > 0);
      else if (sscanf (pLine, "TRACE:%255s", str) > 0)
         buildRootName (str, par.traceFileName, sizeof (par.traceFileName));
      else if (sscanf (pLine, "CGRIB:%255s", str) > 0)
         buildRootName (str, par.gribFileName, sizeof (par.gribFileName));
      else if (sscanf (pLine, "CURRENT_GRIB:%255s", str) > 0)
         buildRootName (str, par.currentGribFileName, sizeof (par.currentGribFileName));
      else if (sscanf (pLine, "WAVE_POL:%255s", str) > 0)
         buildRootName (str, par.wavePolFileName, sizeof (par.wavePolFileName));
      else if (sscanf (pLine, "POLAR:%255s", str) > 0)
         buildRootName (str, par.polarFileName, sizeof (par.polarFileName));
      else if (sscanf (pLine, "ISSEA:%255s", str) > 0)
         buildRootName (str, par.isSeaFileName, sizeof (par.isSeaFileName));
      else if (sscanf (pLine, "TIDES:%255s", str) > 0)
         buildRootName (str, par.tidesFileName, sizeof (par.tidesFileName));
      else if (sscanf (pLine, "MID_COUNTRY:%255s", str) > 0)
         buildRootName (str, par.midFileName, sizeof (par.midFileName));
      else if (sscanf (pLine, "CLI_HELP:%255s", str) > 0)
         buildRootName (str, par.cliHelpFileName, sizeof (par.cliHelpFileName));
      else if (sscanf (pLine, "HELP:%255s", par.helpFileName) > 0); // full link required
      else if (sscanf (pLine, "CURL_SYS:%d", &par.curlSys) > 0);
      else if (sscanf (pLine, "PYTHON:%d", &par.python) > 0);
      else if (sscanf (pLine, "SMTP_SCRIPT:%255[^\n]s", par.smtpScript) > 0)
         g_strstrip (par.smtpScript);
      else if (sscanf (pLine, "IMAP_TO_SEEN:%255[^\n]s", par.imapToSeen) > 0)
         g_strstrip (par.imapToSeen);
      else if (sscanf (pLine, "IMAP_SCRIPT:%255[^\n]s", par.imapScript) > 0)
         g_strstrip (par.imapScript);
      else if (sscanf (pLine, "SHP:%255s", str) > 0) {
         buildRootName (str, par.shpFileName [par.nShpFiles], sizeof (par.shpFileName [0]));
         par.nShpFiles += 1;
         if (par.nShpFiles >= MAX_N_SHP_FILES) 
            fprintf (stderr, "In readParam, Error Number max of SHP files reached: %d\n", par.nShpFiles);
      }
      else if (sscanf (pLine, "MOST_RECENT_GRIB:%d", &par.mostRecentGrib) > 0);
      else if (sscanf (pLine, "START_TIME:%lf", &par.startTimeInHours) > 0);
      else if (sscanf (pLine, "T_STEP:%lf", &par.tStep) > 0);
      else if (sscanf (pLine, "RANGE_COG:%d", &par.rangeCog) > 0);
      else if (sscanf (pLine, "COG_STEP:%d", &par.cogStep) > 0);
      else if (sscanf (pLine, "SPECIAL:%d", &par.special) > 0);
      else if (sscanf (pLine, "MOTOR_S:%lf", &par.motorSpeed) > 0);
      else if (sscanf (pLine, "THRESHOLD:%lf", &par.threshold) > 0);
      else if (sscanf (pLine, "DAY_EFFICIENCY:%lf", &par.dayEfficiency) > 0);
      else if (sscanf (pLine, "NIGHT_EFFICIENCY:%lf", &par.nightEfficiency) > 0);
      else if (sscanf (pLine, "X_WIND:%lf", &par.xWind) > 0);
      else if (sscanf (pLine, "MAX_WIND:%lf", &par.maxWind) > 0);
      else if (sscanf (pLine, "CONST_WAVE:%lf", &par.constWave) > 0);
      else if (sscanf (pLine, "CONST_WIND_TWS:%lf", &par.constWindTws) > 0);
      else if (sscanf (pLine, "CONST_WIND_TWD:%lf", &par.constWindTwd) > 0);
      else if (sscanf (pLine, "CONST_CURRENT_S:%lf", &par.constCurrentS) > 0);
      else if (sscanf (pLine, "CONST_CURRENT_D:%lf", &par.constCurrentD) > 0);
      else if (sscanf (pLine, "DUMPI:%255s", str) > 0)
         buildRootName (str, par.dumpIFileName, sizeof (par.dumpIFileName));
      else if (sscanf (pLine, "DUMPR:%255s", str) > 0)
         buildRootName (str, par.dumpRFileName, sizeof (par.dumpRFileName));
      else if (sscanf (pLine, "PAR_INFO:%255s", str) > 0)
         buildRootName (str, par.parInfoFileName, sizeof (par.parInfoFileName));
      else if (sscanf (pLine, "OPT:%d", &par.opt) > 0);
      else if (sscanf (pLine, "J_FACTOR:%d", &par.jFactor) > 0);
      else if (sscanf (pLine, "K_FACTOR:%d", &par.kFactor) > 0);
      else if (sscanf (pLine, "PENALTY0:%d", &par.penalty0) > 0);
      else if (sscanf (pLine, "PENALTY1:%d", &par.penalty1) > 0);
      else if (sscanf (pLine, "N_SECTORS:%d", &par.nSectors) > 0);
      else if (sscanf (pLine, "ISOC_DISP:%d", &par.style) > 0);
      else if (sscanf (pLine, "COLOR_DISP:%d", &par.showColors) > 0);
      else if (sscanf (pLine, "DMS_DISP:%d", &par.dispDms) > 0);
      else if (sscanf (pLine, "WIND_DISP:%d", &par.windDisp) > 0);
      else if (sscanf (pLine, "INFO_DISP:%d", &par.infoDisp) > 0);
      else if (sscanf (pLine, "INDICATOR_DISP:%d", &par.indicatorDisp) > 0);
      else if (sscanf (pLine, "CURRENT_DISP:%d", &par.currentDisp) > 0);
      else if (sscanf (pLine, "WAVE_DISP:%d", &par.waveDisp) > 0);
      else if (sscanf (pLine, "GRID_DISP:%d", &par.gridDisp) > 0);
      else if (sscanf (pLine, "LEVEL_POI_DISP:%d", &par.maxPoiVisible) > 0);
      else if (sscanf (pLine, "SPEED_DISP:%d", &par.speedDisp) > 0);
      else if (sscanf (pLine, "AIS_DISP:%d", &par.aisDisp) > 0);
      else if (sscanf (pLine, "TECHNO_DISP:%d", &par.techno) > 0);
      else if (sscanf (pLine, "SHP_POINTS_DISP:%d", &par.shpPointsDisp) > 0);
      else if (sscanf (pLine, "DESC:%255[^\n]s", par.description) > 0);
      else if (sscanf (pLine, "WEBKIT:%255[^\n]s", par.webkit) > 0)
         g_strstrip (par.webkit);
      else if ((strstr (pLine, "FORBID_ZONE:") != NULL) && (par.nForbidZone < MAX_N_FORBID_ZONE)) {
         pLine = strchr (pLine, ':') + 1;
         g_strstrip (pLine);
         g_strlcpy (par.forbidZone [par.nForbidZone], pLine, MAX_SIZE_LINE);
         forbidZoneAdd (pLine, par.nForbidZone);
         par.nForbidZone += 1;
      }
      else if (sscanf (pLine, "SMTP_SERVER:%255s", par.smtpServer) > 0);
      else if (sscanf (pLine, "SMTP_USER_NAME:%255s", par.smtpUserName) > 0);
      else if (sscanf (pLine, "MAIL_PW:%255s", par.mailPw) > 0);
      else if (sscanf (pLine, "IMAP_SERVER:%255s", par.imapServer) > 0);
      else if (sscanf (pLine, "IMAP_USER_NAME:%255s", par.imapUserName) > 0);
      else if (sscanf (pLine, "IMAP_MAIL_BOX:%255s", par.imapMailBox) > 0);
      else if ((par.nNmea < N_MAX_NMEA_PORTS) && 
              (sscanf (pLine, "NMEA:%255s %d", par.nmea [par.nNmea].portName, &par.nmea [par.nNmea].speed) > 0)) {
         par.nNmea += 1;
      }
      else fprintf (stderr, "In readParam, Error Cannot interpret: %s\n", pLine);
   }
   if (par.mailPw [0] != '\0') {
      par.storeMailPw = true;
   }
   if (competitors.n > 0) {
      par.pOr.lat = competitors.t [0].lat;
      par.pOr.lon = competitors.t [0].lon;
   }
   if (par.constWindTws != 0) initZone (&zone);
   fclose (f);
   return true;
}

/*! write parameter file from fill struct par 
   header or not, password or not */
bool writeParam (const char *fileName, bool header, bool password) {
   char strLat [MAX_SIZE_NAME] = "";
   char strLon [MAX_SIZE_NAME] = "";
   FILE *f = NULL;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "In writeParam, Error Cannot write: %s\n", fileName);
      return false;
   }
   if (header) 
      fprintf (f, "Name             Value\n");
   fprintf (f, "DESC:            %s\n", par.description);
   fprintf (f, "WD:              %s\n", par.workingDir);
   fprintf (f, "ALLWAYS_SEA:     %d\n", par.allwaysSea);
   fprintf (f, "POI:             %s\n", par.poiFileName);
   fprintf (f, "PORT:            %s\n", par.portFileName);
   
   latToStr (par.pOr.lat, par.dispDms, strLat, sizeof (strLat));
   lonToStr (par.pOr.lon, par.dispDms, strLon, sizeof (strLon));
   fprintf (f, "POR:             %.2lf,%.2lf #%s,%s\n", par.pOr.lat, par.pOr.lon, strLat, strLon);

   latToStr (par.pDest.lat, par.dispDms, strLat, sizeof (strLat));
   lonToStr (par.pDest.lon, par.dispDms, strLon, sizeof (strLon));
   fprintf (f, "PDEST:           %.2lf,%.2lf #%s,%s\n", par.pDest.lat, par.pDest.lon, strLat, strLon);

      

   if (par.pOrName [0] != '\0')
      fprintf (f, "POR_NAME:        %s\n", par.pOrName);
   if (par.pDestName [0] != '\0')
      fprintf (f, "PDEST_NAME:        %s\n", par.pDestName);
   for (int i = 0; i < wayPoints.n; i++)
      fprintf (f, "WP:              %.2lf,%.2lf\n", wayPoints.t [i].lat, wayPoints.t [i].lon);

   for (int i = 0; i < competitors.n; i++) { // other competitors
      latToStr (competitors.t [i].lat, par.dispDms, strLat, sizeof (strLat));
      lonToStr (competitors.t [i].lon, par.dispDms, strLon, sizeof (strLon));
      fprintf (f, "COMPETITOR:        %2d; %s,%s; %s\n", competitors.t[i].colorIndex, strLat, strLon, competitors.t[i].name);
   }
   fprintf (f, "TRACE:           %s\n", par.traceFileName);
   fprintf (f, "CGRIB:           %s\n", par.gribFileName);
   if (par.currentGribFileName [0] != '\0')
      fprintf (f, "CURRENT_GRIB:    %s\n", par.currentGribFileName);
   fprintf (f, "MOST_RECENT_GRIB:%d\n", par.mostRecentGrib);
   fprintf (f, "GRIB_RESOLUTION: %.2lf\n", par.gribResolution);
   fprintf (f, "GRIB_TIME_STEP:  %d\n", par.gribTimeStep);
   fprintf (f, "GRIB_TIME_MAX:   %d\n", par.gribTimeMax);
   fprintf (f, "POLAR:           %s\n", par.polarFileName);
   fprintf (f, "WAVE_POL:        %s\n", par.wavePolFileName);
   fprintf (f, "ISSEA:           %s\n", par.isSeaFileName);
   fprintf (f, "MID_COUNTRY:     %s\n", par.midFileName);
   fprintf (f, "TIDES:           %s\n", par.tidesFileName);
   fprintf (f, "HELP:            %s\n", par.helpFileName);
   fprintf (f, "CLI_HELP:        %s\n", par.cliHelpFileName);
   for (int i = 0; i < par.nShpFiles; i++)
      fprintf (f, "SHP:             %s\n", par.shpFileName [i]);

   fprintf (f, "START_TIME:      %.2lf\n", par.startTimeInHours);
   fprintf (f, "T_STEP:          %.2lf\n", par.tStep);
   fprintf (f, "RANGE_COG:       %d\n", par.rangeCog);
   fprintf (f, "COG_STEP:        %d\n", par.cogStep);
   fprintf (f, "SPECIAL:         %d\n", par.special);
   fprintf (f, "PENALTY0:        %d\n", par.penalty0);
   fprintf (f, "PENALTY1:        %d\n", par.penalty1);
   fprintf (f, "MOTOR_S:         %.2lf\n", par.motorSpeed);
   fprintf (f, "THRESHOLD:       %.2lf\n", par.threshold);
   fprintf (f, "DAY_EFFICIENCY:  %.2lf\n", par.dayEfficiency);
   fprintf (f, "NIGHT_EFFICIENCY:%.2lf\n", par.nightEfficiency);
   fprintf (f, "X_WIND:          %.2lf\n", par.xWind);
   fprintf (f, "MAX_WIND:        %.2lf\n", par.maxWind);

   if (par.constWave != 0)
      fprintf (f, "CONST_WAVE:      %.2lf\n", par.constWave);

   if (par.constWindTws != 0) {
      fprintf (f, "CONST_WIND_TWS:  %.2lf\n", par.constWindTws);
      fprintf (f, "CONST_WIND_TWD:  %.2lf\n", par.constWindTwd);
   }
   if (par.constCurrentS != 0) {
      fprintf (f, "CONST_CURRENT_S: %.2lf\n", par.constCurrentS);
      fprintf (f, "CONST_CURRENT_D: %.2lf\n", par.constCurrentD);
   }

   fprintf (f, "DUMPI:           %s\n", par.dumpIFileName);
   fprintf (f, "DUMPR:           %s\n", par.dumpRFileName);
   fprintf (f, "PAR_INFO:        %s\n", par.parInfoFileName);
   fprintf (f, "OPT:             %d\n", par.opt);
   fprintf (f, "ISOC_DISP:       %d\n", par.style);
   fprintf (f, "COLOR_DISP:      %d\n", par.showColors);
   fprintf (f, "DMS_DISP:        %d\n", par.dispDms);
   fprintf (f, "WIND_DISP:       %d\n", par.windDisp);
   fprintf (f, "INFO_DISP:       %d\n", par.infoDisp);
   fprintf (f, "INDICATOR_DISP:  %d\n", par.indicatorDisp);
   fprintf (f, "CURRENT_DISP:    %d\n", par.currentDisp);
   fprintf (f, "WAVE_DISP:       %d\n", par.waveDisp);
   fprintf (f, "GRID_DISP:       %d\n", par.gridDisp);
   fprintf (f, "LEVEL_POI_DISP:  %d\n", par.maxPoiVisible);
   fprintf (f, "SPEED_DISP:      %d\n", par.speedDisp);
   fprintf (f, "AIS_DISP:        %d\n", par.aisDisp);
   fprintf (f, "SHP_POINTS_DISP: %d\n", par.shpPointsDisp);
   fprintf (f, "TECHNO_DISP:     %d\n", par.techno);
   fprintf (f, "J_FACTOR:        %d\n", par.jFactor);
   fprintf (f, "K_FACTOR:        %d\n", par.kFactor);
   fprintf (f, "N_SECTORS:       %d\n", par.nSectors);
   fprintf (f, "PYTHON:          %d\n", par.python);
   fprintf (f, "CURL_SYS:        %d\n", par.curlSys);
   fprintf (f, "SMTP_SCRIPT:     %s\n", par.smtpScript);
   fprintf (f, "IMAP_TO_SEEN:    %s\n", par.imapToSeen);
   fprintf (f, "IMAP_SCRIPT:     %s\n", par.imapScript);
   fprintf (f, "WEBKIT:          %s\n", par.webkit);
   fprintf (f, "SMTP_SERVER:     %s\n", par.smtpServer);
   fprintf (f, "SMTP_USER_NAME:  %s\n", par.smtpUserName);
   fprintf (f, "IMAP_SERVER:     %s\n", par.imapServer);
   fprintf (f, "IMAP_USER_NAME:  %s\n", par.imapUserName);
   fprintf (f, "IMAP_MAIL_BOX:   %s\n", par.imapMailBox);
   for (int i = 0; i < par.nNmea; i++)
      fprintf (f, "NMEA:            %s %d\n", par.nmea [i].portName, par.nmea [i].speed); 

   for (int i = 0; i < par.nForbidZone; i++) {
      fprintf (f, "FORBID_ZONE:     ");
      for (int j = 0; j < forbidZones [i].n; j++) {
         latToStr (forbidZones [i].points [j].lat, par.dispDms, strLat, sizeof (strLat));
         lonToStr (forbidZones [i].points [j].lon, par.dispDms, strLon, sizeof (strLon));
         fprintf (f, "%s,%s; ", strLat, strLon);
      }
      fprintf (f, "\n");
   }
   if (password)
      fprintf (f, "MAIL_PW:         %s\n", par.mailPw);

   fclose (f);
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
   time_t meteoTime = time (NULL) - (3600 * ((typeWeb == NOAA_WIND) ? NOAA_DELAY: ECMWF_DELAY));
   struct tm * timeInfos = gmtime (&meteoTime);  // time x hours ago
   // printf ("Delay: %d hours ago: %s\n", delay [type], asctime (timeInfos));
   int yy = timeInfos->tm_year + 1900;
   int mm = timeInfos->tm_mon + 1;
   int dd = timeInfos->tm_mday;
   int hh;
   char startUrl [MAX_SIZE_LINE];
   const int resolution = (par.gribResolution == 0.25 || typeWeb == ARPEGE_WIND) ? 25 : 50; // 25 mean 0.25 degree, 50 for 0.5 degree

   int rightLonNew = ((leftLon > 0) && (rightLon < 0)) ? rightLon + 360 : rightLon;

   switch (typeWeb) {
   case NOAA_WIND:
      hh = (timeInfos->tm_hour / 6) * 6;    // select 0 or 6 or 12 or 18
      snprintf (startUrl, sizeof (startUrl), "%sfilter_gfs_0p%d.pl?", NOAA_ROOT_GRIB_URL, resolution);

      if (resolution == 25) // resolution = 0.25
         snprintf (fileName, sizeof (fileName), "gfs.t%02dz.pgrb2.0p25.f%03d", hh, step);
      else                  // resolution = 0.25
         snprintf (fileName, sizeof (fileName), "gfs.t%02dz.pgrb2full.0p50.f%03d", hh, step);

      snprintf (url, maxLen, "%sdir=/gfs.%4d%02d%02d/%02d/atmos&file=%s&%s&subregion=&toplat=%d&leftlon=%d&rightlon=%d&bottomlat=%d", 
            startUrl, yy, mm, dd, hh, fileName, NOAA_GENERAL_PARAM_GRIB_URL, topLat, 
            leftLon, rightLonNew, bottomLat);
      break;
   case ECMWF_WIND:
      hh = (timeInfos->tm_hour / 12) * 12;    // select 0 or 12
      // example of directory; https://data.ecmwf.int/forecasts/20240806/00z/ifs/0p25/oper/
      // example of filename : 20240806000000-102h-oper-fc.grib2
      snprintf (url, maxLen, "%s%4d%02d%02d/%02dz/ifs/0p25/oper/%4d%02d%02d%02d0000-%dh-oper-fc.grib2",\
          ECMWF_ROOT_GRIB_URL, yy, mm, dd, hh, yy, mm, dd, hh, step); 
      break;
   case ARPEGE_WIND:
      hh = (timeInfos->tm_hour / 6) * 6;    // select 0 or 6 or 12 or 18
      // "https://object.data.gouv.fr/meteofrance-pnt/pnt/2024-12-24T12:00:00Z/arpege/025/SP1/arpege__025__SP1__000H024H__2024-12-24T12:00:00Z.grib2"
      snprintf (fileName, sizeof (fileName), "arpege__0%d__SP1__%03dH%03dH__%4d-%02d-%02dT%02d:00:00Z.grib2", resolution, step, step2, yy, mm, dd, hh);
      snprintf (url, maxLen, "%s%4d-%02d-%02dT%02d:00:00Z/arpege/0%d/SP1/%s", ARPEGE_ROOT_GRIB_URL, yy, mm, dd, hh, resolution, fileName);
      break;
   default:
      fprintf (stderr, "In buildGribUrl Error typeWeb not found: %d\n", typeWeb);
      return -1;
   }
   return hh;
}

/*! curGet download URL and save the content in outputFile using system command */
bool curlGetSys(const char *url, const char *outputFile, char *errMessage, size_t maxLen) {
   char command [MAX_SIZE_STD];
   if (snprintf(command, sizeof(command), "curl -L --fail -o %s \"%s\" 2>/dev/null", outputFile, url) >= (int)sizeof(command)) {
      snprintf(errMessage, maxLen, "Command too long");
      return false;
   }
   int ret = system (command); // execute the commande
   if (ret == -1) {
      snprintf (errMessage, maxLen, "In curlGetSys: System call failed: %s", strerror(errno));
      return false;
   }

   if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) { // Check the return value of the curl command
      return true;
   } else {
      if (WIFEXITED(ret)) {
         snprintf (errMessage, maxLen, "In curlGetSys: Curl failed with exit code: %d", WEXITSTATUS(ret));
      } else {
         snprintf (errMessage, maxLen, "In curlGetSys: Curl terminated abnormally");
      }
      return false;
   }
   printf ("File downloaded with system curl: %s\n", outputFile);
}

/*! curGet download URL and save the content in outputFile */
bool curlGet (const char *url, const char *outputFile, char *errMessage, size_t maxLen) {
   if (par.curlSys)
      return curlGetSys (url, outputFile, errMessage, maxLen);

   if (url == NULL || outputFile == NULL || errMessage == NULL || maxLen == 0) {
      g_strlcpy (errMessage, "Invalid arguments", maxLen);
      return false;
   }
   long http_code = 0;
   CURL *curl = curl_easy_init();
   if (curl == NULL) {
      return false;
   }
   curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
   curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 2L); // Temps min. en secondes avant un timeout
   curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L); // Par exemple 100 KB

   // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Verbose activated

   FILE *f = fopen(outputFile, "wb");
   if (f == NULL) {
      snprintf(errMessage, maxLen, "Error opening file: %s", outputFile);
      curl_easy_cleanup(curl);
      return false;
   }

   curl_easy_setopt (curl, CURLOPT_URL, url);
   curl_easy_setopt (curl, CURLOPT_WRITEDATA, f); // no explicit WRITEFUNCTION 

   // blocking mode activated
   curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
   curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

   CURLcode res = curl_easy_perform(curl);
   fflush(f); // Assure
   fclose(f);

   if (res != CURLE_OK) {
      snprintf(errMessage, maxLen, "Curl error: %s", curl_easy_strerror(res));
      curl_easy_cleanup(curl);
      return false;
   }

   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
   curl_easy_cleanup (curl);

   if (http_code >= 400) {
      snprintf(errMessage, maxLen, "HTTP error: %ld", http_code);
      return false;
   }
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

/*! check server accessibility */
bool isServerAccessible (const char *url) {
    CURL *curl;
    CURLcode res;

    curl = curl_easy_init ();

    if (curl) {
        curl_easy_setopt (curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        res = curl_easy_perform (curl);
        if (res != CURLE_OK) {
            fprintf (stderr, "In isServerAccessible, Error Curl request: %s\n", curl_easy_strerror(res));
            curl_easy_cleanup (curl);
            return false; // server not accessible request error
        }

        long http_code;
        curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

        // 2xx means server accessible
        if (http_code >= 200 && http_code < 300) {
            curl_easy_cleanup(curl);
            return true;
        } else {
            curl_easy_cleanup(curl);
            return false; 
        }
    } else {
        fprintf (stderr, "In isServerAccessible, Error Init curl.\n");
        return false;
    }
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

/*! true if day light, false if night 
   simplified : day if local theoric time is >= 6 and <= 18 
   t is the time in hours from beginning of grib */
bool isDayLight (double t, double lat, double lon) {
   long date = zone.dataDate [0];
   long time = zone.dataTime [0];
   struct tm tm0 = {0};
   tm0.tm_year = (date / 10000) - 1900;
   tm0.tm_mon = ((date % 10000) / 100) - 1;
   tm0.tm_mday = date % 100;
   tm0.tm_hour = time / 100;
   tm0.tm_min = time % 100;
   tm0.tm_isdst = -1;                              // avoid adjustments with hours summer winter
   lon = lonCanonize (lon);
   tm0.tm_sec += 3600 * (t + lon / 15.0);          // add one hour every 15° + = east, - = west
   mktime (&tm0);                                  // adjust with secondes added
   // printf ("isDayLignt UTC; %s", asctime (&tm0));
   if (lat > 75.0) return  (tm0.tm_mon > 3 && tm0.tm_mon < 9); // specific for high latitides
   if (lat < -75.0) return ! (tm0.tm_mon > 3 && tm0.tm_mon < 9);
   return (tm0.tm_hour >= 6) && (tm0.tm_hour <= 18);
}

/*! reset way route with only pDest */
void initWayPoints (void) {
   wayPoints.n = 0;
   wayPoints.totOrthoDist = 0;
   wayPoints.totLoxoDist = 0;
   wayPoints.t [0].lat = par.pDest.lat;
   wayPoints.t [0].lon = par.pDest.lon;
}

/*! replace gribFileName with most recent file found ing grib ditrectory */
void initWithMostRecentGrib (void) {
   char fileName [MAX_SIZE_FILE_NAME];
   par.gribFileName [0] = '\0';
   if ((strlen (par.workingDir) + strlen ("grib/"))  < sizeof (par.gribFileName)) { 
      g_strlcat (par.gribFileName, par.workingDir, sizeof (par.gribFileName));
      g_strlcat (par.gribFileName, "grib/", sizeof (par.gribFileName));
      mostRecentFile (par.gribFileName, ".gr", fileName, sizeof (fileName)); // most recent grib file found
      if ((strlen (par.gribFileName) + strlen (fileName))  < sizeof (par.gribFileName))
         g_strlcat (par.gribFileName, fileName, sizeof (par.gribFileName));
   }
}

/*! build object and body of grib mail */
bool buildGribMail (int type, double lat1, double lon1, double lat2, double lon2, char *object,  char *body, size_t maxLen) {
   int i;
   char temp [MAX_SIZE_LINE];
   
   lat1 = floor (lat1);
   lat2 = ceil (lat2);
   lon1 = floor (lonCanonize (lon1));
   lon2 = ceil (lonCanonize (lon2));
   
   if ((lon1 > 0) && (lon2 < 0)) { // zone crossing antimeridien
      printf ("In buildGribMail: Tentative to cross antemeridian !\n");
      // lon2 = 179.0;
   }
   if (setlocale (LC_ALL, "C") == NULL) {                // very important for printf decimal numbers
      fprintf (stderr, "In buildGribMail, Error: setlocale failed");
      return false;
   }
   
   if (type != MAILASAIL) {
      snprintf (body, maxLen, "send %s:%d%c,%d%c,%d%c,%d%c|%.2lf,%.2lf|0,%d,..%d|%s",\
         mailServiceTab [type].service,\
         (int) fabs (round(lat1)), (lat1 > 0) ? 'N':'S', (int) fabs (round(lat2)), (lat2 > 0) ? 'N':'S',\
		   (int) fabs (round(lon1)), (lon1 > 0) ? 'E':'W', (int) fabs (round(lon2)), (lon2 > 0) ? 'E':'W',\
		   par.gribResolution, par.gribResolution, par.gribTimeStep, par.gribTimeMax,\
         mailServiceTab [type].suffix);
      snprintf (object, maxLen, "grib");
   }
   else { //MAILASAIL:
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

