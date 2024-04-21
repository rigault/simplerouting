/* this file includes in other file provides
. Constant definition with #define
. Type deiition with typedef
. small inline functions
*/

#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <math.h>
#define N_WIND_URL            6
#define N_CURRENT_URL         6
#define ROOT_GRIB_URL         "https://static1.mclcm.net/mc2020/int/cartes/marine/grib/" // Meteoconsult
#define WORKING_DIR           "/home/rr/routing/"
#define PARAMETERS_FILE       WORKING_DIR"par/routing.par" // default parameter file
#define TEMP_FILE_NAME        "routing.tmp"  
#define PROG_LOGO             "routing.png"  
#define MIN_SPEED_FOR_TARGET  5                 // knots. Difficult to explain. See engine.c, optimize()..
//#define MISSING               (-999999)       // for grib file missing values
#define MISSING               (0.001)           // for grib file missing values
#define MS_TO_KN              (3600.0/1852.0)
#define KN_TO_MS              (1852.0/3600.0)
#define RAD_TO_DEG            (180.0/M_PI)
#define DEG_TO_RAD            (M_PI/180.0)
#define SIZE_T_IS_SEA         (3601 * 1801)
#define MAX_N_WAY_POINT       10
#define PROG_WEB_SITE         "http://www.orange.com"  
#define PROG_NAME             "routing"  
#define PROG_VERSION          "0.1"  
#define PROG_AUTHOR           "René Rigault"
#define DESCRIPTION           "Routing calculates best route from pOr (Origin) to pDest (Destination) \
   taking into account grib files and boat polars"
#define LIBRAIRIES            "eccodes from ECMWF\ncurl\ngpsd\nshapelib\n"
#define MILLION               1000000
#define NIL                   (-100000)
#define MAX_N_ISOC            512               // max number of isochrones in isocArray
#define MAX_SIZE_ISOC         100000            // max number of point in an isochrone
#define MAX_N_POL_MAT_COLS    128
#define MAX_N_POL_MAT_LINES   128
#define MAX_SIZE_LINE         256		         // max size of pLine in text files
#define MAX_SIZE_DATE         32                // max size of a string with date inside
#define MAX_SIZE_BUFFER       100000
#define MAX_N_TIME_STAMPS     512
#define MAX_N_SHORT_NAME      64
#define MAX_N_GRIB_LAT        1024
#define MAX_N_GRIB_LON        2048
#define MAX_SIZE_SHORT_NAME   16
#define MAX_SIZE_NAME         64
#define MAX_SIZE_NUMBER       64                // max size of string represnting a number
#define MAX_SIZE_FILE_NAME    128               // max size of pLine in text files
#define MAX_SIZE_DIR_NAME     192               // max size of directory name
#define MAX_N_SHP_FILES       4                 // max number of shape file
#define MAX_N_POI             4096              // max number of poi in poi file
#define MAX_SIZE_POI_NAME     32                // max size of city name
#define GPSD_TCP_PORT         "2947"            // TCP port for gps demon
#define MAX_SIZE_FORBID_ZONE  100               // max size per forbidden zone
#define MAX_N_FORBID_ZONE     10                // max nummber of forbidden zones
#define N_PROVIDERS           8                 // for DictProvider dictTab size
#define MAX_INDEX_ENTITY      512               // for shp. Index.

enum {WIND, CURRENT};                           // for grib information, either WIND or CURRENT
enum {POLAR, WAVE_POLAR};                       // for polar information, either POLAR or WAVE
enum {POI_SEL, PORT_SEL};                       // for editor or spreadsheet, call either POI or PORT
enum {BASIC, DD, DM, DMS};                      // degre, degre decimal, degre minutes, degre minutes seconds
enum {TRIBORD, BABORD};                         // amure
enum {NO_COLOR, B_W, COLOR};                    // wind representation 
enum {NONE, ARROW, BARBULE};                    // wind representation 
enum {NOTHING, POINT, SEGMENT, BEZIER};         // bezier or segment representation
enum {UNVISIBLE, NORMAL, CAT, PORT, NEW};       // for POI point of interest
enum {RUNNING, STOPPED, NO_SOLUTION, EXIST_SOLUTION}; // for chooseDeparture.ret values

