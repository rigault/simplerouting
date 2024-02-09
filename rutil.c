/*! compilation gcc -Wall -c rutil.c */
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
#include <unistd.h>
#include <time.h>
#include <math.h>
#include "eccodes.h"
#include "rtypes.h"
#include "shapefil.h"

/*! dictionnary of meteo services */
struct DictElmt dicTab [] = {{7, "Weather service US"}, {78, "DWD Germany"}, {85, "Meteo France"}, {98,"ECMWF European"}};
char *tWho [] = {"gfs", "ECMWF", "ICON", "RTOFS"}; // for saildocs

extern Route route; // defined in engine.c
const char *CSV_SEP = ";,\t";
/*! polar matrix description */
PolMat polMat;

/* polar matrix for waves */
PolMat wavePolMat;

/*! parameter */
Par par;

/* geographic zone covered by grib file */
Zone zone = {false, 0, 0, 0, 0, 0, 90.0, 0.0, 359.99, 0.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, {0}, {0}, {0}};          // wind
Zone currentZone = {false, 0, 0, 0, 0, 0, 90.0, 0.0, 359.99, 0.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL,  {0}, {0}, {0}};   // current

/* table describing if sea or earth */
char *tIsSea = NULL; 

/*! grib data description */
FlowP *gribData = NULL;             // wind
FlowP *currentGribData = NULL;      // current

//City tCities [MAX_N_POI];
Poi tPoi [MAX_N_POI];
int nPoi = 0;
int readGribRet = -1;               // to check if readGrib is terminated
int readCurrentGribRet = -1;        // to check if readCurrentGrib is terminated

// for shp file
int nEntities = 0;                  // number of entities in current shp file
int nTotEntities = 0;               // cumulated number of entities
Entity *entities;


/*! Thread for GPS management */
struct ThreadData {
   struct gps_data_t gps_data;
   MyGpsData my_gps_data;  
};

struct ThreadData thread_data;
pthread_t gps_thread;
MyGpsData my_gps_data;
pthread_mutex_t gps_data_mutex = PTHREAD_MUTEX_INITIALIZER;

const int delay [] = {6, 12}; // 6 hours before now for wind, 12 hours for current
const char *WIND_URL [N_WIND_URL * 2] = {
   "Atlantic North",   "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Nord_Atlantique.grb",
   "Atlantic Center",  "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Centre_Atlantique.grb",
   "Gascogne",         "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Gascogne.grb",
   "Europe",           "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Europe.grb"
};
const char *CURRENT_URL [N_CURRENT_URL * 2] = {
   "Atlantic North",    "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Nord_Atlantique.grb",
   "Atlantic Center",   "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Centre_Atlantique.grb",
   "Gascogne",          "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Gascogne.grb",
   "Europe",            "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Europe.grb"
};

/*! build entities based on .shp file */
bool initSHP (char* nameFile) {
   int nShapeType;
   int k;
   SHPHandle hSHP = SHPOpen (nameFile, "rb");
   if (hSHP == NULL) {
      printf("Cannot open Shapefile: %s\n", nameFile);
      return false;
   }

   SHPGetInfo(hSHP, &nEntities, &nShapeType, NULL, NULL);
   printf ("In initSHP, nEntities: %d, nShapeType: %d\n", nEntities, nShapeType);

   if (nTotEntities == 0) entities = malloc(sizeof(Entity) * nEntities); 
   // ATTENTION NE MARCHE QUE POUR 1 SHP
   for (int i = 0; i < nEntities; i++) {
      SHPObject *psShape = SHPReadObject(hSHP, i);
      if (psShape == NULL) {
         printf("Erreur lors de la lecture de l'entité %d\n", i);
         continue;
      }
      k = nTotEntities + i;

      entities[k].numPoints = psShape->nVertices;
      entities[k].nSHPType = psShape->nSHPType;
      entities[k].points = malloc(sizeof(Point) * entities[k].numPoints);

      for (int j = 0; j < psShape->nVertices; j++) {
         entities[k].points[j].lon = psShape->padfX[j];
         entities[k].points[j].lat = psShape->padfY[j];
      }
      SHPDestroyObject(psShape);
   }
   SHPClose(hSHP);
   nTotEntities += nEntities;
   return true;
}

/*! free memory allocated */
void freeSHP () {
   for (int i = 0; i < nEntities; i++) {
      free (entities[i].points);
   }
   free(entities);
}

/* true if name contains a number */
bool isNumber (const char *name) {
   while (*name) {
      if (isdigit(*name))
      return true;
      name++;
   }
   return false; 
}

/*! translate str in double for latitude longitudes */
double getCoord (const char *str) {
   double val =  0.0, deg = 0.0, min = 0.0, sec = 0.0;
   const char *neg = "SsWwOo"; // value returned is negative if south or West
   if ((strchr (str, '"') != NULL)) {
      sec = strtod (strchr (str, '\'') + 1, NULL);
   }
   if ((strchr (str, '\'') != NULL)) {
      min = strtod (strstr (str, "°") + 2, NULL); // degree ° is UTF8 coded with 2 bytes
   }
   deg  = strtod (str, NULL);
   val = deg + min/60.0 + sec/3600.0;
   while (*neg) {
      if (strchr (str, *neg) != NULL) { // south or West
         return -val;
         neg += 1;
      }
      neg += 1;
   }
   return val;
}

/*! delete spaces before and after str */
void strip (char *str0) {
   char *str = str0;
   if (str == NULL || *str == '\0') return;
   while (isspace((unsigned char)(*str)))
      str++;
   if (*str == '\0') return;
   char *end = str + strlen(str) - 1;
   while (end > str && isspace((unsigned char)(*end)))
      end--;
   *(end + 1) = '\0';
   strcpy (str0, str);
}

/*! build root name if not already a root name */
char *buildRootName (const char *fileName, char *rootName) {
   if (fileName [0] == '/') 
      strcpy (rootName, fileName);
   else if (par.workingDir [0] != '\0')
      sprintf (rootName, "%s%s", par.workingDir, fileName);
   else sprintf (rootName, "%s%s", WORKING_DIR, fileName);
   return (rootName);
}

/*! get file size */
long getFileSize (const char *fileName) {
    struct stat st;
    if (stat(fileName, &st) == 0) {
        return st.st_size;
    } else {
        fprintf (stderr, "Error getFileSize: %s\n", fileName);
        return -1;
    }
}

/*! return date and time using ISO notation after adding myTime (hours) to the Date */
char *newDate (long intDate, double  myTime, char *res) {
   time_t seconds = 0;
   struct tm *tm0 = localtime (&seconds);;
   tm0->tm_year = ((int ) intDate / 10000) - 1900;
   tm0->tm_mon = ((int) (intDate % 10000) / 100) - 1;
   tm0->tm_mday = (int) (intDate % 100);
   tm0->tm_hour = 0; tm0->tm_min = 0; tm0->tm_sec = 0;
   time_t theTime = mktime (tm0);                      // converion struct tm en time_t
   theTime += 3600 * myTime;                           // calcul ajout en secondes
   struct tm *timeInfos = localtime (&theTime);        // conversion en struct tm
   sprintf (res, "%4d/%02d/%02d %02d:%02d", timeInfos->tm_year + 1900, timeInfos->tm_mon + 1, timeInfos->tm_mday,\
      timeInfos->tm_hour, timeInfos->tm_min);
   return res;
}

/*! convert lat to str according to type */
char *latToStr (double lat, int type, char* str) {
   double mn = 60 * lat - 60 * (int) lat;
   double sec = 3600 * lat - (3600 * (int) lat) - (60 * (int) mn);
   char c = (lat > 0) ? 'N' : 'S';
   switch (type) {
   case BASIC: sprintf (str, "%.2lf°", lat); break;
   case DD: sprintf (str, "%06.2lf°%c", fabs (lat), c); break;
   case DM: sprintf (str, "%02d°%05.2lf'%c", (int) fabs (lat), fabs(mn), c); break;
   case DMS: 
      sprintf (str, "%02d°%02d'%02.0lf\"%c", (int) fabs (lat), (int) fabs(mn), fabs(sec), c);
      break;
   default:;
   }
   return str;
}

