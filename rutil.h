/*! Zone description */
extern Zone zone;                      // wind
extern Zone currentZone;               // current

/*! grib data description */
extern FlowP *tGribData [];            // wind, current

/*! list of wayPoint */
extern WayPointList wayPoints;

/*! forbid zones */
extern MyPolygon forbidZones [MAX_N_FORBID_ZONE];

/*! Meteo service */
extern struct MeteoElmt meteoTab [N_METEO_ADMIN];
extern const struct MailService mailServiceTab [];
extern const struct GribService serviceTab [];

/* sail attributes */
extern const char *sailName [];
extern const char *colorStr [];

/*! polar description */
extern PolMat polMat;
extern PolMat sailPolMat;
extern PolMat wavePolMat;

/*! parameters desciption */
extern Par par;

extern char *tIsSea;                   // array of byte. 0 if earth, 1 if sea

/*! Point of Interests and ports descriotion */
extern Poi tPoi [MAX_N_POI];
extern int nPoi;

/*! for shp files */
extern int    nTotEntities;
extern Entity *entities;

/*! for curlGet url */
extern const int delay [];
extern const char *METEO_CONSULT_WIND_URL [];
extern const char *METEO_CONSULT_CURRENT_URL [];

/*! for competitors */
extern CompetitorsList competitors;

/*! functions defined in rutil.c */
extern char  *fSailName (int val, char *str, size_t maxLen);
extern void   initZone (Zone *zone);
extern int    maxTimeRange (int service, int mailService);
extern int    howManyShortnames (int service, int mailService);
extern char   *newFileNameSuffix (const char *fileName, const char *suffix, char *newFileName, size_t maxLen);
extern void   removeAllTmpFilesWithPrefix (const char *prefix);
extern double offsetLocalUTC (void);
extern void   initStart (struct tm *start);
extern double diffTimeBetweenNowAndGribOrigin (long intDate, double nHours);
extern void   *commandRun (void *data);
extern bool   compact (const char *dir, const char *inFile,\
   const char *shortNames, double lonLeft, double lonRight, double latMin, double latMax, const char *outFile);
extern bool   concat (const char *prefix, const char *suffix, int limit0, int step0, int step1, int max, const char *fileRes);
extern bool   isEmpty (const char *str);
extern char   *formatThousandSep (char *buffer, size_t maxLen, int value);
extern bool   initSHP (const char* nameFile);
extern void   freeSHP (void);
extern char   *buildRootName (const char *fileName, char *rootName, size_t maxLen);
extern bool   isNumber (const char *name);
extern double getCoord (const char *str, double min, double max);
extern bool   analyseCoord (const char *strCoord, double *lat, double *lon);
extern void   polygonToStr (char *buffer, size_t maxLen);
extern long   getFileSize (const char *fileName);
extern char   *latToStr (double lat, int type, char* str, size_t maxLen);
extern char   *lonToStr (double lon, int type, char* str, size_t maxLen);
extern char   *durationToStr (double duration, char *res, size_t maxLen);
extern char   *epochToStr (time_t t, bool seconds, char *str, size_t len);
extern time_t gribDateTimeToEpoch (long date, long time);
extern double getDepartureTimeInHour (struct tm *start);
extern char   *gribDateTimeToStr (long date, long time, char *str, size_t len);
extern char   *newDate (long intDate, double myTime, char *res, size_t maxLen);
extern char   *newDateWeekDay (long intDate, double myTime, char *res, size_t maxLen);
extern char   *newDateWeekDayVerbose (long intDate, double myTime, char *res, size_t maxLen);
extern bool   readParam (const char *fileName);
extern bool   writeParam (const char *fileName, bool header, bool password);
extern bool   readIsSea (const char *fileName);
extern int    readPoi (const char *fileName);
extern bool   writePoi (const char *fileName);
extern int    findPoiByName (const char *name, double *lat, double *lon);
extern void   poiPrint ();
extern char   *nearestPort (double lat, double lon, const char *fileName, char *str, size_t maxLen);
extern int    poiToStr (bool portCheck, char *str, size_t maxLen);
extern char   *dollarSubstitute (const char* str, char *res, size_t maxLen);
extern char   *strCpyMaxWidth (const char *str, int n, char *res, size_t maxLen);
extern bool   curlGet (const char *url, const char *outputFile, char *errMessage, size_t maxLen); 
extern void   updateIsSeaWithForbiddenAreas (void);
extern bool   isServerAccessible (const char *url);
extern bool   exportWpToGpx (const char *gpxFileName);
extern bool   exportTraceToGpx (const char *fileName, const char *gpxFileName);
extern bool   addTraceGPS (const char *fileName);
extern bool   addTracePt (const char *fileName, double lat, double lon);
extern bool   findLastTracePoint (const char *fileName, double *lat, double *lon, double *time);
extern bool   distanceTraceDone (const char *fileName, double *od, double *ld, double *rd, double *sog);
extern bool   infoDigest (const char *fileName, double *od, double *ld, double *rd, double *sog);
extern bool   isDayLight (double t, double lat, double lon);
extern void   initWayPoints (void);
extern bool   mostRecentFile (const char *directory, const char *pattern, char *name, size_t maxLen);
extern int    buildMeteoConsultUrl (int type, int i, int delay, char *url, size_t maxLen);
extern int    buildGribUrl (int typeWeb, int topLat, int leftLon, int bottomLat, int rightLon, int step, int step2, char *url, size_t maxLen);
extern bool   buildGribMail (int type, double lat1, double lon1, double lat2, double lon2, char *object, char *command, size_t maxLen);
