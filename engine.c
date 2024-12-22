/*! \Engine
* tws = True Wind Speed
* twd = True Wind Direction
* twa = True Wind Angle - the angle of the boat to the wind
* sog = Speed over Ground of the boat
* cog = Course over Ground  of the boat
* */
/*! compilation: gcc -c engine.c `pkg-config --cflags glib-2.0` */
#include <glib.h>
#include <float.h>   
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include "rtypes.h"
#include "inline.h"
#include "rutil.h"

/*! global variables */
Pp        *isocArray = NULL;                      // list of isochrones Two dimensions array : maxNIsoc  * MAX_SIZE_ISOC
//Pp        isocArray [MAX_N_ISOC * MAX_SIZE_ISOC]; // static way
IsoDesc   *isoDesc = NULL;                         // Isochrone meta data. Array one dimension.
//IsoDesc   isoDesc [MAX_N_ISOC];                   // static way
int       maxNIsoc = 0;                           // Max number of isochrones based on based on Grib zone time stamp and isochrone time step
int       nIsoc = 0;                              // total number of isochrones 
Pp        lastClosest;                            // closest point to destination in last isochrone computed

/*! store sail route calculated in engine.c by routing */  
SailRoute route;

HistoryRouteList historyRoute = { .n = 0, .r = NULL };

ChooseDeparture chooseDeparture;                  // for choice of departure time

/*! following global variable could be static but linking issue */
double    pOrToPDestCog = 0;                      // cog from pOr to pDest.

struct {
   double dd;
   double vmc;
   double nPt;
} sector [2][MAX_SIZE_ISOC];                    // we keep even and odd last sectors

int      pId = 1;                               // global ID for points. -1 and 0 are reserved for pOr and pDest
double   tDeltaCurrent = 0.0;                   // delta time in hours between wind zone and current zone
double   lastClosestDist;                       // closest distance to destination in last isochrone computed
double   lastBestVmg = 0.0;                     // best VMG found up to now


#define DEG_TO_NM (60.0)                        // Approximation : 1°latitude = 60 NM


/*! return distance from point X do segment [AB]. Meaning to H, projection of X en [AB] */
double distSegment (double latX, double lonX, double latA, double lonA, double latB, double lonB) {
    // Facteur de conversion pour la longitude en milles nautiques à la latitude de X
    double cosLatX = cos(latX * DEG_TO_RAD);

    // Conversion des latitudes et longitudes en milles nautiques
    double xA = lonA * cosLatX * DEG_TO_NM;
    double yA = latA * DEG_TO_NM;
    double xB = lonB * cosLatX * DEG_TO_NM;
    double yB = latB * DEG_TO_NM;
    double xX = lonX * cosLatX * DEG_TO_NM;
    double yX = latX * DEG_TO_NM;

    // Vecteurs AB et AX
    double ABx = xB - xA;
    double ABy = yB - yA;
    double AXx = xX - xA;
    double AXy = yX - yA;

    // Projection scalaire de AX sur AB
    double AB_AB = ABx * ABx + ABy * ABy;
    double t = (AXx * ABx + AXy * ABy) / AB_AB;

    // Limiter t à l'intervalle [0, 1] pour rester sur le segment AB
    t = fmax(0.0, fmin(1.0, t));

    // Point projeté H sur AB
    double xH = xA + t * ABx;
    double yH = yA + t * ABy;

    // Distance entre X et H
    double dx = xX - xH;
    double dy = yX - yH;

    return sqrt (dx * dx + dy * dy);
}

/*! return max speed of boat at tws for all twa */
static inline double maxSpeedInPolarAt (double tws, const PolMat *mat) {
   double max = 0.0;
   double speed;
   for (int i = 1; i < mat->nLine; i++) {
      speed = findPolar (mat->t [i][0], tws, *mat);
      if (speed > max) max = speed;
   }
   return max;
}

/*! find first point in isochrone. Useful for drawAllIsochrones */ 
static inline int findFirst (int nIsoc) {
   int best = 0;
   int next;
   double d;
   double distMax = 0;
   for (int i = 0; i < isoDesc [nIsoc].size; i++) {
      next = (i >= (isoDesc[nIsoc].size -1)) ? 0 : i + 1;
      d = orthoDist (isocArray [nIsoc * MAX_SIZE_ISOC + i].lat, isocArray [nIsoc * MAX_SIZE_ISOC + i].lon, 
                     isocArray [nIsoc * MAX_SIZE_ISOC + next].lat, isocArray [nIsoc * MAX_SIZE_ISOC + next].lon);
      if (d > distMax) {
         distMax = d;
         best = next;
      }
   }
   return best;
}

/*! initialization of sector */
static inline void initSector (int nIsoc, int nMax) {
   for (int i = 0; i < nMax; i++) {
      sector [nIsoc][i].dd = DBL_MAX;
	   sector [nIsoc][i].vmc = 0;
      sector [nIsoc][i].nPt = 0;
   }
}

/*! reduce the size of Isolist 
   note influence of parameters par.nSector, par.jFactor and par.kFactor */
static inline int forwardSectorOptimize (const Pp *pOr, const Pp *pDest, int nIsoc, const Pp *isoList, int isoLen, Pp *optIsoc) {
   const double epsilon = 0.1;
   int iSector, k;
   double alpha, theta;
   double thetaStep = 360.0 / par.nSectors;
   Pp ptTarget;
   double dLat = -par.jFactor * cos (DEG_TO_RAD * pOrToPDestCog);  // nautical miles in N S direction
   double dLon = -par.jFactor * sin (DEG_TO_RAD * pOrToPDestCog) / cos (DEG_TO_RAD * (pOr->lat + pDest->lat)/2); // nautical miles in E W direction

   memset (&ptTarget, 0, sizeof (Pp));
   ptTarget.lat = pOr->lat + dLat / 60; 
   ptTarget.lon = pOr->lon + dLon / 60;
   isoDesc [nIsoc].focalLat = ptTarget.lat;
   isoDesc [nIsoc].focalLon = ptTarget.lon;
  
   double beta = orthoCap (pOr->lat, pOr->lon, pDest->lat, pDest->lon);
   initSector (nIsoc % 2, par.nSectors);
   for (int i = 0; i < par.nSectors; i++)
      optIsoc [i].lat = DBL_MAX;

   for (int i = 0; i < isoLen; i++) {
      alpha = orthoCap (ptTarget.lat, ptTarget.lon, isoList [i].lat, isoList [i].lon);
      theta = beta - alpha;
      if (theta < 0) theta += 360;
      if (fabs (theta) <= 360) {
         iSector = round ((360.0 - theta) / thetaStep);
		   if ((isoList [i].dd < sector [nIsoc%2][iSector].dd) && (isoList [i].vmc > sector [nIsoc%2] [iSector].vmc)) {
            sector [nIsoc%2][iSector].dd = isoList [i].dd;
            sector [nIsoc%2][iSector].vmc = isoList [i].vmc;
            optIsoc [iSector] = isoList [i];
         }
         sector [nIsoc%2][iSector].nPt += 1;
      }
   }
   // we keep only sectors that match conditions (not empti, vmc not too bad, kFactor dependant condition)
   k = 0;
   for (iSector = 0; iSector < par.nSectors; iSector++) {
      if ((sector [nIsoc%2][iSector].dd < DBL_MAX -1) 
         && (sector [nIsoc%2][iSector].vmc > 0.6 * lastBestVmg) 
         && (sector [nIsoc%2][iSector].vmc < pOr->dd * 1.1) 
         && ((par.kFactor == 0)
            || ((par.kFactor == 1) && (sector [nIsoc%2][iSector].vmc >= sector [(nIsoc-1)%2][iSector].vmc)) // Best !
            || ((par.kFactor == 2) && (sector [nIsoc%2][iSector].dd <= sector [(nIsoc-1)%2][iSector].dd))
            || ((par.kFactor == 3) && (sector [nIsoc%2][iSector].vmc >= sector [(nIsoc-1)%2][iSector].vmc) && \
                  (sector [nIsoc%2][iSector].dd <= sector [(nIsoc-1)%2][iSector].dd))
            || (fabs (sector [nIsoc%2][iSector].dd - sector [(nIsoc-1)%2][iSector].dd) < epsilon)))        // no wind, no change, we keep
      {
         optIsoc [k] = optIsoc [iSector];
         optIsoc [k].sector = iSector;
	      k += 1;
      }
   }
   return k; 
}

