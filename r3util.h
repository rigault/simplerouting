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

/*! for competitors */
extern CompetitorsList competitors;

/*! functions defined in r3util.c */
extern struct tm gribDateToTm (long intDate, double nHours);
extern bool   isDayLight (struct tm *tm0, double t, double lat, double lon);
extern char  *fSailName (int val, char *str, size_t maxLen);
extern char   *newFileNameSuffix (const char *fileName, const char *suffix, char *newFileName, size_t maxLen);
extern double offsetLocalUTC (void);
extern char   *formatThousandSep (char *buffer, size_t maxLen, int value);
extern char   *buildRootName (const char *fileName, char *rootName, size_t maxLen);
extern bool   isNumber (const char *name);
extern double getCoord (const char *str, double min, double max);
extern char   *latToStr (double lat, int type, char* str, size_t maxLen);
extern char   *lonToStr (double lon, int type, char* str, size_t maxLen);
extern char   *durationToStr (double duration, char *res, size_t maxLen);
extern time_t gribDateTimeToEpoch (long date, long time);
extern double getDepartureTimeInHour (struct tm *start);
extern char   *gribDateTimeToStr (long date, long time, char *str, size_t len);
extern char   *newDate (long intDate, double myTime, char *res, size_t maxLen);
extern char   *newDateWeekDay (long intDate, double myTime, char *res, size_t maxLen);
extern char   *newDateWeekDayVerbose (long intDate, double myTime, char *res, size_t maxLen);
extern bool   readParam (const char *fileName);
extern bool   writeParam (const char *fileName, bool header, bool password);
extern bool   readIsSea (const char *fileName);
extern void   updateIsSeaWithForbiddenAreas (void);
extern bool   mostRecentFile (const char *directory, const char *pattern0, const char *pattern1, char *name, size_t maxLen);
extern double fPenalty (int shipIndex, int type, double tws, double energy, double *cStamina);
extern double fPointLoss (int shipIndex, int type, double tws, bool fullPack);
extern double fTimeToRecupOnePoint (double tws);
extern GString *paramToJson (Par *par);


