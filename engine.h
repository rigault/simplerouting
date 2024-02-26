extern Route   route;
extern IsoDesc isoDesc  [MAX_N_ISOC];
extern Pp      isocArray [MAX_N_ISOC][MAX_SIZE_ISOC];
extern int     nIsoc;
extern int     pId;                           // global ID for points. -1 and 0 are reserved for pOr and pDest
extern Pp      lastClosest;                   // closest point to destination in last isocgrone computed

extern bool    extIsSea (double lon, double lat);
extern double  extFindPolar (double twa, double w, PolMat mat);
extern void    storeRoute (Pp pDest, double lastStepDuration);
extern void    allIsocToStr (char *str);
extern int     routing (Pp pOr, Pp pDest, double t, double dt, double *lastStepDuration);
extern void    *routingLaunch ();
extern bool    dumpAllIsoc (char *fileName);
extern bool    isoDectToStr (char *str, int maxLength);
extern bool    dumpRoute (char *fileName, Pp dest);
extern void    routeToStr (Route route, char *str);