/*! choice of algorithm used to reduce the size of Isolist */
static inline int optimize (const Pp *pOr, const Pp *pDest, int nIsoc, int algo, const Pp *isoList, int isoLen, Pp *optIsoc) {
   switch (algo) {
      case 0: 
         memcpy (optIsoc, isoList, isoLen * sizeof (Pp)); 
         return isoLen;
      case 1:
         return forwardSectorOptimize (pOr, pDest, nIsoc, isoList, isoLen, optIsoc);
   } 
   return 0;
}

/*! build the new list describing the next isochrone, starting from isoList 
  returns length of the newlist built or -1 if error*/
static int buildNextIsochrone (const Pp *pOr, const Pp *pDest, const Pp *isoList, int isoLen, double t, double dt, Pp *newList, double *bestVmg) {
   Pp newPt;
   bool motor = false;
   int lenNewL = 0, directCog;
   double u, v, gust, w, twa, sog, uCurr, vCurr, currTwd, currTws, vDirectCap;
   double dLat, dLon, penalty;
   double twd = par.constWindTwd, tws = par.constWindTws;
   double waveCorrection = 1.0;
   *bestVmg = 0;
   
   for (int k = 0; k < isoLen; k++) {
      if (!isInZone (isoList [k].lat, isoList [k].lon, &zone) && (par.constWindTws == 0))
         continue;
      findWindGrib (isoList [k].lat, isoList [k].lon, t, &u, &v, &gust, &w, &twd, &tws);
      if (tws > par.maxWind)
         continue; // avoid location where wind speed too high...
      findCurrentGrib (isoList [k].lat, isoList [k].lon, t - tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);
      vDirectCap = orthoCap (isoList [k].lat, isoList [k].lon, pDest->lat, pDest->lon);
      directCog = ((int)(vDirectCap / par.cogStep)) * par.cogStep;
      motor = (maxSpeedInPolarAt (tws * par.xWind, &polMat) < par.threshold) && (par.motorSpeed > 0);

      for (int cog = (directCog - par.rangeCog); cog < (directCog + par.rangeCog + 1); cog+=par.cogStep) {
         twa = fTwa (cog, twd);
         //printf ("twd: %.0lf, cog: %d, twa: %.0lf\n", twd, cog, twa); 
         //angle of the boat with the wind
         newPt.amure = (twa > 0) ? TRIBORD : BABORD;
         newPt.toIndexWp = pDest->toIndexWp;
         //speed of the boat considering twa and wind speed (tws) in polar
         double efficiency = (isDayLight (t, isoList [k].lat, isoList [k].lon)) ? par.dayEfficiency : par.nightEfficiency;
         sog = (motor) ? par.motorSpeed : efficiency * findPolar (twa, tws * par.xWind, polMat); 
         newPt.motor = motor;
         if ((w > 0) && ((waveCorrection = findPolar (twa, w, wavePolMat)) > 0)) {
            // printf ("sog before wave: %.2lf, after: %.2lf\n", sog, sog * waveCorrection / 100.0);
            sog = sog * (waveCorrection / 100.0);
         }
         if ((!motor) && (newPt.amure != isoList [k].amure)) {       // changement amure
            if (fabs (twa) < 90) penalty = par.penalty0 / 60.0;      // virement de bord
            else penalty = par.penalty1 / 60.0;                      // empannage
         }
         else penalty = 0;
         // printf ("sog = %.2lf\n", sog);
         
         dLat = sog * (dt - penalty) * cos (DEG_TO_RAD * cog);            // nautical miles in N S direction
         dLon = sog * (dt - penalty) * sin (DEG_TO_RAD * cog) / cos (DEG_TO_RAD * isoList [k].lat);  // nautical miles in E W direction
         
         // printf ("vcurr : %.2lf, vcurr : %.2lf\n", vCurr, uCurr);
         dLat += MS_TO_KN * vCurr * dt;                   // correction for current 
         dLon += MS_TO_KN * uCurr * dt / cos (DEG_TO_RAD * isoList [k].lat); 
 
	      // printf ("builN dLat %lf dLon %lf\n", dLat, dLon);
         newPt.lat = isoList [k].lat + dLat/60;
         newPt.lon = isoList [k].lon + dLon/60;
         newPt.id = pId;
         newPt.father = isoList [k].id;
         newPt.vmc = 0;
         newPt.sector = 0;
         if (par.allwaysSea || isSea (tIsSea, newPt.lat, newPt.lon)) {
            newPt.dd = orthoDist (newPt.lat, newPt.lon, pDest->lat, pDest->lon);
            double alpha = orthoCap (pOr->lat, pOr->lon, newPt.lat, newPt.lon) - pOrToPDestCog;
         
            newPt.vmc = orthoDist (newPt.lat, newPt.lon, pOr->lat, pOr->lon) * cos (DEG_TO_RAD * alpha);
            if (newPt.vmc > *bestVmg) *bestVmg = newPt.vmc;
            if (lenNewL < MAX_SIZE_ISOC) {
               newList [lenNewL++] = newPt;                      // new point added to the isochrone
            }
            else {
               fprintf (stderr, "Error in buildNextIsochrone: Limit of MAX_SIZE_ISOC reached: %d\n", MAX_SIZE_ISOC);
               return -1;
            }
            pId += 1;
         }
      }
   }
   return lenNewL;
}

/*! find father of point in previous isochrone  */
static int findFather (int ptId, int i, int lIsoc) {
   for (int k = 0; k < lIsoc; k++) {
      if (isocArray [i * MAX_SIZE_ISOC + k].id == ptId)
         return k;
   }
   fprintf (stderr, "Error in father: ptId not found: %d, Isoc No:%d, Isoc Len: %d\n", ptId, i, lIsoc);
   return -1;
}

