extern MyGpsData my_gps_data; 
extern GHashTable *aisTable;

extern bool gpsToJson (char * str, size_t len);
extern bool aisTableInit (void);
extern bool testAisTable ();
extern int aisToStr (char *str, size_t maxLen);
extern int aisToJson (char *str, size_t maxLen);
extern char *nmeaInfo (char *strGps, size_t len);
extern void *getNmea (gpointer x);
