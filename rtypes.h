#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#define N_WIND_URL            6
#define N_CURRENT_URL         6
#define ROOT_GRIB_URL         "https://static1.mclcm.net/mc2020/int/cartes/marine/grib/" // Meteoconsult
#define WORKING_DIR           "/home/rr/routing/"
#define PARAMETERS_FILE       WORKING_DIR"par/routing.par" // default parameter file
#define TEMP_FILE_NAME        "routing.tmp"  
#define PROG_LOGO             "routing.png"  
#define MIN_SPEED_FOR_TARGET  5                // knots. Difficult to explain. See engine.c, optimize()..
#define MISSING               (-999999)        // for grib file missing values
//#define MISSING               0              // for grib file missing values
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
#define MAX_SIZE_FILE_NAME    128               // max size of pLine in text files
#define MAX_SIZE_DIR_NAME     192               // max size of directory name
#define MAX_N_SHP_FILES       4                 // max number of shape file
#define MAX_N_POI             15000             // max number of poi in poi file
#define MAX_SIZE_POI_NAME     32                // max size of city name
#define GPSD_TCP_PORT         "2947"            // TCP port for gps demon
#define MAX_SIZE_FORBID_ZONE  100               // max size per forbidden zone
#define MAX_N_FORBID_ZONE     10                // max nummber of forbidden zones
#define N_PROVIDERS           6                 // for DictProvider dictTab size
#define MAX_INDEX_ENTITY      32                // for shp. Index.

enum {WIND, CURRENT};                           // for grib information, either WIND or CURRENT
enum {POLAR, WAVE_POLAR};                       // for polar information, either POLAR or WAVE
enum {POI_SEL, PORT_SEL};                       // for editor or spreadsheet, call either POI or PORT
enum {BASIC, DD, DM, DMS};                      // degre, degre decimal, degre minutes, degre minutes seconds
enum {TRIBORD, BABORD};                         // amure
enum {NO_COLOR, B_W, COLOR};                    // wind representation 
enum {NONE, ARROW, BARBULE};                    // wind representation 
enum {NOTHING, POINT, SEGMENT, BEZIER};         // bezier or segment representation
enum {UNVISIBLE, NORMAL, CAT, PORT, NEW};       // for POI point of interest

/*! for meteo services */
enum {SAILDOCS_GFS, SAILDOCS_ECMWF, SAILDOCS_ICON, SAILDOCS_CURR, MAILASAIL, GLOBALMARINET}; // grib mail service providers
struct DictElmt {int id; char name [MAX_SIZE_NAME];};
struct DictProvider {char address [MAX_SIZE_LINE]; char libelle [MAX_SIZE_LINE]; char service [MAX_SIZE_NAME];};

/*! Structure to point for SHP and forbid zones */
typedef struct {
   double lat;
   double lon;
} Point;

/*! Structure for polygon */
typedef struct {
    int n;
    Point *points;
} Polygon;

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
} Entity;

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
   int    sector;
   double lat;
   double lon;
   double dd;     // distance to pDest
   double vmg;    // velocity made good
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

/*! Point for Route description  */
typedef struct {
   double lat;
   double lon;
   int    id;
   int    father;
   double cap;
   double d;
   double dOrtho;
} Pr;

/*! Route description  */
typedef struct {
   Pr     t [MAX_N_ISOC + 1];
   double calculationTime;
   int    n;
   long   kTime0;                           // relative index time of departure
   double duration;
   double totDist;
   int    ret;                              // return value of routing
   bool   destinationReached;
} Route;

/*! Parameters */
typedef struct {
   int maxPoiVisible;                        // poi visible if <= maxPoiVisible
   int opt;                                  // 0 if no optimization, else number of opt algorithm
   double tStep;                             // hours
   int cogStep;                              // step of cog in degrees
   int rangeCog;                             // range of cog from x - RANGE_GOG, x + RAGE_COG+1
   int maxIso;                               // max number of isochrones
   int verbose;                              // verbose level of display
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
   int closestDisp;                          // display closest point to pDest in isochrones
   int focalDisp;                            // display focal point 
   double penalty0;                          // penalty in hours when amure change front
   double penalty1;                          // penalty in hours when amure change back
   double motorSpeed;                        // motor speed if used
   double threshold;                         // threshold for motor use
   double efficiency;                        // efficiency of team 
   char editor [MAX_SIZE_NAME];              // name of text file editor
   char spreadsheet [MAX_SIZE_NAME];         // name of text file editor
   char mailPw [MAX_SIZE_NAME];              // password for smtp and imap
   double dispLonLatRatio;                   // for display ratio between deltaLon and deltaLat
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
   int    ret;
   double lat;
   double lon;
   double alt;
   int    status;
   int    nSat;
   struct timespec timestamp;
} MyGpsData;
