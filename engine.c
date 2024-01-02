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
Pp isocArray [MAX_N_ISOC][MAX_SIZE_ISOC];
double tDistance [MAX_N_ISOC];   // best distance from isoc to dest
int nIsoc = 0;
int sizeIsoc [MAX_N_ISOC] = {0}; // size isoc
double pOrTtoPDestDist = 0;      // distance from par.pOr to par.pDest.
double pOrTtoPDestCog = 0;       // cog from par.pOr to par.pDest.
double tDist [MAX_SIZE_ISOC];    // distance to dest
double nPt [MAX_SIZE_ISOC];      // nb of point per sector
int firstInIsoc [MAX_N_ISOC];    // index of fiest point, useful for drawAllIsocrones
int first;
int pId = 1;                     // global ID for points. -1 and 0 are reserved for pOr and pDest
Route route;                     // store route found by routing
double tDeltaCurrent = 0.0;      // delta time in hours betwwen Wind zone and current zone
Pp lastClosest;                  // closest point to destination in last isocgrone computed
double lastClosestDist;          // closest distance to destination in last isocgrone computed

/*! return pythagore distance */
static inline double dist (double x, double y) {
   return sqrt (x*x + y*y);
}

/*! nautical loxodromic distance between p0 and P1 */
static inline double loxoDist (Pp p0, Pp p1) {
   double coeffLat = cos (DEG_TO_RAD * (p0.lat + p1.lat)/2);
   return 60 * dist (p0.lat - p1.lat, (p0.lon-p1.lon) * coeffLat);
}

/*! direct loxodromic cap from pFrom to pDest */
static double directCap (Pp pFrom, Pp pDest) {
   return RAD_TO_DEG * atan2 ((pDest.lon - pFrom.lon) * \
      cos (DEG_TO_RAD * (pFrom.lat+pDest.lat)/2), pDest.lat - pFrom.lat);
}

/*! use translation and rotation to compute if all the points are on one side of the segment p0 p1 */
static bool isSegment (Pp p0, Pp p1, Isoc bigList, int bigLen) {
   double x, y, xx;
   int oldSignX, signX;
   double dLat = p1.lat - p0.lat;
   double dLon = (p1.lon - p0.lon) * cos ((DEG_TO_RAD * (p0.lat + p1.lat))/2);
   double distP0P1 = dist (dLat, dLon); 
   Pp pt;
   
   if (distP0P1 == 0) return false;

   double sinTeta = dLon/distP0P1;
   double cosTeta = dLat/distP0P1;
   
   // rotation and translation
   oldSignX = NIL;
   for (int k = 0; k < bigLen; k++) {
      pt = bigList [k];
      if ((pt.id != p0.id) && (pt.id != p1.id)) {
         x = pt.lon - p0.lon;                // translation using p0 as reference point (0, x)
         y = pt.lat - p0.lat;                //idem (0, y)
         xx = (-cosTeta * x + sinTeta * y);  //rotation Teta
         signX = (xx < 0) ? -1 : 1;
         if ((signX != oldSignX) && (oldSignX != NIL)) return false;
         oldSignX = signX;
      }
   }
   return true;
}

/*! reduce the size of Isolist */
static int segmentOptimize (Isoc isoList, int isoLen, Isoc optIsoc) {
   int lOptIsoc = 0;
   for (int i = 0; i < isoLen; i++) {
      for (int j = 0; j < isoLen; j++) {
         if ((i != j) && (isSegment (isoList [i], isoList [j], isoList, isoLen))) {
               optIsoc [lOptIsoc] = isoList [i];
               //optIsoc [lOptIsoc].brother = isoList [j].id; // track adjacent point
               lOptIsoc += 1;
               // optIsoc [lOptIsoc] = isoList [j];
               // optIsoc [lOptIsoc].brother = isoList [i].id; // track adjacent point
               // lOptIsoc += 1;
               break;
          }
      }
   }
   return lOptIsoc;
}

/*! bof */
static int nSector (double dist) { 
   if (dist > 200) return 2000;
   if (dist > 100) return 1000;
   if (dist > 50) return 500;
   if (dist > 20) return 50;
   return 20;
}

