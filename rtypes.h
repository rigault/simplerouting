/* this file includes in other file provides
. Constant definition with #define and enum
. Type definitions with typedef
*/

#define N_METEO_CONSULT_WIND_URL    6
#define N_METEO_CONSULT_CURRENT_URL 6
#define METEO_CONSULT_WIND_DELAY    5  // nb hours after time run to get new grib
#define METEO_CONSULT_CURRENT_DELAY 12
#define NOAA_DELAY                  4
#define ECMWF_DELAY                 8
#define METEO_CONSULT_ROOT_GRIB_URL "https://static1.mclcm.net/mc2020/int/cartes/marine/grib/"
#define NOAA_ROOT_GRIB_URL          "https://nomads.ncep.noaa.gov/cgi-bin/"
#define NOAA_GENERAL_PARAM_GRIB_URL "var_GUST=on&var_PRMSL=on&var_PRATE=on&var_UGRD=on&var_VGRD=on&lev_10_m_above_ground=on&lev_surface=on&lev_mean_sea_level=on"
#define ECMWF_ROOT_GRIB_URL         "https://data.ecmwf.int/forecasts/"
#define ARPEGE_ROOT_GRIB_URL        "https://object.data.gouv.fr/meteofrance-pnt/pnt/"
#define N_MAX_NMEA_PORTS      3
#define CSV_SEP               ",;\t"            // general separators for CSV files
#define CSV_SEP_POLAR         ";\t"             // for polar no comma because decimal separator may be comma 
#define GPS_TIME_OUT          2000000           // 2 seconds
//#define WORKING_DIR           "/home/rr/routing/"
#define WORKING_DIR           "" //ATT
#define PARAMETERS_FILE       WORKING_DIR"par/routing.par" // default parameter file
#define TEMP_FILE_NAME        "routing.tmp"  
#define PROG_LOGO             "routing.png"  
#define MIN_LAT               (-80.0)           // Minimum latitude for getCoord
#define MAX_LAT               80.0              // Maximum latitude for getCoord
#define MIN_LON               (-180.0)          // Minimum longitude for getCoord
#define MAX_LON               360.0             // Maximum longitude for getCoord
#define MIN_SPEED_FOR_TARGET  5                 // knots. Difficult to explain. See engine.c, optimize()..
//#define MISSING               (-999999)       // for grib file missing values
#define MISSING               (0.001)           // for grib file missing values
#define MS_TO_KN              (3600.0/1852.0)
#define KN_TO_MS              (1852.0/3600.0)
#define EARTH_RADIUS          3440.065          // Earth's radius in nautical miles
#define RAD_TO_DEG            (180.0/G_PI)
#define DEG_TO_RAD            (G_PI/180.0)
#define SIZE_T_IS_SEA         (3601 * 1801)
#define MAX_N_WAY_POINT       10               // Max number of Way Points
#define PROG_WEB_SITE         "http://www.orange.com"  
#define PROG_NAME             "routing"  
#define PROG_VERSION          "0.1"  
#define PROG_AUTHOR           "René Rigault"
#define DESCRIPTION           "Routing calculates best route from pOr (Origin) to pDest (Destination) \
   taking into account grib files and boat polars"

#define MILLION               1000000
#define NIL                   (-100000)
#define MAX_N_DAYS_WEATHER    16               // Max number od days for weather forecast
#define MAX_SIZE_ISOC         50000            // max number of point in an isochrone
#define MAX_N_ISOC            (384 + 1) * 4    // Max Hours in 16 days * 4 times per hours max (tSep = 15 mn) required for STATIC way
#define MAX_N_POL_MAT_COLS    128
#define MAX_N_POL_MAT_LINES   128
#define MAX_SIZE_LINE         256		         // max size of pLine in text files
#define MAX_SIZE_STD          1024		         // max size of lines standard
#define MAX_SIZE_LINE_BASE64  1024              // max size of line in base64 mail file
#define MAX_SIZE_TEXT         2048		         // max size of text
#define MAX_SIZE_MESSAGE      2048		         // max size of a mail message
#define MAX_SIZE_URL          1024		         // max size of a URL
#define MAX_SIZE_DATE         32                // max size of a string with date inside
#define MAX_SIZE_BUFFER       1000000
#define MAX_N_TIME_STAMPS     512
#define MAX_N_DATA_DATE       4                // 1 in practise
#define MAX_N_DATA_TIME       4                // 1 in practise
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
#define SMALL_SIZE            5                 // for short string
#define MAX_SIZE_POI_NAME     64                // max size of city name
#define MAX_SIZE_COUNTRY_CODE 3                 // max size of country code
#define MAX_HISTORY_ROUTE     10                // max number of history route stored
#define GPSD_TCP_PORT         "2947"            // TCP port for gps demon
#define MAX_SIZE_FORBID_ZONE  100               // max size per forbidden zone
#define MAX_N_FORBID_ZONE     10                // max nummber of forbidden zones
#define N_METEO_ADMIN         4                 // administration: Weather Service US, DWD, etc
#define N_MAIL_SERVICES       9                 // for mailServiceTab size
#define N_WEB_SERVICES        3                 // for service Tab size (NOAA and ECMWF and ARPEGE)
#define MAX_INDEX_ENTITY      512               // for shp. Index.
#define MAX_N_COMPETITORS     10                // Number max of competitors
#define MAX_SIZE_SHIP_NAME    21                // see AIS specificatiions
#define MAX_N_SAIL            8                 // max number of sails in sailName table

