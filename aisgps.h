extern MyGpsData my_gps_data; 
extern GHashTable *aisTable;

extern bool aisTableInit (void);
extern bool testAisTable ();
extern int aisToStr (char *str, size_t maxLen);
extern char *nmeaInfo (char *strGps, int len);
extern void *getNmea (gpointer x);
