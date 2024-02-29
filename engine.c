/*! \Engine
* tws = True Wind Speed
* twd = True Wind Direction
* twa = True Wind Angle - the angle of the boat to the wind
* sog = Speed over Ground of the boat
* cog = Course over Ground  of the boat
* */
/*! compilation gcc -Wall -c engine.c */
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
#include "rutil.h"

/*! global variables */
Pp      isocArray [MAX_N_ISOC][MAX_SIZE_ISOC];  // list of isochrones
IsoDesc isoDesc [MAX_N_ISOC];                   // Isochrone meta data
int     nIsoc = 0;                              // total number of isochrones 
Route   route;                                  // store route found by routing
Pp      lastClosest;                            // closest point to destination in last isochrone computed

double   pOrTtoPDestCog = 0;                    // cog from par.pOr to par.pDest.

struct {
   double dd;
   double vmg;
   double nPt;
} sector [2][MAX_SIZE_ISOC];                    // we keep even and odd last sectors

//int first;
int      pId = 1;                               // global ID for points. -1 and 0 are reserved for pOr and pDest
double   tDeltaCurrent = 0.0;                   // delta time in hours between Wind zone and current zone
double   lastClosestDist;                       // closest distance to destination in last isochrone computed
double   lastBestVmg = 0;                       // best VMG found up to now

/*! true if P is within the zone */
static inline bool isInZone (Pp pt, Zone zone) {
   if (par.constWindTws > 0) return true;
   else return (pt.lat >= zone.latMin) && (pt.lat <= zone.latMax) && (pt.lon >= zone.lonLeft) && (pt.lon <= zone.lonRight);
}

/*! return fx : linear interpolation */
static inline double interpolate (double x, double x0, double x1, double fx0, double fx1) {
   if (x1 == x0) return fx0;
   else return fx0 + (x-x0) * (fx1-fx0) / (x1-x0);
}

/*! find in polar boat speed or wave coeff */
static inline double findPolar (double twa, double w, PolMat mat) {
   int l, c, lInf, cInf, lSup, cSup;
   if (twa > 180) twa = 360 - twa;
   else if (twa < 0) twa = -twa;
   for (l = 1; l < mat.nLine; l++) 
      if (mat.t [l][0] > twa) break;
   lSup = (l < mat.nLine - 1) ? l : mat.nLine - 1;
   lInf = (l == 1) ? 1 : l - 1;
   for (c = 1; c < mat.nCol; c++) 
      if (mat.t [0][c] > w) break;
   cSup = (c < mat.nCol - 1) ? c : mat.nCol - 1;
   cInf = (c == 1) ? 1 : c - 1;
   double s0 = interpolate (twa, mat.t [lInf][0], mat.t [lSup][0], mat.t [lInf][cInf], mat.t [lSup][cInf]);
   double s1 = interpolate (twa, mat.t [lInf][0], mat.t [lSup][0], mat.t [lInf][cSup], mat.t [lSup][cSup]);
   return interpolate (w, mat.t [0][cInf], mat.t [0][cSup], s0, s1);
}

/*! idem findPolar but extern not inline */
double extFindPolar (double twa, double w, PolMat mat) {
   return findPolar (twa, w, mat);
}

/*! return max speed of boat at tws for all twa */
static inline double maxSpeedInPolarAt (double tws, PolMat mat) {
   double max = 0.0;
   double speed;
   for (int i = 1; i < mat.nLine; i++) {
      speed = findPolar (mat.t [i][0], tws, mat);
      if (speed > max) max = speed;
   }
   return max;
}

/*! say if point is in sea */
static inline bool isSea (double lon, double lat) {
   if (tIsSea == NULL) return true;
   int iLon = round (lon * 10  + 1800);
   int iLat = round (-lat * 10  + 900);
   return tIsSea [(iLat * 3601) + iLon];
}

/*! idem isSea but not inline */
bool extIsSea (double lon, double lat) {
   return isSea (lon, lat);
}

