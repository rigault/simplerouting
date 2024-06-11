/*! \Engine
* tws = True Wind Speed
* twd = True Wind Direction
* twa = True Wind Angle - the angle of the boat to the wind
* sog = Speed over Ground of the boat
* cog = Course over Ground  of the boat
* */
/*! compilation gcc -Wall -c engine.c */
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
#include "engine.h"

/*! global variables */
Pp        isocArray [MAX_N_ISOC][MAX_SIZE_ISOC];  // list of isochrones
IsoDesc   isoDesc [MAX_N_ISOC];                   // Isochrone meta data
int       nIsoc = 0;                              // total number of isochrones 
Pp        lastClosest;                            // closest point to destination in last isochrone computed
HistoryRoute historyRoute = {0};
ChooseDeparture chooseDeparture;                  // for choice of departure time

/*! following global variable could be static but linking issue */
double    pOrTtoPDestCog = 0;                      // cog from pOr to pDest.

struct {
   double dd;
   double vmc;
   double nPt;
} sector [2][MAX_SIZE_ISOC];                    // we keep even and odd last sectors

int      pId = 1;                               // global ID for points. -1 and 0 are reserved for pOr and pDest
double   tDeltaCurrent = 0.0;                   // delta time in hours between Wind zone and current zone
double   lastClosestDist;                       // closest distance to destination in last isochrone computed
double   lastBestVmg = 0.0;                     // best VMG found up to now

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
      d = orthoDist (isocArray [nIsoc][i].lat, isocArray [nIsoc][i].lon, isocArray [nIsoc][next].lat, isocArray [nIsoc][next].lon);
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
static inline int forwardSectorOptimize (const Pp *pOr, const Pp *pDest, int nIsoc, const Isoc isoList, int isoLen, Isoc optIsoc) {
   const double epsilon = 0.1;
   int iSector, k;
   double alpha, theta;
   double thetaStep = 360.0 / par.nSectors;
   Pp ptTarget;
   double dLat = -par.jFactor * cos (DEG_TO_RAD * pOrTtoPDestCog);  // nautical miles in N S direction
   double dLon = -par.jFactor * sin (DEG_TO_RAD * pOrTtoPDestCog) / cos (DEG_TO_RAD * (pOr->lat + pDest->lat)/2); // nautical miles in E W direction

   memset (&ptTarget, 0, sizeof (Pp));
   ptTarget.lat = pOr->lat + dLat / 60; 
   ptTarget.lon = pOr->lon + dLon / 60;
   isoDesc [nIsoc].focalLat = ptTarget.lat;
   isoDesc [nIsoc].focalLon = ptTarget.lon;
  
   //ptTarget = pDest; 
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
   // on retasse si certains secteurs vides ou faiblement peuples ou moins avances que precedent
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
static inline int optimize (const Pp *pOr, const Pp *pDest, int nIsoc, int algo, const Isoc isoList, int isoLen, Isoc optIsoc) {
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
static int buildNextIsochrone (const Pp *pOr, const Pp *pDest, const Isoc isoList, int isoLen, double t, double dt, Isoc newList, double *bestVmg) {
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
         if (isSea (newPt.lat, newPt.lon)) {
            newPt.dd = orthoDist (newPt.lat, newPt.lon, pDest->lat, pDest->lon);
            double alpha = orthoCap (pOr->lat, pOr->lon, newPt.lat, newPt.lon) - pOrTtoPDestCog;
         
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
      if (isocArray [i] [k].id == ptId)
         return k;
   }
   fprintf (stderr, "Error in father: ptId not found: %d, Isoc No:%d, Isoc Len: %d\n", ptId, i, lIsoc);
   return -1;
}

/*! copy isoc Descriptors in a string 
   true if enough space, false if truncated */
bool isoDescToStr (char *str, size_t maxLength) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   const int DIST_MAX = 100000;
   double distance;
   strcpy (str, "No toWp Size First Closest Distance VMC      FocalLat  FocalLon\n");
   for (int i = 0; i < nIsoc; i++) {
      distance = isoDesc [i].distance;
      if (distance > DIST_MAX) distance = -1;
      snprintf (line, MAX_SIZE_LINE, "%03d %03d %03d  %03d   %03d     %07.2lf  %07.2lf  %s  %s\n", i, isoDesc [i].toIndexWp, 
         isoDesc[i].size, isoDesc[i].first, 
         isoDesc[i].closest, distance, isoDesc[i].bestVmg, 
         latToStr (isoDesc [i].focalLat, par.dispDms, strLat), lonToStr (isoDesc [i].focalLon, par.dispDms, strLon));
      if ((strlen (str) + strlen (line)) > maxLength) 
         return false;
      strncat (str, line, maxLength - strlen (str)); 
   }
   return true;
}

/*! copy all isoc in a string 
   true if enough space, false if truncated */
bool allIsocToStr (char *str, size_t maxLength) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   Pp pt;
   strcpy (str, "No  Lat         Lon             Id Father  Amure  Motor\n");
   for (int i = 0; i < nIsoc; i++) {
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = isocArray [i][k];
         snprintf (line, MAX_SIZE_LINE, "%03d %-12s %-12s %6d %6d %6d %6d\n", i, latToStr (pt.lat, par.dispDms, strLat),\
            lonToStr (pt.lon, par.dispDms, strLon), pt.id, pt.father, pt.amure, pt.motor);
         if ((strlen (str) + strlen (line)) > maxLength) 
            return false;
         strncat (str, line, maxLength - strlen (str));
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
         pt = isocArray [i][k];
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
      pt = isocArray [i][iFather];
      fprintf (f, "%4d; %06.2f; %06.2f; %4d; %4d\n", i, pt.lat, pt.lon, pt.id, pt.father);
   }
   pt = par.pOr;
   fprintf (f, "%4d; %06.2f; %06.2f; %4d; %4d\n", -1, pt.lat, pt.lon, pt.id, pt.father);
   fclose (f);
   return true;
}