enum {NOAA_WIND, ECMWF_WIND, ARPEGE_WIND, MAIL, MAIL_SAILDOCS_CURRENT}; // NOAA or ECMWF for web download or MAIL. Specific for current
enum {GPS_INDEX, AIS_INDEX};                    // for NMEA USB serial port reading
enum {WIND, CURRENT};                           // for grib information, either WIND or CURRENT
enum {WIND_POLAR, WAVE_POLAR, SAIL_POLAR};      // for polar information, either WIND or WAVE or SAIL
enum {POI_SEL, PORT_SEL};                       // for editor or spreadsheet, call either POI or PORT
enum {BASIC, DD, DM, DMS};                      // degre, degre decimal, degre minutes, degre minutes seconds
enum {TRIBORD, BABORD};                         // amure
enum {NO_COLOR, B_W, COLOR};                    // wind representation 
enum {NONE, ARROW, BARBULE};                    // wind representation 
enum {NOTHING, JUST_POINT, SEGMENT, BEZIER};    // bezier or segment representation
enum {UNVISIBLE, NORMAL, CAT, PORT, NEW};       // for POI point of interest
enum {RUNNING, STOPPED, NO_SOLUTION, EXIST_SOLUTION};          // for chooseDeparture.ret values and allCompetitors check
enum {GRIB_STOPPED = -2, GRIB_RUNNING = -1, GRIB_ERROR = 0, GRIB_OK = 1, GRIB_UNCOMPLETE = 2};   // for readGribCheck and readCurentGribCheck
enum {ROUTING_STOPPED = -2, ROUTING_ERROR = -1, ROUTING_RUNNING = 0};   // for routingLaunch
enum {NO_ANIMATION, PLAY, LOOP};                // for animationActive status
enum {WIND_DISP, GUST_DISP, WAVE_DISP, RAIN_DISP, PRESSURE_DISP}; // for display

/*! for mail services */
enum {SAILDOCS_GFS, SAILDOCS_ECMWF, SAILDOCS_ICON, SAILDOCS_ARPEGE, SAILDOCS_AROME, SAILDOCS_CURR, MAILASAIL, NOT_MAIL}; 
// grib mail service providers
// GLOBAL_MARINET no longer supported
struct MeteoElmt {
   int id; 
   char name [MAX_SIZE_NAME];
};

struct GribService { // NOAA_WIND, ECMWF_WIND, ARPEGE_WIND
   int  nShortNames;
   char warning [MAX_SIZE_LINE];
};

struct MailService { // different saildocs services and mailasail
   char address [MAX_SIZE_LINE];
   char libelle [MAX_SIZE_LINE];
   char service [MAX_SIZE_NAME];
   int  nShortNames;
   char suffix [MAX_SIZE_NAME];
   char warning [MAX_SIZE_LINE];
};

/*! Structure to store coordinates */
typedef struct {
    double x;
    double y;
} Coordinates;

/*! displayed zone */
typedef struct {
   unsigned int xL;
   unsigned int xR;
   unsigned int yB;
   unsigned int yT;
   double latMin;
   bool   anteMeridian;       // set at true if zone crosses meridian 180
   double latMax;
   double lonLeft;
   double lonRight;
   double latStep;
   double lonStep;
   double zoom;
} DispZone;

/*! Structure for SHP and forbid zones */
typedef struct {
   double lat;
   double lon;
} Point;

/*! Structure for polygon */
typedef struct {
    int n;
    Point *points;
} MyPolygon;

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

/*! Structure for geo map shputil */
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
   double msl;                               // mean sea level pressure
   double prate;                             // precipitation rate
} FlowP;                                     // ether wind or current

/*! zone description */
typedef struct {
   bool   anteMeridian;       // set at true if zone crosses meridian 180
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
   char   shortName [MAX_N_SHORT_NAME][MAX_SIZE_SHORT_NAME];
   //char** shortName;
   long   timeStamp [MAX_N_TIME_STAMPS];
   long   dataDate [MAX_N_DATA_DATE];
   long   dataTime [MAX_N_DATA_TIME];
} Zone;