/*! convert lon to str according to type */
char *lonToStr (double lon, int type, char* str) {
   double mn = 60 * lon - 60 * (int) lon;
   double sec = 3600 * lon - (3600 * (int) lon) - (60 * (int) mn);
   char cc [10];
   strcpy (cc, (lon > 0) ? "E  " : "W");
   switch (type) {
   case BASIC: sprintf (str, "%.2lf°", lon); break;
   case DD: sprintf (str, "%06.2lf°%s", fabs (lon), cc); break;
   case DM: sprintf (str, "%03d°%05.2lf'%s", (int) fabs (lon), fabs(mn), cc); break;
   case DMS:
      sprintf (str, "%03d°%02d'%02.0lf\"%s", (int) fabs (lon), (int) fabs(mn), fabs(sec), cc);
      break;
   default:;
   }
   return str;
}

/*! send SMTP request wih right parameters to grib mail provider */
bool smtpGribRequestPython (int type, double lat1, double lon1, double lat2, double lon2) {
   int i;
   char temp [MAX_SIZE_LINE];
   char *suffix = "WIND,WAVES"; 
   if (lon1 > 180) lon1 -= 360;
   if (lon2 > 180) lon2 -= 360;
   lat1 = floor (lat1);
   lat2 = ceil (lat2);
   lon1 = floor (lon1);
   lon2 = ceil (lon2);
   char command [MAX_SIZE_BUFFER] = "";
   switch (type) {
   case SAILDOCS_GFS:
   case SAILDOCS_ECMWF:
   case SAILDOCS_ICON:
   case SAILDOCS_CURR:
      printf ("smtp saildocs python with: %s %s\n", tWho [type], suffix);
      sprintf (command, "%s %s grib \"send %s:%d%c,%d%c,%d%c,%d%c|%.1lf,%.1lf|0,%d,..%d|%s\" %s\n",\
         par.smtpScript, par.smtpTo [type], tWho [type],\
         (int) fabs (round(lat1)), (lat1 > 0) ? 'N':'S', (int) fabs (round(lat2)), (lat2 > 0) ? 'N':'S',\
		   (int) fabs (round(lon1)), (lon1 > 0) ? 'E':'W', (int) fabs (round(lon2)), (lon2 > 0) ? 'E':'W',\
		   par.gribLatStep, par.gribLonStep, par.gribTimeStep, par.gribTimeMax,\
         (type == SAILDOCS_CURR) ? "CURRENT" : suffix, par.mailPw);
      break;
   case MAILASAIL:
      printf ("smtp mailasail python\n");
      sprintf (command, "%s %s \"grib gfs %d%c:%d%c:%d%c:%d%c ", 
         par.smtpScript, par.smtpTo [type], \
         (int) fabs (round(lat1)), (lat1 > 0) ? 'N':'S', (int) fabs (round(lon1)), (lon1 > 0) ? 'E':'W',\
         (int) fabs (round(lat2)), (lat2 > 0) ? 'N':'S', (int) fabs (round(lon2)), (lon2 > 0) ? 'E':'W');
      for (i = 0; i < par.gribTimeMax; i+= par.gribTimeStep) {
         sprintf (temp, "%d,", i);
	      strcat (command, temp);
      }
      sprintf (temp, "%d GRD,WAVE\" grib %s", i, par.mailPw);
      strcat (command, temp);   
      break;
   case GLOBALMARINET:
      printf ("smtp globalmarinet\n");
      int lat = (int) (round ((lat1 + lat2)/2));
      int lon = (int) (round ((lon1 + lon2) /2));
      int size =  (int) fabs (lat2 - lat1) * 60;
      int sizeLon = (int)(fabs (lon2 - lon1) * cos (DEG_TO_RAD * lat)) * 60;
      if (sizeLon > size) size = sizeLon;
      sprintf (command, "%s %s \"%d%c:%d%c:%d 7day\" \"\" %s",  
         par.smtpScript, par.smtpTo [type], \
         abs (lat), (lat > 0) ? 'N':'S', abs (lon), (lon > 0) ? 'E':'W',\
         size, par.mailPw);
      break;
   default:;
   }
   if (system (command) != 0) {
      fprintf (stderr, "Error in smtpGribRquest system call %s\n", command);
      return false;
   }
   char *pt = strrchr (command, ' '); // do not print password
   *pt = '\0';
   printf ("command: %s\n", command);
   return true;
}
   
/*! true wind direction */
double extTwd (double u, double v) {
	double val = 180 + RAD_TO_DEG * atan2 (u, v);
   return (val > 180) ? val - 360 : val;
}

/*! true wind speed. cf Pythagore */
double extTws (double u, double v) {
    return MS_TO_KN * sqrt ((u * u) + (v * v));
}

/*! Read poi file and fill internal data structure. Return number of poi */
int readPoi (const char *fileName) {
   char buffer [MAX_SIZE_LINE];
   char *pLine = &buffer [0];
	FILE *f;
   int i = 0;
   char *last;
   char *latToken, *lonToken, *vToken;
	if ((f = fopen (fileName, "r")) == NULL) {
		fprintf (stderr, "Error in readPoi: impossible to read: %s\n", fileName);
		return 0;
	}
   while ((fgets (pLine, MAX_SIZE_LINE, f) != NULL )) {
      if (*pLine == '#') {
         pLine += 1;
         tPoi [i].type = UNVISIBLE;
      }
      else tPoi [i].type = VISIBLE;
      
      if ((latToken = strtok (pLine, CSV_SEP)) != NULL) {            // lat
         if ((lonToken = strtok (NULL, CSV_SEP)) != NULL) {          // lon
            if ((vToken = strtok (NULL, CSV_SEP))!= NULL ) {         // Name
               tPoi [i].lat = getCoord (latToken);
               tPoi [i].lon = getCoord (lonToken);
               while (isspace (*vToken)) vToken++;                   // delete space beginning
               if ((last = strrchr (vToken, '\n')) != NULL)          // delete CR ending
                  *last = '\0';
               strcpy (tPoi [i].name, vToken);
            }
         }
      }
      i += 1;
      if (i > MAX_N_POI) {
         fprintf (stderr, "In readPoi, exceed MAX_N_POI : %d\n", i);
	      fclose (f);
         return 0;
      } 
	}
	fclose (f);
   return i;
}

/*! write in a file poi data structure */
bool writePoi (const char *fileName) {
   FILE *f;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "Routing Error in writePoi cannot write: %s\n", fileName);
      return false;
   }
   for (int i = 0; i < nPoi; i++) {
      if (tPoi[i].type == VISIBLE)
         fprintf (f, "%.2lf; %.2lf; %s\n", tPoi [i].lat, tPoi [i].lon, tPoi [i].name);
      else   
         fprintf (f, "# %.2lf; %.2lf; %s\n", tPoi [i].lat, tPoi [i].lon, tPoi [i].name);
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
   char upperName [MAX_SIZE_NAME] = "";
   char upperPoi [MAX_SIZE_NAME] = "";
   strToUpper (name, upperName);
   strip (upperName);
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

/*! translate poi table into a string */
char *poiToStr (char *str) {
   char line [MAX_SIZE_LINE] = "";
   char strLat [MAX_SIZE_LINE] = "";
   char strLon [MAX_SIZE_LINE] = "";
   sprintf (str, "Lat         Lon         Name\n");
   for (int i = 0; i < nPoi; i++) {
      if (tPoi [i].type == VISIBLE) {
         sprintf (line, "%-12s %-12s %s\n", latToStr (tPoi[i].lat, par.dispDms, strLat), \
            lonToStr (tPoi[i].lon, par.dispDms, strLon), tPoi[i].name);
	      strcat (str, line);
      }
   }
   sprintf (line, "\nNumber of Points Of Interest: %d\n", nPoi);
   strcat (str, line);
   return str;
}

/*! read issea file and fill table tIsSea */
int readIsSea (const char *fileName) {
   FILE *f;
   int i = 0;
   char c;
   int nSea = 0;
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "Error in readIsSea cannot open: %s\n", fileName);
      return false;
   }
	if ((tIsSea = (char *) malloc (SIZE_T_IS_SEA + 1)) == NULL) {
		fprintf (stderr, "Error in readIsSea : Malloc");
		return false;
	}

   while (((c = fgetc (f)) != -1) && (i < SIZE_T_IS_SEA)) {
      if (c == '1') nSea += 1;
      tIsSea [i] = c - '0';
      i += 1;
   }
   fclose (f);
   printf ("nSea: %d size: %d proportion sea: %lf\n", nSea, i, (double) nSea/ (double) i); 
   return i;
} 

/* reinit zone decribing geographic meta data */
void initConst (Zone *zone) {
   zone->latMin = -90;
   zone->latMax = 89;
   zone->lonRight = 0;
   zone->lonLeft = 359.99;
   zone->latStep = 5;
   zone->lonStep = 5;
   zone->nbLat = 0;
   zone->nbLon = 0;
}

