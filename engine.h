extern Route   route;
extern Pp      isocArray [MAX_N_ISOC][MAX_SIZE_ISOC];
extern IsoDesc isoDesc  [MAX_N_ISOC];
extern int     nIsoc;
extern Pp      lastClosest;                   // closest point to destination in last isocgrone computed

extern bool    extIsSea (double lon, double lat);
extern double  extFindPolar (double twa, double w, PolMat mat);
extern void    storeRoute (Pp pDest, double lastStepDuration);
extern void    allIsocToStr (char *str);
extern int     routing (Pp pOr, Pp pDest, double t, double dt, double *lastStepDuration);
extern void    *routingLaunch ();
extern bool    isoDectToStr (char *str, size_t maxLength);
extern bool    dumpAllIsoc (const char *fileName);
extern bool    dumpRoute (const char *fileName, Pp dest);
extern void    routeToStr (Route route, char *str);