/*! Point in isochrone */
typedef struct {
   int    id;
   int    father;
   int    amure;
   int    sail;
   bool   motor;
   int    sector;
   int    toIndexWp;
   double lat;
   double lon;
   double dd;     // distance to pDest
   double vmc;    // velocity made on course
} Pp;

/*! isochrone meta data */ 
typedef struct {
   int    toIndexWp; // index of waypoint targetted
   double distance;  // best distance from Isoc to pDest
   double bestVmc;   // best VMC (distance)
   int    closest;   // index in Iso of closest point to pDest
   int    first;     // index of first point, useful for drawAllIsochrones
   int    size;      // size of isochrone
   double focalLat;  // focal point Lat
   double focalLon;  // focal point Lon
} IsoDesc;

/*! polat Matrix description */
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

/*! Point for Sail Route  */
typedef struct {
   double lat;
   double lon;
   int    id;
   int    father;
   int    amure;
   int    toIndexWp;
   int    sail;
   bool   motor;
   double time;   // time in hours after start
   double oCap;   // orthodromic cap
   double od;     // orthodromic distance
   double lCap;   // loxodromic cap
   double ld;     // loxodromic distance
   double sog;    // speed over ground
   double u;      // east west wind or current in meter/s
   double v;      // north south component of wind or current in meter/s
   double w;      // waves height WW3 model
   double g;      // wind speed gust
   double twd;    // wind direction
   double tws;    // wind speed un Knots
} SailPoint;

/* Way point route description */
typedef struct {
   int n;
   double totOrthoDist;
   double totLoxoDist;
   WayPoint t [MAX_N_WAY_POINT];
} WayPointList;

/*! Point for competitor */
typedef struct {
   int    colorIndex;                      // index used to select color
   double lat;
   double lon;
   double dist;                            // Orthodromic distance to destination 
   double duration;                        // in hours, duration of the trip.
   char   strETA [MAX_SIZE_DATE];          // estimated Time of Arrival
   char   name [MAX_SIZE_NAME];
} Competitor;

/* list of competitors */
typedef struct {
   int n;                                  // number of competitors
   int runIndex;                           // index of last competitor running. -1 if all run !
   int ret;                                // return of allCompetitors running function
   Competitor t [MAX_N_COMPETITORS];
} CompetitorsList;

/*! Route description  */
typedef struct {
   int    nIsoc;                            // number of Isochrones
   double isocTimeStep;                     // isoc time step for this route
   int    n;                                // number of steps
   double calculationTime;                  // compute time to calculate the route
   double lastStepDuration;                 // in hours
   double duration;                         // total time in hours of the route
   double motorDuration;                    // total time in hours using motor
   double totDist;                          // total distance in NM
   double motorDist;                        // distance using motor in NM
   double tribordDist;                      // distance sail tribord in NM
   double babordDist;                       // distance sail babord in NM
   int    ret;                              // return value of routing
   bool   destinationReached;               // true if destination reaches
   double avrTws;                           // average wind speed of the route
   double avrGust;                          // average gust of the route
   double avrWave;                          // average wave of the route
   double avrSog;                           // average Speed Over Ground
   double maxTws;                           // max wind speed of the route
   double maxGust;                          // max gust of the route
   double maxWave;                          // max wave of the route
   double maxSog;                           // max Speed Over Ground
   int    competitorIndex;                  // index of competitor. See CompetitorsList.
   int    nSailChange;
   SailPoint *t;                            // array of points (maxNIsoc + 1)
   //SailPoint t [MAX_N_ISOC + 1];            // idem STATIC
} SailRoute;

/*! History Route description  */
typedef struct {
   int n;
   SailRoute *r;
} HistoryRouteList;