/*! copy isoc Descriptors in a string 
   true if enough space, false if truncated */
bool isoDescToStr (char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE], strLon [MAX_SIZE_LINE];
   const int DIST_MAX = 100000;
   double distance;
   if (maxLen < MAX_SIZE_LINE) 
      return false;
   g_strlcpy (str, "No toWp Size First Closest Distance VMC      FocalLat    FocalLon\n", MAX_SIZE_LINE);
   for (int i = 0; i < nIsoc; i++) {
      distance = isoDesc [i].distance;
      if (distance > DIST_MAX) distance = -1;
      snprintf (line, MAX_SIZE_LINE, "%03d %03d %03d  %03d   %03d     %07.2lf  %07.2lf  %s  %s\n", i, isoDesc [i].toIndexWp, 
         isoDesc[i].size, isoDesc[i].first, 
         isoDesc[i].closest, distance, isoDesc[i].bestVmg, 
         latToStr (isoDesc [i].focalLat, par.dispDms, strLat, sizeof (strLat)), 
         lonToStr (isoDesc [i].focalLon, par.dispDms, strLon, sizeof (strLon)));
      if ((strlen (str) + strlen (line)) > maxLen) 
         return false;
      g_strlcat (str, line, maxLen);
   }
   return true;
}

/*! copy all isoc in a string 
   true if enough space, false if truncated */
bool allIsocToStr (char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   Pp pt;
   if (maxLen < MAX_SIZE_LINE) 
      return false;
   g_strlcpy (str, "No  Lat         Lon               Id Father  Amure  Motor\n", MAX_SIZE_LINE);
   for (int i = 0; i < nIsoc; i++) {
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = isocArray [i * MAX_SIZE_ISOC + k];
         snprintf (line, MAX_SIZE_LINE, "%03d %-12s %-12s %6d %6d %6d %6d\n", i, latToStr (pt.lat, par.dispDms, strLat, sizeof (strLat)),\
            lonToStr (pt.lon, par.dispDms, strLon, sizeof (strLon)), pt.id, pt.father, pt.amure, pt.motor);
         if ((strlen (str) + strlen (line)) > maxLen) 
            return false;
         g_strlcat (str, line, maxLen);
      }
   }
   return true;
}
   
/*! write in CSV file Isochrones */
bool dumpAllIsoc (const char *fileName) {
   FILE *f;
   Pp pt;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "Error in dumpAllIsoc: Cannot write isoc: %s\n", fileName);
      return false;
   }
   fprintf (f, "n;     Lat;   Lon;      Id; Father;  Amure;  Motor\n");
   for (int i = 0; i < nIsoc; i++) {
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = isocArray [i * MAX_SIZE_ISOC + k];
         fprintf (f, "%03d; %06.2f; %06.2f; %6d; %6d; %6d; %6d\n", i, pt.lat, pt.lon, pt.id, pt.father, pt.amure, pt.motor);
      }
   }
   fprintf (f, "\n");
   fclose (f);
   return true;
}

/*! write in CSV file routes */
bool dumpRoute (const char *fileName, Pp dest) {
   FILE *f = NULL;
   int i;
   int iFather;
   int dep = (dest.id == 0) ? nIsoc : nIsoc - 1;
   Pp pt = dest;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "Error in dumpRoute: Cannot write route: %s\n", fileName);
      return false;
   }
   fprintf (f, "%4d; %06.2f; %06.2f; %4d; %4d\n", dep, pt.lat, pt.lon, pt.id, pt.father);
      
   for (i = dep - 1; i >= 0; i--) {
      iFather = findFather (pt.father, i, isoDesc[i].size);
      pt = isocArray [i * MAX_SIZE_ISOC + iFather];
      fprintf (f, "%4d; %06.2f; %06.2f; %4d; %4d\n", i, pt.lat, pt.lon, pt.id, pt.father);
   }
   pt = par.pOr;
   fprintf (f, "%4d; %06.2f; %06.2f; %4d; %4d\n", -1, pt.lat, pt.lon, pt.id, pt.father);
   fclose (f);
   return true;
}

/*! store current route in history */
static void saveRoute (SailRoute *route) {
   // allocate or rallocate space for routes
   SailRoute *newRoutes = realloc (historyRoute.r, (historyRoute.n + 1) * sizeof(SailRoute));
   if (newRoutes == NULL) {
      fprintf (stderr, "Error in saveRoute: Memory allocation failed\n");
      return;
   }
   historyRoute.r = newRoutes;
   historyRoute.r[historyRoute.n] = *route; // Copy simple filld witout pointerq

   // Allocation and copy of points
   size_t pointsSize = (route->nIsoc + 1) * sizeof (SailPoint);
   historyRoute.r[historyRoute.n].t = malloc (pointsSize);
   if (! historyRoute.r[historyRoute.n].t) {
      fprintf (stderr, "Error in saveRoute: Memory allocation for SailPoint array failed\n");
      return;
   }
   memcpy (historyRoute.r[historyRoute.n].t, route->t, pointsSize); // deep copy of points
   
   historyRoute.n += 1;
}


/* free space for history route */
void freeHistoryRoute () {
   // for (int i = 0; i < historyRoute.n; i += 1) free (historyRoute.r [i].t); // STATIC
   free (historyRoute.r);
   historyRoute.r = NULL;
   historyRoute.n = 0;
}