/*! direct loxodromic cap from origin to dest */
double loxCap (double lat1, double lon1, double lat2, double lon2) {
   return RAD_TO_DEG * atan2 ((lon2 - lon1) * cos (DEG_TO_RAD * (lat1+lat2)/2), lat2 - lat1);
}

/*! return pythagore distance */
static inline double dist (double x, double y) {
   return sqrt (x*x + y*y);
}

/*! direct loxodromic dist from origi to dest */
double loxDist (double lat1, double lon1, double lat2, double lon2) {
   double coeffLat = cos (DEG_TO_RAD * (lat1 + lat2)/2);
   return 60 * dist ((lat1 - lat2), (lon2 - lon1) * coeffLat);
}

/*! return orthodomic distance in nautical miles */
double orthoDist (double lat1, double lon1, double lat2, double lon2) {
    double theta = lon1 - lon2;
    double distRad = acos(
        sin(lat1 * M_PI/180) * sin(lat2 * M_PI/180) + 
        cos(lat1 * M_PI/180) * cos(lat2 * M_PI/180) * cos(theta * M_PI/180)
    );
    return fabs (60 * (180/M_PI) * distRad);
}

/*! return givry correction to apply to loxodromic cap to get orthodromic cap  */
double givry (double lat1, double lon1, double lat2, double lon2) {
   double theta = lon1 - lon2;
   double latM = (lat1 + lat2) / 2;
   return (theta/2) * sin (latM * M_PI/180);
}

/*! return fx : linear interpolation */
static inline double interpolate (double x, double x0, double x1, double fx0, double fx1) {
   if (x1 == x0) return fx0;
   else return fx0 + (x-x0) * (fx1-fx0) / (x1-x0);
}

/*! true if P is within the zone */
bool extIsInZone (Pp pt, Zone zone) {
   if (par.constWindTws > 0) return true;
   else return (pt.lat >= zone.latMin) && (pt.lat <= zone.latMax) & (pt.lon >= zone.lonLeft) && (pt.lon <= zone.lonRight);
}

/*! convert long date found in grib file in seconds time_t */
time_t dateToTime_t (long date) {
   time_t seconds = 0;
   struct tm *tm0 = gmtime (&seconds);;
   tm0->tm_year = ((int ) date / 10000) - 1900;
   tm0->tm_mon = ((int) (date % 10000) / 100) - 1;
   tm0->tm_mday = (int) (date % 100);
   tm0->tm_hour = 0; 
   tm0->tm_min = 0; 
   tm0->tm_sec = 0;
   return (mktime (tm0));                      // converion struct tm en time_t
}

/*! return difference in hours between two zones (current zone and Wind zone) */
double zoneTimeDiff (Zone zone1, Zone zone0) {
   time_t time1 = dateToTime_t (zone1.dataDate [0]) + 3600 * (zone1.dataTime [0] / 100);
   time_t time0 = dateToTime_t (zone0.dataDate [0]) + 3600 * (zone0.dataTime [0] /100);   // add seconds
   return (time1 - time0) / 3600;
} 

/* returns number of second between beginning of grib to now */
time_t diffNowGribTime0 (Zone zone) {
   time_t now;
   return time (&now) - (dateToTime_t (zone.dataDate [0]) + 3600 * (zone.dataTime [0] / 100));
}

/*! print Grib information */
void printGrib (Zone zone, FlowP *gribData) {
   double t;
   int iGrib;
   for (size_t  k = 0; k < zone.nTimeStamp; k++) {
      t = zone.timeStamp [k];
      printf ("Time: %.0lf\n", t);
      for (int i = 0; i < zone.nbLat; i++) {
         for (int j = 0; j < zone.nbLon; j++) {
	         iGrib = (k * zone.nbLat * zone.nbLon) + (i * zone.nbLon) + j;
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

/*! check Grib inAformation 
    return true if something written in buffer */
bool checkGribToStr (char *buffer, Zone zone, FlowP *gribData) {
   //double t; 
   double u, v, lat, lon;
   int count = 0;
   int iGrib, n = 0, nSuspect = 0, nLatSuspect = 0, nLonSuspect = 0;
   char str [MAX_SIZE_LINE];
   strcpy (buffer, "\n");
   for (size_t i = 0; i < zone.nShortName; i++) {
      if ((strcmp (zone.shortName [i], "10u") == 0) || (strcmp (zone.shortName [i], "u") == 0)) count += 1;
      else if ((strcmp (zone.shortName [i], "10v") == 0)  || (strcmp (zone.shortName [i], "v") == 0)) count += 1;
      else if (strcmp (zone.shortName [i], "ucurr") == 0) count += 1;
      else if (strcmp (zone.shortName [i], "vcurr") == 0) count += 1;
   }
   if (count < 2) {
      strcpy (buffer, "No consistent info for shortname: 10u 10v ucurr vcurr");
      return true;
   }
   for (size_t k = 0; k < zone.nTimeStamp; k++) {
      //t = zone.timeStamp [k];
      for (int i = 0; i < zone.nbLat; i++) {
         for (int j = 0; j < zone.nbLon; j++) {
            n += 1,
            //iGrib = indexOf ((int) timeStep, lat, lon, zone);
	         iGrib = (k * zone.nbLat * zone.nbLon) + (i * zone.nbLon) + j;
            u = gribData [iGrib].u;
            v = gribData [iGrib].v;
            lat = zone.latMin + i * zone.latStep;
            lon = zone.lonLeft + j * zone.lonStep;
            if (fabs (lat - gribData [iGrib].lat) > (zone.latStep / 2.0)) {
               printf ("CheckGribStr strange: lat: %.2lf gribData lat: %.2lf\n", lat, gribData [iGrib].lat);
               nLatSuspect += 1;
            }
            if (fabs (lon - gribData [iGrib].lon) > (zone.lonStep / 2.0)) {
               printf ("CheckGribStr strange: lon: %.2lf gribData lon: %.2lf\n", lon, gribData [iGrib].lon);
               nLonSuspect += 1;
            }

            if ((u > 50) || (u < -50) || (v > 50) || (v < -50)) { 
               nSuspect += 1;
            }
         }
      }
   }
   if (n == 0) strcpy (str, "no value");
   else if ((nSuspect >0 ) || (nLatSuspect > 0) || (nLonSuspect > 0)) {
      sprintf (str, "n Values        : %10d\n", n);
      strcat (buffer, str);
      sprintf (str, "n suspect Values: %10d, ratio Val suspect: %.2lf %% \n", nSuspect, 100 * (double)nSuspect/(double) (n));
      strcat (buffer, str);
      sprintf (str, "n suspect Lat   : %10d, ratio Lat suspect: %.2lf %% \n", nLatSuspect, 100 * (double)nLatSuspect/(double) (n));
      strcat (buffer, str);
      sprintf (str, "n suspect Lon   : %10d, ratio Lon suspect: %.2lf %% \n", nLonSuspect, 100 * (double)nLonSuspect/(double) (n));
      strcat (buffer, str);
   }
   return (nSuspect > 0) || (nLatSuspect > 0) || (nLonSuspect > 0);
}

/*! find iTinf and iTsup in order t : zone.timeStamp [iTInf] <= t <= zone.timeStamp [iTSup] */
static inline void findTimeAround (double t, int *iTInf, int *iTSup, Zone zone) { 
   size_t k;
   if (t <= zone.timeStamp [0]) {
      *iTInf = 0;
      *iTSup = 0;
      return;
   }
   for (k = 0; k < zone.nTimeStamp; k++) {
      if (t == zone.timeStamp [k]) {
         *iTInf = k;
	      *iTSup = k;
	      return;
      }
      if (t < zone.timeStamp [k]) {
         *iTInf = k - 1;
         *iTSup = k;
         return;
      }
   }
   *iTInf = zone.nTimeStamp -1;
   *iTSup = zone.nTimeStamp -1;
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
   double *latMax, double *lonMin, double *lonMax, Zone zone) {
   *latMin = arrondiMin (lat, zone.latStep);
   *latMax = arrondiMax (lat, zone.latStep);
   *lonMin = arrondiMin (lon, zone.lonStep);
   *lonMax = arrondiMax (lon, zone.lonStep);
  
   // check limits
   if (zone.latMin > *latMin) *latMin = zone.latMin;
   if (zone.latMax < *latMax) *latMax = zone.latMax; 
   if (zone.lonLeft > *lonMin) *lonMin = zone.lonLeft; 
   if (zone.lonRight < *lonMax) *lonMax = zone.lonRight;
   
   if (zone.latMax < *latMin) *latMin = zone.latMax; 
   if (zone.latMin > *latMax) *latMax = zone.latMin; 
   if (zone.lonRight < *lonMin) *lonMin = zone.lonRight; 
   if (zone.lonLeft > *lonMax) *lonMax = zone.lonLeft;
}

/*! return indice of lat in gribData ot currentGribData */
static inline int indLat (double lat, Zone zone) {
   return (int) round ((lat - zone.latMin)/zone.latStep); // ATTENTION
}

/*! return indice of lon in gribData or currentGribData */
static inline int indLon (double lon, Zone zone) {
   return (int) round ((lon - zone.lonLeft)/zone.lonStep);
}

/*! interpolation to return tws at index time iT */
double findTwsByIt (Pp p, int iT0) {
   double latMin, latMax, lonMin, lonMax;
   double a, b, u0, v0;

   FlowP windP00, windP01, windP10, windP11;
   if ((!zone.wellDefined) || (zone.nbLat == 0) || (! extIsInZone (p, zone))) {
      return 0;
   }
   
   find4PointsAround (p.lat, p.lon, &latMin, &latMax, &lonMin, &lonMax, zone);
   
   windP00 = gribData [iT0 * zone.nbLat * zone.nbLon + indLat(latMax, zone) * zone.nbLon + indLon(lonMin, zone)];  
   windP01 = gribData [iT0 * zone.nbLat * zone.nbLon + indLat(latMax, zone) * zone.nbLon + indLon(lonMax, zone)];
   windP10 = gribData [iT0 * zone.nbLat * zone.nbLon + indLat(latMin, zone) * zone.nbLon + indLon(lonMax, zone)];
   windP11 = gribData [iT0 * zone.nbLat * zone.nbLon + indLat(latMin, zone) * zone.nbLon + indLon(lonMin, zone)];

   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.u, windP01.u);
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.u, windP11.u); 
   u0 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.v, windP01.v); 
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.v, windP11.v); 
   v0 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);

   return extTws (u0, v0);
}