/*! for meteo services */
enum {SAILDOCS_GFS, SAILDOCS_ECMWF, SAILDOCS_ICON, SAILDOCS_ARPEGE, SAILDOCS_AROME, SAILDOCS_CURR, MAILASAIL, GLOBALMARINET}; // grib mail service providers
struct DictElmt {int id; char name [MAX_SIZE_NAME];};
struct DictProvider {char address [MAX_SIZE_LINE]; char libelle [MAX_SIZE_LINE]; char service [MAX_SIZE_NAME];};

/*! Structure to store coordinates */
typedef struct {
    double x;
    double y;
} Coordinates;

/*! Structure for SHP and forbid zones */
typedef struct {
   double lat;
   double lon;
} Point;

/*! Structure for polygon */
typedef struct {
    int n;
    Point *points;
} Polygon;

/* Structure for best departure time choice */
typedef struct {
   int ret;
   int count;
   int tBegin;
   int tEnd;
   int tStep;
   int tStop;
   double t [MAX_N_TIME_STAMPS];
   double minDuration;
   double maxDuration;
   int bestTime;
} ChooseDeparture;

/*! My date */
typedef struct {
   int year;
   int mon;
   int day;
   int hour;
   int min;
   int sec;
} MyDate;

/*! For geo map shputil */
typedef struct {
   Point  *points;
   double latMin;
   double latMax;
   double lonMin;
   double lonMax;
   int    numPoints;
   int    nSHPType;
   int    index [MAX_INDEX_ENTITY]; 
   int    maxIndex;
} Entity;

/*! Check grib structure */
typedef struct {
   int uMissing;
   int vMissing;
   int gMissing;
   int wMissing;
   int uStrange;
   int vStrange;
   int gStrange;
   int wStrange;
   int outZone;
} CheckGrib;

/*! Wind point */
typedef struct {
   double lat;
   double lon;
   double u;                                 // east west wind or current in meter/s
   double v;                                 // north south component of wind or current in meter/s
   double w;                                 // waves height WW3 model
   double g;                                 // wind speed gust
} FlowP;                                     // ether wind or current

/*! zone description */
typedef struct {
   bool   allTimeStepOK;
   bool   wellDefined;
   long   centreId;
   int    nMessage;
   long   editionNumber;
   long   numberOfValues;
   long   stepUnits;
   double latMin;
   double latMax;
   double lonLeft;
   double lonRight;
   double latStep;
   double lonStep;
   long   nbLat;
   long   nbLon;
   size_t nTimeStamp;
   size_t nDataDate;
   size_t nDataTime;
   double time0Grib;                      // time of the first forecast in grib in hours
   size_t nShortName;
   char** shortName;
   long   timeStamp [MAX_N_TIME_STAMPS];
   long   dataDate [16];
   long   dataTime [16];
} Zone;

/*! Point in isochrone */
typedef struct {
   int    id;
   int    father;
   int    amure;
   bool   motor;
   int    sector;
   double lat;
   double lon;
   double dd;     // distance to pDest
   double vmc;    // velocity made on course
} Pp;

/*! isochrone meta data */ 
typedef struct {
   double distance;  // best distance from Isoc to pDest
   double bestVmg;   // best VMG (distance)
   int    closest;   // index in Iso of closest point to pDest
   int    first;     // index of first point, useful for drawAllIsochrones
   int    size;      // size of isochrone
   double focalLat;  // focal point Lat
   double focalLon;  // focal point Lon
} IsoDesc;

/*! pol Mat description*/
typedef struct {
   double t [MAX_N_POL_MAT_LINES][MAX_N_POL_MAT_COLS];
   int    nLine;
   int    nCol;
} PolMat;

/*! Point for way point route */
typedef struct {
   double lat;
   double lon;
   double od;     // ortho dist
   double oCap;   // ortho cap 
   double ld;     // loxo dist
   double lCap;   // loxo  cap 
} WayPoint;

/* Way point route description */
typedef struct {
   int n;
   double totOrthoDist;
   double totLoxoDist;
   WayPoint t [MAX_N_WAY_POINT];
} WayRoute;

/*! Point for Sail Route  */
typedef struct {
   double lat;
   double lon;
   int    id;
   int    father;
   bool   motor;
   double od;
   double oCap;
   double ld;
   double lCap;
} SailPoint;