/*! return pythagore distance */
static inline double dist (double x, double y) {
   return sqrt (x*x + y*y);
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
static void initSector (int nIsoc, int nMax) {
   for (int i = 0; i < nMax; i++) {
      sector [nIsoc][i].dd = DBL_MAX;
	   sector [nIsoc][i].vmg = 0;
      sector [nIsoc][i].nPt = 0;
   }
}

/*! reduce the size of Isolist 
   note influence of parameters par.nSector, par.jFactor and par.kFactor */

static inline int forwardSectorOptimize (int nIsoc, Isoc isoList, int isoLen, Isoc optIsoc) {
   int iSector, k;
   double alpha, theta;
   double thetaStep = 360.0 / par.nSectors;
   Pp ptTarget;
   double dLat = -par.jFactor * cos (DEG_TO_RAD * pOrTtoPDestCog);  // nautical miles in N S direction
   double dLon = -par.jFactor * sin (DEG_TO_RAD * pOrTtoPDestCog) / cos (DEG_TO_RAD * (par.pOr.lat + par.pDest.lat)/2); // nautical miles in E W direction

   memset (&ptTarget, 0, sizeof (Pp));
   ptTarget.lat = par.pOr.lat + dLat / 60; 
   ptTarget.lon = par.pOr.lon + dLon / 60;
   isoDesc [nIsoc].focalLat = ptTarget.lat;
   isoDesc [nIsoc].focalLon = ptTarget.lon;
  
   //ptTarget = par.pDest; 
   double beta = directCap (par.pOr.lat, par.pOr.lon, par.pDest.lat, par.pDest.lon);
   initSector (nIsoc % 2, par.nSectors);
   for (int i = 0; i < par.nSectors; i++)
      optIsoc [i].lat = DBL_MAX;

   for (int i = 0; i < isoLen; i++) {
      alpha = directCap (ptTarget.lat, ptTarget.lon, isoList [i].lat, isoList [i].lon);
      theta = beta - alpha;
      // if (round ((180.0 - theta) / thetaStep) == 0) first = i; 
      if (theta < 0) theta += 360;
      if (fabs (theta) <= 360) {
         iSector = round ((360.0 - theta) / thetaStep);
		   if ((isoList [i].dd < sector [nIsoc%2][iSector].dd) && (isoList [i].vmg > sector [nIsoc%2] [iSector].vmg)) {
            sector [nIsoc%2][iSector].dd = isoList [i].dd;
            sector [nIsoc%2][iSector].vmg = isoList [i].vmg;
            optIsoc [iSector] = isoList [i];
         }
         sector [nIsoc%2][iSector].nPt += 1;
      }
   }
   // on retasse si certains secteurs vides ou faiblement peuples ou moins avances que precedent
   k = 0;
   for (iSector = 0; iSector < par.nSectors; iSector++) {
      if ((sector [nIsoc%2][iSector].nPt >= par.minPt)  
         && (sector [nIsoc%2][iSector].dd < DBL_MAX -1) 
         && (sector [nIsoc%2][iSector].vmg > 0.6 * lastBestVmg) 
         && ((par.kFactor == 0)
            || ((par.kFactor == 1) && (sector [nIsoc%2][iSector].vmg >= sector [(nIsoc-1)%2][iSector].vmg))
            || ((par.kFactor == 2) && (sector [nIsoc%2][iSector].dd < sector [(nIsoc-1)%2][iSector].dd))
            || ((par.kFactor == 3) && (sector [nIsoc%2][iSector].vmg >= sector [(nIsoc-1)%2][iSector].vmg) && (sector [nIsoc%2][iSector].dd < sector [(nIsoc-1)%2][iSector].dd)))) {
         optIsoc [k] = optIsoc [iSector];
         optIsoc [k].sector = iSector;
	      k += 1;
      }
	}
	return k; 
}

/*! do nothing. Just copy. For testing. */
static int isocCpy (const Isoc isoList, int isoLen, Isoc optIsoc) {
   for (int i = 0; i < isoLen; i++) {
       optIsoc [i] = isoList [i];
   }
   return isoLen;
}

/*! choice of algorithm used to reduce the size of Isolist */
static inline int optimize (int nIsoc, int algo, Isoc isoList, int isoLen, Isoc optIsoc) {
   switch (algo) {
      case 0: return isocCpy (isoList, isoLen, optIsoc);
      case 1: return forwardSectorOptimize (nIsoc, isoList, isoLen, optIsoc);
   } 
   return 0;
}

/*! build the new list describing the next isochrone, starting from isoList 
  returns length of the newlist built or -1 if error*/
static int buildNextIsochrone (Isoc isoList, int isoLen, Pp pDest, double t, double dt, Isoc newList, double *bestVmg) {
   double vMaxSpeedInPolarAt;           // memorise result of last call to maxSpeedinPolarAt
   int lenNewL = 0;
   double u, v, gust, w, twa, sog, uCurr, vCurr, bidon;
   double vDirectCap;
   int directCog;
   double dLat, dLon;
   double penalty;
   Pp newPt;
   double twd = par.constWindTwd;
   double tws = par.constWindTws;
   bool motor = false;
   double efficiency = par.efficiency;
   double waveCorrection = 1.0;
   *bestVmg = 0;
   
   for (int k = 0; k < isoLen; k++) {
      uCurr = 0; vCurr = 0; w = 0;
      if (par.constWindTws == 0) {
         findFlow (isoList [k], t, &u, &v, &gust, &w, zone, tGribData [WIND]);
         //printf ("u: %.2lf v:%.2lf\n", u, v);
         twd = fTwd (u, v);
         tws = fTws (u, v);
      } 
      // printf ("twd: %.2lf tws:%.2lf\n", twd, tws);
      if (par.constWave != 0) w = par.constWave;

      if (par.constCurrentS != 0) {
         uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
         vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
      }
      else findFlow (isoList [k], t - tDeltaCurrent, &uCurr, &vCurr, &gust, &bidon, currentZone, tGribData [CURRENT]);
      vDirectCap = directCap (isoList [k].lat, isoList [k].lon, pDest.lat, pDest.lon);
      directCog = ((int)(vDirectCap / par.cogStep)) * par.cogStep;
      sog = par.motorSpeed;
      vMaxSpeedInPolarAt = maxSpeedInPolarAt (tws, polMat); // store in global variable for future use
      motor =  (vMaxSpeedInPolarAt < par.threshold) && (par.motorSpeed > 0);
      if (motor) {
         efficiency = 1.0;
         vMaxSpeedInPolarAt = par.motorSpeed;
      }
      //printf ("vMaxSpeedInPolarAt: %.2lf\n", vMaxSpeedInPolarAt); 
      for (int cog = (directCog - par.rangeCog); cog < (directCog + par.rangeCog + 1); cog+=par.cogStep) {
         twa = (cog > twd) ? cog - twd : cog - twd + 360;   //angle of the boat with the wind
         
         if (twa > 360) twa -= 360; 
         if (! motor) sog = findPolar (twa, tws, polMat); //speed of the boat considering twa and wind speed (tws) in polar
         else sog = par.motorSpeed;
         // printf ("twa : %.2lf, tws: %.2lf, sog: %.2lf\n", twa, tws, sog);
	      // if (sog > 7) printf ("bUilN sog %lf twa %lf twd %lf\n", sog, twa, tws);
         newPt.amure = (cog > twd) ? BABORD : TRIBORD;
         if ((waveCorrection = findPolar (twa, w, wavePolMat)) > 0)
            sog *= waveCorrection / 100.0; 
         sog *= efficiency;
         if ((!motor) && (newPt.amure != isoList [k].amure)) {                   // changement amure
            if ((twa > 90) && (twa < 270)) penalty = par.penalty1;    // empannage
            else penalty = par.penalty0;                           // virement de bord
         }
         else penalty = 0;
         dLat = sog * (dt - penalty) * cos (DEG_TO_RAD * cog);            // nautical miles in N S direction
         dLon = sog * (dt - penalty) * sin (DEG_TO_RAD * cog) / cos (DEG_TO_RAD * isoList [k].lat);  // nautical miles in E W direction
         dLat += MS_TO_KN * vCurr * dt;                   // correction for current
         dLon += MS_TO_KN * uCurr * dt / cos (DEG_TO_RAD * isoList [k].lat);
 
	      // printf ("builN dLat %lf dLon %lf\n", dLat, dLon);
         newPt.lat = isoList [k].lat + dLat/60;
         newPt.lon = isoList [k].lon + dLon/60;
         newPt.id = pId;
         newPt.father = isoList [k].id;
         newPt.vmg = 0;
         newPt.sector = 0;
         if (isSea (newPt.lon, newPt.lat)) {
            newPt.dd = orthoDist (newPt.lat, newPt.lon, pDest.lat, pDest.lon);
            double alpha = directCap (par.pOr.lat, par.pOr.lon, newPt.lat, newPt.lon) - pOrTtoPDestCog;
         
            newPt.vmg = orthoDist (newPt.lat, newPt.lon, par.pOr.lat, par.pOr.lon) * cos (DEG_TO_RAD * alpha);
            if (newPt.vmg > *bestVmg) *bestVmg = newPt.vmg;
            //if (newPt.vmg > 0.5 * lastBestVmg) { // vmg not too bad compared to the padt ? Really a good ideé ?
               if (lenNewL < MAX_SIZE_ISOC)
                  newList [lenNewL++] = newPt;                      // new point added to the isochrone
               else {
                  fprintf (stderr, "Routing Error in buildNextIsochrone, limit of MAX_SIZE_ISOC: %d\n", MAX_SIZE_ISOC);
                  return -1;
               }
               pId += 1;
           //} 
         }
      }
   }
   return lenNewL;
}

/*! find father of point in previous isochrone */
static int findFather (int ptId, Isoc isoc, int lIsoc) {
   for (int k = 0; k < lIsoc; k++) {
      if (isoc [k].id == ptId)
         return k;
   }
   // fprintf (stderr, "Error in father ptId not found;%d\n", ptId);
   return -1;
}

/*! copy isoc Descriptors in a string 
   true if enough space, false if truncated */
bool isoDectToStr (char *str, size_t maxLength) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   const int DIST_MAX = 100000;
   double distance;
   sprintf (str, "No  Size First Closest Distance VMG      FocalLat  FocalLon\n");
   for (int i = 0; i < nIsoc; i++) {
      distance = isoDesc [i].distance;
      if (distance > DIST_MAX) distance = -1;
      snprintf (line, MAX_SIZE_LINE, "%03d %03d  %03d   %03d     %07.2lf  %07.2lf  %s  %s\n", i, isoDesc[i].size, isoDesc[i].first, 
         isoDesc[i].closest, distance, isoDesc[i].bestVmg, 
         latToStr (isoDesc [i].focalLat, par.dispDms, strLat), lonToStr (isoDesc [i].focalLon, par.dispDms, strLon));
      if ((strlen (str) + strlen (line)) > maxLength) return false;
      strncat (str, line, MAX_SIZE_LINE); 
   }
   return true;
}