/*! interpolation to get u, v, g (gust), w (waves) at point p and time t */
bool findFlow (Pp p, double t, double *rU, double *rV, double *rG, double *rW, Zone zone, FlowP *gribData) {
   double t0,t1;
   double latMin, latMax, lonMin, lonMax;
   double a, b, u0, u1, v0, v1, g0, g1, w0, w1;
   int iT0, iT1;
   FlowP windP00, windP01, windP10, windP11;
   if ((!zone.wellDefined) || (zone.nbLat == 0) || (! extIsInZone (p, zone)) || (t < 0)){
      *rU = 0, *rV = 0, *rG = 0; *rW = 0;
      return false;
   }
   
   findTimeAround (t, &iT0, &iT1, zone);
   find4PointsAround (p.lat, p.lon, &latMin, &latMax, &lonMin, &lonMax, zone);
   //printf ("\n\nFindWind\n");
   //printf ("lat %lf iT0 %d, iT1 %d, latMin %lf latMax %lf lonMin %lf lonMax %lf\n", p.lat, iT0, iT1, latMin, latMax, lonMin, lonMax);  
   // 4 points at time t0
   windP00 = gribData [iT0 * zone.nbLat * zone.nbLon + indLat(latMax, zone) * zone.nbLon + indLon(lonMin, zone)];  
   windP01 = gribData [iT0 * zone.nbLat * zone.nbLon + indLat(latMax, zone) * zone.nbLon + indLon(lonMax, zone)];
   windP10 = gribData [iT0 * zone.nbLat * zone.nbLon + indLat(latMin, zone) * zone.nbLon + indLon(lonMax, zone)];
   windP11 = gribData [iT0 * zone.nbLat * zone.nbLon + indLat(latMin, zone) * zone.nbLon + indLon(lonMin, zone)];

   // printf ("00lat %lf  01lat %lf  10lat %lf 11lat %lf\n", windP00.lat, windP01.lat,windP10.lat,windP11.lat); 
   // printf ("00tws %lf  01tws %lf  10tws %lf 11tws %lf\n", windP00.tws, windP01.tws,windP10.tws,windP11.tws); 
   // speed longitude interpolation at northern latitudes
  
   // interpolation  for u, v, w, g
   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.u, windP01.u);
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.u, windP11.u); 
   u0 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.v, windP01.v); 
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.v, windP11.v); 
   v0 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.g, windP01.g); 
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.g, windP11.g); 
   g0 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);
    
   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.w, windP01.w); 
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.w, windP11.w); 
   w0 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);
    
   //printf ("lat %lf a %lf  b %lf  00lat %lf 10lat %lf  %lf\n", p.lat, a, b, windP00.lat, windP10.lat, tws0); 

   // 4 points at time t1
   windP00 = gribData [iT1 * zone.nbLat * zone.nbLon + indLat(latMax, zone) * zone.nbLon + indLon(lonMin, zone)];  
   windP01 = gribData [iT1 * zone.nbLat * zone.nbLon + indLat(latMax, zone) * zone.nbLon + indLon(lonMax, zone)];
   windP10 = gribData [iT1 * zone.nbLat * zone.nbLon + indLat(latMin, zone) * zone.nbLon + indLon(lonMax, zone)];
   windP11 = gribData [iT1 * zone.nbLat * zone.nbLon + indLat(latMin, zone) * zone.nbLon + indLon(lonMin, zone)];

   // interpolatuon for  for u, v
   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.u, windP01.u);
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.u, windP11.u); 
   u1 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.v, windP01.v); 
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.v, windP11.v); 
   v1 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);
    
   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.g, windP01.g); 
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.g, windP11.g); 
   g1 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (p.lon, windP00.lon, windP01.lon, windP00.w, windP01.w); 
   b = interpolate (p.lon, windP10.lon, windP11.lon, windP10.w, windP11.w); 
   w1 = interpolate (p.lat, windP00.lat, windP10.lat, a, b);
   
   // finally, inderpolation twd tws between t0 and t1
   t0 = zone.timeStamp [iT0];
   t1 = zone.timeStamp [iT1];
   *rU = interpolate (t, t0, t1, u0, u1);
   *rV = interpolate (t, t0, t1, v0, v1);
   *rG = interpolate (t, t0, t1, g0, g1);
   *rW = interpolate (t, t0, t1, w0, w1);

   return true;
}

