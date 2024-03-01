extern SailRoute route;
extern Pp        isocArray [MAX_N_ISOC][MAX_SIZE_ISOC];
extern IsoDesc   isoDesc  [MAX_N_ISOC];
extern int       nIsoc;
extern Pp        lastClosest;                   // closest point to destination in last isocgrone computed
extern ChooseDeparture chooseDeparture;           // for choice of departure time

extern void    storeRoute (Pp pDest, double lastStepDuration);
extern void    allIsocToStr (char *str);
extern bool    isoDescToStr (char *str, size_t maxLength);
extern void    routeToStr (SailRoute route, char *str);
extern bool    dumpAllIsoc (const char *fileName);
extern bool    dumpRoute (const char *fileName, Pp dest);
extern void    *routingLaunch ();
extern void    *bestTimeDeparture ();