/*! copy all isoc in a string */
void allIsocToStr (char *str) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   Pp pt;
   sprintf (str, "No  Lat         Lon             Id Father  Amure\n");
   for (int i = 0; i < nIsoc; i++) {
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = isocArray [i][k];
         snprintf (line, MAX_SIZE_LINE, "%03d %-12s %-12s %6d %6d %6d\n", i, latToStr (pt.lat, par.dispDms, strLat),\
            lonToStr (pt.lon, par.dispDms, strLon), pt.id, pt.father, pt.amure);
         strncat (str, line, MAX_SIZE_LINE);
      }
   }
}
   
/*! write in CSV file Isochrones */
bool dumpAllIsoc (const char *fileName) {
   FILE *f;
   Pp pt;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "Routing Error in dumpAllIsoc, cannot write isoc: %s\n", fileName);
      return false;
   }
   fprintf (f, "n;     Lat;   Lon;      Id; Father;  Amure\n");
   for (int i = 0; i < nIsoc; i++) {
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = isocArray [i][k];
         fprintf (f, "%03d; %06.2f; %06.2f; %6d; %6d; %6d\n", i, pt.lat, pt.lon, pt.id, pt.father, pt.amure);
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
      fprintf (stderr, "Routing Error in dumpRoute, Cannot write route: %s\n", fileName);
      return false;
   }
   fprintf (f, "%4d; %06.2f; %06.2f; %4d; %4d\n", dep, pt.lat, pt.lon, pt.id, pt.father);
      
   for (i = dep - 1; i >= 0; i--) {
      iFather = findFather (pt.father, isocArray [i], isoDesc[i].size);
      pt = isocArray [i][iFather];
      fprintf (f, "%4d; %06.2f; %06.2f; %4d; %4d\n", i, pt.lat, pt.lon, pt.id, pt.father);
   }
   pt = par.pOr;
   fprintf (f, "%4d; %06.2f; %06.2f; %4d; %4d\n", -1, pt.lat, pt.lon, pt.id, pt.father);
   fclose (f);
   return true;
}