/*! stat route */
static void statRoute (SailRoute *route, double lastStepDuration) {
   Pp p = {0};
   if (route->n == 0) return;
   route->totDist = route->motorDist = route->tribordDist = route->babordDist = 0;
   route->lastStepDuration = lastStepDuration;
   route->duration = route->motorDuration = 0;
   route->maxTws = route->maxGust = route->maxWave = 0;
   route->avrTws = route->avrGust = route->avrWave = 0;
   for (int i = 1; i < route->n; i++) {
      p.lat = route->t [i-1].lat;
      p.lon = route->t [i-1].lon;
      route->t [i-1].time = par.tStep * (i - 1) + par.startTimeInHours;
      route->t [i-1].lCap = directCap (route->t[i-1].lat, route->t[i-1].lon, route->t[i].lat, route->t[i].lon);
      route->t [i-1].oCap = orthoCap (route->t[i-1].lat, route->t[i-1].lon, route->t[i].lat, route->t[i].lon);
      route->t [i-1].ld  = loxoDist (route->t[i-1].lat, route->t[i-1].lon, route->t[i].lat, route->t[i].lon);
      route->t [i-1].od = orthoDist (route->t[i-1].lat, route->t[i-1].lon, route->t[i].lat, route->t[i].lon);
      route->t [i-1].sog = route->t [i-1].od / par.tStep;
      route->totDist += route->t [i-1].od;
      // printf ("i = %d, od = %.2lf, tot = %.2lf\n", i, route->t [i-1].od, route->totDist);
      route->duration += par.tStep;
      if (route->t [i-1].motor) {
         route->motorDuration += par.tStep;
         route->motorDist += route->t [i-1].od;
      }
      else 
         if (route->t [i-1].amure == TRIBORD)
            route->tribordDist += route->t [i-1].od;
         else
            route->babordDist += route->t [i-1].od;
      findWindGrib (p.lat, p.lon, route->t [i-1].time, &route->t [i-1].u, &route->t [i-1].v, &route->t [i-1].g, &route->t [i-1].w, 
         &route->t [i-1].twd, &route->t [i-1].tws);
      route->avrTws  += route->t [i-1].tws;
      route->avrGust += route->t [i-1].g;
      route->avrWave += route->t [i-1].w;
      route->maxTws  = MAX (route->maxTws, route->t [i-1].tws);
      route->maxGust = MAX (route->maxGust, route->t [i-1].g);
      route->maxWave = MAX (route->maxWave, route->t [i-1].w);
      route->maxSog = MAX (route->maxSog, route->t [i-1].sog);
   }
   route->t [route->n-1].time = par.tStep * (route->n-1) + par.startTimeInHours;
   route->duration += lastStepDuration;
   if (route->t [route->n-1].motor) {
      route->motorDuration += lastStepDuration;
      route->motorDist += route->t [route->n-1].od;
   }
   else 
      if (route->t [route->n-1].amure == TRIBORD)
         route->tribordDist += route->t [route->n-1].od;
      else
         route->babordDist += route->t [route->n-1].od;
   p.lat = route->t [route->n-1].lat;
   p.lon = route->t [route->n-1].lon;
   findWindGrib (p.lat, p.lon, route->t [route->n-1].time, &route->t [route->n-1].u, &route->t [route->n-1].v, &route->t [route->n-1].g, 
      &route->t [route->n-1].w, &route->t [route->n-1].twd, &route->t [route->n-1].tws);
   
   route->maxTws  = MAX (route->maxTws, route->t [route->n-1].tws);
   route->maxGust = MAX (route->maxGust, route->t [route->n-1].g);
   route->maxWave = MAX (route->maxWave, route->t [route->n-1].w);
   route->avrTws  += route->t [route->n-1].tws;
   route->avrGust += route->t [route->n-1].g;
   route->avrWave += route->t [route->n-1].w;

   if (route->destinationReached) {
      route->t [route->n-1].lCap = directCap (route->t[route->n-1].lat, route->t[route->n-1].lon, par.pDest.lat, par.pDest.lon);
      route->t [route->n-1].oCap = orthoCap (route->t[route->n-1].lat, route->t[route->n-1].lon, par.pDest.lat, par.pDest.lon);
      route->t [route->n-1].ld = loxoDist (route->t[route->n-1].lat, route->t[route->n-1].lon, par.pDest.lat, par.pDest.lon);
      route->t [route->n-1].od = orthoDist (route->t[route->n-1].lat, route->t[route->n-1].lon, par.pDest.lat, par.pDest.lon);
      route->t [route->n-1].sog =  route->t [route->n-1].od / route->lastStepDuration;
      route->maxSog = MAX (route->maxSog, route->t [route->n-1].sog);
      route->totDist += route->t [route->n-1].od;
   }
   route->avrTws /= route->n;
   route->avrGust /= route->n;
   route->avrWave /= route->n;
   route->avrSog = route->totDist / route->duration;
}

/*! store route */
void storeRoute (SailRoute *route, const Pp *pOr, const Pp *pDest, double lastStepDuration) {
   // route->destinationReached = (pDest.id == 0);
   int iFather;
   route->nIsoc = nIsoc;
   route->n =  (pDest->id == 0) ? nIsoc + 2 : nIsoc + 1;
   if (nIsoc == 0) {
      route->n = 1;
      statRoute (route, lastStepDuration);
      return;
   }
   Pp pt = *pDest;
   Pp ptLast = *pDest;
   route->t [route->n - 1].lat = pDest->lat;
   route->t [route->n - 1].lon = lonCanonize (pDest->lon);
   route->t [route->n - 1].id = pDest->id;
   route->t [route->n - 1].father = pDest->father;
   route->t [route->n - 1].motor = pDest->motor;
   route->t [route->n - 1].amure = pDest->amure;
   route->t [route->n - 1].toIndexWp = pDest->toIndexWp;
   for (int i = route->n - 3; i >= 0; i--) {
      iFather = findFather (pt.father, i, isoDesc[i].size);
      if (iFather == -1) continue;
      pt = isocArray [i * MAX_SIZE_ISOC + iFather];
      route->t [i+1].lat = pt.lat;
      route->t [i+1].lon = lonCanonize (pt.lon);
      route->t [i+1].id = pt.id;
      route->t [i+1].father = pt.father;
      route->t [i+1].motor = ptLast.motor;
      route->t [i+1].amure = ptLast.amure;
      route->t [i+1].toIndexWp = pt.toIndexWp;
      ptLast = pt;
   }
   route->t [0].lat = pOr->lat;
   route->t [0].lon = lonCanonize (pOr->lon);
   route->t [0].id = pOr->id;
   route->t [0].father = pOr->father;
   route->t [0].motor = ptLast.motor;
   route->t [0].amure = ptLast.amure;
   route->t [0].toIndexWp = pt.toIndexWp;
   statRoute (route, lastStepDuration);
   saveRoute (route);
}

/*! produce string that says if Motor, Tribord, Babord */
static char *motorTribordBabord (bool motor, int amure, char* str, size_t maxLen) {
   if (motor)
      g_strlcpy (str, "Mot", maxLen);
   else
      if (amure == TRIBORD)
         g_strlcpy (str, "Tri", maxLen);
      else
         g_strlcpy (str, "Bab", maxLen);
   return str;
}

/*! copy route in a string
   true if enough space, false if truncated */