/*! Route description  */
typedef struct {
   int    n;                                // number of steps
   double calculationTime;                  // compute time to calculate the route
   long   kTime0;                           // relative index time of departure
   double duration;                         // total time in hours of the route
   double motorDuration;                    // total time in hours using motor
   double totDist;                          // total distance in NM
   double motorDist;                        // distance using motor in NM
   int    ret;                              // return value of routing
   bool   destinationReached;               // true if destination reaches
   SailPoint t [MAX_N_ISOC + 1];
} SailRoute;

/*! Parameters */
typedef struct {
   int maxPoiVisible;                        // poi visible if <= maxPoiVisible
   int opt;                                  // 0 if no optimization, else number of opt algorithm
   double tStep;                             // hours
   int cogStep;                              // step of cog in degrees
   int rangeCog;                             // range of cog from x - RANGE_GOG, x + RAGE_COG+1
   int maxIso;                               // max number of isochrones
   int special;                              // special puropose
   double constWindTws;                      // if not equal 0, constant wind used in place of grib file
   double constWindTwd;                      // the direction of constant wind if used
   double constWave;                         // constant wave height if used
   double constCurrentS;                     // if not equal 0, contant current speed Knots
   double constCurrentD;                     // the direction of cinstant current if used
   int jFactor;                              // factor for target point distance used in sectorOptimize
   int kFactor;                              // factor for target point distance used in sectorOptimize
   int minPt;                                // min point per sector. See sectorOptimize
   double maxTheta;                          // angle for optimization by sector
   int nSectors;                             // number of sector for optimization by sector
   char workingDir [MAX_SIZE_FILE_NAME];     // working directory
   char gribFileName [MAX_SIZE_FILE_NAME];   // name of grib file
   double gribResolution;                    // grib lat step for mail request
   int gribTimeStep;                         // grib time step for mail request
   int gribTimeMax;                          // grib time max fir mail request
   char currentGribFileName [MAX_SIZE_FILE_NAME];   // name of current grib file
   char polarFileName [MAX_SIZE_FILE_NAME];  // name of polar file
   char wavePolFileName [MAX_SIZE_FILE_NAME];// name of Wave polar file
   char dumpIFileName [MAX_SIZE_FILE_NAME];  // name of file where to dump isochrones
   char dumpRFileName [MAX_SIZE_FILE_NAME];  // name of file where to dump isochrones
   char helpFileName [MAX_SIZE_FILE_NAME];   // name of html help file
   char shpFileName [MAX_N_SHP_FILES][MAX_SIZE_FILE_NAME];    // name of SHP file for geo map
   char isSeaFileName [MAX_SIZE_FILE_NAME];  // name of file defining sea on earth
   char cliHelpFileName [MAX_SIZE_FILE_NAME];// text help for cli mode
   char poiFileName [MAX_SIZE_FILE_NAME];    // list of point of interest
   char portFileName [MAX_SIZE_FILE_NAME];   // list of ports
   char parInfoFileName [MAX_SIZE_FILE_NAME]; // parameter info
   int nShpFiles;                            // number of Shp files
   double startTimeInHours;                  // time of beginning of routing after time0Grib
   Pp pOr;                                   // point of origine
   Pp pDest;                                 // point of destination
   char pOrName [MAX_SIZE_NAME];             // Name of pOr if exist
   char pDestName [MAX_SIZE_NAME];           // Name of pDest if exist
   int style;                                // style of isochrones
   int showColors;                           // colors for wind speed
   char smtpScript [MAX_SIZE_LINE];          // script used to send request for grib files
   char imapToSeen [MAX_SIZE_LINE];          // script used to flag all messages to seen
   char imapScript [MAX_SIZE_LINE];          // script used to receive grib files
   int dispDms;                              // display degre, degre minutes, degre minutes sec
   int windDisp;                             // display wind nothing or barbule or arrow
   int currentDisp;                          // display current
   int waveDisp;                             // display wave height
   int averageOrGustDisp;                    // display wave height
   int closestDisp;                          // display closest point to pDest in isochrones
   int focalDisp;                            // display focal point 
   double penalty0;                          // penalty in hours when amure change front
   double penalty1;                          // penalty in hours when amure change back
   double motorSpeed;                        // motor speed if used
   double threshold;                         // threshold for motor use
   double efficiency;                        // efficiency of team 
   double xWind;                             // multiply factor for wind
   char editor [MAX_SIZE_NAME];              // name of text file editor
   char webkit [MAX_SIZE_NAME];              // name of webkit application
   char spreadsheet [MAX_SIZE_NAME];         // name of spreadshhet application
   char mailPw [MAX_SIZE_NAME];              // password for smtp and imap
   int nForbidZone;                          // number of forbidden zones
   char forbidZone [MAX_N_FORBID_ZONE][MAX_SIZE_LINE]; // array of forbid zones
} Par;