/*! store route */
void storeRoute (Pp pDest, double lastStepDuration) {
   //route.destinationReached = (pDest.id == 0);
   route.totDist = 0;
   int k;
   int iFather;
   int dep = (pDest.id == 0) ? nIsoc : nIsoc - 1;
   bool found = false;
   if (nIsoc == 0) {
      route.totDist = loxoDist (par.pOr.lat, par.pOr.lon, pDest.lat, pDest.lon);
      route.duration = lastStepDuration; // hours
      return;
   }
   Pp ptLast = pDest;
   Pp pt = pDest;
   route.t [dep+1].lat = pDest.lat;
   route.t [dep+1].lon = pDest.lon;
   route.t [dep+1].id = pDest.id;
   route.t [dep+1].father = pDest.father;
   route.t [dep+1].cap = 0;
   route.t [dep+1].d  = 0;
   route.t [dep+1].dOrtho = 0;

   for (int i = dep - 1; i >= 0; i--) {
      iFather = findFather (pt.father, isocArray [i], isoDesc[i].size);
      if (iFather == -1) continue;
      found = true;
      pt = isocArray [i][iFather];
      k = i+1;
      route.t [k].lat = pt.lat;
      route.t [k].lon = pt.lon;
      route.t [k].id = pt.id;
      route.t [k].father = pt.father;
      route.t [k].cap = directCap (pt.lat, pt.lon, ptLast.lat, ptLast.lon);
      route.t [k].d  = loxoDist (pt.lat, pt.lon, ptLast.lat, ptLast.lon);
      route.t [k].dOrtho = orthoDist (pt.lat, pt.lon, pDest.lat, pDest.lon);;
      route.totDist += route.t [k].d;
      ptLast = pt;
   }
   if (!found) {
      route.totDist = 0;
      return;
   }
   route.t [0].lat = par.pOr.lat;
   route.t [0].lon = par.pOr.lon;
   route.t [0].id = par.pOr.id;
   route.t [0].father = par.pOr.father;
   route.t [0].cap = directCap (par.pOr.lat, par.pOr.lon, ptLast.lat, ptLast.lon);
   route.t [0].d  = loxoDist (par.pOr.lat, par.pOr.lon, ptLast.lat, ptLast.lon);
   route.t [0].dOrtho = orthoDist (par.pOr.lat, par.pOr.lon, pDest.lat, pDest.lon);
   route.totDist += route.t[0].d;
   route.n = dep + 2;
   route.duration = par.tStep * nIsoc + lastStepDuration; // hours
}