/*! Read grib fill and fill gribData tables */
/*! Read lists zone.timeStamp, shortName, zone.dataDate, dataTime before full grib reading */
static bool readGribLists (char *fileName, Zone *zone) {
   codes_index* index = NULL;
   int ret = 0;
   //memset (zone, 0,  sizeof (Zone));
    
   /* Create an index given set of keys*/
   index = codes_index_new (0,"dataDate,dataTime,shortName,level,number,step",&ret);
   if (ret) {
      fprintf (stderr, "Error in readGribLists index: %s\n",codes_get_error_message(ret)); 
      return false;
   }
   //printf ("In readGreablists 1\n");
   /* Indexes a file */
   ret = codes_index_add_file (index, fileName);
   if (ret) {
       fprintf (stderr, "Error in readGribLists ret: %s\n",codes_get_error_message(ret)); 
       return false;
   }
   //printf ("In readGreablists 2\n");
 
   /* get the number of distinct values of "step" in the index */
   CODES_CHECK (codes_index_get_size (index,"step", &zone->nTimeStamp),0);
    
   /* get the list in ascending order of distinct steps from the index */
   CODES_CHECK (codes_index_get_long (index,"step", zone->timeStamp, &zone->nTimeStamp),0);
   
   CODES_CHECK(codes_index_get_size(index,"shortName",&zone->nShortName),0);
   
   if (zone->shortName != NULL) free (zone->shortName); 
   if ((zone->shortName = (char**) malloc (sizeof(char*)*zone->nShortName)) == NULL) {
      fprintf (stderr, "Routing Error in readGribLists Malloc zone.shortName\n");
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
static bool readGribParameters (char *fileName, Zone *zone) {
   int err = 0;
   FILE* f = NULL;
   double lat1, lat2;
   codes_handle *h = NULL;
 
   if ((f = fopen (fileName, "r")) == NULL) {
       fprintf(stderr, "ERROR: unable to open file %s\n",fileName);
       return false;
   }
 
   /* create new handle from the first message in the file*/
   h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err);
   if (h == NULL) {
       fprintf (stderr, "Error: unable to create handle from file %s\n",fileName);
       fprintf (stderr, "Error in readGribLists ret: %s\n",codes_get_error_message(err)); 
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
      fprintf (stderr, "In readGribParameters zone.nbLat exceed MAX_N_GRIB_LAT %d\n", MAX_N_GRIB_LAT);
      return false;
   }
   if (zone->nbLon > MAX_N_GRIB_LON) {
      fprintf (stderr, "In reasGribParameters zone.nbLon exceed MAX_N_GRIB_LON %d\n", MAX_N_GRIB_LON);
      return false;
   }
 
   /* get as a double*/
   CODES_CHECK(codes_get_double (h,"latitudeOfFirstGridPointInDegrees",&lat1),0);
   CODES_CHECK(codes_get_double (h,"longitudeOfFirstGridPointInDegrees",&zone->lonLeft),0);
   CODES_CHECK(codes_get_double (h,"latitudeOfLastGridPointInDegrees",&lat2),0);
   CODES_CHECK(codes_get_double (h,"longitudeOfLastGridPointInDegrees",&zone->lonRight),0);
   if (zone->lonLeft >= 180) zone->lonLeft -= 360;
   if (zone->lonRight >= 180) zone->lonRight -= 360;
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

/* find index in gribData table */
static inline int indexOf (int timeStep, double lat, double lon, Zone zone) {
   int iLat = indLat (lat, zone);
   int iLon = indLon (lon, zone);
   int iT = -1;
   for (iT = 0; iT < (int) zone.nTimeStamp; iT++) {
      if (timeStep == zone.timeStamp [iT])
         break;
   }
   if (iT == -1) {
      fprintf (stderr, "indexOf Error: cannot find index of time: %d\n", timeStep);
      return -1;
   }
   //printf ("iT: %d iLon :%d iLat: %d\n", iT, iLon, iLat); 
   return  (iT * zone.nbLat * zone.nbLon) + (iLat * zone.nbLon) + iLon;
}

/*! read grib file using eccodes C API*/
void *readGrib () {
   FILE* f = NULL;
   int err = 0, iGrib = 0;
   long bitmapPresent  = 0, timeStep;
   double lat, lon, val;
   char shortName [MAX_SIZE_SHORT_NAME];
   size_t lenName = MAX_SIZE_SHORT_NAME;
   
   zone.wellDefined = false;

   if (! readGribLists (par.gribFileName, &zone)) {
      readGribRet = 0;
      return NULL;
   }
   if (! readGribParameters (par.gribFileName, &zone)) {
      readGribRet = 0;
      return NULL;
   }
   if (zone.nShortName < 2) {
      readGribRet = 0;
      fprintf (stderr, "readGrib ShortName not presents in: %s\n", par.gribFileName);
      return NULL;
   }
   if (gribData != NULL) free (gribData); 
   if ((gribData = calloc (sizeof(FlowP),  zone.nTimeStamp * zone.nbLat * zone.nbLon)) == NULL) {
      readGribRet = 0;
      fprintf (stderr, "readGrib Error, calloc gribData\n");
      return NULL;
   }
   printf ("In readGrib: %ld allocated\n", sizeof(FlowP) * zone.nTimeStamp * zone.nbLat * zone.nbLon);
   
   /* Message handle. Required in all the ecCodes calls acting on a message.*/
   codes_handle* h = NULL;
   /* Iterator on lat/lon/values.*/
   codes_iterator* iter = NULL;
   if ((f = fopen (par.gribFileName, "rb")) == NULL) {
       fprintf(stderr, "readGrib Error: unable to open file %s\n", par.gribFileName);
       return NULL;
   }
   zone.nMessage = 0;
 
   /* Loop on all the messages in a file.*/
   while ((h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err)) != NULL) {
      // printf ("n = %d\n", n);
      if (err != CODES_SUCCESS) CODES_CHECK(err, 0);

      /* Check if a bitmap applies */
      CODES_CHECK(codes_get_long(h, "bitmapPresent", &bitmapPresent), 0);
      if (bitmapPresent) {
          CODES_CHECK(codes_set_double(h, "missingValue", MISSING), 0);
      }

      lenName = MAX_SIZE_SHORT_NAME;
      CODES_CHECK(codes_get_string(h, "shortName", shortName, &lenName), 0);
      CODES_CHECK(codes_get_long(h, "step", &timeStep), 0);
      // printf ("shortname :%s, timeStep %ld\n", shortName, timeStep);
 
      /* A new iterator on lat/lon/values is created from the message handle h. */
      iter = codes_grib_iterator_new(h, 0, &err);
      if (err != CODES_SUCCESS) CODES_CHECK(err, 0);
 
      /* Loop on all the lat/lon/values. */
      while (codes_grib_iterator_next(iter, &lat, &lon, &val)) {
         // printf("%.2f %.2f %.2lf %ld %s\n", lat, lon, val, timeStep, shortName);
         if (lon > 180) lon -= 360;
         iGrib = indexOf ((int) timeStep, lat, lon, zone);
         if (iGrib == -1) return NULL;
         gribData [iGrib].lat = lat; 
         gribData [iGrib].lon = lon;  
         if ((strcmp (shortName, "10u") == 0) || (strcmp (shortName, "u") == 0))
            gribData [iGrib].u = val;
         else if ((strcmp (shortName, "10v") == 0) || (strcmp (shortName, "v") == 0))
            gribData [iGrib].v = val;
         else if (strcmp (shortName, "gust") == 0) // waves
            gribData [iGrib].g = val;
         else if (strcmp (shortName, "swh") == 0) // waves
            gribData [iGrib].w = val;
         // printf ("%7.2lf; %7.2lf; %.0lf; %7.2lf\n", lat, lon, timeStep, val);
      }
      codes_grib_iterator_delete (iter);
      codes_handle_delete (h);
      zone.nMessage += 1;
   }
 
   fclose (f);
   readGribRet = 1;
   zone.wellDefined = true;
   return NULL;
}
   
/*! read grib file current using command line from eccodes */
void *readCurrentGrib () {
   FILE* f = NULL;
   int err = 0, iGrib = 0;
   long bitmapPresent  = 0, timeStep;
   double lat, lon, val;
   char shortName [MAX_SIZE_SHORT_NAME];
   size_t lenName = MAX_SIZE_SHORT_NAME;
   
   currentZone.wellDefined = false;

   if (! readGribLists (par.currentGribFileName, &currentZone)) {
      readCurrentGribRet = 0;
      return NULL;
   }
   if (! readGribParameters (par.currentGribFileName, &currentZone)) {
      readCurrentGribRet = 0;
      return NULL;
   }
   if (currentZone.nShortName < 2) {
      fprintf (stderr, "readGrib current ShortName not presents in: %s\n", par.currentGribFileName);
      readCurrentGribRet = 0;
      return NULL;
   }

   if (currentGribData != NULL) free (currentGribData);
   if ((currentGribData = calloc (sizeof(FlowP),  currentZone.nTimeStamp * currentZone.nbLat * currentZone.nbLon)) == NULL) {
      fprintf (stderr, "readCurrentGrib Error, calloc currentGribData\n");
      readCurrentGribRet = 0;
      return NULL;
   }
   printf ("In readCurrentGrib: %ld allocated\n", sizeof(FlowP) * currentZone.nTimeStamp * currentZone.nbLat * currentZone.nbLon);

   /* Message handle. Required in all the ecCodes calls acting on a message.*/
   codes_handle* h = NULL;
   /* Iterator on lat/lon/values.*/
   codes_iterator* iter = NULL;
   if ((f = fopen (par.currentGribFileName, "rb")) == NULL) {
      fprintf(stderr, "readCurrentGrib Error: unable to open file %s\n", par.currentGribFileName);
      readCurrentGribRet = 0;
      return NULL;
   }
   currentZone.nMessage = 0;
   /* Loop on all the messages in a file.*/
   while ((h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err)) != NULL) {
      if (err != CODES_SUCCESS) CODES_CHECK(err, 0);

      /* Check if a bitmap applies */
      CODES_CHECK(codes_get_long(h, "bitmapPresent", &bitmapPresent), 0);
      if (bitmapPresent) {
          CODES_CHECK(codes_set_double(h, "missingValue", MISSING), 0);
      }

      lenName = MAX_SIZE_SHORT_NAME;
      CODES_CHECK(codes_get_string(h, "shortName", shortName, &lenName), 0);
      CODES_CHECK(codes_get_long(h, "step", &timeStep), 0);
      // printf ("shortname :%s, timeStep %ld\n", shortName, timeStep);
 
      /* A new iterator on lat/lon/values is created from the message handle h. */
      iter = codes_grib_iterator_new(h, 0, &err);
      if (err != CODES_SUCCESS) CODES_CHECK(err, 0);
 
      /* Loop on all the lat/lon/values. */
      while (codes_grib_iterator_next(iter, &lat, &lon, &val)) {
          //printf("%.2f %.2f %.2lf %ld %s\n", lat, lon, val, timeStep, shortName);
         if (lon > 180) lon -= 360;
         iGrib = indexOf ((int) timeStep, lat, lon, currentZone);
         if (iGrib == -1) return NULL;
         currentGribData [iGrib].lat = lat; 
         currentGribData [iGrib].lon = lon;  
      
         if (strcmp (shortName, "ucurr") == 0)
            currentGribData [iGrib].u = val;
         else if (strcmp (shortName, "vcurr") == 0)
            currentGribData [iGrib].v = val;
         // printf ("%7.2lf; %7.2lf; %.0lf; %7.2lf\n", lat, lon, timeStep, val);
      }
      codes_grib_iterator_delete(iter);
      codes_handle_delete(h);
      currentZone.nMessage += 1;
   }
 
   fclose (f);
   readCurrentGribRet = 1;
   currentZone.wellDefined = true;
   return NULL;
}
   
