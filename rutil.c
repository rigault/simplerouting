/*! compilation gcc -Wall -c rutil.c */
#include <glib.h>
#include <curl/curl.h>
#include <pthread.h>
#include <gps.h>
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
#include "eccodes.h"
#include "rtypes.h"
#include "inline.h"
#include "shapefil.h"
#include <locale.h>

static const char *CSV_SEP = ";,\t";

/*! store sail route calculated in engine.c by routing */  
SailRoute route;

/*! forbid zones is a set of polygons */
Polygon forbidZones [MAX_N_FORBID_ZONE];

/*! dictionnary of meteo services */
struct DictElmt dicTab [] = {{7, "Weather service US"}, {78, "DWD Germany"}, {85, "Meteo France"}, {98,"ECMWF European"}};

struct DictProvider providerTab [N_PROVIDERS] = {
   {"query@saildocs.com",          "Saildocs Wind GFS",      "gfs"},
   {"query@saildocs.com",          "Saildocs Wind ECWMF",    "ECMWF"},
   {"query@saildocs.com",          "Saildocs Wind ICON",     "ICON"},
   {"query@saildocs.com",          "Saildocs Wind ARPEGE",   "ARPEGE"},
   {"query@saildocs.com",          "Saildocs Wind Arome",    "AROME"},
   {"query@saildocs.com",          "Saildocs Current RTOFS", "RTOFS"},
   {"weather@mailasail.com",       "MailaSail Wind GFS",     ""},
   {"gmngrib@globalmarinenet.net", "GlobalMarinet GFS",      ""}
};

/*! list of wayPoint */
WayPointList wayPoints;

/*! polar matrix description */
PolMat polMat;

/*! polar matrix for waves */
PolMat wavePolMat;

/*! parameter */
Par par;

/*! geographic zone covered by grib file */
Zone zone;                       // wind
Zone currentZone;                // current

/*! table describing if sea or earth */
char *tIsSea = NULL; 

/*! grib data description */
FlowP *tGribData []= {NULL, NULL};  //wind, current

/*! poi sata strutures */ 
Poi tPoi [MAX_N_POI];
int nPoi = 0;

int readGribRet = -1;             // to check if readGrib is terminated
int readCurrentGribRet = -1;      // to check if readCurrentGrib is terminated

/*! for shp file */
int  nTotEntities = 0;            // cumulated number of entities
Entity *entities = NULL;

/*! Thread for GPS management */
struct ThreadData {
   struct gps_data_t gps_data;
   MyGpsData my_gps_data;  
};

struct ThreadData thread_data;
pthread_t gps_thread;
MyGpsData my_gps_data;
pthread_mutex_t gps_data_mutex = PTHREAD_MUTEX_INITIALIZER;

const int delay [] = {6, 12};    // 6 hours before now for wind, 12 hours for current
const char *WIND_URL [N_WIND_URL * 2] = {
   "Atlantic North",   "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Nord_Atlantique.grb",
   "Atlantic Center",  "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Centre_Atlantique.grb",
   "Antilles",         "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Antilles.grb",
   "Europe",           "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Europe.grb",
   "Manche",           "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Manche.grb",
   "Gascogne",         "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Gascogne.grb"
};
const char *CURRENT_URL [N_CURRENT_URL * 2] = {
   "Atlantic North",    "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Nord_Atlantique.grb",
   "Atlantic Center",   "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Centre_Atlantique.grb",
   "Antilles",          "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Antilles.grb",
   "Europe",            "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Europe.grb",
   "Manche",            "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Manche.grb",
   "Gascogne",          "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Gascogne.grb"
};

/*! say if point is in sea */
bool isSea (double lat, double lon) {
   if (tIsSea == NULL) return true;
   int iLon = round (lon * 10  + 1800);
   int iLat = round (-lat * 10  + 900);
   return tIsSea [(iLat * 3601) + iLon];
}

