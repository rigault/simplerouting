/*! list of wayPoint */
extern WayPointList wayPoints;

/*! forbid zones */
extern MyPolygon forbidZones [MAX_N_FORBID_ZONE];

/*! Meteo service */
extern struct DictElmt dicTab [];
extern struct DictProvider providerTab [];
extern const char *providerArray [N_PROVIDERS + 1];

/*! polar description */
extern PolMat polMat;
extern PolMat wavePolMat;

extern Par par;

/*! Zone description */
extern Zone zone;                      // wind
extern Zone currentZone;               // current

extern char *tIsSea;                   // array of byte. 0 if earth, 1 if sea

/*! grib data description */
extern FlowP *tGribData [];            // wind, current

extern Poi tPoi [MAX_N_POI];
extern int nPoi;

extern int readGribRet;                // to check if readGrib is terminated
extern int readCurrentGribRet;         // to check if readCurrentGrib is terminated

/*! for shp files */
extern int    nTotEntities;
extern Entity *entities;

/*! for curlGet url */
extern const int delay [];
extern const char *WIND_URL [];
extern const char *CURRENT_URL [];

/*! functions defined in rutil.c */
extern bool   concat (const char *prefix, int step, int max, const char *fileRes);
extern bool   isSea (double lat, double lon);
extern bool   mostRecentFile (const char *directory, const char *pattern, char *name);
extern char   *formatThousandSep (char *buffer, int value);
extern bool   initSHP (const char* nameFile);
extern void   freeSHP (void);
extern char   *buildRootName (const char *fileName, char *rootName);
extern bool   isNumber (const char *name);
extern double getCoord (const char *str);
extern long   getFileSize (const char *fileName);
extern char   *latToStr (double lat, int type, char* str);
extern char   *lonToStr (double lon, int type, char* str);
extern char   *durationToStr (double duration, char *res, size_t len);
extern void   initZone (Zone *zone);
extern char   *epochToStr (time_t t, bool seconds, char *str, size_t len);
extern time_t gribDateTimeToEpoch (long date, long time);
extern char   *gribDateTimeToStr (long date, long time, char *str, size_t len);
extern double zoneTimeDiff (const Zone *zone1, const Zone *zone0);
extern bool   readPolar (const char *fileName, PolMat *mat);
extern char   *polToStr (char *str, const PolMat *mat, size_t maxLength);
extern void   *readGrib (void *data);
extern bool   readGribAll (const char *fileName, Zone *zone, int iFlow);
extern void   findWindGrib (double lat, double lon, double t, double *u, double *v, double *gust, double *w, double *twd, double *tws );
extern double findRainGrib (double lat, double lon, double t);
extern double findPressureGrib (double lat, double lon, double t);
extern void   findCurrentGrib (double lat, double lon, double t, double *uCurr, double *vCurr, double *tcd, double *tcs);
extern char   *newDate (long intDate, double myTime, char *res);
extern char   *newDateWeekDay (long intDate, double myTime, char *res);
extern char   *gribToStr (char *str, const Zone *zone, size_t maxLength);
extern double maxValInPol (const PolMat *mat);
extern void   bestVmg (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed);
extern void   bestVmgBack (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed);
extern bool   fileToStr (char *fileName, char* str, size_t maxLength);
extern bool   readParam (const char *fileName);
extern bool   writeParam (const char *fileName, bool header, bool password);
extern void   printGrib (const Zone *zone, const FlowP *gribData);
extern bool   readIsSea (const char *fileName);
extern int    readPoi (const char *fileName);
extern bool   writePoi (const char *fileName);
extern int    findPoiByName (const char *name, double *lat, double *lon);
extern char   *poiToStr (bool portCheck, char *str, size_t maxLength);
extern bool   smtpGribRequestPython (int type, double lat1, double lon1, double lat2, double lon2, char *command, size_t maxLength);
extern bool   checkGribToStr (char *buffer, size_t maxLength);
extern char   *dollarSubstitute (const char* str, char *res, size_t len);
extern char   *buildMeteoUrl (int type, int i, char *url);
extern char   *buildNoaaUrl (char *url, int topLat, int leftLon, int bottomLat, int rightLon, int timeStep);
extern bool   curlGet (const char *url, char *outputFile); 
extern void   updateIsSeaWithForbiddenAreas (void);
extern bool   isServerAccessible (const char *url);
extern bool   addTraceGPS (const char *fileName);
extern bool   addTracePt (const char *fileName, double lat, double lon);
extern bool   findLastTracePoint (const char *fileName, double *lat, double *lon, double *time);
extern bool   distanceTraceDone (const char *fileName, double *od, double *ld, double *rd, double *sog);
extern bool   infoDigest (const char *fileName, double *od, double *ld, double *rd, double *sog);
extern bool   isDayLight (double t, double lat, double lon);
extern void   initWayPoints (void);
extern void   initWithMostRecentGrib (void);