bool routeToStr (const SailRoute *route, char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE], strDate [MAX_SIZE_DATE];
   char strLat [MAX_SIZE_NAME], strLon [MAX_SIZE_NAME];  
   char shortStr [SMALL_SIZE], strDur [MAX_SIZE_LINE], strMot [MAX_SIZE_LINE];
   double awa, aws;
   double maxAws = 0.0;
   double sumAws = 0.0;
   double twa = fTwa (route->t[0].lCap, route->t[0].twd);
   fAwaAws (twa, route->t[0].tws, route->t[0].sog, &awa, &aws);
   if (maxLen < MAX_SIZE_LINE)
      return false;

   g_strlcpy (str, "  No  WP Lat        Lon         Date-Time         M/T/B  COG\
     Dist     SOG  Twd   Twa      Tws    Gust  Awa      Aws   Waves\n", MAX_SIZE_LINE);
   snprintf (line, MAX_SIZE_LINE, " pOr %3d %-12s%-12s %6s %6s %4d° %7.2lf %7.2lf %4d° %4.0lf° %7.2lf %7.2lf %4.0lf° %7.2lf %7.2lf\n",\
      route->t[0].toIndexWp,\
      latToStr (route->t[0].lat, par.dispDms, strLat, sizeof (strLat)),\
      lonToStr (route->t[0].lon, par.dispDms, strLon, sizeof (strLon)),\
      newDate (zone.dataDate [0], (zone.dataTime [0]/100) + route->t[0].time, strDate, sizeof (strDate)),\
      motorTribordBabord (route->t[0].motor, route->t[0].amure, shortStr, SMALL_SIZE),\
      ((int) (route->t[0].lCap + 360) % 360), route->t[0].od, route->t[0].sog,\
      (int) (route->t[0].twd + 360) % 360,\
      twa, route->t[0].tws,\
      MS_TO_KN * route->t[0].g, awa, aws, route->t[0].w);

   g_strlcat (str, line, maxLen);
   for (int i = 1; i < route->n; i++) {
      twa = fTwa (route->t[i].lCap, route->t[i].twd);
      fAwaAws (twa, route->t[i].tws, route->t[i].sog, &awa, &aws);
      if (aws > maxAws) maxAws = aws;
      sumAws += aws;
      if ((fabs(route->t[i].lon) > 180.0) || (fabs (route->t[i].lat) > 90.0))
         snprintf (line, MAX_SIZE_LINE, " Isoc %3d: Error on latitude or longitude\n", i-1);   
      else
         snprintf (line, MAX_SIZE_LINE, "%4d %3d %-12s%-12s %s %6s %4d° %7.2f %7.2lf %4d° %4.0lf° %7.2lf %7.2lf %4.0lf° %7.2lf %7.2lf\n",\
            i-1,\
            route->t[i].toIndexWp,\
            latToStr (route->t[i].lat, par.dispDms, strLat, sizeof (strLat)),\
            lonToStr (route->t[i].lon, par.dispDms, strLon, sizeof (strLon)),\
            newDate (zone.dataDate [0], (zone.dataTime [0]/100) + route->t[i].time, strDate, sizeof (strDate)), \
            motorTribordBabord (route->t[i].motor, route->t[i].amure, shortStr, SMALL_SIZE), \
            ((int) (route->t[i].lCap  + 360) % 360), route->t[i].od, route->t[i].sog,\
            (int) (route->t[i].twd + 360) % 360,\
            fTwa (route->t[i].lCap, route->t[i].twd), route->t[i].tws,\
            MS_TO_KN * route->t[i].g, awa, aws, route->t[i].w);
      g_strlcat (str, line, maxLen);
   }
   
   snprintf (line, MAX_SIZE_LINE, " Avr %65s  %5.2lf             %7.2lf %7.2lf       %7.2lf %7.2lf\n", " ", \
      route->avrSog, route->avrTws, MS_TO_KN * route->avrGust, sumAws / route->n, route->avrWave); 
   g_strlcat (str, line, maxLen);
   snprintf (line, MAX_SIZE_LINE, " Max %65s  %5.2lf             %7.2lf %7.2lf       %7.2lf %7.2lf\n", " ", \
      route->maxSog, route->maxTws, MS_TO_KN * route->maxGust, maxAws, route->maxWave); 
   g_strlcat (str, line, maxLen);
   snprintf (line, MAX_SIZE_LINE, "\n Total distance   : %7.2lf NM,   Motor distance: %7.2lf NM\n", route->totDist, route->motorDist);
   g_strlcat (str, line, maxLen);
   snprintf (line, MAX_SIZE_LINE, " Total Duration   : %s, Motor Duration: %s\n", \
      durationToStr (route->duration, strDur, sizeof (strDur)), durationToStr (route->motorDuration, strMot, sizeof (strMot)));
   g_strlcat (str, line, maxLen);

   newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route->duration, strDate, sizeof (strDate));
   snprintf (line, MAX_SIZE_LINE, " Arrival Date&Time: %s", newDate (zone.dataDate [0], \
      zone.dataTime [0]/100 + par.startTimeInHours + route->duration, strDate, sizeof (strDate)));
   g_strlcat (str, line, maxLen);
   return true;
}

/*! return true if pDest can be reached from pA - in fact segment [pA pB] - in less time than dt
   bestTime give the time to get from pFrom to pDest 
   motor true if goal reached with motor */
static inline bool goalP (const Pp *pA, const Pp *pB, const Pp *pDest, double t, double dt, double *timeTo, double *distance, bool *motor, int *amure) {
   double u, v, gust, w, twd, tws, twa, sog;
   double coeffLat = cos (DEG_TO_RAD * (pA->lat + pDest->lat)/2);
   double dLat = pDest->lat - pA->lat;
   double dLon = pDest->lon - pA->lon;
   double cog = RAD_TO_DEG * atan2 (dLon * coeffLat, dLat);
   double penalty;
   const double EPSILON = 0.1;
   *motor = false;
   double waveCorrection = 1.0;
   double distToSegment = distSegment (pDest->lat, pDest->lon, pA->lat, pA->lon, pB->lat, pB->lon); 

   *distance = orthoDist (pDest->lat, pDest->lon, pA->lat, pA->lon);
   
   findWindGrib (pA->lat, pA->lon, t, &u, &v, &gust, &w, &twd, &tws);
   // findCurrentGrib (pFrom->lat, pFrom->lon, t - tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);
   // ATTENTION Courant non pris en compte dans la suite !!!
   twa = fTwa (cog, tws);      // angle of the boat with the wind
   *amure = (twa > 0) ? TRIBORD : BABORD;
   // *motor =  ((maxSpeedInPolarAt (tws * par.xWind, &polMat) < par.threshold) && (par.motorSpeed > 0)); // ATT
   // printf ("maxSpeedinPolar: %.2lf\n", maxSpeedInPolarAt (tws, &polMat));
   double efficiency = (isDayLight (t, pA->lat, pA->lon)) ? par.dayEfficiency : par.nightEfficiency;
   sog = (*motor) ? par.motorSpeed : efficiency * findPolar (twa, tws * par.xWind, polMat); 
   if ((w > 0) && ((waveCorrection = findPolar (twa, w, wavePolMat)) > 0)) { 
      sog = sog * (waveCorrection / 100.0);
   }
   if (sog > EPSILON)
      *timeTo = *distance/sog;
   else {
      *timeTo = DBL_MAX;
      return false;
   }
   // printf ("distance: %.2lf, twa : %.2lf, tws: %.2lf, sog:%.2lf, timeTo:%.2lf \n", *distance, twa, tws, sog, *timeTo);
   if ((!(*motor)) && (pDest->amure != pA->amure)) {     // changement amure
      if (fabs (twa) < 90) penalty = par.penalty0 / 60.0;   // virement de bord
      else penalty = par.penalty1 / 60.0;                   // empannage Jibe
   }
   else penalty = 0;
   // printf ("In goalP: distance = %.2lf, sog = %.2lf motor = %d\n", *distance, sog, *motor);
   return ((sog * (dt - penalty)) > distToSegment); // distToSegment considered as the real distance
}

/*! return closest point to pDest in Isoc 
  true if goal can be reached directly in dt from isochrone 
  side effect : pDest.father can be modified ! */