/*! write Grib information in string */
char *gribToStr (char *str, Zone zone) {
   char line [MAX_SIZE_LINE] = "";
   char strTmp [MAX_SIZE_LINE] = "";
   char strLat0 [MAX_SIZE_NAME] = "", strLon0 [MAX_SIZE_NAME] = "";
   char strLat1 [MAX_SIZE_NAME] = "", strLon1 [MAX_SIZE_NAME] = "";
   char centreName [MAX_SIZE_NAME] = "";
   
   for (size_t i = 0; i < sizeof(dicTab) / sizeof(dicTab[0]); i++) // search name of center
     if (dicTab [i].id == zone.centreId) strcpy (centreName, dicTab [i].name);
         
   newDate (zone.dataDate [0], zone.dataTime [0]/100, strTmp);
   sprintf (str,  "Centre ID: %ld %s   %s   Ed number: %ld\nnMessages: %d\nstepUnits: %ld\n# values : %ld\n",\
      zone.centreId, centreName, strTmp, zone.editionNumber, zone.nMessage, zone.stepUnits, zone.numberOfValues);
   sprintf (line, "Zone From: %s, %s To: %s, %s\n", \
      latToStr (zone.latMin, par.dispDms, strLat0), lonToStr (zone.lonLeft, par.dispDms, strLon0),\
      latToStr (zone.latMax, par.dispDms, strLat1), lonToStr (zone.lonRight, par.dispDms, strLon1));
   strcat (str, line);
   sprintf (line, "LatStep  : %04.4f° LonStep: %04.4f°\n", zone.latStep, zone.lonStep);
   strcat (str, line);
   sprintf (line, "Nb Lat   : %ld      Nb Lon : %ld\n", zone.nbLat, zone.nbLon);
   strcat (str, line);
   if (zone.nTimeStamp < 8) {
      sprintf (line, "TimeStamp List of %ld : [ ", zone.nTimeStamp);
      strcat (str, line);
      for (size_t k = 0; k < zone.nTimeStamp; k++) {
         sprintf (line, "%ld ", zone.timeStamp [k]);
         strcat (str, line);
      }
      strcat (str, "]\n");
   }
   else {
      sprintf (line, "TimeStamp List of %ld : [%ld, %ld, ..%ld]\n", zone.nTimeStamp,\
         zone.timeStamp [0], zone.timeStamp [1], zone.timeStamp [zone.nTimeStamp - 1]);
      strcat (str, line);
   }

   sprintf (line, "Shortname List: [ ");
   strcat (str, line);
   for (size_t k = 0; k < zone.nShortName; k++) {
      sprintf (line, "%s ", zone.shortName [k]);
      strcat (str, line);
   }
   strcat (str, "]\n");
   if ((zone.nDataDate > 1) || (zone.nDataTime > 1)) {
      sprintf (line, "Warning number of Date: %ld, number of Time: %ld\n", zone.nDataDate, zone.nDataTime);
      strcat (str, line);
   }
   sprintf (line, "Zone is       :  %s\n", (zone.wellDefined) ? "Well defined" : "Undefined");
   strcat (str, line);
   return str;
}

/* read polar file and fill poLMat matrix */
bool readPolar (char *fileName, PolMat *mat) {
   FILE *f;
   char buffer [MAX_SIZE_LINE];
   char *pLine = &buffer [0];
   char *strToken;
   double v = 0;
   double lastValInCol0 = -1;
   int c;
   mat->nLine = 0;
   mat->nCol = 0;
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "Error readPolar cannot open: %s\n", fileName);
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
         fprintf (stderr, "Error readPolar: max number of line: %d\n", MAX_N_POL_MAT_LINES);
         return false;
      }
      if (mat->nCol < c) mat->nCol = c; // max
   }
   mat -> t [0][0] = -1;
   fclose (f);
   return true;
}

/*! return the max value found in polar */
double maxValInPol (PolMat mat) {
   double max = 0.0;
   for (int i = 1; i < mat.nLine; i++)
      for (int j = 1; j < mat.nCol; j++) 
         if (mat.t [i][j] > max) max = mat.t [i][j];
   return max;
}

/*! write polar information in string */
char *polToStr (char * str, PolMat mat) {
   char line [MAX_SIZE_LINE] = "";
   str [0] = '\0';
   for (int i = 0; i < mat.nLine; i++) {
      for (int j = 0; j < mat.nCol; j++) {
         sprintf (line, "%6.2f ", mat.t [i][j]);
	      strcat (str, line);
      }
      strcat (str, "\n");
   }
   sprintf (line, "Number of rows in polar : %d\n", mat.nCol);
   strcat (str, line);
   sprintf (line, "Number of lines in polar: %d\n", mat.nLine);
   strcat (str, line);
   sprintf (line, "Max                     : %.2lf\n", maxValInPol (mat));
   strcat (str, line);
   return str;
}

/*! copy a text file in a string */
bool fileToStr (char* fileName, char *str) {
   FILE *f;
   char line [MAX_SIZE_LINE] = "";
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "Error: fileToStr cannot open: %s\n", fileName);
      return (false);
   }
   while ((fgets (line, MAX_SIZE_LINE, f) != NULL ))
      strcat (str, line);
   fclose (f);
   return true;
}

/*! replace $ by sequence */
void dollarReplace (char* str) {
   char res [MAX_SIZE_LINE];
   size_t len = 0;
   if (str == NULL) return;
   for (size_t i = 0; str[i] != '\0'; ++i) {
      if (str[i] == '$') {
         res[len++] = '\\';
         res[len++] = '$';
      } else {
         res[len++] = str[i];
      }
   }
   res[len] = '\0';
   strcpy (str, res);
}

/*! find name, lat and lon */
void analyseCoord (char *str, char* name, double *lat, double *lon) {
   char *pt = NULL;
   name [0] = '\0';
   strip (str);
   if (isNumber (str)) {
      pt = strchr (str, ',');
      *lon = getCoord (strchr (str, ',') +1);
      *pt = '\0'; // pas joli
      *lat = getCoord (str);
   }
}