/*! find fiest point in isochrone useful for drawAllIsochrones */ 
static inline int findFirst (int nIsoc) {
   int best = 0;
   int next;
   double d;
   double distMax = 0;
   for (int i = 0; i < sizeIsoc [nIsoc]; i++) {
      next = (i >= (sizeIsoc [nIsoc] -1)) ? 0 : i + 1;
      d = loxoDist (isocArray [nIsoc][i], isocArray [nIsoc][next]);
      if (d > distMax) {
         distMax = d;
         best = next;
      }
   }
   return best;
}

/*! reduce the size of Isolist */
static int sectorOptimize (Isoc isoList, int isoLen, Isoc optIsoc, int nMaxOptIsoc, double distTarget) {
   int iSector, k;
   double alpha, theta, d;
   const int MIN_PT = 2;
   // nMaxOptIsoc = nSector (lastClosestDist);
   double thetaStep = 360.0 / nMaxOptIsoc;
   Pp ptTarget;
   double dPtTarget = pOrTtoPDestDist - lastClosestDist + distTarget;
   double dLat = dPtTarget * cos (DEG_TO_RAD * pOrTtoPDestCog);            // nautical miles in N S direction
   ptTarget.lat = par.pOr.lat + dLat / 60;
   double dLon = dPtTarget * sin (DEG_TO_RAD * pOrTtoPDestCog) / cos (DEG_TO_RAD * ptTarget.lat);  // nautical miles in E W direction
   ptTarget.lon = par.pOr.lon + dLon / 60;
  
   //ptTarget = par.pDest; 
   double beta = directCap (par.pOr, par.pDest);
   for (int i = 0; i < nMaxOptIsoc; i++) { 
	  tDist [i] = DBL_MAX;
     nPt [i] = 0;
     optIsoc [i].lat = DBL_MAX;
   }

   for (int i = 0; i < isoLen; i++) {
      alpha = directCap (isoList [i], ptTarget);
      theta = beta - alpha;
      if (round ((180.0 - theta) / thetaStep) == 0)
         first = i; 
      if (theta < 0) theta += 360;
      iSector = round ((360.0 - theta) / thetaStep);
	   d = loxoDist (par.pDest, isoList [i]);
		if (d < tDist [iSector]) {
         tDist[iSector] = d;
         optIsoc [iSector] = isoList [i];
      }
      nPt [iSector] += 1;
   }
   // on retasse si certains secteurs vides ou faiblement peuples
   k = 0;
   for (iSector = 0; iSector < nMaxOptIsoc; iSector++) {
      if ((nPt [iSector] >= MIN_PT) && (tDist [iSector] < DBL_MAX -1)) { //&& (optIsoc [iSector].lat < DBL_MAX -1)) {
         optIsoc [k] = optIsoc [iSector];
	      k += 1;
      }
	}
	return k; 
}

/*! reduce the size of Isolist */
static int OsectorOptimize (bool invEnabled, Isoc isoList, int isoLen, Isoc optIsoc, int nMaxOptIsoc, double maxTheta) {
   int iSector, k;
   double tDist [MAX_SIZE_ISOC];
   double alpha, theta, d;
   nMaxOptIsoc = nSector (lastClosestDist);
   double thetaStep = 2.0 * maxTheta / nMaxOptIsoc;
   double coeffLat = cos (DEG_TO_RAD * (par.pOr.lat+par.pDest.lat)/2);
   bool inverse = false;
   double beta = directCap (par.pOr, par.pDest);
   // printf ("Opt2 beta :%lf\n", beta);
   if (invEnabled && ((beta > 135) || (beta < -135))) {
      inverse = true;
      beta = RAD_TO_DEG * atan2 ((par.pDest.lat - par.pOr.lat), (par.pDest.lon - par.pOr.lon) * coeffLat);
   }
   for (int i = 0; i < nMaxOptIsoc; i++) { 
	  tDist [i] = DBL_MAX;
     optIsoc [i].lat = DBL_MAX;
   }

   for (int i = 0; i < isoLen; i++) {
      if (inverse) {
         alpha = RAD_TO_DEG * atan2 ((isoList [i].lat - par.pOr.lat), (isoList [i].lon - par.pOr.lon) * coeffLat);
         theta = beta - alpha;
      }
      else {
         alpha = directCap (isoList [i], par.pDest);
         theta = beta - alpha;
      }
		iSector = round ((maxTheta - theta) / thetaStep);
	   // d = loxoDist (par.pDest, isoList [i]) * cos (DEG_TO_RAD * theta); // * (2 + fabs (sin (DEG_TO_RAD * theta))/3.0);
	   d = loxoDist (par.pDest, isoList [i]);
		// printf ("Theta %.2lf, iSector: %d d: %.2lf\n", theta, iSector, d);
		if (d < tDist [iSector]) {
         tDist[iSector] = d;
         optIsoc [iSector] = isoList [i];
      }
   }
   // on retasse si certains secteurs vides
   k = 0;
   for (iSector = 0; iSector < nMaxOptIsoc; iSector++) {
      if ((tDist [iSector] < DBL_MAX -1)) { //&& (optIsoc [iSector].lat < DBL_MAX -1)) {
         optIsoc [k] = optIsoc [iSector];
	      k += 1;
      }
	}
	return k; 
}

