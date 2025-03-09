/*! compilation: gcc -c rutil.c `pkg-config --cflags glib-2.0` */
#define _POSIX_C_SOURCE 200809L // to avoid warning with -std=c11 when using popen function (Posix)
#define MIN_NAME_LENGTH 3       // for poi. Minimul length to select pomei
#define MAX_N_SHIP_TYPE 2       // for Virtual Regatta Stamina calculation

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
struct tm;

/*! see aisgps file */
MyGpsData my_gps_data; 

/* For virtual regatta Stamina calculation */
struct {
   char name [MAX_SIZE_NAME];
   double cShip;  
   double tMin [3];
   double tMax [3];
} shipParam [MAX_N_SHIP_TYPE] = {
   {"Imoca", 1.2, {300, 300, 420}, {660, 660, 600}}, 
   {"Normal", 1.0, {300, 300, 336}, {660, 660, 480}}
};

/*! forbid ones is a set of polygons */
MyPolygon forbidZones [MAX_N_FORBID_ZONE];

/*! dictionnary of meteo services */
const struct MeteoElmt meteoTab [N_METEO_ADMIN] = {{7, "Weather service US"}, {78, "DWD Germany"}, {85, "Meteo France"}, {98,"ECMWF European"}};

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

/*! sail attributes */
const char *sailName [MAX_N_SAIL] = {"NA", "C0", "HG", "Jib", "LG", "LJ", "Spi", "SS"}; // for sail polars
const char *colorStr [MAX_N_SAIL] = {"black", "green", "purple", "gray", "blue", "yellow", "orange", "red"};

/*! list of wayPoint */
WayPointList wayPoints;

/*! list of competitors */
CompetitorsList competitors;

/*! polar matrix description */
PolMat polMat;

/*! polar matrix for sails */
PolMat sailPolMat;


/*! polar matrix for waves */
PolMat wavePolMat;

/*! parameter */
Par par;

/*! table describing if sea or earth */
char *tIsSea = NULL; 

/*! poi sata strutures */ 
Poi tPoi [MAX_N_POI];
int nPoi = 0;

/*! geographic zone covered by grib file */
Zone zone;                          // wind
Zone currentZone;                   // current

FlowP *tGribData [2] = {NULL, NULL};  // wind, current

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