/*! store current route in history */
static void saveRoute (void) {
   int z = historyRoute.n;
   if (z < MAX_HISTORY_ROUTE) {
      memcpy (&historyRoute.r[z], &route, sizeof (SailRoute));
      historyRoute.n += 1;
   }
   else
      fprintf (stderr, "Error in saveRoute: Limit of MAX_HISTORY_ROUTE reached: %d\n", MAX_HISTORY_ROUTE);
}

/*! stat route */
static void statRoute (double lastStepDuration) {
   Pp p = {0};
   if (route.n == 0) return;
   route.totDist = route.motorDist = route.tribordDist = route.babordDist = 0;
   route.lastStepDuration = lastStepDuration;
   route.duration = route.motorDuration = 0;
   route.maxTws = route.maxGust = route.maxWave = 0;
   route.avrTws = route.avrGust = route.avrWave = 0;
   for (int i = 1; i < route.n; i++) {
      p.lat = route.t [i-1].lat;
      p.lon = route.t [i-1].lon;
      route.t [i-1].time = par.tStep * (i - 1) + par.startTimeInHours;
      route.t [i-1].lCap = directCap (route.t[i-1].lat, route.t[i-1].lon, route.t[i].lat, route.t[i].lon);
      route.t [i-1].oCap = orthoCap (route.t[i-1].lat, route.t[i-1].lon, route.t[i].lat, route.t[i].lon);
      route.t [i-1].ld  = loxoDist (route.t[i-1].lat, route.t[i-1].lon, route.t[i].lat, route.t[i].lon);
      route.t [i-1].od = orthoDist (route.t[i-1].lat, route.t[i-1].lon, route.t[i].lat, route.t[i].lon);
      route.t [i-1].sog = route.t [i-1].od / par.tStep;
      route.totDist += route.t [i-1].od;
      route.duration += par.tStep;
      if (route.t [i-1].motor) {
         route.motorDuration += par.tStep;
         route.motorDist += route.t [i-1].od;
      }
      else 
         if (route.t [i-1].amure == TRIBORD)
            route.tribordDist += route.t [i-1].od;
         else
            route.babordDist += route.t [i-1].od;
      findWindGrib (p.lat, p.lon, route.t [i-1].time, &route.t [i-1].u, &route.t [i-1].v, &route.t [i-1].g, &route.t [i-1].w, 
         &route.t [i-1].twd, &route.t [i-1].tws);
      route.avrTws  += route.t [i-1].tws;
      route.avrGust += route.t [i-1].g;
      route.avrWave += route.t [i-1].w;
      route.maxTws  = MAX (route.maxTws, route.t [i-1].tws);
      route.maxGust = MAX (route.maxGust, route.t [i-1].g);
      route.maxWave = MAX (route.maxWave, route.t [i-1].w);
      route.maxSog = MAX (route.maxSog, route.t [i-1].sog);
   }
   route.t [route.n-1].time = par.tStep * (route.n-1) + par.startTimeInHours;
   route.duration += lastStepDuration;
   route.totDist += route.t [route.n-1].od;
   if (route.t [route.n-1].motor) {
      route.motorDuration += lastStepDuration;
      route.motorDist += route.t [route.n-1].od;
   }
   else 
      if (route.t [route.n-1].amure == TRIBORD)
         route.tribordDist += route.t [route.n-1].od;
      else
         route.babordDist += route.t [route.n-1].od;
   p.lat = route.t [route.n-1].lat;
   p.lon = route.t [route.n-1].lon;
   findWindGrib (p.lat, p.lon, route.t [route.n-1].time, &route.t [route.n-1].u, &route.t [route.n-1].v, &route.t [route.n-1].g, 
      &route.t [route.n-1].w, &route.t [route.n-1].twd, &route.t [route.n-1].tws);
   
   route.maxTws  = MAX (route.maxTws, route.t [route.n-1].tws);
   route.maxGust = MAX (route.maxGust, route.t [route.n-1].g);
   route.maxWave = MAX (route.maxWave, route.t [route.n-1].w);
   route.avrTws  += route.t [route.n-1].tws;
   route.avrGust += route.t [route.n-1].g;
   route.avrWave += route.t [route.n-1].w;

   if (route.destinationReached) {
      route.t [route.n-1].lCap = directCap (route.t[route.n-1].lat, route.t[route.n-1].lon, par.pDest.lat, par.pDest.lon);
      route.t [route.n-1].oCap = orthoCap (route.t[route.n-1].lat, route.t[route.n-1].lon, par.pDest.lat, par.pDest.lon);
      route.t [route.n-1].ld = loxoDist (route.t[route.n-1].lat, route.t[route.n-1].lon, par.pDest.lat, par.pDest.lon);
      route.t [route.n-1].od = orthoDist (route.t[route.n-1].lat, route.t[route.n-1].lon, par.pDest.lat, par.pDest.lon);
      route.t [route.n-1].sog =  route.t [route.n-1].od / route.lastStepDuration;
   }
   route.avrTws /= route.n;
   route.avrGust /= route.n;
   route.avrWave /= route.n;
   route.avrSog = route.totDist / route.duration;
}

