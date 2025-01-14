extern Pp        *isocArray;                    // two dimensions array for isochrones
extern IsoDesc   *isoDesc;                      // one dimension array for isochrones meta data
extern int       nIsoc;                         // number of isochrones calculated  by routing
extern int       maxNIsoc;                      // max number of Isoc considering Grib meta information and timestep
extern Pp        lastClosest;                   // closest point to destination in last isochrone computed
extern ChooseDeparture chooseDeparture;         // for choice of departure time
extern HistoryRouteList historyRoute;           // history of calculated routes
extern SailRoute route;                         // current route

extern double  distSegment (double latX, double lonX, double latA, double lonA, double latB, double lonB);
extern void    storeRoute (SailRoute *route, const Pp *pOr, const Pp *pDest, double lastStepDuration);
extern bool    allIsocToStr (char *str, size_t maxLen);
extern bool    isoDescToStr (char *str, size_t maxLen);
extern bool    routeToStr (const SailRoute *route, char *str, size_t maxLen, char *footer, size_t maxLenFooter);
extern bool    dumpAllIsoc (const char *fileName);
extern bool    dumpRoute (const char *fileName, Pp dest);
extern void    *routingLaunch ();
extern void    *bestTimeDeparture ();
extern void    *allCompetitors ();
extern void    freeHistoryRoute ();
extern void    competitorsToStr (CompetitorsList *copyComp, char *buffer, size_t maxLen);
extern void    logReport (int n);
