extern MyGpsData my_gps_data; 

/*! Meteo service */
extern const struct MailService mailServiceTab [];
extern const struct GribService serviceTab [];

/*! Point of Interests and ports descriotion */
extern Poi tPoi [MAX_N_POI];
extern int nPoi;

/*! for curlGet Meteo consult */
extern const int delay [];
extern const char *METEO_CONSULT_WIND_URL [];
extern const char *METEO_CONSULT_CURRENT_URL [];

/*! functions defined in rutil.c */
extern void   initZone (Zone *zone);
extern int    maxTimeRange (int service, int mailService);
extern int    howManyShortnames (int service, int mailService);
extern void   removeAllTmpFilesWithPrefix (const char *prefix);
extern void   initStart (struct tm *start);
extern double diffTimeBetweenNowAndGribOrigin (long intDate, double nHours);
extern void   *commandRun (void *data);
extern bool   compact (const char *dir, const char *inFile,\
   const char *shortNames, double lonLeft, double lonRight, double latMin, double latMax, const char *outFile);
extern bool   concat (const char *prefix, const char *suffix, int limit0, int step0, int step1, int max, const char *fileRes);
extern bool   isEmpty (const char *str);
extern bool   analyseCoord (const char *strCoord, double *lat, double *lon);
extern void   polygonToStr (char *buffer, size_t maxLen);
extern long   getFileSize (const char *fileName);
extern char   *epochToStr (time_t t, bool seconds, char *str, size_t len);
extern int    readPoi (const char *fileName);
extern bool   writePoi (const char *fileName);
extern int    findPoiByName (const char *name, double *lat, double *lon);
extern void   poiPrint ();
extern char   *nearestPort (double lat, double lon, const char *fileName, char *str, size_t maxLen);
extern int    poiToStr (bool portCheck, char *str, size_t maxLen);
extern char   *strCpyMaxWidth (const char *str, int n, char *res, size_t maxLen);
extern bool   exportWpToGpx (const char *gpxFileName);
extern bool   exportTraceToGpx (const char *fileName, const char *gpxFileName);
extern bool   addTraceGPS (const char *fileName);
extern bool   addTracePt (const char *fileName, double lat, double lon);
extern bool   findLastTracePoint (const char *fileName, double *lat, double *lon, double *time);
extern bool   distanceTraceDone (const char *fileName, double *od, double *ld, double *rd, double *sog);
extern bool   infoDigest (const char *fileName, double *od, double *ld, double *rd, double *sog);
extern void   initWayPoints (void);
extern int    buildMeteoConsultUrl (int type, int i, int delay, char *url, size_t maxLen);
extern int    buildGribUrl (int typeWeb, int topLat, int leftLon, int bottomLat, int rightLon, int step, int step2, char *url, size_t maxLen);
extern bool   buildGribMail (int type, double lat1, double lon1, double lat2, double lon2, char *object, char *command, size_t maxLen);