/*! store route */
void storeRoute (const Pp *pOr, const Pp *pDest, double lastStepDuration) {
   //route.destinationReached = (pDest.id == 0);
   int iFather;
   // bool found = false;
   route.nIsoc = nIsoc;
   route.n =  (pDest->id == 0) ? nIsoc + 2 : nIsoc + 1;
   if (nIsoc == 0) {
      route.n = 1;
      statRoute (lastStepDuration);
      return;
   }
   Pp pt = *pDest;
   Pp ptLast = *pDest;
   route.t [route.n - 1].lat = pDest->lat;
   route.t [route.n - 1].lon = pDest->lon;
   route.t [route.n - 1].id = pDest->id;
   route.t [route.n - 1].father = pDest->father;
   route.t [route.n - 1].motor = pDest->motor;
   route.t [route.n - 1].amure = pDest->amure;
   route.t [route.n - 1].toIndexWp = pDest->toIndexWp;
   for (int i = route.n - 3; i >= 0; i--) {
      iFather = findFather (pt.father, i, isoDesc[i].size);
      if (iFather == -1) continue;
      // found = true;
      pt = isocArray [i][iFather];
      route.t [i+1].lat = pt.lat;
      route.t [i+1].lon = pt.lon;
      route.t [i+1].id = pt.id;
      route.t [i+1].father = pt.father;
      route.t [i+1].motor = ptLast.motor;
      route.t [i+1].amure = ptLast.amure;
      route.t [i+1].toIndexWp = pt.toIndexWp;
      ptLast = pt;
   }
   route.t [0].lat = pOr->lat;
   route.t [0].lon = pOr->lon;
   route.t [0].id = pOr->id;
   route.t [0].father = pOr->father;
   route.t [0].motor = ptLast.motor;
   route.t [0].amure = ptLast.amure;
   route.t [0].toIndexWp = pt.toIndexWp;
   statRoute (lastStepDuration);
   saveRoute ();
}