/*! return the name of the sail */
char *fSailName (int val, char *str, size_t maxLen) {
   if (val >= 0 && val < MAX_N_SAIL)
      g_strlcpy (str, sailName [val], maxLen);
   else
      g_strlcpy (str, "--", maxLen);
   return str;
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

/*! replace former suffix (after last dot) by suffix 
   example : "pol/bibi.toto.csv" with suffix "sailpol" will give: "pol/bibi.toto.sailpol" */
char *newFileNameSuffix (const char *fileName, const char *suffix, char *newFileName, size_t maxLen) {
   const char *lastDot = strrchr(fileName, '.'); // find last point position
   const char *baseName = fileName;
   size_t baseLen;

   if (lastDot != NULL) // // length of base
      baseLen = lastDot - fileName;
   else
      baseLen = strlen (fileName);

   // check resulting lenght is in maxLen
   if (baseLen + strlen (suffix) + 1 >= maxLen) { // for the  '.'
      return NULL; // not enough space
   }
   g_strlcpy (newFileName, baseName, baseLen + 1);
   strcat (newFileName, ".");
   strcat (newFileName, suffix);

   return newFileName;
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

/*! select most recent file in "directory" that contains "pattern" in name 
  return true if found with name of selected file */
bool mostRecentFile (const char *directory, const char *pattern, char *name, size_t maxLen) {
   DIR *dir = opendir (directory);
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
            snprintf (name, maxLen, "%s%s", directory, entry->d_name);
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

/*! replace $ by sequence */
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

/*! Build root name if not already a root name.
 * Combines the working directory with the file name if it is not absolute.
 * Stores the result in `rootName` and ensures it does not exceed `maxLen`.
 *
 * @param fileName  The name of the file (absolute or relative).
 * @param rootName  The buffer to store the resulting root name.
 * @param maxLen    The maximum length of the buffer `rootName`.
 * @return          A pointer to `rootName`, or NULL on error.
 */
char *buildRootName (const char *fileName, char *rootName, size_t maxLen) {
   const char *workingDir = par.workingDir[0] != '\0' ? par.workingDir : WORKING_DIR;
   char *fullPath = (g_path_is_absolute (fileName)) ? g_strdup (fileName) : g_build_filename (workingDir, fileName, NULL);
   if (!fullPath) return NULL;
   g_strlcpy (rootName, fullPath, maxLen);
   g_free (fullPath);
   return rootName;
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
struct tm gribDateToTm (long intDate, double nHours) {
   struct tm tm0 = {0};
   tm0.tm_year = (intDate / 10000) - 1900;
   tm0.tm_mon = ((intDate % 10000) / 100) - 1;
   tm0.tm_mday = intDate % 100;
   tm0.tm_isdst = -1;                     // avoid adjustments with hours summer winter
   int totalMinutes = (int)(nHours * 60);
   tm0.tm_min += totalMinutes;            // adjust tm0 struct (manage day, mon year overflow)
   mktime (&tm0);  
   return tm0;
}

/*! return date and time using ISO notation after adding myTime (hours) to the Date */
char *newDate (long intDate, double nHours, char *res, size_t maxLen) {
   struct tm tm0 = gribDateToTm (intDate, nHours);
   snprintf (res, maxLen, "%4d-%02d-%02d %02d:%02d", tm0.tm_year + 1900, tm0.tm_mon + 1, tm0.tm_mday,\
      tm0.tm_hour, tm0.tm_min);
   return res;
}

/*! return date and time using week day after adding myTime (hours) to the Date */
char *newDateWeekDay (long intDate, double nHours, char *res, size_t maxLen) {
   struct tm tm0 = gribDateToTm (intDate, nHours);
   strftime (res, maxLen, "%a %H:%M", &tm0);
   return res;
}

/*! return date and time using week day after adding myTime (hours) to the Date */
char *newDateWeekDayVerbose (long intDate, double nHours, char *res, size_t maxLen) {
   struct tm tm0 = gribDateToTm (intDate, nHours);
   strftime (res, maxLen, "%A, %b %d at %H:%M UTC", &tm0);
   return res;
}

/*! convert lat to str according to type */
char *latToStr (double lat, int type, char* str, size_t maxLen) {
   double mn = 60 * lat - 60 * (int) lat;
   double sec = 3600 * lat - (3600 * (int) lat) - (60 * (int) mn);
   char c = (lat > 0) ? 'N' : 'S';

   if (lat > 90.0 || lat < -90.0) {
      g_strlcpy (str, "Lat Errror", maxLen);
      return str;
   }

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

   if (lon > 180.0 || lon < -180.0) {
      g_strlcpy (str, "Lon Errror", maxLen);
      return str;
   }

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

/*! return offset Local UTC in seconds */
double offsetLocalUTC (void) {
   time_t now;
   time (&now);                           
   struct tm localTm = *localtime (&now);
   struct tm utcTm = *gmtime (&now);
   time_t localTime = mktime (&localTm);
   time_t utcTime = mktime (&utcTm);
   double offsetSeconds = difftime (localTime, utcTime);
   if (localTm.tm_isdst > 0)
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
      snprintf (str, maxLen, "%d-%02d-%02d %02d:%02d:%02d", 
         utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
         utc->tm_hour, utc->tm_min, utc->tm_sec);
   else
      snprintf (str, maxLen, "%d-%02d-%02d %02d:%02d", 
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

/*! calculate difference in hours between departure time and time 0 */
double getDepartureTimeInHour (struct tm *start) {
   time_t theTime0 = gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]);
   start->tm_isdst = -1;                                       // avoid adjustments with hours summer winter
   time_t startTime = mktime (start);                          
   return (startTime - theTime0)/3600.0;                       // calculated in hours
} 

/*! find name, lat and lon */
bool analyseCoord (const char *strCoord, double *lat, double *lon) {
   char *pt = NULL;
   char *str = g_strdup (strCoord);
   g_strstrip (str);
   // be careful with '-' separator: ambiguity if -45-60
   if (isNumber (str) && ((pt = strchr (str, ',')) || (pt = strchr (str, '-'))) && isNumber (pt + 1)) {
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
   par.nSectors = MAX_N_SECTORS;
   par.style = 1;
   par.showColors =2;
   par.dispDms = 2;
   par.windDisp = 1;
   par.xWind = 1.0;
   par.maxWind = 50.0;
   par.stepIsocDisp = 1;
   par.staminaVR = 100.0;
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

      if (sscanf (pLine, "DESC:%255[^\n]", par.description) > 0)
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
            if (analyseCoord (strchr (pLine, ':') + 1, &wayPoints.t [wayPoints.n].lat, &wayPoints.t [wayPoints.n].lon)) {
               wayPoints.n += 1;
            }
         }
      }
      else if  (strstr (pLine, "COMPETITOR:") != NULL) {
         if (competitors.n > MAX_N_COMPETITORS)
            fprintf (stderr, "In readParam, Error number of competitors exceeded; %d\n", MAX_N_COMPETITORS);
         else {
            if (sscanf (pLine, "COMPETITOR:%d;%255[^;];%255s", &competitors.t [competitors.n].colorIndex, str,
                        competitors.t [competitors.n].name) > 2) {
               g_strstrip (competitors.t [competitors.n].name);
               if (analyseCoord (str, &competitors.t [competitors.n].lat, &competitors.t [competitors.n].lon)) {
                  if (competitors.n == 0) {
                     par.pOr.lat = competitors.t [0].lat;
                     par.pOr.lon = competitors.t [0].lon;
                  }
                  competitors.n += 1;
               }
               else fprintf (stderr, "In readParam, COMPETITOR: Coordinates Error: %s\n", str);
            }
            else fprintf (stderr, "In readParam, COMPETITOR: Syntax Error: %s\n", pLine);
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
      else if (strstr (pLine, "VR_DASHBOARD:") != NULL)  {
         pLine = strchr (pLine, ':') + 1;
         g_strstrip (pLine);
         buildRootName (pLine, par.dashboardVR, sizeof (par.dashboardVR));
      }
      else if (sscanf (pLine, "VR_STAMINA:%lf", &par.staminaVR) > 0);
      else if (sscanf (pLine, "VR_DASHB_UTC:%d", &par.dashboardUTC) > 0);
      else if (sscanf (pLine, "HELP:%255s", par.helpFileName) > 0); // full link required
      else if (sscanf (pLine, "CURL_SYS:%d", &par.curlSys) > 0);
      else if (sscanf (pLine, "PYTHON:%d", &par.python) > 0);
      else if (sscanf (pLine, "SMTP_SCRIPT:%255[^\n]", par.smtpScript) > 0)
         g_strstrip (par.smtpScript);
      else if (sscanf (pLine, "IMAP_TO_SEEN:%255[^\n]", par.imapToSeen) > 0)
         g_strstrip (par.imapToSeen);
      else if (sscanf (pLine, "IMAP_SCRIPT:%255[^\n]", par.imapScript) > 0)
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
      else if (sscanf (pLine, "WP_GPX_FILE:%255s", str) > 0)
         buildRootName (str, par.wpGpxFileName, sizeof (par.wpGpxFileName));
      else if (sscanf (pLine, "DUMPI:%255s", str) > 0)
         buildRootName (str, par.dumpIFileName, sizeof (par.dumpIFileName));
      else if (sscanf (pLine, "DUMPR:%255s", str) > 0)
         buildRootName (str, par.dumpRFileName, sizeof (par.dumpRFileName));
      else if (sscanf (pLine, "PAR_INFO:%254s", str) > 0)
         buildRootName (str, par.parInfoFileName, sizeof (par.parInfoFileName));
      else if (sscanf (pLine, "LOG:%254s", str) > 0)
         buildRootName (str, par.logFileName, sizeof (par.logFileName));
      else if ((sscanf (pLine, "WEB:%254s", str) > 0) || (strstr (pLine, "WEB:") != NULL)) {
         buildRootName (str, par.web, sizeof (par.web));
      }
      else if (sscanf (pLine, "OPT:%d", &par.opt) > 0);
      else if (sscanf (pLine, "J_FACTOR:%d", &par.jFactor) > 0);
      else if (sscanf (pLine, "K_FACTOR:%d", &par.kFactor) > 0);
      else if (sscanf (pLine, "PENALTY0:%d", &par.penalty0) > 0);
      else if (sscanf (pLine, "PENALTY1:%d", &par.penalty1) > 0);
      else if (sscanf (pLine, "PENALTY2:%d", &par.penalty2) > 0);
      else if (sscanf (pLine, "N_SECTORS:%d", &par.nSectors) > 0);
      else if (sscanf (pLine, "ISOC_DISP:%d", &par.style) > 0);
      else if (sscanf (pLine, "STEP_ISOC_DISP:%d", &par.stepIsocDisp) > 0);
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
      else if (sscanf (pLine, "CLOSEST_DISP:%d", &par.closestDisp) > 0);
      else if (sscanf (pLine, "FOCAL_DISP:%d", &par.focalDisp) > 0);
      else if (sscanf (pLine, "SHP_POINTS_DISP:%d", &par.shpPointsDisp) > 0);
      else if (sscanf (pLine, "GOOGLE_API_KEY:%1024s", par.googleApiKey) > 0);
      else if (sscanf (pLine, "WINDY_API_KEY:%1024s", par.windyApiKey) > 0);
      else if (sscanf (pLine, "WEBKIT:%255[^\n]", par.webkit) > 0)
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
   par.staminaVR = CLAMP (par.staminaVR, 0.0, 100.0);
   fclose (f);
   par.nSectors = MIN (par.nSectors, MAX_N_SECTORS);
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
   fprintf (f, "VR_DASHBOARD:    %s\n", par.dashboardVR);
   fprintf (f, "VR_STAMINA:      %.2lf\n", par.staminaVR);
   fprintf (f, "VR_DASHB_UTC:    %d\n", par.dashboardUTC);

   for (int i = 0; i < par.nShpFiles; i++)
      fprintf (f, "SHP:             %s\n", par.shpFileName [i]);

   fprintf (f, "START_TIME:      %.2lf\n", par.startTimeInHours);
   fprintf (f, "T_STEP:          %.2lf\n", par.tStep);
   fprintf (f, "RANGE_COG:       %d\n", par.rangeCog);
   fprintf (f, "COG_STEP:        %d\n", par.cogStep);
   fprintf (f, "SPECIAL:         %d\n", par.special);
   fprintf (f, "PENALTY0:        %d\n", par.penalty0);
   fprintf (f, "PENALTY1:        %d\n", par.penalty1);
   fprintf (f, "PENALTY2:        %d\n", par.penalty2);
   fprintf (f, "MOTOR_S:         %.2lf\n", par.motorSpeed);
   fprintf (f, "THRESHOLD:       %.2lf\n", par.threshold);
   fprintf (f, "DAY_EFFICIENCY:  %.2lf\n", par.dayEfficiency);
   fprintf (f, "NIGHT_EFFICIENCY:%.2lf\n", par.nightEfficiency);
   fprintf (f, "X_WIND:          %.2lf\n", par.xWind);
   fprintf (f, "MAX_WIND:        %.2lf\n", par.maxWind);

   if (par.constWave != 0)
      fprintf (f, "CONST_WAVE:      %.6lf\n", par.constWave);

   if (par.constWindTws != 0) {
      fprintf (f, "CONST_WIND_TWS:  %.6lf\n", par.constWindTws);
      fprintf (f, "CONST_WIND_TWD:  %.2lf\n", par.constWindTwd);
   }
   if (par.constCurrentS != 0) {
      fprintf (f, "CONST_CURRENT_S: %.6f\n", par.constCurrentS);
      fprintf (f, "CONST_CURRENT_D: %.2lf\n", par.constCurrentD);
   }

   fprintf (f, "WP_GPX_FILE:     %s\n", par.wpGpxFileName);
   fprintf (f, "DUMPI:           %s\n", par.dumpIFileName);
   fprintf (f, "DUMPR:           %s\n", par.dumpRFileName);
   fprintf (f, "PAR_INFO:        %s\n", par.parInfoFileName);
   fprintf (f, "LOG:             %s\n", par.logFileName);
   fprintf (f, "OPT:             %d\n", par.opt);
   fprintf (f, "ISOC_DISP:       %d\n", par.style);
   fprintf (f, "STEP_ISOC_DISP:  %d\n", par.stepIsocDisp);
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
   fprintf (f, "CLOSEST_DISP:    %d\n", par.closestDisp);
   fprintf (f, "FOCAL_DISP:      %d\n", par.focalDisp);
   fprintf (f, "J_FACTOR:        %d\n", par.jFactor);
   fprintf (f, "K_FACTOR:        %d\n", par.kFactor);
   fprintf (f, "N_SECTORS:       %d\n", par.nSectors);
   fprintf (f, "PYTHON:          %d\n", par.python);
   fprintf (f, "CURL_SYS:        %d\n", par.curlSys);
   fprintf (f, "SMTP_SCRIPT:     %s\n", par.smtpScript);
   fprintf (f, "IMAP_TO_SEEN:    %s\n", par.imapToSeen);
   fprintf (f, "IMAP_SCRIPT:     %s\n", par.imapScript);
   fprintf (f, "WEB:             %s\n", par.web);
   fprintf (f, "WINDY_API_KEY:   %s\n", par.windyApiKey);
   fprintf (f, "GOOGLE_API_KEY:  %s\n", par.googleApiKey);
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

/*! for virtual Regatta. return penalty in seconds for manoeuvre type. Depend on tws and energy. Give also sTamina coefficient */
double fPenalty (int shipIndex, int type, double tws, double energy, double *cStamina) {
   if (type < 0 || type > 2) {
      fprintf (stderr, "In fPenalty, type unknown: %d\n", type);
      return -1;
   };
   const double kPenalty = 0.015;
   double cShip = shipParam [shipIndex].cShip;
   *cStamina = 2 - fmin (energy, 100.0) * kPenalty;
   double tMin = shipParam [shipIndex].tMin [type];
   double tMax = shipParam [shipIndex].tMax [type];
   double fTws = 50.0 - 50.0 * cos (G_PI * ((fmax (10.0, fmin (tws, 30.0))-10.0)/(30.0 - 10.0)));
   //printf ("cShip: %.2lf, cStamina: %.2lf, tMin: %.2lf, tMax: %.2lf, fTws: %.2lf\n", cShip, cStamina, tMin, tMax, fTws);
   return  cShip * (*cStamina) * (tMin + fTws * (tMax - tMin) / 100.0);
}

/*! for virtual Regatta. return point loss with manoeuvre types. Depends on tws and fullPack */
double fPointLoss (int shipIndex, int type, double tws, bool fullPack) {
   const double fPCoeff = (type == 2 && fullPack) ? 0.8 : 1;
   double loss = (type == 2) ? 0.2 : 0.1;
   double cShip = shipParam [shipIndex].cShip;
   double fTws = (tws <= 10.0) ? 0.02 * tws + 1 : 
                 (tws <= 20.0) ? 0.03 * tws + 0.9 : 
                 (tws <= 30) ? 0.05 * tws + 0.5 :  
                 2.0;
   return fPCoeff * loss * cShip * fTws;
}

/*! for virtual Regatta. return type in second to get back one energy point */
double fTimeToRecupOnePoint (double tws) {
   const double timeToRecupLow = 5;   // minutes
   const double timeToRecupHigh = 15; // minutes
   double fTws = 1.0 - cos (G_PI * (fmin (tws, 30.0)/30.0));
   return 60 * (timeToRecupLow + fTws * (timeToRecupHigh - timeToRecupLow) / 2.0);
}