/*! read parameter file and build par struct */
bool readParam (const char *fileName) {
   FILE *fp;
   char *pt;
   char str [MAX_SIZE_LINE], str1 [MAX_SIZE_LINE];
   char buffer [MAX_SIZE_BUFFER];
   char *pLine = &buffer [0];
   int poiIndex = -1;
   if ((fp = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "Routing Error in readParam cannot open: %s\n", fileName);
      return false;
   }
   memset (&par, 0, sizeof (Par));
   par.opt = 1;
   par.tStep = 3;
   par.cogStep = 5;
   par.rangeCog = 90;
   par.maxIso = MAX_SIZE_ISOC;
   par.efficiency = 1;
   par.kFactor = 20;
   par.minPt = 2;
   route.n = 0;

   while (fgets (pLine, MAX_SIZE_BUFFER, fp) != NULL ) {
      str [0] = '\0';
      while (isspace (*pLine)) pLine++;
      // printf ("%s", pLine);
      if ((!*pLine) || *pLine == '#' || *pLine == '\n') continue;
      if ((pt = strchr (pLine, '#')) != NULL) // comment elimination
         *pt = '\0';
      if (sscanf (pLine, "WD:%s", par.workingDir) > 0);
      else if (sscanf (pLine, "POI:%s", str) > 0) {// POI should be analysed before POR: and PDEST
         buildRootName (str, par.poiFileName);
         nPoi = readPoi (par.poiFileName);
      }
      else if (strstr (pLine, "POR:") != NULL) {
         analyseCoord (strchr (pLine, ':') + 1, par.pOrName, &par.pOr.lat, &par.pOr.lon);
         if (par.pOr.lon > 180) par.pOr.lon -= 360;
         par.pOr.id = -1;
         par.pOr.father = -1;
      } 
      else if (strstr (pLine, "PDEST:") != NULL) {
         analyseCoord (strchr (pLine, ':') + 1, par.pDestName, &par.pDest.lat, &par.pDest.lon);
         if (par.pDest.lon > 180) par.pDest.lon -= 360;
         par.pDest.id = 0;
         par.pDest.father = 0;
      }
      else if (sscanf (pLine, "POR_NAME:%s", par.pOrName) > 0) {
         if ((poiIndex = findPoiByName (par.pOrName, &par.pOr.lat, &par.pOr.lon)) != -1)
            strcpy (par.pOrName, tPoi [poiIndex].name);
         else par.pOrName [0] = '\0';
      }
      else if (sscanf (pLine, "PDEST_NAME:%s", par.pDestName) > 0) {
         if ((poiIndex = findPoiByName (par.pDestName, &par.pDest.lat, &par.pDest.lon)) != -1)
            strcpy (par.pDestName, tPoi [poiIndex].name);
         else par.pDestName [0] = '\0';
      }
      else if (sscanf (pLine, "GRIB_LAT_STEP:%lf", &par.gribLatStep) > 0);
      else if (sscanf (pLine, "GRIB_LON_STEP:%lf", &par.gribLonStep) > 0);
      else if (sscanf (pLine, "GRIB_TIME_STEP:%d", &par.gribTimeStep) > 0);
      else if (sscanf (pLine, "GRIB_TIME_MAX:%d", &par.gribTimeMax) > 0);
      else if (sscanf (pLine, "CGRIB:%s", str) > 0)
         buildRootName (str, par.gribFileName);
      else if (sscanf (pLine, "CURRENT_GRIB:%s", str) > 0)
         buildRootName (str, par.currentGribFileName);
      else if (sscanf (pLine, "WAVE_POL:%s", str) > 0)
         buildRootName (str, par.wavePolFileName);
      else if (sscanf (pLine, "POLAR:%s", str) > 0)
         buildRootName (str, par.polarFileName);
      else if (sscanf (pLine, "ISSEA:%s", str) > 0)
         buildRootName (str, par.isSeaFileName);
      else if (sscanf (pLine, "CLI_HELP:%s", str) > 0)
         buildRootName (str, par.cliHelpFileName);
      else if (sscanf (pLine, "HELP:%s", par.helpFileName) > 0);
      else if (sscanf (pLine, "SMTP_SCRIPT:%s%s", par.smtpScript, str) > 0) {
         strcat (par.smtpScript, " ");
         strcat (par.smtpScript, str);
      }
      else if (sscanf (pLine, "SMTP_TO:%s %s", str, str1) > 1) {
         if (par.nSmtp < MAX_N_SMTP_TO) {
            strcpy (par.smtpName [par.nSmtp], str);
            strcpy (par.smtpTo [par.nSmtp], str1);
            par.nSmtp += 1;
         }
         else
            fprintf (stderr, "In readParam, number max of SMTP_TO reached: %d\n", par.nSmtp);
      }
      else if (sscanf (pLine, "IMAP_TO_SEEN:%s%s", par.imapToSeen, str) > 0) {
         strcat (par.imapToSeen, " ");
         strcat (par.imapToSeen, str);
      }
      else if (sscanf (pLine, "IMAP_SCRIPT:%s%s", par.imapScript, str) > 0) {
         strcat (par.imapScript, " ");
         strcat (par.imapScript, str);
      }
      else if (sscanf (pLine, "SHP:%s", str) > 0) {
         buildRootName (str, par.shpFileName [par.nShpFiles]);
         par.nShpFiles += 1;
         if (par.nShpFiles >= MAX_N_SHP_FILES) 
            fprintf (stderr, "In readParam, number max of SHP files reached: %d\n", par.nShpFiles);
      }
      else if (sscanf (pLine, "START_TIME:%lf", &par.startTimeInHours) > 0);
      else if (sscanf (pLine, "T_STEP:%lf", &par.tStep) > 0);
      else if (sscanf (pLine, "RANGE_COG:%d", &par.rangeCog) > 0);
      else if (sscanf (pLine, "COG_STEP:%d", &par.cogStep) > 0);
      else if (sscanf (pLine, "MAX_ISO:%d", &par.maxIso) > 0);
      else if (sscanf (pLine, "VERBOSE:%d", &par.verbose) > 0);
      else if (sscanf (pLine, "MOTOR_S:%lf", &par.motorSpeed) > 0);
      else if (sscanf (pLine, "THRESHOLD:%lf", &par.threshold) > 0);
      else if (sscanf (pLine, "EFFICIENCY:%lf", &par.efficiency) > 0);
      else if (sscanf (pLine, "CONST_WAVE:%lf", &par.constWave) > 0);
      else if (sscanf (pLine, "CONST_WIND_TWS:%lf", &par.constWindTws) > 0);
      else if (sscanf (pLine, "CONST_WIND_TWD:%lf", &par.constWindTwd) > 0);
      else if (sscanf (pLine, "CONST_CURRENT_S:%lf", &par.constCurrentS) > 0);
      else if (sscanf (pLine, "CONST_CURRENT_D:%lf", &par.constCurrentD) > 0);
      else if (sscanf (pLine, "DUMPI:%s", str) > 0)
         buildRootName (str, par.dumpIFileName);
      else if (sscanf (pLine, "DUMPR:%s", str) > 0)
         buildRootName (str, par.dumpRFileName);
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
      else if (sscanf (pLine, "CURRENT_DISP:%d", &par.currentDisp) > 0);
      else if (sscanf (pLine, "WAVE_DISP:%d", &par.waveDisp) > 0);
      else if (sscanf (pLine, "MAIL_PW:%s", par.mailPw) > 0);
      else if (strstr (pLine, "EDITOR:")) {
         pLine += 7;
         while (isspace (*pLine)) pLine ++;
         strcpy (par.editor, pLine);
         strip (par.editor);
      }
      else printf ("Cannot interpret: %s\n", pLine);
   }
   if (par.mailPw [0] != '\0')
      dollarReplace (par.mailPw);
   if (par.maxIso > MAX_N_ISOC) par.maxIso = MAX_N_ISOC;
   if (par.constWindTws != 0) initConst (&zone);
   if ((tIsSea == NULL) && (par.isSeaFileName [0] != '\0'))
      readIsSea (par.isSeaFileName);
   for (int i= 0; i < par.nShpFiles; i++)
      initSHP (par.shpFileName [i]);
   printf ("Editor: %s\n", par.editor); 
   printf ("Working dir: %s \n", par.workingDir); 
   fclose (fp);
   return true;
}