/*! produce struinc thatbsay if Motor; Tribord, Babord */
static char *motorTribordBabord (bool motor, int amure, char* str, size_t len) {
   if (motor)
      strncpy (str, "Mot", len);
   else
      if (amure == TRIBORD)
         strncpy (str, "Tri", len);
      else
         strncpy (str, "Bab", len);
   return str;
}

/*! copy route in a string
   true if enough space, false if truncated */
bool routeToStr (const SailRoute *route, char *str, size_t maxLength) {
   char line [MAX_SIZE_LINE], strDate [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE], strLon [MAX_SIZE_LINE];  
   char shortStr [SMALL_SIZE], strDur [MAX_SIZE_LINE], strMot [MAX_SIZE_LINE];
   double awa, aws, maxAws = 0.0, sumAws = 0.0;
   double twa = fTwa (route->t[0].lCap, route->t[0].twd);
   fAwaAws (twa, route->t[0].tws, route->t[0].sog, &awa, &aws);

   strcpy (str, "  No  WP Lat        Lon         Date-Time         M/T/B  COG\
     Dist     SOG  Twd   Twa      Tws    Gust  Awa      Aws   Waves\n");
   snprintf (line, MAX_SIZE_LINE, " pOr %3d %-12s%-12s %6s %6s %4.0lf° %7.2lf %7.2lf %4d° %4.0lf° %7.2lf %7.2lf %4.0lf° %7.2lf %7.2lf\n", \
      route->t[0].toIndexWp,\
      latToStr (route->t[0].lat, par.dispDms, strLat), lonToStr (route->t[0].lon, par.dispDms, strLon), \
      newDate (zone.dataDate [0], (zone.dataTime [0]/100) + route->t[0].time, strDate), \
      motorTribordBabord (route->t[0].motor, route->t[0].amure, shortStr, SMALL_SIZE), \
      route->t[0].lCap, route->t[0].ld, route->t[0].sog, \
      (int) (route->t[0].twd + 360) % 360,\
      twa, route->t[0].tws, \
      MS_TO_KN * route->t[0].g, awa, aws, route->t[0].w);

   strncat (str, line, maxLength - strlen (str));
   for (int i = 1; i < route->n; i++) {
      twa = fTwa (route->t[i].lCap, route->t[i].twd);
      fAwaAws (twa, route->t[i].tws, route->t[i].sog, &awa, &aws);
      if (aws > maxAws) maxAws = aws;
      sumAws += aws;
      if ((fabs(route->t[i].lon) > 180.0) || (fabs (route->t[i].lat) > 90.0))
         snprintf (line, MAX_SIZE_LINE, " Isoc %3d: Erreur sur latitude ou longitude\n", i-1);   
      else
         snprintf (line, MAX_SIZE_LINE, "%4d %3d %-12s%-12s %s %6s %4.0lf° %7.2f %7.2lf %4d° %4.0lf° %7.2lf %7.2lf %4.0lf° %7.2lf %7.2lf\n", \
            i-1,\
            route->t[i].toIndexWp,\
            latToStr (route->t[i].lat, par.dispDms, strLat), lonToStr (route->t[i].lon, par.dispDms, strLon), \
            newDate (zone.dataDate [0], (zone.dataTime [0]/100) + route->t[i].time, strDate), \
            motorTribordBabord (route->t[i].motor, route->t[i].amure, shortStr, SMALL_SIZE), \
            route->t[i].lCap, route->t[i].ld, route->t[i].sog,\
            (int) (route->t[i].twd + 360) % 360,\
            fTwa (route->t[i].lCap, route->t[i].twd), route->t[i].tws,\
            MS_TO_KN * route->t[i].g, awa, aws, route->t[i].w);
      strncat (str, line, maxLength - strlen (str));
   }
   
   snprintf (line, MAX_SIZE_LINE, " Avr %65s  %5.2lf             %7.2lf %7.2lf       %7.2lf %7.2lf\n", " ", \
      route->avrSog, route->avrTws, MS_TO_KN * route->avrGust, sumAws / route->n, route->avrWave); 
   strncat (str, line, maxLength - strlen (str));
   snprintf (line, MAX_SIZE_LINE, " Max %65s  %5.2lf             %7.2lf %7.2lf       %7.2lf %7.2lf\n", " ", \
      route->maxSog, route->maxTws, MS_TO_KN * route->maxGust, maxAws, route->maxWave); 
   strncat (str, line, maxLength - strlen (str));
   snprintf (line, MAX_SIZE_LINE, "\n Total distance   : %7.2lf NM,   Motor distance: %7.2lf NM\n", route->totDist, route->motorDist);
   strncat (str, line, maxLength - strlen (str));
   snprintf (line, MAX_SIZE_LINE, " Total Duration   : %s, Motor Duration: %s\n", \
      durationToStr (route->duration, strDur, sizeof (strDur)), durationToStr (route->motorDuration, strMot, sizeof (strMot)));
   strncat (str, line, maxLength - strlen (str));

   newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route->duration, strDate);
   snprintf (line, MAX_SIZE_LINE, " Arrival Date&Time: %s", newDate (zone.dataDate [0], \
      zone.dataTime [0]/100 + par.startTimeInHours + route->duration, strDate));
   strncat (str, line, maxLength - strlen (str));
   return true;
}