static inline bool goal (Pp *pDest, int nIsoc, const Pp *isoList, int len, double t, double dt, double *lastStepDuration, bool *motor, int *amure) {
   double bestTime = DBL_MAX;
   double time, distance;
   bool destinationReached = false;
   isoDesc [nIsoc].distance = 9999.99;

   for (int k = 1; k < len; k++) {
      if (par.allwaysSea || isSea (tIsSea, isoList [k].lat, isoList [k].lon)) { 
         if (goalP (&isoList [k-1], &isoList [k], pDest, t, dt, &time, &distance, motor, amure)) {
            destinationReached = true;
            printf ("Destination reached point: %d\n", k);
         }
         if (time < bestTime) {
            bestTime = time;
            if (destinationReached) {
               pDest->father = isoList [k-1].id; // ATTENTION !!!! we choose k-1. It could be k.
               pDest->motor = *motor;
               pDest->amure = *amure;
            }
         }
         if (distance < isoDesc [nIsoc].distance) {
            isoDesc [nIsoc].distance = distance;
         }
      }
   }
   *lastStepDuration = bestTime;
   return destinationReached;
}

/*! return closest point to pDest in Isoc */ 
static inline Pp closest (const Pp *isoc, int n, const Pp *pDest, int *index) {
   Pp best = isoc [0];
   double d;
   int i;
   *index = -1;
   lastClosestDist = DBL_MAX;
   for (i = 0; i < n; i++) {
      d = orthoDist (pDest->lat, pDest->lon, isoc [i].lat, isoc [i].lon);
      if (d < lastClosestDist) {
         lastClosestDist = d;
         best = isoc [i];
         *index = i;
      }
   }
   return best;
}

/*! when no wind build next isochrone as a replica of previous isochrone */
static void replicate (int n) {
   int len = isoDesc [n - 1].size;
   for (int i = 0; i < len; i++) {
      isocArray [n * MAX_SIZE_ISOC + i].id = isocArray [(n-1) * MAX_SIZE_ISOC + i].id + len; 
      isocArray [n * MAX_SIZE_ISOC + i].father = isocArray [(n-1) * MAX_SIZE_ISOC + i].id; 
      isocArray [n * MAX_SIZE_ISOC + i].amure = isocArray [(n-1) * MAX_SIZE_ISOC + i].amure; 
      isocArray [n * MAX_SIZE_ISOC + i].sector = isocArray [(n-1) * MAX_SIZE_ISOC + i].sector; 
      isocArray [n * MAX_SIZE_ISOC + i].lat = isocArray [(n-1) * MAX_SIZE_ISOC + i].lat; 
      isocArray [n * MAX_SIZE_ISOC + i].lon = isocArray [(n-1) * MAX_SIZE_ISOC + i].lon; 
      isocArray [n * MAX_SIZE_ISOC + i].dd = isocArray [(n-1) * MAX_SIZE_ISOC + i].dd; 
      isocArray [n * MAX_SIZE_ISOC + i].vmc = isocArray [(n-1) * MAX_SIZE_ISOC + i].vmc; 
   }
   isoDesc [n].distance = isoDesc [n-1].distance;
   isoDesc [n].bestVmg = isoDesc [n-1].bestVmg;
   isoDesc [n].size = isoDesc [n-1].size;
   isoDesc [n].focalLat = isoDesc [n-1].focalLat;
   isoDesc [n].focalLon = isoDesc [n-1].focalLon;
   isoDesc [n].toIndexWp = isoDesc [n-1].toIndexWp;
}

/*! find optimal routing from p0 to pDest using grib file and polar
    return number of steps to reach pDest, NIL if unreached, -1 if problem, -2 if stopped by user, 
    0 reserved for not terminated
    return also lastStepDuration if the duration of last step if destination reached (0 if unreached) */