/*! copy route in a string */
void routeToStr (Route route, char *str) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   sprintf (str, " No       Lat        Lon             Id Father     Cap     Dist      Ortho\n");
   snprintf (line, MAX_SIZE_LINE, " pOr:     %-12s%-12s %6d %6d %7.2f° %7.2f    %7.2f\n", \
      latToStr (route.t[0].lat, par.dispDms, strLat), lonToStr (route.t[0].lon, par.dispDms, strLon), \
      route.t[0].id, route.t[0].father, route.t[0].cap, route.t[0].d, route.t[0].dOrtho);
   
   strncat (str, line, MAX_SIZE_LINE);
   for (int i = 1; i < route.n-1; i++) {
      if ((fabs(route.t[i].lon) > 180.0) || (fabs (route.t[i].lat) > 90.0))
         snprintf (line, MAX_SIZE_LINE, " Isoc %2d: Erreur sur latitude ou longitude\n", i-1);   
      else
         snprintf (line, MAX_SIZE_LINE, " Isoc %2d: %-12s%-12s %6d %6d %7.2f° %7.2f    %7.2f\n", \
            i-1, latToStr (route.t[i].lat, par.dispDms, strLat), lonToStr (route.t[i].lon, par.dispDms, strLon), \
            route.t[i].id, route.t[i].father, \
            route.t[i].cap, route.t[i].d, route.t[i].dOrtho); 
      strncat (str, line, MAX_SIZE_LINE);
   }
   if (route.destinationReached)
      snprintf (line, MAX_SIZE_LINE, " Dest:    %-12s%-12s %6d %6d \n", latToStr (route.t[route.n-1].lat, par.dispDms, strLat),\
         lonToStr (route.t[route.n-1].lon, par.dispDms, strLon),\
         route.t[route.n-1].id, route.t[route.n-1].father);
   else 
      snprintf (line, MAX_SIZE_LINE, " Isoc %2d: %-12s%-12s %6d %6d \n", nIsoc -1, latToStr (route.t[route.n-1].lat, par.dispDms, strLat),\
         lonToStr (route.t[route.n-1].lon, par.dispDms, strLon),\
         route.t[route.n-1].id, route.t[route.n-1].father);
   strncat (str, line, MAX_SIZE_LINE);
   snprintf (line, MAX_SIZE_LINE, " Total distance: %.2f NM", route.totDist);
   strncat (str, line, MAX_SIZE_LINE);
}