/*! copy route in a string
   true if enough space, false if truncated */
bool OrouteToStr (const SailRoute *route, char *str, size_t maxLength) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   strcpy (str, " No        Lat        Lon             Id Father  Motor     Cap       Dist      SOG     Twd      Tws    Gust   Waves\n");
   snprintf (line, MAX_SIZE_LINE, " pOr:      %-12s%-12s %6d %6d %6d %7.2f°   %7.2lf  %7.2lf  %6.0lf° %7.2lf %7.2lf %7.2lf\n", \
      latToStr (route->t[0].lat, par.dispDms, strLat), lonToStr (route->t[0].lon, par.dispDms, strLon), \
      route->t[0].id, route->t[0].father, route->t[0].motor, route->t[0].lCap, route->t[0].ld, route->t[0].ld/par.tStep,
      route->t[0].twd, route->t[0].tws, MS_TO_KN * route->t[0].g, route->t[0].w);

   strncat (str, line, maxLength - strlen (str));
   for (int i = 1; i < route->n; i++) {
      if ((fabs(route->t[i].lon) > 180.0) || (fabs (route->t[i].lat) > 90.0))
         snprintf (line, MAX_SIZE_LINE, " Isoc %3d: Erreur sur latitude ou longitude\n", i-1);   
      else
         snprintf (line, MAX_SIZE_LINE, " Isoc %3d: %-12s%-12s %6d %6d %6d %7.2f°   %7.2f  %7.2lf  %6.0lf° %7.2lf %7.2lf %7.2lf\n", \
            i-1,
            latToStr (route->t[i].lat, par.dispDms, strLat), lonToStr (route->t[i].lon, par.dispDms, strLon), \
            route->t[i].id, route->t[i].father, route->t[i].motor,\
            route->t[i].lCap, route->t[i].ld, route->t[i].ld/par.tStep,
            route->t[i].twd, route->t[i].tws, MS_TO_KN * route->t[i].g, route->t[i].w);
      strncat (str, line, maxLength - strlen (str));
   }
   
   snprintf (line, MAX_SIZE_LINE, " Avr %85s  %7.2lf %7.2lf %7.2lf\n", " ", route->avrTws, MS_TO_KN * route->avrGust, route->avrWave); 
   strncat (str, line, maxLength - strlen (str));
   snprintf (line, MAX_SIZE_LINE, " Max %85s  %7.2lf %7.2lf %7.2lf\n", " ", route->maxTws, MS_TO_KN * route->maxGust, route->maxWave); 
   strncat (str, line, maxLength - strlen (str));
   snprintf (line, MAX_SIZE_LINE, "\n Total distance: %7.2lf NM,    Motor distance: %7.2lf NM\n", route->totDist, route->motorDist);
   strncat (str, line, maxLength - strlen (str));
   snprintf (line, MAX_SIZE_LINE, " Total Duration: %7.2lf hours, Motor Duration: %7.2lf hours\n", route->duration, route->motorDuration);
   strncat (str, line, maxLength - strlen (str));
   return true;
}

/*! return true if pDest can be reached from pFrom in less time than dt
   bestTime give the time to get from pFrom to pDest 
   motor true if goal reached with motor */
