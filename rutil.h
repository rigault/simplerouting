/*! forbid zones */
extern Polygon forbidZones [MAX_N_FORBID_ZONE];

/*! Meteo service */
extern struct DictElmt dicTab [];
extern struct DictProvider providerTab [];

/*! grib data description */
extern FlowP *gribData;                // wind
extern FlowP *currentGribData;         // current

extern Par par;

/*! Zone description */
extern Zone zone;                      // wind
extern Zone currentZone;               // current

/*! polar description */
extern PolMat polMat;
extern PolMat wavePolMat;

extern char *tIsSea;                   // array of byte. 0 if earth, 1 if sea
extern Poi tPoi [MAX_N_POI];
extern int nPoi;
extern int readGribRet;                // to check if readGrib is terminated
extern int readCurrentGribRet;         // to check if readCurrentGrib is terminated

//for shp files
extern int    nEntities;
extern int    nTotEntities;
extern Entity *entities;

//for gps
extern struct ThreadData thread_data;
extern pthread_t gps_thread;           //thread pour la lecture GPS
extern MyGpsData my_gps_data; 
extern pthread_mutex_t gps_data_mutex;

// for curlGet url
extern const int delay [];
extern const char *WIND_URL [];
extern const char *CURRENT_URL [];

/*! functions defined in rutil.c */
extern double lonCanonize (double lon);
extern char   *formatThousandSep (char *buffer, int value);
extern void   strip (char *str0);
extern bool   gpsToStr (char *buffer);
extern void   *gps_thread_function(void *data);
extern int    initGPS ();
extern void   closeGPS ();
extern bool   initSHP (char* nameFile);
extern void   freeSHP ();
extern char   *buildRootName (const char *fileName, char *rootName);
extern bool   isNumber (const char *name);
extern double getCoord (const char *str);
extern long   getFileSize (const char *fileName);
extern char   *latToStr (double lat, int type, char* str);
extern char   *lonToStr (double lon, int type, char* str);
extern double extTwd (double u, double v);
extern double extTws (double u, double v);
extern void   initConst ();
extern double loxCap (double lat1, double lon1, double lat2, double lon2);
extern double loxDist (double lat1, double lon1, double lat2, double lon2);
extern double orthoDist (double lat1, double lon1, double lat2, double lon2);
extern double givry (double lat1, double lon1, double lat2, double lon2);
extern bool   extIsInZone (Pp pt, Zone zone);
extern time_t dateToTime_t (long date);
extern double zoneTimeDiff (Zone zone1, Zone zone0);
extern time_t diffNowGribTime0 (Zone zone);
extern bool   readPolar (char *fileName, PolMat *mat);
extern char   *polToStr (char*str, PolMat mat);
extern void   *readGrib (void *data);
extern void   *readCurrentGrib (void *data);
extern double findTwsByIt (Pp p, int iT0);
extern bool   findFlow (Pp p, double t, double *rU, double *rV, double *rG, double *rW, Zone zone, FlowP *gribData);
extern char   *newDate (long intDate, double myTime, char *res);
extern char   *gribToStr (char *str, Zone zone);
extern double maxValInPol (PolMat mat);
extern bool   fileToStr (char *fileName, char* str);
extern bool   readParam (const char *fileName);
extern bool   writeParam (const char *fileName, bool header);
extern char   *paramToStr (char * str);
extern void   printGrib (Zone zone, FlowP *gribData);
extern bool   readIsSea (const char *fileName);
extern int    readPoi (const char *fileName);
extern bool   writePoi (const char *fileName);
extern int    findPoiByName (const char *name, double *lat, double *lon);
extern char   *poiToStr (char *str);
extern bool   smtpGribRequestPython (int type, double lat1, double lon1, double lat2, double lon2, char *command);
extern bool   checkGribToStr (char *buffer, Zone zone, FlowP *gribData);
extern char   *strchrReplace (char *source, char cIn, char cOut, char *dest);
extern void   dollarReplace (char* str);
extern char   *buildMeteoUrl (int type, int i, char *url);
extern bool   curlGet (char *url, char *outputFile); 
extern bool   isInPolygon (Point p, Polygon po);
extern bool   isInForbidArea (Pp p);
extern void   updateIsSeaWithForbiddenAreas ();