static int routing (Pp *pOr, Pp *pDest, int toIndexWp, double t, double dt, double *lastStepDuration) {
   bool motor = false;
   int amure;
   double distance;
   double timeToReach = 0;
   int lTempList = 0;
   double timeLastStep;
   int index;                       // index of closest point to pDest in insochrone
   double theBestVmg;
   Pp *tempList = NULL;             // one dimension array of points
   
   const double MIN_STEP = 0.25;

   if (dt < MIN_STEP) {
      fprintf (stderr, "In routing: time step for routing <= %.2lf\n", MIN_STEP);
      return -1;
   }

   if ((tempList = malloc (MAX_SIZE_ISOC * sizeof(Pp))) == NULL) {
      fprintf (stderr, "in Routing: error in memory templIst allocation\n");
      return -1;
   }

   maxNIsoc = (int) ((1 + zone.timeStamp [zone.nTimeStamp - 1]) / dt);
   if (maxNIsoc > MAX_N_ISOC) {
      fprintf (stderr, "in Routing maxNIsoc exeed MAX_N_ISOC\n");
      free (tempList);
      return -1;
   } 
   printf ("time Step: %.2lf, maxNIsoc: %d\n", dt, maxNIsoc);

   Pp *tempIsocArray = (Pp*) realloc (isocArray, maxNIsoc * MAX_SIZE_ISOC * sizeof(Pp));
   if (tempIsocArray == NULL) {
      fprintf (stderr, "in Routing: realloc error for isocArray\n");
      free (tempList);
      return -1;
   }
   isocArray = tempIsocArray;
   
   IsoDesc *tempIsoDesc = (IsoDesc *) realloc (isoDesc, maxNIsoc * sizeof (IsoDesc));
   if (tempIsoDesc == NULL) {
      fprintf (stderr, "in Routing: realloc for IsoDesc failed\n");
      free (isocArray);
      free (tempList);
      return -1;
   } 
   isoDesc = tempIsoDesc;
    
   SailPoint *tempSailPoint = (SailPoint *) realloc (route.t, (maxNIsoc + 1) * sizeof(SailPoint));
   if (tempSailPoint  == NULL) {
      fprintf (stderr, "in Routing: realloc for route.t failed\n");
      free (isoDesc);
      free (isocArray);
      free (tempList);
      return -1;
   } 
   route.t = tempSailPoint;

   pOr->dd = orthoDist (pOr->lat, pOr->lon, pDest->lat, pDest->lon);
   pOr->vmc = 0;
   pOrToPDestCog = orthoCap (pOr->lat, pOr->lon, pDest->lat, pDest->lon);
   pDest->toIndexWp = toIndexWp;
   lastClosestDist = pOr->dd;
   lastBestVmg = 0;
   tempList [0] = *pOr;             // list with just one elemnet;
   initSector (nIsoc % 2, par.nSectors); 
   
   if (goalP (pOr, pOr, pDest, t, dt, &timeToReach, &distance, &motor, &amure)) {
      pDest->father = pOr->id;
      pDest->motor = motor;
      pDest->amure = amure;
      *lastStepDuration = timeToReach;
      free (tempList);
      return nIsoc + 1;
   }
   isoDesc [nIsoc].size = buildNextIsochrone (pOr, pDest, tempList, 1, t, dt, &isocArray [nIsoc * MAX_SIZE_ISOC], &theBestVmg);

   if (isoDesc [nIsoc].size == -1) {
      free (tempList);
      return -1;
   }
   // printf ("%-20s%d, %d\n", "Isochrone no, len: ", 0, isoDesc [0].size);
   lastBestVmg = theBestVmg;
   isoDesc [nIsoc].bestVmg = theBestVmg;
   isoDesc [nIsoc].first = 0; 
   lastClosest = closest (&isocArray [nIsoc * MAX_SIZE_ISOC], isoDesc[nIsoc].size, pDest, &index);
   isoDesc [nIsoc].closest = index; 
   isoDesc [nIsoc].toIndexWp = toIndexWp; 
   if (isoDesc [nIsoc].size == 0) { // no wind at the beginning. 
      isoDesc [nIsoc].size = 1;
      isocArray [nIsoc * MAX_SIZE_ISOC + 0] = *pOr;
   }
   
   nIsoc += 1;
   //printf ("Routing t = %.2lf, zone: %.2ld\n", t, zone.timeStamp [zone.nTimeStamp-1]);
   while (t < (zone.timeStamp [zone.nTimeStamp - 1]/* + par.tStep*/) && (nIsoc < maxNIsoc)) {
      if (route.ret == -2) {
         free (tempList);
         return -2; // stopped by user in another thread !!!
      }
      t += dt;
      // printf ("nIsoc = %d\n", nIsoc);
      if (goal (pDest, nIsoc - 1, &isocArray [(nIsoc - 1) * MAX_SIZE_ISOC], isoDesc[nIsoc - 1].size, t, dt, &timeLastStep, &motor, &amure)) {
         isoDesc [nIsoc].size = optimize (pOr, pDest, nIsoc, par.opt, tempList, lTempList, &isocArray [nIsoc * MAX_SIZE_ISOC]);
         if (isoDesc [nIsoc].size == 0) { // no Wind ... we copy
            isoDesc  [nIsoc].size = isoDesc [nIsoc - 1].size;
            memcpy (&isocArray [nIsoc * MAX_SIZE_ISOC], &isocArray [(nIsoc - 1) * MAX_SIZE_ISOC], isoDesc [nIsoc - 1].size * sizeof (Pp));
         }
         isoDesc [nIsoc].first = findFirst (nIsoc);
         lastClosest = closest (&isocArray [nIsoc * MAX_SIZE_ISOC], isoDesc[nIsoc].size, pDest, &index);
         isoDesc [nIsoc].closest = index; 
         isoDesc [nIsoc].toIndexWp = toIndexWp; 
         *lastStepDuration = timeLastStep;
         // printf ("destination reached\n");
         free (tempList);
         return nIsoc + 1;
      }
      // printf ("continue\n");
      lTempList = buildNextIsochrone (pOr, pDest, &isocArray [(nIsoc -1) * MAX_SIZE_ISOC], isoDesc [nIsoc - 1].size, t, dt, tempList, &theBestVmg);
      if (lTempList == -1) {
         free (tempList);
         return -1;
      }
      lastBestVmg = theBestVmg;
      isoDesc [nIsoc].bestVmg = theBestVmg;
      isoDesc [nIsoc].size = optimize (pOr, pDest, nIsoc, par.opt, tempList, lTempList, &isocArray [nIsoc * MAX_SIZE_ISOC]);
      if (isoDesc [nIsoc].size == 0) { // no Wind ... we copy
         replicate (nIsoc);
      }
      isoDesc [nIsoc].first = findFirst (nIsoc);; 
      lastClosest = closest (&isocArray [nIsoc * MAX_SIZE_ISOC], isoDesc [nIsoc].size, pDest, &index);
      isoDesc [nIsoc].closest = index;
      isoDesc [nIsoc].toIndexWp = toIndexWp; 

      /*
      char strDist [MAX_SIZE_NAME];
      if ((nIsoc > 2) && (isoDesc [nIsoc-1].distance > isoDesc [nIsoc-2].distance)) {
         snprintf (strDist, sizeof (strDist), "%.2lf", isoDesc [nIsoc-1].distance);  
         fprintf (stderr, "Distance to destination is raising, nIsoc: %d, distance: %s previous distance: %.2lf\n", 
            nIsoc - 1, (isoDesc [nIsoc -1].distance == DBL_MAX) ? "INFINITY" : strDist, isoDesc [nIsoc - 2].distance);
         break;
      }*/
      // printf ("Isoc: %d Biglist length: %d optimized size: %d\n", nIsoc, lTempList, isoDesc [nIsoc].size);
      nIsoc += 1;
   }
   *lastStepDuration = 0.0;
   free (tempList);
   return NIL;
}

/*! global variable initialization for routing */
static void initRouting (void) {
   lastClosest = par.pOr;
   tDeltaCurrent = zoneTimeDiff (&currentZone, &zone); // global variable
   par.pOr.id = -1;
   par.pOr.father = -1;
   par.pDest.id = 0;
   par.pDest.father = 0;
   pId = 1;
   route.n = 0;
   route.destinationReached = false;
   for (int i = 0; i < maxNIsoc; i++) {
      isoDesc [i].size = 0;
      isoDesc [i].distance = DBL_MAX;
      isoDesc [i].bestVmg = 0;
   }
   nIsoc = 0;
}

/*! launch routing wih parameters */
void *routingLaunch () {
   struct timeval t0, t1;
   long ut0, ut1;
   double lastStepDuration;
   Pp pNext;
   initRouting ();
   route.competitorIndex = competitors.runIndex;

   gettimeofday (&t0, NULL);
   ut0 = t0.tv_sec * MILLION + t0.tv_usec;
   double wayPointStartTime = par.startTimeInHours;

   //Launch routing
   if (wayPoints.n == 0)
      route.ret = routing (&par.pOr, &par.pDest, -1, wayPointStartTime, par.tStep, &lastStepDuration);
   else {
      for (int i = 0; i < wayPoints.n; i ++) {
         pNext.lat = wayPoints.t[i].lat;
         pNext.lon = wayPoints.t[i].lon;
         pNext.id = 0;
         if (i == 0) {
            route.ret = routing (&par.pOr, &pNext, i, wayPointStartTime, par.tStep, &lastStepDuration);
         }
         else
            route.ret = routing (&isocArray [(nIsoc-1) * MAX_SIZE_ISOC + 0], &pNext, i, wayPointStartTime, par.tStep, &lastStepDuration);
         if (route.ret > 0) {
            wayPointStartTime = par.startTimeInHours + (nIsoc * par.tStep) + lastStepDuration;
            isocArray [nIsoc * MAX_SIZE_ISOC + 0].lat = wayPoints.t[i].lat;
            isocArray [nIsoc * MAX_SIZE_ISOC + 0].lon = wayPoints.t[i].lon;
            isocArray [nIsoc * MAX_SIZE_ISOC + 0].father = pNext.father;
            isocArray [nIsoc * MAX_SIZE_ISOC + 0].id = pId++;
            isoDesc [nIsoc].size = 1;
            nIsoc += 1;
         }
         else break;
      }
      if (route.ret > 0)
         route.ret = routing (&isocArray [(nIsoc-1) * MAX_SIZE_ISOC + 0], &par.pDest, -1, wayPointStartTime, par.tStep, &lastStepDuration);
   }
   if (route.ret == -1)
      return NULL;

   gettimeofday (&t1, NULL);
   ut1 = t1.tv_sec * MILLION + t1.tv_usec;
   route.calculationTime = (double) ((ut1-ut0)/MILLION);
   route.destinationReached = (route.ret > 0);
   storeRoute (&route, &par.pOr, &lastClosest, lastStepDuration);

   if (strlen (par.dumpRFileName) > 0) dumpRoute (par.dumpRFileName, lastClosest);
   if (strlen (par.dumpIFileName) > 0) dumpAllIsoc (par.dumpIFileName);
   return NULL;
}