static inline bool goalP (const Pp *pFrom, const Pp *pDest, double t, double dt, double *timeTo, double *distance, bool *motor, int *amure) {
   double u, v, gust, w, twd, tws, twa, sog;
   double coeffLat = cos (DEG_TO_RAD * (pFrom->lat + pDest->lat)/2);
   double dLat = pDest->lat - pFrom->lat;
   double dLon = pDest->lon - pFrom->lon;
   double cog = RAD_TO_DEG * atan2 (dLon * coeffLat, dLat);
   double penalty;
   *motor = false;
   double waveCorrection = 1.0;
   
   *distance = orthoDist (pDest->lat, pDest->lon, pFrom->lat, pFrom->lon);
   findWindGrib (pFrom->lat, pFrom->lon, t, &u, &v, &gust, &w, &twd, &tws);
   // findCurrentGrib (pFrom->lat, pFrom->lon, t - tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);
   // ATTENTION Courant non pris en compte dans la suite !!!
   twa = fTwa (cog, tws);      // angle of the boat with the wind
   *amure = (twa > 0) ? TRIBORD : BABORD;
   *motor =  ((maxSpeedInPolarAt (tws * par.xWind, &polMat) < par.threshold) && (par.motorSpeed > 0));
   // printf ("maxSpeedinPolar: %.2lf\n", maxSpeedInPolarAt (tws, &polMat));
   double efficiency = (isDayLight (t, pFrom->lat, pFrom->lon)) ? par.dayEfficiency : par.nightEfficiency;
   sog = (motor) ? par.motorSpeed : efficiency * findPolar (twa, tws * par.xWind, polMat); 
   if ((w > 0) && ((waveCorrection = findPolar (twa, w, wavePolMat)) > 0)) { 
      sog = sog * (waveCorrection / 100.0);
   }
   *timeTo = *distance/sog;
   if ((!(*motor)) && (pDest->amure != pFrom->amure)) {     // changement amure
      if (fabs (twa) < 90) penalty = par.penalty0 / 60.0;   // virement de bord
      else penalty = par.penalty1 / 60.0;                   // empannage Jibe
   }
   else penalty = 0;
   return ((sog * (dt - penalty)) > *distance);
}

/*! return closest point to pDest in Isoc 
  true if goal can be reached directly in dt from isochrone 
  side effect : pDest.father can be modified ! */
