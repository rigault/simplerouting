extern Route  route;
extern double tDistance [MAX_N_ISOC];       
extern Pp     isocArray [MAX_N_ISOC][MAX_SIZE_ISOC];
extern int    nIsoc;
extern int    sizeIsoc [MAX_N_ISOC];
extern int    firstInIsoc [MAX_N_ISOC];
extern int    pId;                   // global ID for points. -1 and 0 are reserved for pOr and pDest

extern void   storeRoute (Pp pDest, double lastStepDuration);
extern int    routing (Pp pOr, Pp pDest, double t, double dt, int *indexBest, double *lastStepDuration);
extern bool   dumpAllIsoc (char *fileName);
extern bool   dumpRoute (char *fileName, Pp dest);
extern void   routeToStr (Route route, char *str);