/*! do nothing. Just copy. For testing. */
static int Noptimize (Isoc isoList, int isoLen, Isoc optIsoc) {
   for (int i = 0; i < isoLen; i++) {
       optIsoc [i] = isoList [i];
   }
   return isoLen;
}

/*! choice of algorithm used to reduce the size of Isolist */
static int optimize (int algo, Isoc isoList, int isoLen, Isoc optIsoc) {
   switch (algo) {
      case 0: return Noptimize (isoList, isoLen, optIsoc);
      case 1: return sectorOptimize (isoList, isoLen, optIsoc, par.nSectors, par.distTarget);
      case 2: return OsectorOptimize (true, isoList, isoLen, optIsoc, par.nSectors, par.maxTheta);
      case 3: return segmentOptimize (isoList, isoLen, optIsoc);
      default:; 
   } 
   return 0;
}

/*! build the new list describing the next isochrone, starting from isoList 
  returns length of the newlist built or -1 if error*/
static int buildNextIsochrone (Isoc isoList, int isoLen, Pp pDest, double t, double dt, Isoc newList) {
   int lenNewL = 0;
   double u, v, w, twa, sog, uCurr, vCurr, bidon;
   double vDirectCap;
   int directCog;
   double dLat, dLon;
   double penalty;
   Pp newPt;
   double twd = par.constWindTwd;
   double tws = par.constWindTws;
   
   for (int k = 0; k < isoLen; k++) {
      uCurr = 0; vCurr = 0;
      if (par.constWindTws == 0) {
         findFlow (isoList [k], t, &u, &v, &w, zone, gribData);
         twd = fTwd (u, v);
         tws = fTws (u, v);
      } 
      if (par.constCurrentS != 0) {
         uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
         vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
      }
      else findFlow (isoList [k], t - tDeltaCurrent, &uCurr, &vCurr, &bidon, currentZone, currentGribData);
      vDirectCap = directCap (isoList [k], pDest);
      directCog = ((int)(vDirectCap / par.cogStep)) * par.cogStep;
      for (int cog = (directCog - par.rangeCog); cog < (directCog + par.rangeCog + 1); cog+=par.cogStep) {
      //for (int cog = -80; cog < 80; cog+=par.cogStep) {
         twa = (cog > twd) ? cog - twd : cog - twd + 360;   //angle of the boat with the wind
         sog = estimateSog (twa, tws, polMat);              //speed of the boat considering twa and wind speed (tws) in polar
	      // if (sog > 7) printf ("bUilN sog %lf twa %lf twd %lf\n", sog, twa, tws);
         newPt.amure = (cog > twd) ? BABORD : TRIBORD;
         sog = sog * coeffWaves (twa, w, wavePolMat);
         if (newPt.amure != isoList [k].amure) {            // changement amure
            if ((fabs (twa) < 90)  || (twa > 270)) penalty = par.penalty0;    // virement de bord
            else penalty = par.penalty1;                    // empannage Jibe
         }
         else penalty = 0;
         dLat = sog * (dt - penalty) * cos (DEG_TO_RAD * cog);            // nautical miles in N S direction
         dLon = sog * (dt - penalty) * sin (DEG_TO_RAD * cog) / cos (DEG_TO_RAD * isoList [k].lat);  // nautical miles in E W direction
         dLat += MS_TO_KN * vCurr * dt;                   // correction vor current
         dLon += MS_TO_KN * uCurr * dt / cos (DEG_TO_RAD * isoList [k].lat);
 
	      // printf ("builN dLat %lf dLon %lf\n", dLat, dLon);
         newPt.lat = isoList [k].lat + dLat/60;
         newPt.lon = isoList [k].lon + dLon/60;
         newPt.id = pId;
         newPt.father = isoList [k].id;
         if (extIsSea (newPt.lon, newPt.lat) && isInZone (newPt, zone)) {
            if (lenNewL < MAX_SIZE_ISOC)
               newList [lenNewL++] = newPt;                      // new point added to the isochrone
            else {
               fprintf (stderr, "Routing Error in buildNextIsochrone, limit of MAX_SIZE_ISOC: %d\n", MAX_SIZE_ISOC);
               return -1;
            }
            pId += 1;
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
   return -1;
}
   
/*! write in CSV file Isochrones */
bool dumpAllIsoc (char *fileName) {
   FILE *f;
   Pp pt;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "Routing Error in dumpAllIsoc, cannot write isoc: %s\n", fileName);
      return false;
   }
   fprintf (f, "n;     Lat;   Lon;      Id; Father;  Amure\n");
   for (int i = 0; i < nIsoc; i++) {
      for (int k = 0; k < sizeIsoc [i]; k++) {
         pt = isocArray [i][k];
         fprintf (f, "%02d; %06.2f; %06.2f; %6d; %6d; %6d\n", i, pt.lat, pt.lon, pt.id, pt.father, pt.amure);
      }
   }
   fprintf (f, "\n");
   fclose (f);
   return true;
}

/*! write in CSV file routes */
bool dumpRoute (char *fileName, Pp dest) {
   FILE *f;
   int i = nIsoc;
   int iFather;
   int dep = (dest.id == 0) ? nIsoc : nIsoc - 1;
   Pp pt = dest;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "Routing Error in dumpRoute, Cannot write route: %s\n", fileName);
      return false;
   }
   fprintf (f, "%4d; %06.2f; %06.2f; %4d; %4d\n", dep, pt.lat, pt.lon, pt.id, pt.father);
      
   for (i = dep - 1; i >= 0; i--) {
      iFather = findFather (pt.father, isocArray [i], sizeIsoc [i]);
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
   route.destinationReached = (pDest.id == 0);
   route.totDist = 0;
   int k;
   int iFather;
   int dep = (pDest.id == 0) ? nIsoc : nIsoc - 1;
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
      iFather = findFather (pt.father, isocArray [i], sizeIsoc [i]);
      pt = isocArray [i][iFather];
      k = i+1;
      route.t [k].lat = pt.lat;
      route.t [k].lon = pt.lon;
      route.t [k].id = pt.id;
      route.t [k].father = pt.father;
      route.t [k].cap = directCap (pt, ptLast);
      route.t [k].d  = loxoDist (pt, ptLast);
      route.t [k].dOrtho = orthoDist (pt.lat, pt.lon, pDest.lat, pDest.lon);;
      route.totDist += route.t [k].d;
      ptLast = pt;
   }
   route.t [0].lat = par.pOr.lat;
   route.t [0].lon = par.pOr.lon;
   route.t [0].id = par.pOr.id;
   route.t [0].father = par.pOr.father;
   route.t [0].cap = directCap (par.pOr, ptLast);
   route.t [0].d  = loxoDist (par.pOr, ptLast);
   route.t [0].dOrtho = orthoDist (par.pOr.lat, par.pOr.lon, pDest.lat, pDest.lon);
   route.totDist += route.t[0].d;
   route.n = dep + 2;
   route.duration = par.tStep * nIsoc + lastStepDuration; // hours
}