/*! Isochrone */
typedef Pp Isoc [MAX_SIZE_ISOC];             // isochrone is an array of points

/*! for point of interest management */
typedef struct {
   double lat;
   double lon;
   int    type;
   int    level;
   char   name [MAX_SIZE_POI_NAME];
} Poi;

/*! For GPS management */
typedef struct {
   double lat;
   double lon;
   double alt;
   int    status;
   int    nSat;
   struct timespec timestamp;
   int    ret;
} MyGpsData;

// inlines small functions

/*! true wind direction */
static inline double fTwd (double u, double v) {
	double val = 180 + RAD_TO_DEG * atan2 (u, v);
   return (val > 180) ? val - 360 : val;
}

/*! true wind speed. cf Pythagore */
static inline double fTws (double u, double v) {
    return MS_TO_KN * sqrt ((u * u) + (v * v));
}

/*! return fx : linear interpolation */
static inline double interpolate (double x, double x0, double x1, double fx0, double fx1) {
   if (x1 == x0) return fx0;
   else return fx0 + (x-x0) * (fx1-fx0) / (x1-x0);
}

/*! return pythagore distance */
static inline double dist (double x, double y) {
   return sqrt (x*x + y*y);
}

/*! return givry correction to apply to direct or loxodromic cap to get orthodromic cap  */
static inline double givry (double lat1, double lon1, double lat2, double lon2) {
   double theta = lon1 - lon2;
   double latM = (lat1 + lat2) / 2;
   return (theta/2) * sin (latM * DEG_TO_RAD);
}
/*! direct loxodromic cap from origin to dest */
static inline double directCap (double lat1, double lon1, double lat2, double lon2) {
   return RAD_TO_DEG * atan2 ((lon2 - lon1) * cos (DEG_TO_RAD * (lat1+lat2)/2), lat2 - lat1);
}

/*! direct loxodromic cap from origin to dest */
static inline double orthoCap (double lat1, double lon1, double lat2, double lon2) {
   return directCap (lat1, lon1, lat2, lon2) + givry (lat1, lon1, lat2, lon2);
}

/*! direct loxodromic dist from origi to dest */
static inline double loxoDist (double lat1, double lon1, double lat2, double lon2) {
   double coeffLat = cos (DEG_TO_RAD * (lat1 + lat2)/2);
   return 60 * dist ((lat1 - lat2), (lon2 - lon1) * coeffLat);
}

/*! return orthodomic distance in nautical miles */
static inline double orthoDist (double lat1, double lon1, double lat2, double lon2) {
    double theta = lon1 - lon2;
    double distRad = acos(
        sin(lat1 * DEG_TO_RAD) * sin(lat2 * DEG_TO_RAD) + 
        cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * cos(theta * DEG_TO_RAD)
    );
    return fabs (60 * (180/M_PI) * distRad);
}

/*! find in polar boat speed or wave coeff */
static inline double findPolar (double twa, double w, PolMat mat) {
   int l, c, lInf, cInf, lSup, cSup;
   if (twa > 180) twa = 360 - twa;
   else if (twa < 0) twa = -twa;
   for (l = 1; l < mat.nLine; l++) 
      if (mat.t [l][0] > twa) break;
   lSup = (l < mat.nLine - 1) ? l : mat.nLine - 1;
   lInf = (l == 1) ? 1 : l - 1;
   for (c = 1; c < mat.nCol; c++) 
      if (mat.t [0][c] > w) break;
   cSup = (c < mat.nCol - 1) ? c : mat.nCol - 1;
   cInf = (c == 1) ? 1 : c - 1;
   double s0 = interpolate (twa, mat.t [lInf][0], mat.t [lSup][0], mat.t [lInf][cInf], mat.t [lSup][cInf]);
   double s1 = interpolate (twa, mat.t [lInf][0], mat.t [lSup][0], mat.t [lInf][cSup], mat.t [lSup][cSup]);
   return interpolate (w, mat.t [0][cInf], mat.t [0][cSup], s0, s1);
}