/*! return true if pDest can be reached from pFrom in less time than dt
   bestTime give the time to get from pFrom to pDest */
static inline bool goalP (Pp pFrom, Pp pDest, double t, double dt, double *timeTo, double *distance) {
   double u, v, gust, w, twd, tws, twa, sog, uCurr, vCurr, bidon;
   double coeffLat = cos (DEG_TO_RAD * (pFrom.lat + pDest.lat)/2);
   double dLat = pDest.lat - pFrom.lat;
   double dLon = pDest.lon - pFrom.lon;
   double cog = RAD_TO_DEG * atan2 (dLon * coeffLat, dLat);
   double penalty;
   bool motor = false;
   double efficiency;
   double waveCorrection = 0;
   *distance = orthoDist (pDest.lat, pDest.lon, pFrom.lat, pFrom.lon);
   if (par.constWindTws != 0) {
      twd = par.constWindTwd;
      tws = par.constWindTws;
      w = 0;
   }
   else {
      findFlow (pFrom, t, &u, &v, &gust, &w, zone, tGribData [WIND]);
      twd = fTwd (u, v);
      tws = fTws (u, v);
   }
   if (par.constWave != 0) w = par.constWave;

   if (par.constCurrentS != 0) {
      uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
      vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
   }
   else findFlow (pFrom, t - tDeltaCurrent, &uCurr, &vCurr, &gust, &bidon, currentZone, tGribData [CURRENT]);
   // ATTENTION Courant non pris en compte dans la suite !!!
   twa = (cog > twd) ? cog - twd : cog - twd + 360;      // angle of the boat with the wind
   motor =  ((maxSpeedInPolarAt (tws, polMat) < par.threshold) && (par.motorSpeed > 0));
   // printf ("maxSpeedinPolar: %.2lf\n", maxSpeedInPolarAt (tws, polMat));
   if (motor) {
      sog = par.motorSpeed;
      efficiency = 1.0;
   }
   else {
      efficiency = par.efficiency;
      sog = findPolar (twa, tws, polMat);  //speed of the boat considering twa and wind speed (tws) in polar
   }
   if ((waveCorrection = findPolar (twa, w, wavePolMat)) > 0) 
      sog *= waveCorrection / 100.0;
   sog *= efficiency;
   *timeTo = *distance/sog;
   //if ((sog * dt) > distance) printf ("goal sog %lf dt %lf distance %lf\n", sog, dt, distance);
   if ((!motor) && (pDest.amure != pFrom.amure)) {    // changement amure
      if (fabs (twa) < 90) penalty = par.penalty0;    // virement de bord
      else penalty = par.penalty1;                    // empannage Jibe
   }
   else penalty = 0;
   return ((sog * (dt - penalty)) > *distance);
}