/*! copy route in a string */
void routeToStr (Route route, char *str) {
   char line [MAX_SIZE_LINE];
   sprintf (str, " No           Lon      Lat     Id  Father     Cap    Dist      Ortho\n");
   sprintf (line, " pOr:     %7.2f° %7.2f° %6d %6d %7.2f° %7.2f    %7.2f\n", \
      route.t[0].lon, route.t[0].lat, route.t[0].id, route.t[0].father, \
      route.t[0].cap, route.t[0].d, route.t[0].dOrtho); 
   strcat (str, line);
   for (int i = 1; i < route.n-1; i++) {
      sprintf (line, " Isoc %2d: %7.2f° %7.2f° %6d %6d %7.2f° %7.2f    %7.2f\n", \
            i-1, route.t[i].lon, route.t[i].lat, route.t[i].id, route.t[i].father, \
            route.t[i].cap, route.t[i].d, route.t[i].dOrtho); 
      strcat (str, line);
   }
   if (route.destinationReached)
      sprintf (line, " Dest:    %7.2f° %7.2f° %6d %6d \n", route.t[route.n-1].lon, route.t[route.n-1].lat,\
         route.t[route.n-1].id, route.t[route.n-1].father);
   else 
      sprintf (line, " Isoc %2d: %7.2f° %7.2f° %6d %6d \n", nIsoc -1, route.t[route.n-1].lon, route.t[route.n-1].lat,\
         route.t[route.n-1].id, route.t[route.n-1].father);
   strcat (str, line);
   sprintf (line, " Total distance: %.2f NM", route.totDist);
   strcat (str, line);
}

