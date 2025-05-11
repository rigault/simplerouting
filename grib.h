/*! grib data description */
extern FlowP *tGribData [];            // wind, current

extern double  zoneTimeDiff (const Zone *zone1, const Zone *zone0);
extern void    findWindGrib (double lat, double lon, double t, double *u, double *v, double *gust, double *w, double *twd, double *tws );
extern double  findRainGrib (double lat, double lon, double t);
extern double  findPressureGrib (double lat, double lon, double t);
extern void    findCurrentGrib (double lat, double lon, double t, double *uCurr, double *vCurr, double *tcd, double *tcs);
extern bool    readGribAll (const char *fileName, Zone *zone, int iFlow);
extern char    *gribToStr (const Zone *zone, char *str, size_t maxLen);
extern void    printGrib (const Zone *zone, const FlowP *gribData);
extern bool    checkGribInfoToStr (int type, Zone *zone, char *buffer, size_t maxLen);
extern bool    checkGribToStr (char *buffer, size_t maxLen);
extern GString *gribToJson (const char *gribName);











