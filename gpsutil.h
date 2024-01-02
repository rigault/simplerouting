extern struct ThreadData thread_data;
extern pthread_t gps_thread;           //thread pour la lecture GPS
extern MyGpsData my_gps_data; 

extern pthread_mutex_t gps_data_mutex;
extern bool gpsToStr (char *buffer);
extern void *gps_thread_function(void *data);
extern int  initGPS ();
extern void closeGPS ();