/*! return true if pDest can be reached from pFrom in less time than dt
   bestTime give the time to get from pFrom to pDest */
static inline bool goalP (Pp pFrom, Pp pDest, double t, double dt, double *timeTo, double *distance) {
   double u, v, w, twd, tws, twa, sog, uCurr, vCurr, bidon;
   double coeffLat = cos (DEG_TO_RAD * (pFrom.lat + pDest.lat)/2);
   double dLat = pDest.lat - pFrom.lat;
   double dLon = pDest.lon - pFrom.lon;
   double cog = RAD_TO_DEG * atan2 (dLon * coeffLat, dLat);
   double penalty;
   *distance = orthoDist (pDest.lat, pDest.lon, pFrom.lat, pFrom.lon);
   //double *distance = 60 * dist (dLat, dLon * coeffLat);
   if (par.constWindTws != 0) {
      twd = par.constWindTwd;
      tws = par.constWindTws;
      w= 0;
   }
   else {
      findFlow (pFrom, t, &u, &v, &w, zone, gribData);
      twd = fTwd (u, v);
      tws = fTws (u, v);
   }

   if (par.constCurrentS != 0) {
      uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
      vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
   }
   else findFlow (pFrom, t - tDeltaCurrent, &uCurr, &vCurr, &bidon, currentZone, currentGribData);
   twa = (cog > twd) ? cog - twd : cog - twd + 360;      // angle of the boat with the wind
   sog = estimateSog (twa, tws, polMat);
   sog = sog * coeffWaves (twa, w, wavePolMat);
   *timeTo = *distance/sog;
   //if ((sog * dt) > distance) printf ("goal sog %lf dt %lf distance %lf\n", sog, dt, distance);
   if (pDest.amure != pFrom.amure) {                  // changement amure
      if (fabs (twa) < 90) penalty = par.penalty0;    // virement de bord
      else penalty = par.penalty1;                    // empannage Jibe
   }
   else penalty = 0;
   return ((sog * (dt - penalty)) > *distance);
}

/*! true if goal can be reached directly in dt from isochrone */
/* side effect : pDest.father can be modified ! */
/* if not reached it will contain index of closest point to destination */
static inline bool goal (int nIsoc, Isoc isoList, int len, double t, double dt, int *i, double *lastStepDuration) {
   double bestTime = DBL_MAX;
   double time, distance;
   bool destinationReached = false;
   bool res;
   tDistance [nIsoc] = DBL_MAX; 
   for (int k = 0; k < len; k++) {
      res = goalP (isoList [k], par.pDest, t, dt, &time, &distance);
      // printf ("k, time: %d %lf\n",  k, time);
      if (res) {
         destinationReached = true;
         }
      if (time < bestTime) {
         if (res) par.pDest.father = isoList [k].id; // ATTENTION !!!!
         bestTime = time;
         *i = k;
      }
      if (distance < tDistance [nIsoc]) {
         tDistance [nIsoc] = distance;
      }
   }
   *lastStepDuration = bestTime;
   return destinationReached;
}