/*! choose best time to reach pDest in minimum time */
void *bestTimeDeparture () {
   double minDuration = DBL_MAX, maxDuration = 0;
   chooseDeparture.bestTime = -1;
   chooseDeparture.count = 0;
   chooseDeparture.tStop = chooseDeparture.tEnd; // default value

   for (int t = chooseDeparture.tBegin; t < chooseDeparture.tEnd; t += chooseDeparture.tStep) {
      //initRouting ();
      par.startTimeInHours = t;
      route.ret = 0;
      routingLaunch ();
      if (route.ret > 0) {
         chooseDeparture.t [t] = route.duration;
         if (route.duration < minDuration) {
            minDuration = route.duration;
            chooseDeparture.bestTime = t;
         }
         if (route.duration > maxDuration) {
            maxDuration = route.duration;
         }
         printf ("Count: %d, time %d, duration: %.2lf, min: %.2lf, bestTime: %d\n", chooseDeparture.count, t, route.duration, minDuration, chooseDeparture.bestTime);
      }
      else {
         chooseDeparture.tStop = t;
         printf ("Count: %d, time %d, Unreachable\n", chooseDeparture.count, t);
         break;
      }
      chooseDeparture.count += 1;
   }
   if (chooseDeparture.bestTime != -1) {
      par.startTimeInHours = chooseDeparture.bestTime;
      printf ("Solution exist: best startTime: %.2lf\n", par.startTimeInHours);
      chooseDeparture.minDuration = minDuration;
      chooseDeparture.maxDuration = maxDuration;
      route.ret = 0;
      routingLaunch ();
      chooseDeparture.ret = EXIST_SOLUTION;
   }  
   else {
      chooseDeparture.ret = NO_SOLUTION;
      printf ("No solution\n");
   }
   return NULL;
}

/*! updatecompretitorsDashboard */
static void updateCompetitorsDashboard (int i, double duration) {
   if (competitors.n == 0)
      return;
   newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + duration, 
      competitors.t [i].strETA, MAX_SIZE_DATE); 
   competitors.t [i].duration = duration; // hours
   competitors.t [i].dist = orthoDist (competitors.t [i].lat, competitors.t [i].lon, par.pDest.lat, par.pDest.lon);
}

/*! compare competittor duration for qsort */
static int compareDuration (const void *a, const void *b) {
    const Competitor *compA = (const Competitor *) a;
    const Competitor *compB = (const Competitor *) b;
    if (compA->duration < compB->duration) return -1;
    if (compA->duration > compB->duration) return 1;
    return 0;
}

/*! launch all competitors */
void *allCompetitors () {
   for (int i = 0; i < competitors.n; i += 1) // reset eta(s)
      competitors.t [i].strETA [0] = '\0';
   
   for (int i = 0; i < competitors.n; i +=1) {
      competitors.runIndex = i;
      par.pOr.lat = competitors.t [i].lat;
      par.pOr.lon = competitors.t [i].lon;
      route.ret = 0;
      routingLaunch ();
      if (route.ret < 0) {
         competitors.ret = NO_SOLUTION;
         fprintf (stderr, "No solution for competitor: %s with return: %d\n", competitors.t[i].name, route.ret);
         return NULL;
      }
      updateCompetitorsDashboard (i, route.duration); // set eta(s) and dist
   }
   if (competitors.n > 1)
      qsort (competitors.t, competitors.n, sizeof(Competitor), compareDuration);
   
   competitors.ret = EXIST_SOLUTION;
   return NULL;
}

/*! translate hours in a string */
static char *delayToStr (double delay, char *str, size_t maxlen) {
   const char prefix = (delay < 0) ? '-' : ' ';
   delay = fabs (delay);

   int days = (int) (delay / 24);        
   int hours = ((int) delay) % 24;         
   int minutes = (int) ((delay - (int) delay) * 60); 
   int seconds = (int) ((delay * 3600)) % 60;

   if (days > 0)
      snprintf (str, maxlen, "%c%d day%s %02d:%02d:%02d", 
              prefix, days, days > 1 ? "s" : "", hours, minutes, seconds);
   else
      snprintf(str, maxlen, "%c%02d:%02d:%02d", prefix, hours, minutes, seconds);
   
   return str;
}

/*! translate competitors strct in a string  */
void competitorsToStr (char *buffer, size_t maxLen) {
   char strDep [MAX_SIZE_DATE];
   char strDelay [MAX_SIZE_LINE], strMainDelay [MAX_SIZE_LINE];
   char line [MAX_SIZE_TEXT];
   char strLat[MAX_SIZE_NAME], strLon[MAX_SIZE_NAME];
   double dist;
   const int iMain = mainCompetitor ();

   g_strlcpy (buffer, "Name;                   Lat.;        Lon.;  Dist To Main;              ETA;     Dist;  To Best Delay;  To Main Delay\n", maxLen);

   for (int i = 0; i < competitors.n; i++) {
      latToStr (competitors.t[i].lat, par.dispDms, strLat, sizeof(strLat));
      lonToStr (competitors.t[i].lon, par.dispDms, strLon, sizeof(strLon));
      
      delayToStr (competitors.t [i].duration - competitors.t [0].duration, 
         strDelay, sizeof (strDelay));          // delay with the winner in hours translated in string

      delayToStr (competitors.t [i].duration - competitors.t [iMain].duration, 
         strMainDelay, sizeof (strMainDelay));  // delay between main and others in hours translated in string

      if (i == iMain) dist = 0;
      else dist = orthoDist (competitors.t [i].lat, competitors.t [i].lon, competitors.t [iMain].lat, competitors.t [iMain].lon);

      snprintf (line, sizeof (line), "%-16s;%12s; %12s;      %8.2lf; %16s; %8.2lf; %14s; %14s\n", 
         competitors.t[i].name, strLat, strLon, dist, competitors.t[i].strETA, competitors.t[i].dist, strDelay, strMainDelay);
      g_strlcat (buffer, line, maxLen);
   }

   newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours, strDep, sizeof (strDep)); 
   snprintf (line, MAX_SIZE_LINE, "Departure Date: %s, Isoc Time Step: %.2lf h\n", strDep, par.tStep);
   g_strlcat (buffer, line, maxLen);
}
