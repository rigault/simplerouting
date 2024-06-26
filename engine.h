extern Pp        isocArray [MAX_N_ISOC][MAX_SIZE_ISOC];
extern IsoDesc   isoDesc  [MAX_N_ISOC];
extern int       nIsoc;
extern Pp        lastClosest;                   // closest point to destination in last isocgrone computed
extern ChooseDeparture chooseDeparture;         // for choice of departure time
extern HistoryRoute historyRoute;

extern void    storeRoute (const Pp *pOr, const Pp *pDest, double lastStepDuration);
extern bool    allIsocToStr (char *str, size_t maxLength);
extern bool    isoDescToStr (char *str, size_t maxLength);
extern bool    routeToStr (const SailRoute *route, char *str, size_t maxLength);
extern bool    dumpAllIsoc (const char *fileName);
extern bool    dumpRoute (const char *fileName, Pp dest);
extern void    *routingLaunch ();
extern void    *bestTimeDeparture ();

