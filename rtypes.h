#include <stdbool.h>
#define EPSILON               0.1
#define GET_GRIB_CMD          "grib_get_data -L \"%.2lf %.2lf\" -F \"%.2lf\" -m -999999 -w shortName=10u/10v/swh -p step,shortName "
#define GET_GRIB_CURRENT_CMD  "grib_get_data -L \"%.2lf %.2lf\" -F \"%.2lf\" -m -999999 -w shortName=ucurr/vcurr -p step,shortName "
#define MISSING               (-999999)
#define MS_TO_KN              (3600.0/1852.0)
#define KN_TO_MS              (1852.0/3600.0)
#define RAD_TO_DEG            (180.0/M_PI)
#define DEG_TO_RAD            (M_PI/180.0)
#define SIZE_T_IS_SEA         (3601 * 1801)
#define MAX_N_WAY_POINT       10
#define ROOT_GRIB_SERVER      "https://data.ecmwf.int/forecasts/"  
#define TEMP_FILE_NAME        "grib.tmp"  
#define PROG_NAME             "Routing"  
#define PROG_VERSION          "0.1"  
#define PROG_AUTHOR           "René Rigault"
#define PROG_WEB_SITE         "http://www.orange.com"  
#define PROG_LOGO             "routing.png"  
#define MILLION               1000000
#define NIL                   (-100000)
#define MAX_N_ISOC            128               // max number of isochrones in isocArray
#define MAX_SIZE_ISOC         200000            // max number of point in an isochrone
#define MAX_N_POL_MAT_COLS    128
#define MAX_N_POL_MAT_LINES   128
#define MAX_SIZE_LINE         256		         // size max of pLine in text files
#define MAX_SIZE_DATE         32                // size max of a string with date inside
#define RR_INFO                "\n2024\nrene.rigault@wanadoo.fr"
#define MAX_SIZE_BUFFER        100000
#define PARAMETERS_FILE       "par/routing.par" // default parameter file
#define MAX_N_TIME_STAMPS     512
#define MAX_N_SHORT_NAME      64
#define MAX_N_GRIB_LAT        1024
#define MAX_N_GRIB_LON        2048
#define MAX_SIZE_NAME         64
#define MAX_SIZE_FILE_NAME    128               // size max of pLine in text files
#define MAX_N_SHP_FILES       4                 // max number of shape file
#define MAX_N_POI             100               // max number of cities in city file
#define MAX_SIZE_POI_NAME     32                // max size of city name
#define GPSD_TCP_PORT         "2947"            // TCP port gor gps demon
#define MAX_N_SMTP_TO         3                 // max nummber of grib mail providers

enum {WIND, CURRENT};                           // for grib information either WIND or CURRENT
enum {SAILDOCS, MAILASAIL, GLOBALMARINET};      // grib mail service providers
enum {DD, DM, DMS};                             // degre decimal, degre minutes, degre minutes seconds
enum {TRIBORD, BABORD};                         // amure

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
   double lat;
   double lon;
} Point;

typedef struct {
    Point *points;
    int numPoints;
    int nSHPType;
} Entity;

/*! Wind point */
typedef struct {
   double lat;
   double lon;
   double u;                                 // east west wind or current in meter/s
   double v;                                 // north south component of wind or current in meter/s
   double w;                                 // waves height WW3 model
} FlowP;                                     // ether wind or current

/*! zone description */
typedef struct {
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
   double lat;
   double lon;
   int id;
   int father;
   int amure;
} Pp;

/* pol Mat description*/
typedef struct {
   double t [MAX_N_POL_MAT_LINES][MAX_N_POL_MAT_COLS];
   int nLine;
   int nCol;
} PolMat;

/*! Point for Route description  */
typedef struct {
   double lat;
   double lon;
   int id;
   int father;
   double cap;
   double d;
   double dOrtho;
} Pr;

/*! Route description  */
typedef struct {
   Pr t [MAX_N_ISOC + 1];
   int n;
   long kTime0;                              // relative index time of departure
   double duration;
   double totDist;
   bool destinationReached;
} Route;

/*! Parameters */
typedef struct {
   int opt;                                  // 0 if no optimization, else number of opt algorithm
   double tStep;                             // hours
   int cogStep;                              // step of cog in degrees
   int rangeCog;                             // range of cog from x - RANGE_GOG, x + RAGE_COG+1
   int maxIso;                               // max number of isochrones
   int verbose;                              // verbose level of display
   double constWindTws;                      // if not equal 0, constant wind used in place of grib file
   double constWindTwd;                      // the direction of constant wind if used
   double constSog;                          // constant speed if used
   double constCurrentS;                     // if not equal 0, contant current speed Knots
   double constCurrentD;                     // the direction of cinstant current if used
   double distTarget;                        // distance for target point used in sectorOptimize
   double maxTheta;                          // angle for optimization by sector
   int nSectors;                             // number of sector for optimization by sector
   char gribFileName [MAX_SIZE_FILE_NAME];   // name of grib file
   double gribLatStep;                       // grib lat step for mail request
   double gribLonStep;                       // grib lon step for mail request
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
   char poiFileName [MAX_SIZE_FILE_NAME];    // list of cities
   int nShpFiles;                            // number of Shp files
   double startTimeInHours;                  // time of beginning of routing after time0Grib
   Pp pOr;                                   // point of origine
   Pp pDest;                                 // point of destination
   int style;                                // style of isochrones
   char smtpScript [MAX_SIZE_LINE];          // script used to send request for grib files
   char smtpTo[MAX_N_SMTP_TO][MAX_SIZE_LINE];// addresses od SMTP grib providers
   int nSmtp;                                // number of SMTP grib providers
   char imapScript [MAX_SIZE_LINE];          // script used to receive grib files
   int dispDms;                              // display degre, degre minutes, degre minutes sec
   double penalty0;                          // penalty in hours when amure change front
   double penalty1;                          // penalty in hours when amure change back
   char editor [MAX_SIZE_NAME];              // name of text file editor
} Par;

/*! Isochrone */
typedef Pp Isoc [MAX_SIZE_ISOC];             // isochrone is an array of points

/*! for cities management */
typedef struct {
   double lat;
   double lon;
   char   name [MAX_SIZE_POI_NAME];
} Poi;

/*! For GPS management */
typedef struct {
   int ret;
   double lat;
   double lon;
   double alt;
   int status;
   int nSat;
   struct timespec timestamp;
} MyGpsData;