/*! write parameter file from fill struct par */
bool writeParam (const char *fileName, bool header) {
   FILE *f;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "Routing Error in writeParam cannot write: %s\n", fileName);
      return false;
   }
   if (header) 
      fprintf (f, "Name             Value\n");
   fprintf (f, "WD:              %s\n", par.workingDir);
   fprintf (f, "POR:             %.2lf,%.2lf\n", par.pOr.lat, par.pOr.lon);
   fprintf (f, "PDEST:           %.2lf,%.2lf\n", par.pDest.lat, par.pDest.lon);
   if (par.pOrName [0] != '\0')
      fprintf (f, "POR_NAME:        %s\n", par.pOrName);
   if (par.pDestName [0] != '\0')
      fprintf (f, "PDEST_NAME:        %s\n", par.pDestName);
   fprintf (f, "CGRIB:           %s\n", par.gribFileName);
   if (par.currentGribFileName [0] != '\0')
      fprintf (f, "CURRENT_GRIB:    %s\n", par.currentGribFileName);
   fprintf (f, "GRIB_LAT_STEP:   %.1lf\n", par.gribLatStep);
   fprintf (f, "GRIB_LON_STEP:   %.1lf\n", par.gribLonStep);
   fprintf (f, "GRIB_TIME_STEP:  %d\n", par.gribTimeStep);
   fprintf (f, "GRIB_TIME_MAX:   %d\n", par.gribTimeMax);
   fprintf (f, "POLAR:           %s\n", par.polarFileName);
   fprintf (f, "WAVE_POL:        %s\n", par.wavePolFileName);
   fprintf (f, "ISSEA:           %s\n", par.isSeaFileName);
   fprintf (f, "POI:             %s\n", par.poiFileName);
   fprintf (f, "HELP:            %s\n", par.helpFileName);
   fprintf (f, "CLI_HELP:        %s\n", par.cliHelpFileName);
   for (int i = 0; i < par.nShpFiles; i++)
      fprintf (f, "SHP:             %s\n", par.shpFileName [i]);

   fprintf (f, "START_TIME:      %.2lf\n", par.startTimeInHours);
   fprintf (f, "T_STEP:          %.2lf\n", par.tStep);
   fprintf (f, "RANGE_COG:       %d\n", par.rangeCog);
   fprintf (f, "COG_STEP:        %d\n", par.cogStep);
   fprintf (f, "MAX_ISO:         %d\n", par.maxIso);
   fprintf (f, "VERBOSE:         %d\n", par.verbose);
   fprintf (f, "PENALTY0:        %.2lf\n", par.penalty0);
   fprintf (f, "PENALTY1:        %.2lf\n", par.penalty1);
   fprintf (f, "MOTOR_S:         %.2lf\n", par.motorSpeed);
   fprintf (f, "THRESHOLD:       %.2lf\n", par.threshold);
   fprintf (f, "EFFICIENCY:      %.2lf\n", par.efficiency);

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
   fprintf (f, "OPT:             %d\n", par.opt);
   fprintf (f, "ISOC_DISP:       %d\n", par.style);
   fprintf (f, "COLOR_DISP:      %d\n", par.showColors);
   fprintf (f, "DMS_DISP:        %d\n", par.dispDms);
   fprintf (f, "WIND_DISP:       %d\n", par.windDisp);
   fprintf (f, "CURRENT_DISP:    %d\n", par.currentDisp);
   fprintf (f, "WAVE_DISP:       %d\n", par.waveDisp);
   fprintf (f, "MAX_THETA:       %.2lf\n", par.maxTheta);
   fprintf (f, "J_FACTOR:        %d\n", par.jFactor);
   fprintf (f, "K_FACTOR:        %d\n", par.kFactor);
   fprintf (f, "MIN_PT:          %d\n", par.minPt);
   fprintf (f, "N_SECTORS:       %d\n", par.nSectors);
   fprintf (f, "SMTP_SCRIPT:     %s\n", par.smtpScript);
   for (int i = 0; i < par.nSmtp; i++)
      fprintf (f, "SMTP_TO:         %s %s\n", par.smtpName [i], par.smtpTo [i]);

   fprintf (f, "IMAP_TO_SEEN:    %s\n", par.imapToSeen);
   fprintf (f, "IMAP_SCRIPT:     %s\n", par.imapScript);
   fprintf (f, "EDITOR:          %s\n", par.editor);
   
   fclose (f);
   return true;
}

/*! write gps information in buffer */
bool gpsToStr (char *buffer) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   if (isnan (my_gps_data.lon) || isnan (my_gps_data.lat)) {
      strcpy (buffer, "No GPS data available\n");
      return false;
   }
   sprintf (buffer, "Position: %s %s\n", \
      latToStr (my_gps_data.lat, par.dispDms, strLat), lonToStr (my_gps_data.lon, par.dispDms, strLon));
   sprintf (line, "Altitude: %.2f\n", my_gps_data.alt);
   strcat (buffer, line);
   sprintf (line, "Status: %d\n", my_gps_data.status);
   strcat (buffer, line);
   sprintf (line, "Number of satellites: %d\n", my_gps_data.nSat);
   strcat (buffer, line);
   struct tm *timeinfo = gmtime (&my_gps_data.timestamp.tv_sec);

   sprintf(line, "GPS Time: %d-%02d-%02d %02d:%02d:%02d UTC\n", 
      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
   strcat (buffer, line);
   return true;
}

/*! Fonction de lecture du GPS exécutée dans un thread */
void *gps_thread_function(void *data) {
   struct ThreadData *thread_data = (struct ThreadData *)data;

   while (true) {
      if (gps_waiting (&thread_data->gps_data, MILLION)) {
         my_gps_data.ret = false;
         if (gps_read (&thread_data->gps_data, NULL, 0) == -1) {
            fprintf(stderr, "Error: GPS read failed.\n");
         } else if (thread_data->gps_data.set & PACKET_SET) {
            // Verrouillez la structure MyGpsData avant la mise à jour
            pthread_mutex_lock(&gps_data_mutex);

            // Mise à jour des données GPS
            my_gps_data.lat = thread_data->gps_data.fix.latitude;
            my_gps_data.lon = thread_data->gps_data.fix.longitude;
            my_gps_data.alt = thread_data->gps_data.fix.altitude;
            my_gps_data.status = thread_data->gps_data.fix.status;
            my_gps_data.nSat = thread_data->gps_data.satellites_visible;
            my_gps_data.timestamp = thread_data->gps_data.fix.time;
            my_gps_data.ret = true;
            // Déverrouillez la structure MyGpsData après la mise à jour
            pthread_mutex_unlock(&gps_data_mutex);
         }
      }
      // Ajoutez un délai ou utilisez une approche plus avancée
      usleep (MILLION); // Délai de 0.5 seconde
   }
   return NULL;
}

/*! GPS init */
bool initGPS () {
   // Initialisez les données du thread
   memset(&thread_data, 0, sizeof(struct ThreadData));
   thread_data.my_gps_data = my_gps_data;

   // Ouvrez la connexion GPS
   if (gps_open("localhost", GPSD_TCP_PORT, &thread_data.gps_data) == -1) {
      fprintf(stderr, "Error: Unable to connect to GPSD.\n");
      return false;
   }

   gps_stream(&thread_data.gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

   // Créez un thread pour la lecture du GPS
   if (pthread_create(&gps_thread, NULL, gps_thread_function, &thread_data) != 0) {
      fprintf(stderr, "Error: Unable to create GPS thread.\n");
      return false;
   }
   return true;
}

/*! GPS close */
void closeGPS () {
   // Fermez la connexion GPS et terminez le thread GPS lors de la fermeture de l'application
   gps_stream(&thread_data.gps_data, WATCH_DISABLE, NULL);
   gps_close(&thread_data.gps_data);
   pthread_cancel(gps_thread);
   pthread_join(gps_thread, NULL);
}

/*! call back for curlGet */
static size_t WriteCallback (void* contents, size_t size, size_t nmemb, void* userp) {
   FILE* fp = (FILE*)userp;
   return fwrite (contents, size, nmemb, fp);
}

/*! URL for METEO Consult Wind 4 hours before now at closest time 0Z, 6Z, 12Z, or 18Z 
	Current 12 hours before new at 0Z */
char *buildMeteoUrl (int type, int i, char *url) {
   time_t meteoTime = time (NULL) - (3600 * delay [type]);
   struct tm * timeInfos = gmtime(&meteoTime);  // time 4 hours ago
   int mm = timeInfos->tm_mon + 1;
   int dd = timeInfos->tm_mday;
   int hh = (timeInfos->tm_hour / 6) * 6;    // select 0 or 6 or 12 por 18
   if (type == CURRENT) {
      sprintf (url, CURRENT_URL [i*2 + 1], ROOT_GRIB_URL, 0, mm, dd); // allways 0
   }
   else {
      sprintf (url, WIND_URL [i*2 + 1], ROOT_GRIB_URL, hh, mm, dd);
   }
   return url;
}
/*! return true if URL has been dowlmoaded */
bool curlGet (char *url, char* outputFile) {
   long http_code;
   CURL* curl = curl_easy_init();
   if (!curl) {
      fprintf (stderr, "In curlGet,  Error: impossible to initialize curl\n");
      return false;
   }
   FILE* fp = fopen (outputFile, "wb");
   if (!fp) {
      fprintf (stderr, "In curlGet, Error: opening ouput file: %s\n", outputFile);
      return false;
   }

   curl_easy_setopt(curl, CURLOPT_URL, url);
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

   CURLcode res = curl_easy_perform(curl);
   
   if (res != CURLE_OK) {
      fprintf(stderr, "In curlGet, Error downloading: %s\n", curl_easy_strerror(res));
   }
   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
   curl_easy_cleanup(curl);
   fclose(fp);
   if (http_code >= 400 || res != CURLE_OK) {
      fprintf (stderr, "In curlget, Error HTTP response code: %ld\n", http_code);
      return false;
   }
   return true;
}