/*! build entities based on .shp file */
bool initSHP (char* nameFile) {
   int nEntities = 0;         // number of entities in current shp file
   int nShapeType;
   int k = 0;
   int nMaxPart = 0;
   double minBound[4], maxBound[4];
   SHPHandle hSHP = SHPOpen (nameFile, "rb");
   if (hSHP == NULL) {
      fprintf (stderr, "Error in InitSHP: Cannot open Shapefile: %s\n", nameFile);
      return false;
   }

   SHPGetInfo(hSHP, &nEntities, &nShapeType, minBound, maxBound);
   printf ("Geo nEntities  : %d, nShapeType: %d\n", nEntities, nShapeType);
   printf ("Geo limits     : %.2lf, %.2lf, %.2lf, %.2lf\n", minBound[0], minBound[1], maxBound[0], maxBound[1]);

   if (nTotEntities == 0) {
      if ((entities = (Entity *) malloc (sizeof(Entity) * nEntities)) == NULL) {
         fprintf (stderr, "Error in initSHP: malloc failed\n");
         return false;
      }
   }
   else {
      Entity *temp = (Entity *)realloc(entities, sizeof(Entity) * (nEntities + nTotEntities));
      if (temp == NULL) {
         free (entities);
         fprintf (stderr, "Error in initSHP: realloc failed\n");
         return false;
      } else {
         entities = temp;
      }
   }
 
   for (int i = 0; i < nEntities; i++) {
      SHPObject *psShape = SHPReadObject(hSHP, i);
      if (psShape == NULL) {
         fprintf (stderr, "Error in initShp: Reading entity: %d\n", i);
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
         fprintf (stderr, "Error in initSHP: malloc entity [k].numPoints failed with k = %d\n", k);
         free (entities);
         return false;
      }
      for (int j = 0; (j < psShape->nParts) && (j < MAX_INDEX_ENTITY); j++) {
         if (j < MAX_INDEX_ENTITY)
            entities[k].index [j] = psShape->panPartStart[j];
         else {
            fprintf (stderr, "Error in InitSHP: MAX_INDEX_ENTITY reached: %d\n", j);
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
void freeSHP () {
   for (int i = 0; i < nTotEntities; i++) {
      free (entities[i].points);
   }
   free(entities);
}

/*! select most recent file in "directory" that contains "pattern" in name 
  return true if found with name of selected file */
bool mostRecentFile (const char *directory, const char *pattern, char *name) {
   DIR *dir = opendir(directory);
   if (dir == NULL) {
      fprintf (stderr, "Error in mostRecentFile: opening: %s\n", directory);
      return false;
   }

   struct dirent *entry;
   struct stat statbuf;
   time_t latestTime = 0;
   char filepath[PATH_MAX];

   while ((entry = readdir(dir)) != NULL) {
      snprintf (filepath, PATH_MAX, "%s/%s", directory, entry->d_name);
      if ((strstr (entry->d_name, pattern) != NULL)
         && (stat(filepath, &statbuf) == 0) 
         && (S_ISREG(statbuf.st_mode) && statbuf.st_mtime > latestTime)) {
         latestTime = statbuf.st_mtime;
         if (strlen (entry->d_name) < MAX_SIZE_FILE_NAME)
            strncpy (name, entry->d_name, MAX_SIZE_FILE_NAME);
         else {
            fprintf (stderr, "Error in mostRecentFile: File name size is: %zu and exceed MAX_SIZE_FILE_NAME: %d\n", strlen (entry->d_name), MAX_SIZE_FILE_NAME);
            break;
         }
      }
   }
   closedir(dir);
   return (latestTime > 0);
}

/*! format big number with thousand sep. Example 1000000 formated as 1 000 000 */
char *formatThousandSep (char *buffer, long value) {
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
   }
   buffer [j] = '\0';
   return buffer;
}

/*! true if name contains a number */
bool isNumber (const char *name) {
   while (*name) {
      if (isdigit(*name))
      return true;
      name++;
   }
   return false; 
}

/*! replace $ by sequence */
char *dollarSubstitute (const char* str, char *res, size_t maxSize) {
   res [0] = '\0';
   if (str == NULL) NULL;
   int len = 0;
   for (size_t i = 0; (i < maxSize) && (str[i] != '\0'); ++i) {
      if (str[i] == '$') {
         res[len++] = '\\';
         res[len++] = '$';
      } else {
         res[len++] = str[i];
      }
   }
   res[len] = '\0';
   return res;
}

/*! translate str in double for latitude longitudes */
double getCoord (const char *str) {
   double deg = 0.0, min = 0.0, sec = 0.0;
   int sign = 1;
   char *pt = NULL;
   const char *neg = "SsWwOo"; // value returned is negative if south or West
   // find sign
   while (*neg) {
      if (strchr (str, *neg) != NULL) { // south or West
         sign = -1;
         break;
      }
      neg += 1;
   }
   // fin degrees
   while (*str && (! (isdigit (*str) || (*str == '-') || (*str == '+')))) 
      str++;
   deg  = strtod (str, NULL);
   if (deg < 0) sign = -1; // force sign if negative value found 
   deg = fabs (deg);
   // find minutes
   if ((strchr (str, '\'') != NULL)) {
      if ((pt = strstr (str, "°")) != NULL) // degree ° is UTF8 coded with 2 bytes
         pt += 2;
      else 
         if (((pt = strchr (str, 'N')) != NULL) || ((pt = strchr (str, 'S')) != NULL) || 
             ((pt = strchr (str, 'E')) != NULL) || ((pt = strchr (str, 'W')) != NULL))
         pt += 1;
            
      if (pt != NULL)
         min = strtod (pt, NULL);
   }
   // find seconds
   if ((strchr (str, '"') != NULL)) {
      sec = strtod (strchr (str, '\'') + 1, NULL);
   }
   return sign * (deg + min/60.0 + sec/3600.0);
}

/*! build root name if not already a root name */
char *buildRootName (const char *fileName, char *rootName) {
   if (fileName [0] == '/') 
      strncpy (rootName, fileName, MAX_SIZE_FILE_NAME);
   else if (par.workingDir [0] != '\0')
      snprintf (rootName, MAX_SIZE_FILE_NAME, "%s%s", par.workingDir, fileName);
   else snprintf (rootName, MAX_SIZE_FILE_NAME, "%s%s", WORKING_DIR, fileName);
   return (rootName);
}

/*! get file size */
long getFileSize (const char *fileName) {
    struct stat st;
    if (stat(fileName, &st) == 0) {
        return st.st_size;
    } else {
        fprintf (stderr, "Error in getFileSize: %s\n", fileName);
        return -1;
    }
}

/*! return date and time using ISO notation after adding myTime (hours) to the Date */
char *newDate (long intDate, double nHours, char *res) {
   struct tm tm0 = {0};
   tm0.tm_year = (intDate / 10000) - 1900;
   tm0.tm_mon = ((intDate % 10000) / 100) - 1;
   tm0.tm_mday = intDate % 100;
   tm0.tm_isdst = -1;                     // avoid adjustments with hours summer winter
   int totalMinutes = (int)(nHours * 60);
   tm0.tm_min += totalMinutes;
   mktime (&tm0);                         // adjust tm0 struct (manage day, mon year overflow)
   snprintf (res, MAX_SIZE_LINE, "%4d/%02d/%02d %02d:%02d", tm0.tm_year + 1900, tm0.tm_mon + 1, tm0.tm_mday,\
      tm0.tm_hour, tm0.tm_min);
   return res;
}

/*! convert lat to str according to type */
char *latToStr (double lat, int type, char* str) {
   double mn = 60 * lat - 60 * (int) lat;
   double sec = 3600 * lat - (3600 * (int) lat) - (60 * (int) mn);
   char c = (lat > 0) ? 'N' : 'S';
   switch (type) {
   case BASIC: snprintf (str, MAX_SIZE_LINE, "%.2lf°", lat); break;
   case DD: snprintf (str, MAX_SIZE_LINE, "%06.2lf°%c", fabs (lat), c); break;
   case DM: snprintf (str, MAX_SIZE_LINE, "%02d°%05.2lf'%c", (int) fabs (lat), fabs(mn), c); break;
   case DMS: 
      snprintf (str, MAX_SIZE_LINE, "%02d°%02d'%02.0lf\"%c", (int) fabs (lat), (int) fabs(mn), fabs(sec), c);
      break;
   default:;
   }
   return str;
}

/*! convert lon to str according to type */
char *lonToStr (double lon, int type, char *str) {
   double mn = 60 * lon - 60 * (int) lon;
   double sec = 3600 * lon - (3600 * (int) lon) - (60 * (int) mn);
   char cc [10];
   strcpy (cc, (lon > 0) ? "E  " : "W");
   switch (type) {
   case BASIC: snprintf (str, MAX_SIZE_LINE, "%.2lf°", lon); break;
   case DD: snprintf (str, MAX_SIZE_LINE, "%06.2lf°%s", fabs (lon), cc); break;
   case DM: snprintf (str, MAX_SIZE_LINE, "%03d°%05.2lf'%s", (int) fabs (lon), fabs(mn), cc); break;
   case DMS:
      snprintf (str, MAX_SIZE_LINE, "%03d°%02d'%02.0lf\"%s", (int) fabs (lon), (int) fabs(mn), fabs(sec), cc);
      break;
   default:;
   }
   return str;
}

/*! return lon on ]-180, 180 ] interval */
double lonCanonize (double lon) {
   lon = fmod (lon, 360);
   if (lon > 180) lon -= 360;
   else if (lon <= -180) lon += 360;
   return lon;
} 

/*! send SMTP request with right parameters to grib mail provider */
bool smtpGribRequestPython (int type, double lat1, double lon1, double lat2, double lon2, char *command, size_t maxLength) {
   int i;
   char temp [MAX_SIZE_LINE];
   char *suffix = "WIND,GUST,WAVES,RAIN,PRMSL"; 
   char newPw [MAX_SIZE_NAME];
   if (par.mailPw [0] == '\0') return false;
   dollarSubstitute (par.mailPw, newPw, MAX_SIZE_NAME);
   
   lon1 = lonCanonize (lon1);
   lon2 = lonCanonize (lon2);
   
   if ((lon1 > 0) && (lon2 < 0)) {// zone crossing antimeridien
      fprintf (stderr, "Error in smtpGribRequestPython: Tentative to cross antemeridian !\n");
      lon2 = 179.0;
   }
   if (setlocale (LC_ALL, "C") == NULL) {                   // very important for printf  decimal numbers
      fprintf (stderr, "Error in main: setlocale failed");
      return EXIT_FAILURE;
   }
   
   lat1 = floor (lat1);
   lat2 = ceil (lat2);
   lon1 = floor (lon1);
   lon2 = ceil (lon2);
   switch (type) {
   case SAILDOCS_GFS:
   case SAILDOCS_ECMWF:
   case SAILDOCS_ICON:
   case SAILDOCS_ARPEGE:
   case SAILDOCS_AROME:
   case SAILDOCS_CURR:
      printf ("smtp saildocs python with: %s %s\n", providerTab [type].service, suffix);
      snprintf (command, MAX_SIZE_BUFFER, "%s %s grib \"send %s:%d%c,%d%c,%d%c,%d%c|%.2lf,%.2lf|0,%d,..%d|%s\" %s\n",\
         par.smtpScript, providerTab [type].address, providerTab [type].service,\
         (int) fabs (round(lat1)), (lat1 > 0) ? 'N':'S', (int) fabs (round(lat2)), (lat2 > 0) ? 'N':'S',\
		   (int) fabs (round(lon1)), (lon1 > 0) ? 'E':'W', (int) fabs (round(lon2)), (lon2 > 0) ? 'E':'W',\
		   par.gribResolution, par.gribResolution, par.gribTimeStep, par.gribTimeMax,\
         (type == SAILDOCS_CURR) ? "CURRENT" : suffix, newPw);
      break;
   case MAILASAIL:
      printf ("smtp mailasail python\n");
      snprintf (command, MAX_SIZE_BUFFER, "%s %s \"grib gfs %d%c:%d%c:%d%c:%d%c ", 
         par.smtpScript, providerTab [type].address, \
         (int) fabs (round(lat1)), (lat1 > 0) ? 'N':'S', (int) fabs (round(lon1)), (lon1 > 0) ? 'E':'W',\
         (int) fabs (round(lat2)), (lat2 > 0) ? 'N':'S', (int) fabs (round(lon2)), (lon2 > 0) ? 'E':'W');
      for (i = 0; i < par.gribTimeMax; i+= par.gribTimeStep) {
         snprintf (temp, MAX_SIZE_LINE, "%d,", i);
	      strncat (command, temp, maxLength - strlen (command));
      }
      snprintf (temp, MAX_SIZE_LINE, "%d GRD,WAVE\" grib %s", i, par.mailPw);
	   strncat (command, temp, maxLength - strlen (command));
      break;
   case GLOBALMARINET:
      printf ("smtp globalmarinet\n");
      int lat = (int) (round ((lat1 + lat2)/2));
      int lon = (int) (round ((lon1 + lon2) /2));
      int size =  (int) fabs (lat2 - lat1) * 60;
      int sizeLon = (int)(fabs (lon2 - lon1) * cos (DEG_TO_RAD * lat)) * 60;
      if (sizeLon > size) size = sizeLon;
      snprintf (command, MAX_SIZE_BUFFER, "%s %s \"%d%c:%d%c:%d 7day\" \"\" %s",  
         par.smtpScript, providerTab [type].address, \
         abs (lat), (lat > 0) ? 'N':'S', abs (lon), (lon > 0) ? 'E':'W',\
         size, par.mailPw);
      break;
   default:;
   }
   if (system (command) != 0) {
      fprintf (stderr, "Error in smtpGribRquest: system call %s\n", command);
      return false;
   }
   char *pt = strrchr (command, ' '); // do not print password
   *pt = '\0';
   printf ("command: %s\n", command);
   return true;
}
   
/*! Read poi file and fill internal data structure. Return number of poi */
int readPoi (const char *fileName) {
   char buffer [MAX_SIZE_LINE];
   char *pLine = &buffer [0];
	FILE *f = NULL;
   int i = nPoi;
   bool first = true;
   char *last, *latToken, *lonToken, *typeToken, *levelToken, *vToken;
	if ((f = fopen (fileName, "r")) == NULL) {
		fprintf (stderr, "Error in readPoi: impossible to read: %s\n", fileName);
		return 0;
	}
   while ((fgets (pLine, MAX_SIZE_LINE, f) != NULL )) {
      if (first) {
         first = false;
         continue;
      }
      if ((latToken = strtok (pLine, CSV_SEP)) != NULL) {            // Lat
         if ((lonToken = strtok (NULL, CSV_SEP)) != NULL) {          // Lon
            if ((typeToken = strtok (NULL, CSV_SEP))!= NULL ) {      // Type
               if ((levelToken = strtok (NULL, CSV_SEP))!= NULL ) {  // Level
                  if ((vToken = strtok (NULL, CSV_SEP))!= NULL ) {   // Name
                     tPoi [i].lat = getCoord (latToken);
                     tPoi [i].lon = getCoord (lonToken);
                     tPoi [i].type = atoi (typeToken);
                     tPoi [i].level = atoi (levelToken);
                     while (isspace (*vToken)) vToken++;             // delete space beginning
                     if ((last = strrchr (vToken, '\n')) != NULL) // delete CR ending
                        *last = '\0';
                     strncpy (tPoi [i].name, vToken, MAX_SIZE_POI_NAME);
                  }
               }
            }
         }
      }
      i += 1;
      if (i >= MAX_N_POI) {
         fprintf (stderr, "Error in readPoi: Exceed MAX_N_POI : %d\n", i);
	      fclose (f);
         return 0;
      } 
	}
	fclose (f);
   return i;
}

/*! write in a file poi data structure except ports */
bool writePoi (const char *fileName) {
   FILE *f = NULL;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "Error in writePoi: cannot write: %s\n", fileName);
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

/*! string to upper */
static char* strToUpper (const char *name, char *upperName) {
   char *pt = upperName;
   while (*name)
      *upperName++ = toupper (*name++);
   *upperName = '\0';
   upperName = pt;
   return upperName;
}

/*! give the point refered by its name. return index found, -1 if not found */
int findPoiByName (const char *name, double *lat, double *lon) {
   const int MIN_NAME_LENGTH = 3;
   char upperName [MAX_SIZE_NAME] = "";
   char upperPoi [MAX_SIZE_NAME] = "";
   strToUpper (name, upperName);
   g_strstrip (upperName);
   if (strlen (upperName) <= MIN_NAME_LENGTH) 
      return -1;
   for (int i = 0; i < nPoi; i++) {
      strToUpper (tPoi [i].name, upperPoi);
      if (strstr (upperPoi, upperName) != NULL) {
         *lat = tPoi [i].lat;
         *lon = tPoi [i].lon;
         return i;
      }
   }
   return -1;
}

/*! printf poi table 
static void poiPrint () {
   char strLat [MAX_SIZE_LINE] = "";
   char strLon [MAX_SIZE_LINE] = "";
   printf ("Lat          Lon         type   level   Name\n");
   for (int i = 0; i < nPoi; i++) {
      if ((tPoi [i].type != UNVISIBLE) && (tPoi [i].type != PORT)) {
         printf ("%-12s %-12s %d %d %s\n", latToStr (tPoi[i].lat, par.dispDms, strLat), \
            lonToStr (tPoi[i].lon, par.dispDms, strLon), tPoi [i].type, tPoi [i].level, tPoi[i].name);
      }
   }
   printf ("\nNumber of Points Of Interest: %d\n", nPoi);
} */

/*! translate poi table into a string */
char *poiToStr (char *str, size_t maxLength) {
   char line [MAX_SIZE_LINE] = "";
   char strLat [MAX_SIZE_LINE] = "";
   char strLon [MAX_SIZE_LINE] = "";
   snprintf (str, MAX_SIZE_LINE, "Lat         Lon         Name\n");
   for (int i = 0; i < nPoi; i++) {
      if (tPoi [i].type > UNVISIBLE) {
         snprintf (line, MAX_SIZE_LINE, "%-12s %-12s %d %d %s\n", latToStr (tPoi[i].lat, par.dispDms, strLat), \
            lonToStr (tPoi[i].lon, par.dispDms, strLon), tPoi [i].type, tPoi [i].level, tPoi[i].name);
	      strncat (str, line, maxLength - strlen (str));
      }
   }
   snprintf (line, MAX_SIZE_LINE, "\nNumber of Points Of Interest: %d\n", nPoi);
   strncat (str, line, maxLength - strlen (str));
   return str;
}

/*! read issea file and fill table tIsSea */
bool readIsSea (const char *fileName) {
   FILE *f = NULL;
   int i = 0;
   char c;
   int nSea = 0;
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "Error in readIsSea: cannot open: %s\n", fileName);
      return false;
   }
	if ((tIsSea = (char *) malloc (SIZE_T_IS_SEA + 1)) == NULL) {
		fprintf (stderr, "Error in readIsSea: Malloc");
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
double offsetLocalUTC () {
   time_t now;
   time (&now);                           // Obtenir le temps actuel en time_t
   struct tm local_tm = *localtime(&now); // Convertir en struct tm en temps local
   struct tm utc_tm = *gmtime(&now);      // Convertir en struct tm en temps UTC
   time_t local_time = mktime(&local_tm); // Convertir ces struct tm en time_t
   time_t utc_time = mktime(&utc_tm);
   double offsetSeconds = difftime(local_time, utc_time); // Calculer la différence en secondes
   if (local_tm.tm_isdst > 0)
        offsetSeconds += 3600;            // Ajouter une heure supplémentaire si heure d'été
   return offsetSeconds;
}
      
/*! convert epoch time to string with or without seconds */
char *epochToStr (time_t t, bool seconds, char *str, size_t len) {
   struct tm *utc = gmtime (&t);
   if (seconds)
      snprintf (str, len, "%d/%02d/%02d %02d:%02d:%02d", 
         utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
         utc->tm_hour, utc->tm_min, utc->tm_sec);
   else
      snprintf (str, len, "%d/%02d/%02d %02d:%02d", 
         utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
         utc->tm_hour, utc->tm_min);
      
   return str;
}

/*! return str representing grib date */
char *gribDateTimeToStr (long date, long time, char *str, size_t len) {
   int year = date / 10000;
   int mon = (date % 10000) / 100;
   int day = date % 100;
   int hour = time / 100;
   int min = time % 100;
   snprintf (str, len, "%4d/%02d/%02d %02d:%02d", year, mon, day, hour, min);
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
   tm0.tm_isdst = -1;                                       // avoid adjustments with hours summer winter
   //return timegm (&tm0);                                  // converion struct tm en time_t NON PORTABLE
   return mktime (&tm0);                                  // converion struct tm en time_t
}

/*! return difference in hours between two zones (current zone and Wind zone) */
double zoneTimeDiff (Zone *zone1, Zone *zone0) {
   if (zone1->wellDefined && zone0->wellDefined) {
      time_t time1 = gribDateTimeToEpoch (zone1->dataDate [0], zone1->dataTime [0]);
      time_t time0 = gribDateTimeToEpoch (zone0->dataDate [0], zone0->dataTime [0]);
      return (time1 - time0) / 3600.0;
   }
   else return 0;
} 

/*! print Grib u v for all lat lon time information */
void printGribFull (Zone *zone, FlowP *gribData) {
   int iGrib;
   printf ("printGrib\n");
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

/*! print Grib u v for all lat lon time information */
void printGrib (Zone *zone, FlowP *gribData) {
   int iGrib;
   printf ("printGrib\n");
   for (size_t  k = 0; k < zone->nTimeStamp; k++) {
      double t = zone->timeStamp [k];
      printf ("Time: %.0lf\n", t);
      for (int i = 0; i < zone->nbLat; i++) {
         for (int j = 0; j < zone->nbLon; j++) {
	         iGrib = (k * zone->nbLat * zone->nbLon) + (i * zone->nbLon) + j;
            printf (" %6.2f %6.2f %6.2f %6.2f\n", \
            gribData [iGrib].lon, \
	         gribData [iGrib].lat, \
            gribData [iGrib].u, \
            gribData [iGrib].v);
         }
      }
     printf ("\n");
   }
}

/* true if all value seem OK */
static bool checkGrib (Zone *zone, int iFlow, CheckGrib *check) {
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
   return (check->uMissing == 0) && (check->vMissing == 0) && (check->gMissing == 0) && (check->wMissing == 0) && (check->outZone == 0);
}

/*! true if (wind) zone and currentZone intersect in geography */
static bool geoIntersectGrib (Zone *zone1, Zone *zone2) {
	return 
	  ((zone2->latMin < zone1 -> latMax) &&
	   (zone2->latMax > zone1 -> latMin) &&
	   (zone2->lonLeft < zone1 -> lonRight) &&
	   (zone2->lonRight > zone1 -> lonLeft));
}

/*! true if (wind) zone and zone 2 intersect in time */
static bool timeIntersectGrib (Zone *zone1, Zone *zone2) {
	time_t timeMin1 = gribDateTimeToEpoch (zone1->dataDate [0], zone1->dataTime [0]);
	time_t timeMin2 = gribDateTimeToEpoch (zone2->dataDate [0], zone2->dataTime [0]);
	time_t timeMax1 = timeMin1 + 3600 * (zone1-> timeStamp [zone1 -> nTimeStamp -1]);
	time_t timeMax2 = timeMin2 + 3600 * (zone2-> timeStamp [zone2 -> nTimeStamp -1]);
	
	return (timeMin2 < timeMax1) && (timeMax2 > timeMin1);
}

/*! check if time steps are regular */
static bool timeStepRegularGrib (Zone *zone) {
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
static bool uvPresentGrib (Zone *zone) {
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
static int consistentGrib (Zone *zone, int iFlow, double epsilon, int *nLatSuspects, int *nLonSuspects) {
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
            lon = lonCanonize (zone->lonLeft + j * zone->lonStep);
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

/*! check Grib information and write report in the buffer
    return true if something wrong  */
bool checkGribToStr (char *buffer, size_t maxLength) {
   const double LOC_EPSILON = 0.01;
   CheckGrib sCheck;
   int nWind = 0, nCurr = 0, nLatSuspects, nLonSuspects;
   bool OK = true;
   char str [MAX_SIZE_LINE] = "";
    
   nWind = consistentGrib (&zone, WIND, LOC_EPSILON, &nLatSuspects, &nLonSuspects);
   if ((nLatSuspects > 0) || (nLonSuspects > 0)) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "n Wind suspect Lat      : %10d, ratio Lat suspect : %.2lf %% \n", nLatSuspects, 100 * (double)nLatSuspects/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
      snprintf (str, MAX_SIZE_LINE, "n Wind suspect Lon      : %10d, ratio Lon suspect : %.2lf %% \n", nLonSuspects, 100 * (double)nLonSuspects/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
   }
   snprintf (buffer, MAX_SIZE_BUFFER, "n Wind Values           : %10d\n", nWind); // at least this line
   snprintf (str, MAX_SIZE_LINE, "%s\n", (zone.wellDefined) ? (zone.allTimeStepOK) ? "Wind Zone Well defined" : "All Wind Zone TimeSteps are not defined" : "Wind Zone Undefined");
   strncat (buffer, str, maxLength - strlen (buffer));
   
   nCurr = consistentGrib (&currentZone, CURRENT, LOC_EPSILON, &nLatSuspects, &nLonSuspects);
   snprintf (str, MAX_SIZE_LINE, "n Current Values        : %10d\n", nCurr); // at least this line also 
   strncat (buffer, str, maxLength - strlen (buffer));
   snprintf (str, MAX_SIZE_LINE, "%s\n", (currentZone.wellDefined) ? (currentZone.allTimeStepOK) ? "Current Well defined" : "All Current TimeSteps are not defined" : "Current Zone Undefined");
   strncat (buffer, str, maxLength - strlen (buffer));
   
   if ((nLatSuspects > 0) || (nLonSuspects > 0)) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "n Current suspect Lat   : %10d, ratio Lat suspect : %.2lf %% \n", nLatSuspects, 100 * (double)nLatSuspects/(double) (nCurr));
      strncat (buffer, str, maxLength - strlen (buffer));
      snprintf (str, MAX_SIZE_LINE, "n Current suspect Lon   : %10d, ratio Lon suspect : %.2lf %% \n", nLonSuspects, 100 * (double)nLonSuspects/(double) (nCurr));
      strncat (buffer, str, maxLength - strlen (buffer));
   }
   
   if (!uvPresentGrib (&zone)) { 
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Wind lack u or v\n");
      strncat (buffer, str, maxLength - strlen (buffer));
   }
   
   if (!uvPresentGrib (&currentZone)) { 
      OK = false;
      snprintf (str, MAX_SIZE_LINE, "Current lack u v\n");
      strncat (buffer, str, maxLength - strlen (buffer));
   }

   if (! timeStepRegularGrib (&zone)) {
      OK = false;
	   snprintf (str, MAX_SIZE_LINE, "Wind timeStep is NOT REGULAR !!!\n");
      strncat (buffer, str, maxLength - strlen (buffer));
   }
   if (! timeStepRegularGrib (&currentZone)) {
      OK = false;
	   snprintf (str, MAX_SIZE_LINE, "Current timeStep is NOT REGULAR !!!\n");
      strncat (buffer, str, maxLength - strlen (buffer));
   }
   
   // check geo intersection between current and wind
   if (! geoIntersectGrib (&zone, &currentZone)) {
      OK = false;
      snprintf (str, MAX_SIZE_LINE, "current and wind grib have no common geo\n");
      strncat (buffer, str, maxLength - strlen (buffer));
	  
   }
    // check time intersection between current and wind
   if (! timeIntersectGrib (&zone, &currentZone)) {
      OK = false;
	   snprintf (str, MAX_SIZE_LINE, "current and wind grib have no common time\n");
      strncat (buffer, str, maxLength - strlen (buffer));
   } 
   
   // check grib missing or strange values and out of zone for wind
   if (! checkGrib (&zone, WIND, &sCheck)) {
      OK = false;
	   snprintf (str, MAX_SIZE_LINE, "u missing Values        : %10d, ratio Missing     : %.2lf %% \n", sCheck.uMissing, 100 * (double)sCheck.uMissing/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
	   snprintf (str, MAX_SIZE_LINE, "v missing Values        : %10d, ratio Missing     : %.2lf %% \n", sCheck.vMissing, 100 * (double)sCheck.vMissing/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
	   snprintf (str, MAX_SIZE_LINE, "w missing Values        : %10d, ratio Missing     : %.2lf %% \n", sCheck.wMissing, 100 * (double)sCheck.wMissing/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
	   snprintf (str, MAX_SIZE_LINE, "g missing Values        : %10d, ratio Missing     : %.2lf %% \n", sCheck.gMissing, 100 * (double)sCheck.gMissing/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
      
	   snprintf (str, MAX_SIZE_LINE, "u strange Values        : %10d, ratio Missing     : %.2lf %% \n", sCheck.uStrange, 100 * (double)sCheck.uStrange/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
	   snprintf (str, MAX_SIZE_LINE, "v strange Values        : %10d, ratio Missing     : %.2lf %% \n", sCheck.vStrange, 100 * (double)sCheck.vStrange/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
	   snprintf (str, MAX_SIZE_LINE, "w strange Values        : %10d, ratio Missing     : %.2lf %% \n", sCheck.wStrange, 100 * (double)sCheck.wStrange/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
	   snprintf (str, MAX_SIZE_LINE, "g strange Values        : %10d, ratio Missing     : %.2lf %% \n", sCheck.gStrange, 100 * (double)sCheck.gStrange/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
	  
	   snprintf (str, MAX_SIZE_LINE, "out zone Values         : %10d, ratio Missing     : %.2lf %% \n", sCheck.outZone, 100 * (double)sCheck.outZone/(double) (nWind));
      strncat (buffer, str, maxLength - strlen (buffer));
   } 
   
   // check grib missing or strange values and out of zone for current
   if (! checkGrib (&currentZone, CURRENT, &sCheck)) {
      OK = false;
	   snprintf (str, MAX_SIZE_LINE, "u Current missing Values: %10d, ratio Missing     : %.2lf %% \n", sCheck.uMissing, 100 * (double)sCheck.uMissing/(double) (nCurr));
      strncat (buffer, str, maxLength - strlen (buffer));
	   snprintf (str, MAX_SIZE_LINE, "v Current missing Values: %10d, ratio Missing     : %.2lf %% \n", sCheck.vMissing, 100 * (double)sCheck.vMissing/(double) (nCurr));
      strncat (buffer, str, maxLength - strlen (buffer));
	   snprintf (str, MAX_SIZE_LINE, "u Current strange Values: %10d, ratio Missing     : %.2lf %% \n", sCheck.uStrange, 100 * (double)sCheck.uStrange/(double) (nCurr));
      strncat (buffer, str, maxLength - strlen (buffer));
	   snprintf (str, MAX_SIZE_LINE, "v Current strange Values: %10d, ratio Missing     : %.2lf %% \n", sCheck.vStrange, 100 * (double)sCheck.vStrange/(double) (nCurr));
      strncat (buffer, str, maxLength - strlen (buffer));
	  
	   snprintf (str, MAX_SIZE_LINE, "out current zone Values : %10d, ratio Missing     : %.2lf %% \n", sCheck.outZone, 100 * (double)sCheck.outZone/(double) (nCurr));
      strncat (buffer, str, maxLength - strlen (buffer));
   }
   return OK;
}

/*! find iTinf and iTsup in order t : zone.timeStamp [iTInf] <= t <= zone.timeStamp [iTSup] */
static inline void findTimeAround (double t, int *iTInf, int *iTSup, Zone *zone) { 
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
static inline int indLat (double lat, Zone *zone) {
   return (int) round ((lat - zone->latMin)/zone->latStep); // ATTENTION
}

/*! return indice of lon in gribData wind or current */
static inline int indLon (double lon, Zone *zone) {
   if (lon < zone->lonLeft) lon += 360;
   return (int) round ((lon - zone->lonLeft)/zone->lonStep);
}

/*! interpolation to get u, v, g (gust), w (waves) at point (lat, lon)  and time t */
static bool findFlow (double lat, double lon, double t, double *rU, double *rV, double *rG, double *rW, double *msl, double *prate, Zone *zone, const FlowP *gribData) {
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
void findWindGrib (double lat, double lon, double t, double *u, double *v, double *gust, double *w, double *twd, double *tws ) {
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
void findCurrentGrib (double lat, double lon, double t, double *uCurr, double *vCurr, double *tcd, double *tcs) {
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

/*! Read lists zone.timeStamp, shortName, zone.dataDate, dataTime before full grib reading */
static bool readGribLists (const char *fileName, Zone *zone) {
   codes_index* index = NULL;
   int ret = 0;
    
   // Create an index given set of keys
   index = codes_index_new (0,"dataDate,dataTime,shortName,level,number,step",&ret);
   if (ret) {
      fprintf (stderr, "Error in readGribLists: index: %s\n",codes_get_error_message(ret)); 
      return false;
   }
   // Indexes a file
   ret = codes_index_add_file (index, fileName);
   if (ret) {
       fprintf (stderr, "Error in readGribLists: ret: %s\n",codes_get_error_message(ret)); 
       return false;
   }
 
   // get the number of distinct values of "step" in the index
   CODES_CHECK (codes_index_get_size (index,"step", &zone->nTimeStamp),0);
    
   // get the list in ascending order of distinct steps from the index
   CODES_CHECK (codes_index_get_long (index,"step", zone->timeStamp, &zone->nTimeStamp),0);
   
   CODES_CHECK(codes_index_get_size(index,"shortName",&zone->nShortName),0);
   
   if (zone->shortName != NULL) free (zone->shortName); 
   if ((zone->shortName = (char**) malloc (sizeof(char*)*zone->nShortName)) == NULL) {
      fprintf (stderr, "Error in readGribLists: Malloc zone.shortName\n");
      return (false);
   }
   CODES_CHECK(codes_index_get_string(index,"shortName",zone->shortName, &zone->nShortName),0);
   CODES_CHECK(codes_index_get_size (index, "dataDate", &zone->nDataDate),0);
   CODES_CHECK (codes_index_get_long (index,"dataDate", zone->dataDate, &zone->nDataDate),0);
   CODES_CHECK(codes_index_get_size (index, "dataTime", &zone->nDataTime),0);
   CODES_CHECK (codes_index_get_long (index,"dataTime", zone->dataTime, &zone->nDataTime),0);
   codes_index_delete (index);
   return true;
}

/*! Read grib parameters in zone before full grib reading */
static bool readGribParameters (const char *fileName, Zone *zone) {
   int err = 0;
   FILE* f = NULL;
   double lat1, lat2;
   codes_handle *h = NULL;
 
   if ((f = fopen (fileName, "r")) == NULL) {
       fprintf (stderr, "Error in readGribParameters: unable to open file %s\n",fileName);
       return false;
   }
 
   // create new handle from the first message in the file
   h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err);
   if (h == NULL) {
       fprintf (stderr, "Error in readGribParameters: code handle from file : %s Code error: %s\n",fileName, codes_get_error_message(err)); 
       return false;
   }
   fclose (f);
 
   CODES_CHECK(codes_get_long (h, "centre", &zone->centreId),0);
   CODES_CHECK(codes_get_long (h, "editionNumber", &zone->editionNumber),0);
   CODES_CHECK(codes_get_long (h, "stepUnits", &zone->stepUnits),0);
   CODES_CHECK(codes_get_long (h, "numberOfValues", &zone->numberOfValues),0);
   CODES_CHECK(codes_get_long (h, "Ni", &zone->nbLon),0);
   CODES_CHECK(codes_get_long (h, "Nj", &zone->nbLat),0);
   if (zone->nbLat > MAX_N_GRIB_LAT) {
      fprintf (stderr, "Error in readGribParameters: zone.nbLat exceed MAX_N_GRIB_LAT %d\n", MAX_N_GRIB_LAT);
      return false;
   }
   if (zone->nbLon > MAX_N_GRIB_LON) {
      fprintf (stderr, "Error in reasGribParameters: zone.nbLon exceed MAX_N_GRIB_LON %d\n", MAX_N_GRIB_LON);
      return false;
   }
 
   CODES_CHECK(codes_get_double (h,"latitudeOfFirstGridPointInDegrees",&lat1),0);
   CODES_CHECK(codes_get_double (h,"longitudeOfFirstGridPointInDegrees",&zone->lonLeft),0);
   CODES_CHECK(codes_get_double (h,"latitudeOfLastGridPointInDegrees",&lat2),0);
   CODES_CHECK(codes_get_double (h,"longitudeOfLastGridPointInDegrees",&zone->lonRight),0);
   zone->lonLeft = lonCanonize (zone->lonLeft);
   zone->lonRight = lonCanonize (zone->lonRight);
   if (lat1 < lat2) {
      zone->latMin = lat1;
      zone->latMax = lat2;
   }
   else {
      zone->latMin = lat2;
      zone->latMax = lat1;
   }

   CODES_CHECK(codes_get_double (h,"iDirectionIncrementInDegrees",&zone->lonStep),0);
   CODES_CHECK(codes_get_double (h,"jDirectionIncrementInDegrees",&zone->latStep),0);
 
   codes_handle_delete (h);
   return true;
}

/*! find index in gribData table */
static inline int indexOf (int timeStep, double lat, double lon, Zone *zone) {
   int iLat = indLat (lat, zone);
   int iLon = indLon (lon, zone);
   int iT = -1;
   for (iT = 0; iT < (int) zone->nTimeStamp; iT++) {
      if (timeStep == zone->timeStamp [iT])
         break;
   }
   if (iT == -1) {
      fprintf (stderr, "Error in indexOf: Cannot find index of time: %d\n", timeStep);
      return -1;
   }
   //printf ("iT: %d iLon :%d iLat: %d\n", iT, iLon, iLat); 
   return  (iT * zone->nbLat * zone->nbLon) + (iLat * zone->nbLon) + iLon;
}

/*! read grib file using eccodes C API 
   return readGribRet */
bool readGribAll (const char *fileName, Zone *zone, int iFlow) {
   FILE* f = NULL;
   int err = 0, iGrib;
   long bitmapPresent  = 0, timeStep, oldTimeStep;
   double lat, lon, val, indicatorOfParameter;
   char shortName [MAX_SIZE_SHORT_NAME];
   size_t lenName;
   const long GUST_GFS = 180;
   long timeInterval = 0;
   memset (zone, 0,  sizeof (Zone));
   zone->wellDefined = false;
   if (! readGribLists (fileName, zone)) {
      return false;
   }
   if (! readGribParameters (fileName, zone)) {
      return false;
   }
   if (zone->nShortName < 2) {
      fprintf (stderr, "Error in readGribAll: ShortName not presents in: %s\n", fileName);
      return false;
   }
   if (zone->nTimeStamp > 1)
      timeInterval = zone->timeStamp [1] - zone->timeStamp [0]; 

   if (tGribData [iFlow] != NULL) free (tGribData [iFlow]); 
   if ((tGribData [iFlow] = calloc (sizeof(FlowP),  zone->nTimeStamp * zone->nbLat * zone->nbLon)) == NULL) {
      fprintf (stderr, "Error in readGribAll: calloc tGribData [iFlow]\n");
      return false;
   }
   printf ("In readGribAll : %zu allocated\n", sizeof(FlowP) * zone->nTimeStamp * zone->nbLat * zone->nbLon);
   
   // Message handle. Required in all the ecCodes calls acting on a message.
   codes_handle* h = NULL;
   // Iterator on lat/lon/values.
   codes_iterator* iter = NULL;
   if ((f = fopen (fileName, "rb")) == NULL) {
       fprintf (stderr, "Error in readGribAll: Unable to open file %s\n", fileName);
       return false;
   }
   zone->nMessage = 0;
   zone->allTimeStepOK = true;
   timeStep = zone->timeStamp [0];
   oldTimeStep = timeStep;
   //int nMes = 0;

   // Loop on all the messages in a file
   while ((h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err)) != NULL) {
      if (err != CODES_SUCCESS) CODES_CHECK (err, 0);
      //printf ("message: %d\n", nMes);
      //nMes += 1;

      // Check if a bitmap applies
      CODES_CHECK(codes_get_long(h, "bitmapPresent", &bitmapPresent), 0);
      if (bitmapPresent) {
          CODES_CHECK(codes_set_double(h, "missingValue", MISSING), 0);
      }

      lenName = MAX_SIZE_SHORT_NAME;
      CODES_CHECK(codes_get_string(h, "shortName", shortName, &lenName), 0);
      CODES_CHECK(codes_get_long(h, "step", &timeStep), 0);

      // check timeStep progress well 
      if ((timeStep != 0) && (timeStep != oldTimeStep) && ((timeStep - oldTimeStep) != timeInterval)) // check timeStep progress well 
         zone->allTimeStepOK = false;
      oldTimeStep = timeStep;

      err = codes_get_double(h, "indicatorOfParameter", &indicatorOfParameter);
      if (err != CODES_SUCCESS) 
         indicatorOfParameter = -1;
 
      // A new iterator on lat/lon/values is created from the message handle h.
      iter = codes_grib_iterator_new(h, 0, &err);
      if (err != CODES_SUCCESS) CODES_CHECK(err, 0);
 
      // Loop on all the lat/lon/values.
      while (codes_grib_iterator_next(iter, &lat, &lon, &val)) {
         // printf("%.2f %.2f %.2lf %ld %s\n", lat, lon, val, timeStep, shortName);
         lon = lonCanonize (lon);
         iGrib = indexOf ((int) timeStep, lat, lon, zone);
         if (iGrib == -1) return NULL;
         tGribData [iFlow] [iGrib].lat = lat; 
         tGribData [iFlow] [iGrib].lon = lon; 
         if ((strcmp (shortName, "10u") == 0) || (strcmp (shortName, "u") == 0) || (strcmp (shortName, "ucurr") == 0))
            tGribData [iFlow] [iGrib].u = val;
         else if ((strcmp (shortName, "10v") == 0) || (strcmp (shortName, "v") == 0) || (strcmp (shortName, "vcurr") == 0))
            tGribData [iFlow] [iGrib].v = val;
         else if (strcmp (shortName, "gust") == 0)
            tGribData [iFlow] [iGrib].g = val;
         else if (strcmp (shortName, "msl") == 0)  { // pressure
            tGribData [iFlow] [iGrib].msl = val;
            // printf ("pressure: %.2lf\n", val);
         }
         else if (strcmp (shortName, "prate") == 0) { // precipitation
            tGribData [iFlow] [iGrib].prate = val;
            // if (val > 0) printf ("prate: %lf\n", val);
         }
         else if (strcmp (shortName, "swh") == 0) // waves
            tGribData [iFlow] [iGrib].w = val;
         else if (indicatorOfParameter == GUST_GFS) // find gust in GFS file specific parameter = 180
            tGribData [iFlow] [iGrib].g = val;
            
         // printf ("%7.2lf; %7.2lf; %.0lf; %7.2lf\n", lat, lon, timeStep, val);
      }
      codes_grib_iterator_delete (iter);
      codes_handle_delete (h);
      zone->nMessage += 1;
   }
 
   fclose (f);
   zone->wellDefined = true;
   return true;
}

/*! launch readGribAll wih Wind or current  parameters */   
void *readGrib (void *data) {
   int *iFlow = (int *) (data);
   if ((*iFlow == WIND) && (par.gribFileName [0] != '\0')) {
      if (readGribAll (par.gribFileName, &zone, WIND))
         readGribRet = 1;
      else readGribRet = 0;
   }
   else if ((*iFlow == CURRENT) && (par.currentGribFileName [0] != '\0')) {
      if (readGribAll (par.currentGribFileName, &currentZone, CURRENT))
         readCurrentGribRet = 1;
      else readCurrentGribRet = 0;
   }
   return NULL;
}

/*! write Grib information in string */
char *gribToStr (char *str, Zone *zone, size_t maxLength) {
   char line [MAX_SIZE_LINE] = "";
   char strTmp [MAX_SIZE_LINE] = "";
   char strLat0 [MAX_SIZE_NAME] = "", strLon0 [MAX_SIZE_NAME] = "";
   char strLat1 [MAX_SIZE_NAME] = "", strLon1 [MAX_SIZE_NAME] = "";
   char centreName [MAX_SIZE_NAME] = "";
   
   for (size_t i = 0; i < sizeof(dicTab) / sizeof(dicTab[0]); i++) // search name of center
     if (dicTab [i].id == zone->centreId) 
        strncpy (centreName, dicTab [i].name, sizeof (centreName) - 1);
         
   newDate (zone->dataDate [0], zone->dataTime [0]/100, strTmp);
   snprintf (str, MAX_SIZE_LINE, "Centre ID: %ld %s   %s   Ed number: %ld\nnMessages: %d\nstepUnits: %ld\n# values : %ld\n",\
      zone->centreId, centreName, strTmp, zone->editionNumber, zone->nMessage, zone->stepUnits, zone->numberOfValues);
   snprintf (line, MAX_SIZE_LINE, "Zone From: %s, %s To: %s, %s\n", \
      latToStr (zone->latMin, par.dispDms, strLat0), lonToStr (zone->lonLeft, par.dispDms, strLon0),\
      latToStr (zone->latMax, par.dispDms, strLat1), lonToStr (zone->lonRight, par.dispDms, strLon1));
   strncat (str, line, maxLength - strlen (str));   
   snprintf (line, MAX_SIZE_LINE, "LatStep  : %04.4f° LonStep: %04.4f°\n", zone->latStep, zone->lonStep);
   strncat (str, line, maxLength - strlen (str));   
   snprintf (line, MAX_SIZE_LINE, "Nb Lat   : %ld      Nb Lon : %ld\n", zone->nbLat, zone->nbLon);
   strncat (str, line, maxLength - strlen (str));   
   if (zone->nTimeStamp < 8) {
      snprintf (line, MAX_SIZE_LINE, "TimeStamp List of %zu : [ ", zone->nTimeStamp);
      strncat (str, line, maxLength - strlen (str));   
      for (size_t k = 0; k < zone->nTimeStamp; k++) {
         snprintf (line, MAX_SIZE_LINE, "%ld ", zone->timeStamp [k]);
         strncat (str, line, maxLength - strlen (str));   
      }
      strncat (str, "]\n", maxLength - strlen (str));
   }
   else {
      snprintf (line, MAX_SIZE_LINE, "TimeStamp List of %zu : [%ld, %ld, ..%ld]\n", zone->nTimeStamp,\
         zone->timeStamp [0], zone->timeStamp [1], zone->timeStamp [zone->nTimeStamp - 1]);
      strncat (str, line, maxLength - strlen (str));   
   }

   snprintf (line, MAX_SIZE_LINE, "Shortname List: [ ");
   strncat (str, line, maxLength - strlen (str));   
   for (size_t k = 0; k < zone->nShortName; k++) {
      snprintf (line, MAX_SIZE_LINE, "%s ", zone->shortName [k]);
      strncat (str, line, maxLength - strlen (str));   
   }
   strncat (str, "]\n", maxLength - strlen (str));
   if ((zone->nDataDate > 1) || (zone->nDataTime > 1)) {
      snprintf (line, MAX_SIZE_LINE, "Warning number of Date: %zu, number of Time: %zu\n", zone->nDataDate, zone->nDataTime);
      strncat (str, line, maxLength - strlen (str));   
   }
   snprintf (line, MAX_SIZE_LINE, "Zone is       :  %s\n", (zone->wellDefined) ? "Well defined" : "Undefined");
   strncat (str, line, maxLength - strlen (str));   
   return str;
}

/*! read polar file and fill poLMat matrix */
bool readPolar (char *fileName, PolMat *mat) {
   FILE *f = NULL;
   char buffer [MAX_SIZE_LINE];
   char *pLine = &buffer [0];
   char *strToken;
   double v = 0;
   double lastValInCol0 = -1;
   int c;
   mat->nLine = 0;
   mat->nCol = 0;
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "Error in readPolar: cannot open: %s\n", fileName);
      return false;
   }
   while (fgets (pLine, MAX_SIZE_LINE, f) != NULL) {
      c = 0;
      strToken = strtok (pLine, CSV_SEP);
      while (strToken != NULL) {
         if (sscanf (strToken, "%lf", &v) == 1) {
            if (c == 0) {
              if (v < lastValInCol0) break; // value of column 0 should progress
              else lastValInCol0 = v; 
            }
            mat->t [mat->nLine][c] = v;
         }
         c += 1;
         strToken = strtok (NULL, CSV_SEP);
      }
      if ((c == 0) && (v < lastValInCol0)) break;
      mat->nLine+= 1;
      if (mat->nLine >= MAX_N_POL_MAT_LINES) {
         fprintf (stderr, "Error in readPolar: max number of line: %d\n", MAX_N_POL_MAT_LINES);
         return false;
      }
      if (mat->nCol < c) mat->nCol = c; // max
   }
   mat -> t [0][0] = -1;
   fclose (f);
   return true;
}

/*! return the max value found in polar */
double maxValInPol (PolMat *mat) {
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
char *polToStr (char * str, PolMat *mat, size_t maxLength) {
   char line [MAX_SIZE_LINE] = "";
   str [0] = '\0';
   for (int i = 0; i < mat->nLine; i++) {
      for (int j = 0; j < mat->nCol; j++) {
         snprintf (line, MAX_SIZE_LINE, "%6.2f ", mat->t [i][j]);
         strncat (str, line, maxLength - strlen (str));
      }
      strncat (str, "\n", maxLength - strlen (str));
   }
   snprintf (line, MAX_SIZE_LINE, "Number of rows in polar : %d\n", mat->nCol);
   strncat (str, line, maxLength - strlen (str));
   snprintf (line, MAX_SIZE_LINE, "Number of lines in polar: %d\n", mat->nLine);
   strncat (str, line, maxLength - strlen (str));
   snprintf (line, MAX_SIZE_LINE, "Max                     : %.2lf\n", maxValInPol (mat));
   strncat (str, line, maxLength - strlen (str));
   return str;
}

/*! find name, lat and lon */
static void analyseCoord (char *str, double *lat, double *lon) {
   char *pt = NULL;
   g_strstrip (str);
   if (isNumber (str)) {
      pt = strchr (str, ',');
      *lon = getCoord (strchr (str, ',') +1);
      *pt = '\0'; // pas joli
      *lat = getCoord (str);
   }
}

/*! return true if p is in polygon po
  Ray casting algorithm */ 
bool isInPolygon (Point *p, Polygon *po) {
   int i, j;
   bool inside = false;
   // printf ("isInPolygon lat:%.2lf lon: %.2lf\n", p.lat, p.lon);
   for (i = 0, j = po->n - 1; i < po->n; j = i++) {
      if ((po->points[i].lat > p->lat) != (po->points[j].lat > p->lat) &&
         (p->lon < (po->points[j].lon - po->points[i].lon) * (p->lat - po->points[i].lat) / (po->points[j].lat - po->points[i].lat) + po->points[i].lon)) {
         inside = !inside;
      }
   }
   return inside;
}

/*! return true if p is in forbid area */ 
static bool isInForbidArea (Pp *pt) {
   Point p = {pt->lat, pt->lon};
   for (int i = 0; i < par.nForbidZone; i++) {
      if (isInPolygon (&p, &forbidZones [i]))
         return true;
   }
   return false;
}

/*! complement according to forbidden areas */
void updateIsSeaWithForbiddenAreas () {
   Pp pt;
   memset (&pt, 0, sizeof (Pp));
   if (tIsSea == NULL) return;
   if (par.nForbidZone <= 0) return;
   for (int i = 0; i < SIZE_T_IS_SEA; i++) {
      pt.lon = (i % 3601) / 10 - 180.0;
      pt.lat = 90.0 - (i / (3601 *10));
      if (isInForbidArea (&pt))
         tIsSea [i] = 0;
   }
}

/*! read forbid zone */
static void forbidZoneAdd (char *line, int n) {
   char *latToken, *lonToken;
   // Allouer de la mémoire pour les points
   if ((forbidZones [n].points = (Point *) malloc (MAX_SIZE_FORBID_ZONE * sizeof(Point))) == NULL) {
      fprintf (stderr, "Error in forbidZoneAdd: malloc with n = %d\n", n);
      return;
   }

   if (((latToken = strtok (line, ",")) != NULL) && ((lonToken = strtok (NULL, ";")) != NULL)) { // first point
      forbidZones [n].points[0].lat = strtod (latToken, NULL);  
      forbidZones [n].points[0].lon = strtod (lonToken, NULL);
      forbidZones [n].n = 1;
      while ((lonToken != NULL) && (latToken != NULL) && (forbidZones [n].n < MAX_SIZE_FORBID_ZONE)) {
         if ((latToken = strtok (NULL, ",")) != NULL) {         // lat
            forbidZones [n].points[forbidZones [n].n].lat = strtod (latToken, NULL);  
            if ((lonToken = strtok (NULL, ";")) != NULL) {      // lon
               forbidZones [n].points[forbidZones [n].n].lon = strtod (lonToken, NULL);
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
   char buffer [MAX_SIZE_BUFFER];
   char *pLine = &buffer [0];
   memset (&par, 0, sizeof (Par));
   par.opt = 1;
   par.tStep = 1;
   par.cogStep = 5;
   par.rangeCog = 90;
   par.maxIso = MAX_SIZE_ISOC;
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
   
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "Error in readParam: Cannot open: %s\n", fileName);
      return false;
   }

   while (fgets (pLine, MAX_SIZE_BUFFER, f) != NULL ) {
      str [0] = '\0';
      while (isspace (*pLine)) pLine++;
      // printf ("%s", pLine);
      if ((!*pLine) || *pLine == '#' || *pLine == '\n') continue;
      if ((pt = strchr (pLine, '#')) != NULL) // comment elimination
         *pt = '\0';
      if (sscanf (pLine, "WD:%255s", par.workingDir) > 0); // should be fiest !!!
      else if (sscanf (pLine, "POI:%255s", str) > 0) 
         buildRootName (str, par.poiFileName);
      else if (sscanf (pLine, "PORT:%255s", str) > 0)
         buildRootName (str, par.portFileName);
      else if (strstr (pLine, "POR:") != NULL) {
         analyseCoord (strchr (pLine, ':') + 1, &par.pOr.lat, &par.pOr.lon);
         par.pOr.lon = lonCanonize (par.pOr.lon);
         par.pOr.id = -1;
         par.pOr.father = -1;
      } 
      else if (strstr (pLine, "PDEST:") != NULL) {
         analyseCoord (strchr (pLine, ':') + 1, &par.pDest.lat, &par.pDest.lon);
         par.pDest.lon = lonCanonize (par.pDest.lon);
         par.pDest.id = 0;
         par.pDest.father = 0;
      }
      else if (strstr (pLine, "WP:") != NULL) {
         if (wayPoints.n > MAX_N_WAY_POINT)
            fprintf (stderr, "Error in readParam: number of wayPoints exceeded; %d\n", MAX_N_WAY_POINT);
         else {
            analyseCoord (strchr (pLine, ':') + 1, &wayPoints.t [wayPoints.n].lat, &wayPoints.t [wayPoints.n].lon);
            wayPoints.n += 1;
         }
      }
      else if (sscanf (pLine, "POR_NAME:%255s", par.pOrName) > 0);
      else if (sscanf (pLine, "PDEST_NAME:%255s", par.pDestName) > 0);
      else if (sscanf (pLine, "GRIB_RESOLUTION:%lf", &par.gribResolution) > 0);
      else if (sscanf (pLine, "GRIB_TIME_STEP:%d", &par.gribTimeStep) > 0);
      else if (sscanf (pLine, "GRIB_TIME_MAX:%d", &par.gribTimeMax) > 0);
      else if (sscanf (pLine, "TRACE:%255s", str) > 0)
         buildRootName (str, par.traceFileName);
      else if (sscanf (pLine, "CGRIB:%255s", str) > 0)
         buildRootName (str, par.gribFileName);
      else if (sscanf (pLine, "CURRENT_GRIB:%255s", str) > 0)
         buildRootName (str, par.currentGribFileName);
      else if (sscanf (pLine, "WAVE_POL:%255s", str) > 0)
         buildRootName (str, par.wavePolFileName);
      else if (sscanf (pLine, "POLAR:%255s", str) > 0)
         buildRootName (str, par.polarFileName);
      else if (sscanf (pLine, "ISSEA:%255s", str) > 0)
         buildRootName (str, par.isSeaFileName);
      else if (sscanf (pLine, "CLI_HELP:%255s", str) > 0)
         buildRootName (str, par.cliHelpFileName);
      else if (sscanf (pLine, "HELP:%255s", par.helpFileName) > 0); // full link required
      else if (strstr (pLine, "SMTP_SCRIPT:") != NULL) {
         pLine = strchr (pLine, ':') + 1;
         g_strstrip (pLine);
         strncpy (par.smtpScript, pLine, sizeof (par.smtpScript) - 1);
      }
      else if (strstr (pLine, "IMAP_TO_SEEN:") != NULL) {
         pLine = strchr (pLine, ':') + 1;
         g_strstrip (pLine);
         strncpy (par.imapToSeen, pLine, sizeof (par.imapToSeen) - 1);
      }
      else if (strstr (pLine, "IMAP_SCRIPT:") != NULL) {
         pLine = strchr (pLine, ':') + 1;
         g_strstrip (pLine);
         strncpy (par.imapScript, pLine, sizeof (par.imapScript) - 1);
      }
      else if (sscanf (pLine, "SHP:%255s", str) > 0) {
         buildRootName (str, par.shpFileName [par.nShpFiles]);
         par.nShpFiles += 1;
         if (par.nShpFiles >= MAX_N_SHP_FILES) 
            fprintf (stderr, "Error in readParam: Number max of SHP files reached: %d\n", par.nShpFiles);
      }
      else if (sscanf (pLine, "MOST_RECENT_GRIB:%d", &par.mostRecentGrib) > 0);
      else if (sscanf (pLine, "START_TIME:%lf", &par.startTimeInHours) > 0);
      else if (sscanf (pLine, "T_STEP:%lf", &par.tStep) > 0);
      else if (sscanf (pLine, "RANGE_COG:%d", &par.rangeCog) > 0);
      else if (sscanf (pLine, "COG_STEP:%d", &par.cogStep) > 0);
      else if (sscanf (pLine, "MAX_ISO:%d", &par.maxIso) > 0);
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
         buildRootName (str, par.dumpIFileName);
      else if (sscanf (pLine, "DUMPR:%255s", str) > 0)
         buildRootName (str, par.dumpRFileName);
      else if (sscanf (pLine, "PAR_INFO:%255s", str) > 0)
         buildRootName (str, par.parInfoFileName);
      else if (sscanf (pLine, "OPT:%d", &par.opt) > 0);
      else if (sscanf (pLine, "MAX_THETA:%lf", &par.maxTheta) > 0);
      else if (sscanf (pLine, "J_FACTOR:%d", &par.jFactor) > 0);
      else if (sscanf (pLine, "K_FACTOR:%d", &par.kFactor) > 0);
      else if (sscanf (pLine, "MIN_PT:%d", &par.minPt) > 0);
      else if (sscanf (pLine, "PENALTY0:%lf", &par.penalty0) > 0);
      else if (sscanf (pLine, "PENALTY1:%lf", &par.penalty1) > 0);
      else if (sscanf (pLine, "N_SECTORS:%d", &par.nSectors) > 0);
      else if (sscanf (pLine, "ISOC_DISP:%d", &par.style) > 0);
      else if (sscanf (pLine, "COLOR_DISP:%d", &par.showColors) > 0);
      else if (sscanf (pLine, "DMS_DISP:%d", &par.dispDms) > 0);
      else if (sscanf (pLine, "WIND_DISP:%d", &par.windDisp) > 0);
      else if (sscanf (pLine, "INFO_DISP:%d", &par.infoDisp) > 0);
      else if (sscanf (pLine, "AVR_OR_GUST_DISP:%d", &par.averageOrGustDisp) > 0);
      else if (sscanf (pLine, "CURRENT_DISP:%d", &par.currentDisp) > 0);
      else if (sscanf (pLine, "WAVE_DISP:%d", &par.waveDisp) > 0);
      else if (sscanf (pLine, "GRID_DISP:%d", &par.gridDisp) > 0);
      else if (sscanf (pLine, "LEVEL_POI_DISP:%d", &par.maxPoiVisible) > 0);
      else if (sscanf (pLine, "SPEED_DISP:%d", &par.speedDisp) > 0);
      else if (sscanf (pLine, "TECHNO_DISP:%d", &par.techno) > 0);
      else if (sscanf (pLine, "WEBKIT:%255s", str) > 0)
         buildRootName (str, par.webkit);
      else if (strstr (pLine, "EDITOR:") != NULL) {
         pLine = strchr (pLine, ':') + 1;
         g_strstrip (pLine);
         strncpy (par.editor, pLine, sizeof (par.editor) - 1);
      }
      else if (strstr (pLine, "SPREADSHEET:") != NULL) {
         pLine = strchr (pLine, ':') + 1;
         g_strstrip (pLine);
         strncpy (par.spreadsheet, pLine, sizeof (par.spreadsheet) - 1);
      }
      else if ((strstr (pLine, "FORBID_ZONE:") != NULL) && (par.nForbidZone < MAX_N_FORBID_ZONE)) {
         pLine = strchr (pLine, ':') + 1;
         g_strstrip (pLine);
         strncpy (par.forbidZone [par.nForbidZone], pLine, sizeof (par.forbidZone) - 1);
         forbidZoneAdd (pLine, par.nForbidZone);
         par.nForbidZone += 1;
      }
      else if (sscanf (pLine, "MAIL_PW:%255s", par.mailPw) > 0);
      else fprintf (stderr, "Error in readParam: Cannot interpret: %s\n", pLine);
   }
   if (par.mailPw [0] != '\0') {
      par.storeMailPw = true;
   }
      
   if (par.maxIso > MAX_N_ISOC) par.maxIso = MAX_N_ISOC;
   if (par.constWindTws != 0) initZone (&zone);
   fclose (f);
   return true;
}

/*! write parameter file from fill struct par 
   header or not, password or not */
bool writeParam (const char *fileName, bool header, bool password) {
   FILE *f = NULL;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "Error in writeParam: Cannot write: %s\n", fileName);
      return false;
   }
   if (header) 
      fprintf (f, "Name             Value\n");
   fprintf (f, "WD:              %s\n", par.workingDir);
   fprintf (f, "POI:             %s\n", par.poiFileName);
   fprintf (f, "PORT:            %s\n", par.portFileName);
   fprintf (f, "POR:             %.2lf,%.2lf\n", par.pOr.lat, par.pOr.lon);
   fprintf (f, "PDEST:           %.2lf,%.2lf\n", par.pDest.lat, par.pDest.lon);
   if (par.pOrName [0] != '\0')
      fprintf (f, "POR_NAME:        %s\n", par.pOrName);
   if (par.pDestName [0] != '\0')
      fprintf (f, "PDEST_NAME:        %s\n", par.pDestName);
   for (int i = 0; i < wayPoints.n; i++)
      fprintf (f, "WP:              %.2lf,%.2lf\n", wayPoints.t [i].lat, wayPoints.t [i].lon);
   fprintf (f, "TRACE:           %s\n", par.traceFileName);
   fprintf (f, "CGRIB:           %s\n", par.gribFileName);
   if (par.currentGribFileName [0] != '\0')
      fprintf (f, "CURRENT_GRIB:    %s\n", par.currentGribFileName);
   fprintf (f, "MOST_RECENT_GRIB:%d\n", par.mostRecentGrib);
   fprintf (f, "GRIB_RESOLUTION: %.1lf\n", par.gribResolution);
   fprintf (f, "GRIB_TIME_STEP:  %d\n", par.gribTimeStep);
   fprintf (f, "GRIB_TIME_MAX:   %d\n", par.gribTimeMax);
   fprintf (f, "POLAR:           %s\n", par.polarFileName);
   fprintf (f, "WAVE_POL:        %s\n", par.wavePolFileName);
   fprintf (f, "ISSEA:           %s\n", par.isSeaFileName);
   fprintf (f, "HELP:            %s\n", par.helpFileName);
   fprintf (f, "CLI_HELP:        %s\n", par.cliHelpFileName);
   for (int i = 0; i < par.nShpFiles; i++)
      fprintf (f, "SHP:             %s\n", par.shpFileName [i]);

   fprintf (f, "START_TIME:      %.2lf\n", par.startTimeInHours);
   fprintf (f, "T_STEP:          %.2lf\n", par.tStep);
   fprintf (f, "RANGE_COG:       %d\n", par.rangeCog);
   fprintf (f, "COG_STEP:        %d\n", par.cogStep);
   fprintf (f, "MAX_ISO:         %d\n", par.maxIso);
   fprintf (f, "SPECIAL:         %d\n", par.special);
   fprintf (f, "PENALTY0:        %.2lf\n", par.penalty0);
   fprintf (f, "PENALTY1:        %.2lf\n", par.penalty1);
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
   fprintf (f, "AVR_OR_GUST_DISP:%d\n", par.averageOrGustDisp);
   fprintf (f, "CURRENT_DISP:    %d\n", par.currentDisp);
   fprintf (f, "WAVE_DISP:       %d\n", par.waveDisp);
   fprintf (f, "GRID_DISP:       %d\n", par.gridDisp);
   fprintf (f, "SPEED_DISP:      %d\n", par.speedDisp);
   fprintf (f, "TECHNO_DISP:     %d\n", par.techno);
   fprintf (f, "MAX_THETA:       %.2lf\n", par.maxTheta);
   fprintf (f, "J_FACTOR:        %d\n", par.jFactor);
   fprintf (f, "K_FACTOR:        %d\n", par.kFactor);
   fprintf (f, "MIN_PT:          %d\n", par.minPt);
   fprintf (f, "N_SECTORS:       %d\n", par.nSectors);
   fprintf (f, "SMTP_SCRIPT:     %s\n", par.smtpScript);
   fprintf (f, "IMAP_TO_SEEN:    %s\n", par.imapToSeen);
   fprintf (f, "IMAP_SCRIPT:     %s\n", par.imapScript);
   fprintf (f, "WEBKIT:          %s\n", par.webkit);
   fprintf (f, "EDITOR:          %s\n", par.editor);
   fprintf (f, "SPREADSHEET:     %s\n", par.spreadsheet);
   if (password)
      fprintf (f, "MAIL_PW:         %s\n", par.mailPw);
   for (int i = 0; i < par.nForbidZone; i++) {
      fprintf (f, "FORBID_ZONE:     ");
      for (int j = 0; j < forbidZones [i].n; j++)
         fprintf (f, "%2.lf, %.2lf; ", forbidZones [i].points [j].lat, forbidZones [i].points [j].lon);
      fprintf (f, "\n");
   }
   fclose (f);
   return true;
}

/*! provide GPS information */
void *getGPS() {
   struct gps_data_t gps_data;
   my_gps_data.OK = false;
   if (gps_open("localhost", GPSD_TCP_PORT, &gps_data) == -1) {
      fprintf(stderr, "Error: Unable to connect to GPSD.\n");
      return NULL;
   }

   gps_stream(&gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

   while (true) {
      if (gps_waiting(&gps_data, GPS_TIME_OUT)) {
         if (gps_read(&gps_data, NULL, 0) == -1) {
            fprintf (stderr, "Error in getGPS: gps_read\n");
         } else {
            if (gps_data.set & PACKET_SET) {
               /*   
               printf("Latitude   : %lf, Longitude: %lf\n", gps_data.fix.latitude, gps_data.fix.longitude);
               printf("Altitude   : %lf meters\n", gps_data.fix.altitude);
               printf("Fix Quality: %d\n", gps_data.fix.status);
               printf("Satellites : %d\n", gps_data.satellites_visible);
               printf("SOG        : %.2lfKn\n", MS_TO_KN * gps_data.fix.speed);
               printf("COG        : %.0lf°\n", gps_data.fix.track);
               printf("Time       : %ld\n\n", gps_data.fix.time.tv_sec);
               */
               if (isfinite(gps_data.fix.latitude) && isfinite(gps_data.fix.longitude) &&
                  ((gps_data.fix.latitude != 0 || gps_data.fix.longitude != 0))) {
                  // printf("Lat: %.2f, Lon: %.2f\n", gps_data.fix.latitude, gps_data.fix.longitude);
                  // update GPS data
                  my_gps_data.lat = gps_data.fix.latitude;
                  my_gps_data.lon = gps_data.fix.longitude;
                  my_gps_data.alt = gps_data.fix.altitude;
                  my_gps_data.cog = gps_data.fix.track;
                  my_gps_data.sog = MS_TO_KN * gps_data.fix.speed;
                  my_gps_data.status = gps_data.fix.status;
                  my_gps_data.nSat = gps_data.satellites_visible;
                  my_gps_data.time = gps_data.fix.time.tv_sec;
                  my_gps_data.OK = true;
               }
               else 
                  my_gps_data.OK = false;
            } else 
               fprintf(stderr, "Error in getGPS: No fix available.\n");
         }
      } else {
         printf("Timeout: No data received from GPS.\n");
      }
   }

   gps_stream(&gps_data, WATCH_DISABLE, NULL);
   gps_close(&gps_data);

   return NULL;
}

/*! GPS init */
bool initGPS () {
   if (pthread_create (&gps_thread, NULL, getGPS, NULL) != 0) {
      fprintf (stderr, "Error in initGPS: Unable to create GPS thread.\n");
      return false;
   }
   return true;
}

/*! GPS close */
void closeGPS () {
   pthread_cancel (gps_thread);
   pthread_join (gps_thread, NULL);
}


/*! URL for METEO Consult Wind delay hours before now at closest time 0Z, 6Z, 12Z, or 18Z 
	Current 12 hours before new at 0Z */
char *buildMeteoUrl (int type, int i, char *url) {
   time_t meteoTime = time (NULL) - (3600 * delay [type]);
   struct tm * timeInfos = gmtime (&meteoTime);  // time 4 hours ago
   // printf ("Delay: %d hours ago: %s\n", delay [type], asctime (timeInfos));
   int mm = timeInfos->tm_mon + 1;
   int dd = timeInfos->tm_mday;
   int hh = (timeInfos->tm_hour / 6) * 6;    // select 0 or 6 or 12 por 18
   if (type == CURRENT) {
      snprintf (url, MAX_SIZE_BUFFER, CURRENT_URL [i*2 + 1], ROOT_GRIB_URL, 0, mm, dd); // allways 0
   }
   else {
      snprintf (url, MAX_SIZE_BUFFER, WIND_URL [i*2 + 1], ROOT_GRIB_URL, hh, mm, dd);
   }
   return url;
}

/*! call back for curlGet */
static size_t WriteCallback (void* contents, size_t size, size_t nmemb, void* userp) {
   FILE* f = (FILE*)userp;
   return fwrite (contents, size, nmemb, f);
}

/*! return true if URL has been downloaded */
bool curlGet (char *url, char* outputFile) {
   long http_code;
   CURL* curl = curl_easy_init();
   if (!curl) {
      fprintf (stderr, "Error in curlGet: Impossible to initialize curl\n");
      return false;
   }
   FILE* f = fopen (outputFile, "wb");
   if (!f) {
      fprintf (stderr, "Error in curlGet: Opening ouput file: %s\n", outputFile);
      return false;
   }

   curl_easy_setopt(curl, CURLOPT_URL, url);
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);

   CURLcode res = curl_easy_perform(curl);
   
   if (res != CURLE_OK) {
      fprintf (stderr, "Error in curlGet: Downloading: %s\n", curl_easy_strerror(res));
   }
   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
   curl_easy_cleanup(curl);
   fclose(f);
   if (http_code >= 400 || res != CURLE_OK) {
      fprintf (stderr, "Errr in curlget: HTTP response code: %ld\n", http_code);
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
      fprintf (stderr, "Error in addTracePoint: cannot write: %s\n", fileName);
      return false;
   }
   fprintf (f, "%.2lf; %.2lf; %ld; %s\n", my_gps_data.lat, my_gps_data.lon, my_gps_data.time, epochToStr (my_gps_data.time, true, str, sizeof (str)));
   fclose (f);
   return true;
}

/*! add trace point to trace file */
bool addTracePt (const char *fileName, double lat, double lon) {
   FILE *f = NULL;
   char str [MAX_SIZE_LINE];
   time_t t = time (NULL);
   if ((f = fopen (fileName, "a")) == NULL) {
      fprintf (stderr, "Error in addTrace: cannot write: %s\n", fileName);
      return false;
   }
   fprintf (f, "%.2lf; %.2lf; %ld; %s\n", lat, lon, t, epochToStr (t, true, str, sizeof (str)));
   fclose (f);
   return true;
}

/*! find last point in trace */
bool findLastTracePoint (const char *fileName, double *lat, double *lon, double *time) {
   FILE *f = NULL;
   char line [MAX_SIZE_LINE];
   char lastLine [MAX_SIZE_LINE];
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "Error in findLastTracePoint: cannot read: %s\n", fileName);
      return false;
   }
   while (fgets (line, MAX_SIZE_LINE, f) != NULL ) {
      strncpy (lastLine, line, MAX_SIZE_LINE);
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
   double lat0 = 0.0, lon0 = 0.0, time0 = 0, latLast, lonLast, lat, lon, time, delta;
   *rd = 0.0;
   
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "Error in distanceTraceDone: cannot read: %s\n", fileName);
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
      fprintf (stderr, "Error in distanceTraceDone: cannot read: %s\n", fileName);
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

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt (curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf (stderr, "Error in isServerAccessible: Curl request: %s\n", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return false; // Serveur non accessible en raison d'une erreur de requête
        }

        long http_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        // 2xx signifie que le serveur est accessible
        if (http_code >= 200 && http_code < 300) {
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return true; // Serveur accessible
        } else {
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return false; // Serveur non accessible en raison d'un code de réponse HTTP non 2xx
        }
    } else {
        fprintf (stderr, "Error in isServerAccessible: Init curl.\n");
        curl_global_cleanup();
        return false; // Erreur d'initialisation de curl
    }
}

/*! true if day light, false if night 
// libnova required for this
bool OisDayLight (time_t t, double lat, double lon) {
   struct ln_lnlat_posn observer;
   struct ln_rst_time rst;
   double julianDay;
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