/*! return closest point to pDest in Isoc */ 
static inline Pp closest (Isoc isoc, int n, Pp pDest) {
   Pp best;
   double d;
   lastClosestDist = DBL_MAX;
   for (int i = 0; i < n; i++) {
	   d = loxoDist (pDest, isoc [i]);
      if (d < lastClosestDist) {
         lastClosestDist = d;
         best = isoc [i];
      }
   }
   return best;
}

/*! find optimal routing from p0 to pDest using grib file and polar
    return number of step to reach pDest, NIL if unreached, -1 if problem */
int routing (Pp pOr, Pp pDest, double t, double dt, int *indexBest, double *lastStepDuration) {
   Isoc tempList;
   double distance;
   double timeToReach = 0;
   int lTempList = 0;
   double timeLastStep;
   pOrTtoPDestCog = directCap (par.pOr, par.pDest);
   pOrTtoPDestDist = loxoDist (par.pOr, par.pDest);
   
   lastClosestDist = pOrTtoPDestDist;
   tDeltaCurrent = zoneTimeDiff (currentZone, zone); // global variable
   nIsoc = 0;
   pId = 1;
   route.n = 0;
   route.destinationReached = false;
   for (int i = 0; i < MAX_N_ISOC; i++) {
      sizeIsoc [i] = 0;
      tDistance [i] = DBL_MAX;
   }
   tempList [0] = pOr; // liste avec un unique élément;
   
   if (goalP (pOr, pDest, t, dt, &timeToReach, &distance)) {
      par.pDest.father = pOr.id;
      *lastStepDuration = timeToReach;
      return 1;
   }
   sizeIsoc [0] = buildNextIsochrone (tempList, 1, pDest, t, dt, isocArray [0]);
   firstInIsoc [0] = 0; 
   lastClosest = closest (isocArray [0], sizeIsoc [0], pDest);
   // printf ("%-20s%d, %d\n", "Isochrone no, len: ", 0, sizeIsoc [0]);
   // printIsochrone (pOr, isocArray [0], sizeIsoc [0]); 
   nIsoc = 1;
   while (t <= (zone.timeStamp [zone.nTimeStamp-1] + par.tStep) && (nIsoc < par.maxIso)) {
      t += dt;
      if (goal (nIsoc-1, isocArray [nIsoc -1], sizeIsoc [nIsoc - 1], t, dt, indexBest, &timeLastStep)) {
         sizeIsoc [nIsoc] = optimize (par.opt, tempList, lTempList, isocArray [nIsoc]);
         firstInIsoc [nIsoc] = findFirst (nIsoc);; 
         *lastStepDuration = timeLastStep;
         return nIsoc + 1;
      }
      lTempList = buildNextIsochrone (isocArray [nIsoc -1], sizeIsoc [nIsoc -1], pDest, t, dt, tempList);
      if (lTempList == -1) return -1;
      // printf ("%-20s%d", "Biglist length: ", lTempList);
      sizeIsoc [nIsoc] = optimize (par.opt, tempList, lTempList, isocArray [nIsoc]);
      firstInIsoc [nIsoc] = findFirst (nIsoc);; 
      lastClosest = closest (isocArray [nIsoc], sizeIsoc [nIsoc], pDest);
      // printf ("%-20s%d\n", " ; Opt length: ", sizeIsoc [nIsoc]);

      // printIsochrone (pOr, isocArray [nIsoc], sizeIsoc [nIsoc]); 
      // printf ("%-20s%d, %d\n", "Isochrone no, len: ", nIsoc, sizeIsoc [nIsoc]);
      nIsoc += 1;
   }
   *lastStepDuration = 0.0;
   return NIL;
}