/*! Parameters */
typedef struct {
   int allwaysSea;                           // if 1 (true) then isSea is allways true. No earth avoidance !
   int dashboardUTC;                         // true if VR Dashboard provide time in UTC. false if local time.
   int maxPoiVisible;                        // poi visible if <= maxPoiVisible
   int opt;                                  // 0 if no optimization, else number of opt algorithm
   double tStep;                             // hours, for isochrones
   int cogStep;                              // step of cog in degrees
   int rangeCog;                             // range of cog from x - RANGE_GOG, x + RAGE_COG+1
   int special;                              // special purpose
   double constWindTws;                      // if not equal 0, constant wind used in place of grib file
   double constWindTwd;                      // the direction of constant wind if used
   double constWave;                         // constant wave height if used
   double constCurrentS;                     // if not equal 0, contant current speed Knots
   double constCurrentD;                     // the direction of cinstant current if used
   int jFactor;                              // factor for target point distance used in sectorOptimize
   int kFactor;                              // factor for target point distance used in sectorOptimize
   int minPt;                                // min point per sector. See sectorOptimize
   int nSectors;                             // number of sector for optimization by sector
   char workingDir [MAX_SIZE_FILE_NAME];     // working directory
   char gribFileName [MAX_SIZE_FILE_NAME];   // name of grib file
   int  mostRecentGrib;                      // true if most recent grib in grib directory to be selected
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
   char parInfoFileName [MAX_SIZE_FILE_NAME];// parameter info
   char traceFileName [MAX_SIZE_FILE_NAME];  // trace is a list of point/time
   char midFileName [MAX_SIZE_FILE_NAME];    // list of mid country related to mmsi and AIS
   char tidesFileName [MAX_SIZE_FILE_NAME];  // list of ports witht lat, lon fo tides
   char logFileName [MAX_SIZE_FILE_NAME];    // log the runs
   char dashboardVR [MAX_SIZE_FILE_NAME];    // Virtual Regatta dashboard thanks to plugin 
   int nShpFiles;                            // number of Shp files
   double startTimeInHours;                  // time of beginning of routing after time0Grib
   Pp pOr;                                   // point of origine
   Pp pDest;                                 // point of destination
   char pOrName [MAX_SIZE_NAME];             // Name of pOr if exist
   char pDestName [MAX_SIZE_NAME];           // Name of pDest if exist
   int style;                                // style of isochrones
   int showColors;                           // colors for wind speed
   char description [MAX_SIZE_LINE];         // script used to send request for grib files
   char smtpScript [MAX_SIZE_LINE];          // script used to send request for grib files
   char imapToSeen [MAX_SIZE_LINE];          // script used to flag all messages to seen
   char imapScript [MAX_SIZE_LINE];          // script used to receive grib files
   int dispDms;                              // display degre, degre minutes, degre minutes sec
   int windDisp;                             // display wind nothing or barbule or arrow
   int currentDisp;                          // display current
   int waveDisp;                             // display wave height
   int indicatorDisp;                        // indicator to display : wind, gist, waves, rain, pressure
   int gridDisp;                             // display meridian and parallels
   int closestDisp;                          // display closest point to pDest in isochrones
   int focalDisp;                            // display focal point 
   int infoDisp;                             // display digest information
   int speedDisp;                            // Speed of Display 
   int aisDisp;                              // AIS display
   int shpPointsDisp;                        // display points only for SHP files
   int penalty0;                             // penalty in minutes for tack
   int penalty1;                             // penalty in minutes fot Gybe
   int penalty2;                             // penalty in minutes for sail change
   double motorSpeed;                        // motor speed if used
   double threshold;                         // threshold for motor use
   double nightEfficiency;                   // efficiency of team at night
   double dayEfficiency;                     // efficiency of team at day
   double xWind;                             // multiply factor for wind
   double maxWind;                           // max Wind supported
   char webkit [MAX_SIZE_NAME];              // name of webkit application
   int  curlSys;                             // true if curl wuth system
   int  python;                              // true if python script used for mail grib request
   char smtpServer [MAX_SIZE_NAME];          // SMTP server name
   char smtpUserName [MAX_SIZE_NAME];        // SMTP user name
   char imapServer [MAX_SIZE_NAME];          // IMAP server name
   char imapUserName [MAX_SIZE_NAME];        // IMAP user name
   char imapMailBox [MAX_SIZE_NAME];         // IMAP mail box
   char mailPw [MAX_SIZE_NAME];              // password for smtp and imap
   bool storeMailPw;                         // store Mail PW
   int  nForbidZone;                         // number of forbidden zones
   char forbidZone [MAX_N_FORBID_ZONE][MAX_SIZE_LINE]; // array of forbid zones
   int techno;                               // additionnal info display for tech experts
   struct {                                  // list of NEMEA ports with for each item, portName and speed 
      char portName [MAX_SIZE_NAME];
      int speed;
      bool open;
   } nmea [N_MAX_NMEA_PORTS];
   int nNmea;                                // number of ports activated
} Par;

/*! for point of interest management */
typedef struct {
   double lat;
   double lon;
   int    type;
   int    level;
   char   cc [MAX_SIZE_COUNTRY_CODE];         // country code
   char   name [MAX_SIZE_POI_NAME];
} Poi;

/*! For GPS management */
typedef struct {
   double lat;
   double lon;
   double alt;
   double cog;    // degrees
   double sog;    // Knots
   int    status;
   int    nSat;
   time_t time;   // epoch time
   bool   OK;
} MyGpsData;

/*! value in NMEA AIS frame */
typedef struct {
   int mmsi;
   double lat;
   double lon;
   double sog;
   int cog;
   char name [MAX_SIZE_SHIP_NAME];
   time_t lastUpdate;
   int minDist;      // evaluation in meters of min Distance to detect collision
} AisRecord;

