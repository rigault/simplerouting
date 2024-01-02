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

/*! functions defined in rutil.c */
extern long   getFileSize (const char *fileName);
extern char   *latToStr (double lat, int type, char* str);
extern char   *lonToStr (double lon, int type, char* str);
extern double fTwd (double u, double v);
extern double fTws (double u, double v);
extern void   initConst ();
extern double loxCap (double lat1, double lon1, double lat2, double lon2);
extern double orthoDist (double lat1, double lon1, double lat2, double lon2);
extern double givry (double lat1, double lon1, double lat2, double lon2);
extern bool   isInZone (Pp pt, Zone zone);
extern time_t dateToTime_t (long date);
extern double zoneTimeDiff (Zone zone1, Zone zone0);
extern bool   readPolar (char *fileName, PolMat *mat);
extern char   *polToStr (char * str, PolMat mat);
extern double coeffWaves (double twa, double w, PolMat mat);
extern double estimateSog (double twa, double tws, PolMat mat);
extern void   *readGrib (void *data);
extern void   *readCurrentGrib (void *data);
extern bool   findFlow (Pp p, double t, double *rU, double *rV, double *rW, Zone zone, FlowP *gribData);
extern char   *newDate (long intDate, double myTime, char *res);
extern char   *gribToStr (char *str, Zone zone);
extern double maxValInPol (PolMat mat);
extern bool   fileToStr (char *fileName, char* str);
extern void   readParam (const char *fileName);
extern bool   writeParam (const char *fileName, bool header);
extern char   *paramToStr (char * str);
extern void   printGrib (Zone zone, FlowP *gribData);
extern bool   readIsSea (const char *fileName);
extern bool   extIsSea (double lon, double lat);
extern int    readPoi (const char *fileName);
extern char   *poiToStr (char *str);
extern void   smtpGribRequest (int type, double lat1, double lon1, double lat2, double lon2);
extern void   checkGribToStr (char *buffer, Zone zone, FlowP *gribData);