/*! return closest point to pDest in Isoc 
  true if goal can be reached directly in dt from isochrone 
  side effect : pDest.father can be modified ! */
static inline bool goal (int nIsoc, Isoc isoList, int len, double t, double dt, double *lastStepDuration) {
   double bestTime = DBL_MAX;
   double time, distance;
   bool destinationReached = false;
   isoDesc [nIsoc].distance = 9999.99;
   for (int k = 0; k < len; k++) {
      if (isSea (isoList [k].lon, isoList [k].lat)) { 
         if (goalP (isoList [k], par.pDest, t, dt, &time, &distance))
            destinationReached = true;
         // printf ("k, time destinationReached: %d %lf %d\n",  k, time, destinationReached);
         if (destinationReached)
            par.pDest.father = isoList [k].id; // ATTENTION !!!!
         if (time < bestTime) {
            if (destinationReached) par.pDest.father = isoList [k].id; // ATTENTION !!!!
            bestTime = time;
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
static inline Pp closest (Isoc isoc, int n, Pp pDest, int *index) {
   Pp best = isoc [0];
   double d;
   int i;
   *index = -1;
   lastClosestDist = DBL_MAX;
   for (i = 0; i < n; i++) {
      d = orthoDist (pDest.lat, pDest.lon, isoc [i].lat, isoc [i].lon);
      if (d < lastClosestDist) {
         lastClosestDist = d;
         best = isoc [i];
         *index = i;
      }
   }
   return best;
}

/*! find optimal routing from p0 to pDest using grib file and polar
    return number of steps to reach pDest, NIL if unreached, -1 if problem, 0 reserved for not terminated
    return also lastStepDuration if the duration of last step if destination reached (0 if unreached) */
int routing (Pp pOr, Pp pDest, double t, double dt, double *lastStepDuration) {
   Isoc tempList;
   double distance;
   double timeToReach = 0;
   int lTempList = 0;
   double timeLastStep;
   int index; // index of closest point to pDest in insochrone
   double bestVmg;
   par.pOr.id = -1;
   par.pOr.father = -1;
   par.pDest.father = 0;
   pOr.dd = orthoDist (pOr.lat, pOr.lon, pDest.lat, pDest.lon);
   pOr.vmg = 0;
   pOrTtoPDestCog = directCap (pOr.lat, pOr.lon, pDest.lat, pDest.lon);
   lastClosestDist = pOr.dd;
   lastBestVmg = 0;
   tDeltaCurrent = zoneTimeDiff (currentZone, zone); // global variable
   nIsoc = 0;
   pId = 1;
   route.n = 0;
   route.destinationReached = false;
   initSector (0, par.nSectors); 
   for (int i = 0; i < MAX_N_ISOC; i++) {
      isoDesc [i].size = 0;
      isoDesc [i].distance = DBL_MAX;
      isoDesc [i].bestVmg = 0;
   }
   tempList [0] = pOr; // liste avec un unique élément;
   
   if (goalP (pOr, pDest, t, dt, &timeToReach, &distance)) {
      pDest.father = pOr.id;
      *lastStepDuration = timeToReach;
      return 1;
   }
   isoDesc [0].size = buildNextIsochrone (tempList, 1, pDest, t, dt, isocArray [0], &bestVmg);
   // printf ("%-20s%d, %d\n", "Isochrone no, len: ", 0, isoDesc [0].size);
   lastBestVmg = bestVmg;
   isoDesc [0].bestVmg = bestVmg;
   isoDesc [0].first = 0; 
   lastClosest = closest (isocArray [0], isoDesc[0].size, pDest, &index);
   isoDesc [0].closest = index; 
   
   nIsoc = 1;
   while (t < (zone.timeStamp [zone.nTimeStamp-1]/* + par.tStep*/) && (nIsoc < par.maxIso)) {
      t += dt;
      if (goal (nIsoc-1, isocArray [nIsoc -1], isoDesc[nIsoc - 1].size, t, dt, &timeLastStep)) {
         isoDesc [nIsoc].size = optimize (nIsoc, par.opt, tempList, lTempList, isocArray [nIsoc]);
         if (isoDesc [nIsoc].size == 0) { // no Wind ... we copy
            isoDesc  [nIsoc].size = isoDesc [nIsoc -1].size;
            isocCpy (isocArray [nIsoc -1], isoDesc [nIsoc-1].size, isocArray [nIsoc]);
         }
         isoDesc [nIsoc].first = findFirst (nIsoc);
         lastClosest = closest (isocArray [nIsoc], isoDesc[nIsoc].size, pDest, &index);
         isoDesc [nIsoc].closest = index; 
         *lastStepDuration = timeLastStep;
         return nIsoc + 1;
      }
      lTempList = buildNextIsochrone (isocArray [nIsoc -1], isoDesc [nIsoc -1].size, pDest, t, dt, tempList, &bestVmg);
      if (lTempList == -1) return -1;
      lastBestVmg = bestVmg;
      isoDesc [nIsoc].bestVmg = bestVmg;
      isoDesc [nIsoc].size = optimize (nIsoc, par.opt, tempList, lTempList, isocArray [nIsoc]);
      if (isoDesc [nIsoc].size == 0) { // no Wind ... we copy
         isoDesc [nIsoc] = isoDesc [nIsoc - 1];
         isocCpy (isocArray [nIsoc -1], isoDesc [nIsoc-1].size, isocArray [nIsoc]);
      }
      //printf ("Isoc: %d Biglist length: %d optimized size: %d\n", nIsoc, lTempList, isoDesc [nIsoc].size);
      else {
         isoDesc [nIsoc].first = findFirst (nIsoc);; 
         lastClosest = closest (isocArray [nIsoc], isoDesc [nIsoc].size, pDest, &index);
         isoDesc [nIsoc].closest = index;
      } 
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
   route.kTime0 = (int) (par.startTimeInHours / (zone.timeStamp [1] - zone.timeStamp [0]));

   gettimeofday (&t0, NULL);
   ut0 = t0.tv_sec * MILLION + t0.tv_usec;

   //Launch routing !
   route.ret = routing (par.pOr, par.pDest, par.startTimeInHours, par.tStep, &lastStepDuration);
   printf ("After routingLaunch:  ret = %d\n", route.ret);

   gettimeofday (&t1, NULL);
   ut1 = t1.tv_sec * MILLION + t1.tv_usec;
   route.calculationTime = (double) ((ut1-ut0)/MILLION);
   route.destinationReached = (route.ret > 0);
   if (route.destinationReached) {
      storeRoute (lastClosest, lastStepDuration); //par.pDest should replace lastClosest
      if (strlen (par.dumpRFileName) > 0) dumpRoute (par.dumpRFileName, par.pDest);
   }
   else {
      storeRoute (lastClosest, lastStepDuration);
      if (strlen (par.dumpRFileName) > 0) dumpRoute (par.dumpRFileName, lastClosest);
   }
   if (strlen (par.dumpIFileName) > 0) dumpAllIsoc (par.dumpIFileName);
   return NULL;
}
