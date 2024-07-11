#define MAX_SIZE_SHIP_NAME    21 // see AIS specificatin
extern MyGpsData my_gps_data; 
extern GHashTable *aisTable;

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

extern bool aisTableInit (void);
extern void aisToStr (char *res, size_t maxLength);
extern char *aisGpsInfo (int type, char *strGps, int len);
extern void *getAisGps ();
extern void *getGpsNmea ();
extern void *getAisNmea ();