static inline bool goal (Pp *pDest, int nIsoc, const Isoc isoList, int len, double t, double dt, double *lastStepDuration, bool *motor, int *amure) {
   double bestTime = DBL_MAX;
   double time, distance;
   bool destinationReached = false;
   isoDesc [nIsoc].distance = 9999.99;
   for (int k = 0; k < len; k++) {
      if (isSea (isoList [k].lat, isoList [k].lon)) { 
         if (goalP (&isoList [k], pDest, t, dt, &time, &distance, motor, amure))
            destinationReached = true;

         if (time < bestTime) {
            bestTime = time;
            if (destinationReached) {
               pDest->father = isoList [k].id; // ATTENTION !!!!
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
static inline Pp closest (const Isoc isoc, int n, const Pp *pDest, int *index) {
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
      isocArray [n][i].id = isocArray [n-1][i].id + len; 
      isocArray [n][i].father = isocArray [n-1][i].id; 
      isocArray [n][i].amure = isocArray [n-1][i].amure; 
      isocArray [n][i].sector = isocArray [n-1][i].sector; 
      isocArray [n][i].lat = isocArray [n-1][i].lat; 
      isocArray [n][i].lon = isocArray [n-1][i].lon; 
      isocArray [n][i].dd = isocArray [n-1][i].dd; 
      isocArray [n][i].vmc = isocArray [n-1][i].vmc; 
   }
   isoDesc [n].distance = isoDesc [n-1].distance;
   isoDesc [n].bestVmg = isoDesc [n-1].bestVmg;
   isoDesc [n].size = isoDesc [n-1].size;
   isoDesc [n].focalLat = isoDesc [n-1].focalLat;
   isoDesc [n].focalLon = isoDesc [n-1].focalLon;
   isoDesc [n].toIndexWp = isoDesc [n-1].toIndexWp;
}

/*! find optimal routing from p0 to pDest using grib file and polar
    return number of steps to reach pDest, NIL if unreached, -1 if problem, 0 reserved for not terminated
    return also lastStepDuration if the duration of last step if destination reached (0 if unreached) */
static int routing (Pp *pOr, Pp *pDest, int toIndexWp, double t, double dt, double *lastStepDuration) {
   bool motor = false;
   int amure;
   Isoc tempList;
   double distance;
   double timeToReach = 0;
   int lTempList = 0;
   double timeLastStep;
   int index; // index of closest point to pDest in insochrone
   double theBestVmg;
   pOr->dd = orthoDist (pOr->lat, pOr->lon, pDest->lat, pDest->lon);
   pOr->vmc = 0;
   pOrTtoPDestCog = orthoCap (pOr->lat, pOr->lon, pDest->lat, pDest->lon);
   pDest->toIndexWp = toIndexWp;
   lastClosestDist = pOr->dd;
   lastBestVmg = 0;
   tempList [0] = *pOr; // liste avec un unique élément;
   initSector (nIsoc, par.nSectors); 
   
   if (goalP (pOr, pDest, t, dt, &timeToReach, &distance, &motor, &amure)) {
      pDest->father = pOr->id;
      pDest->motor = motor;
      pDest->amure = amure;
      *lastStepDuration = timeToReach;
      return nIsoc + 1;
   }
   isoDesc [nIsoc].size = buildNextIsochrone (pOr, pDest, tempList, 1, t, dt, isocArray [nIsoc], &theBestVmg);
   // printf ("%-20s%d, %d\n", "Isochrone no, len: ", 0, isoDesc [0].size);
   lastBestVmg = theBestVmg;
   isoDesc [nIsoc].bestVmg = theBestVmg;
   isoDesc [nIsoc].first = 0; 
   lastClosest = closest (isocArray [nIsoc], isoDesc[nIsoc].size, pDest, &index);
   isoDesc [nIsoc].closest = index; 
   isoDesc [nIsoc].toIndexWp = toIndexWp; 
   if (isoDesc [nIsoc].size == 0) { // no wind at the beginning. 
      isoDesc [nIsoc].size = 1;
      isocArray [nIsoc][0] = *pOr;
   }
   
   nIsoc += 1;
   //printf ("Routing t = %.2lf, zone: %.2ld\n", t, zone.timeStamp [zone.nTimeStamp-1]);
   while (t < (zone.timeStamp [zone.nTimeStamp-1]/* + par.tStep*/) && (nIsoc < par.maxIso)) {
      t += dt;
      if (goal (pDest, nIsoc-1, isocArray [nIsoc -1], isoDesc[nIsoc - 1].size, t, dt, &timeLastStep, &motor, &amure)) {
         isoDesc [nIsoc].size = optimize (pOr, pDest, nIsoc, par.opt, tempList, lTempList, isocArray [nIsoc]);
         if (isoDesc [nIsoc].size == 0) { // no Wind ... we copy
            isoDesc  [nIsoc].size = isoDesc [nIsoc -1].size;
            memcpy (isocArray [nIsoc], isocArray [nIsoc-1], isoDesc [nIsoc-1].size * sizeof (Pp));
         }
         isoDesc [nIsoc].first = findFirst (nIsoc);
         lastClosest = closest (isocArray [nIsoc], isoDesc[nIsoc].size, pDest, &index);
         isoDesc [nIsoc].closest = index; 
         isoDesc [nIsoc].toIndexWp = toIndexWp; 
         *lastStepDuration = timeLastStep;
         return nIsoc + 1;
      }
      lTempList = buildNextIsochrone (pOr, pDest, isocArray [nIsoc -1], isoDesc [nIsoc -1].size, t, dt, tempList, &theBestVmg);
      if (lTempList == -1) return -1;
      lastBestVmg = theBestVmg;
      isoDesc [nIsoc].bestVmg = theBestVmg;
      isoDesc [nIsoc].size = optimize (pOr, pDest, nIsoc, par.opt, tempList, lTempList, isocArray [nIsoc]);
      if (isoDesc [nIsoc].size == 0) { // no Wind ... we copy
         replicate (nIsoc);
      }
      isoDesc [nIsoc].first = findFirst (nIsoc);; 
      lastClosest = closest (isocArray [nIsoc], isoDesc [nIsoc].size, pDest, &index);
      isoDesc [nIsoc].closest = index;
      isoDesc [nIsoc].toIndexWp = toIndexWp; 
      // printf ("Isoc: %d Biglist length: %d optimized size: %d\n", nIsoc, lTempList, isoDesc [nIsoc].size);
      nIsoc += 1;
   }
   *lastStepDuration = 0.0;
   return NIL;
}

/*! launch routing wih parameters */
void *routingLaunch () {
   struct timeval t0, t1;
   long ut0, ut1;
   double lastStepDuration;
   lastClosest = par.pOr;
   tDeltaCurrent = zoneTimeDiff (&currentZone, &zone); // global variable
   Pp pNext;
   par.pOr.id = -1;
   par.pOr.father = -1;
   par.pDest.id = 0;
   par.pDest.father = 0;
   pId = 1;
   route.n = 0;
   route.destinationReached = false;
   for (int i = 0; i < MAX_N_ISOC; i++) {
      isoDesc [i].size = 0;
      isoDesc [i].distance = DBL_MAX;
      isoDesc [i].bestVmg = 0;
   }

   gettimeofday (&t0, NULL);
   ut0 = t0.tv_sec * MILLION + t0.tv_usec;
   nIsoc = 0;
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
            route.ret = routing (&isocArray [nIsoc-1][0], &pNext, i, wayPointStartTime, par.tStep, &lastStepDuration);
         if (route.ret > 0) {
            wayPointStartTime = par.startTimeInHours + (nIsoc * par.tStep) + lastStepDuration;
            isocArray [nIsoc][0].lat = wayPoints.t[i].lat;
            isocArray [nIsoc][0].lon = wayPoints.t[i].lon;
            isocArray [nIsoc][0].father = pNext.father;
            isocArray [nIsoc][0].id = pId++;
            isoDesc [nIsoc].size = 1;
            nIsoc += 1;
         }
         else break;
      }
      if (route.ret > 0)
         route.ret = routing (&isocArray [nIsoc-1][0], &par.pDest, -1, wayPointStartTime, par.tStep, &lastStepDuration);
   }

   gettimeofday (&t1, NULL);
   ut1 = t1.tv_sec * MILLION + t1.tv_usec;
   route.calculationTime = (double) ((ut1-ut0)/MILLION);
   route.destinationReached = (route.ret > 0);
   storeRoute (&par.pOr, &lastClosest, lastStepDuration);

   if (strlen (par.dumpRFileName) > 0) dumpRoute (par.dumpRFileName, lastClosest);
   if (strlen (par.dumpIFileName) > 0) dumpAllIsoc (par.dumpIFileName);
   return NULL;
}

/*! choose best time to reach pDest in minimum time */
void *bestTimeDeparture () {
   int n;
   double lastStep, duration, minDuration = DBL_MAX, maxDuration = 0;
   chooseDeparture.bestTime = -1;
   chooseDeparture.count = 0;
   chooseDeparture.tStop = chooseDeparture.tEnd; // default value

   for (int t = chooseDeparture.tBegin; t < chooseDeparture.tEnd; t += chooseDeparture.tStep) {
      n = routing (&par.pOr, &par.pDest, -1, (double) t, par.tStep, &lastStep);
      if (n > 0) {
         duration = par.tStep * (n-1) + lastStep; // hours
         chooseDeparture.t [t] = duration;
         if (duration < minDuration) {
            minDuration = duration;
            chooseDeparture.bestTime = t;
         }
         if (duration > maxDuration) {
            maxDuration = duration;
         }
         //printf ("Count: %d, time %d, last duration: %.2lf, min: %.2lf, bestTime: %d\n", chooseDeparture.count, t, duration, minDuration, chooseDeparture.bestTime);
      }
      else {
         chooseDeparture.tStop = t;
         //printf ("Count: %d, time %d, Unreachable\n", chooseDeparture.count, t);
         break;
      }
      chooseDeparture.count += 1;
   }
   if (chooseDeparture.bestTime != -1) {
      par.startTimeInHours = chooseDeparture.bestTime;
      chooseDeparture.minDuration = minDuration;
      chooseDeparture.maxDuration = maxDuration;
      chooseDeparture.ret = EXIST_SOLUTION;
      routingLaunch ();
   }  
   else chooseDeparture.ret = NO_SOLUTION; // no solution
   return NULL;
}

