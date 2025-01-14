/*! this file contains small inlines functions
 to be included in source files */

#include <math.h>

/*! say if point is in sea */
static inline bool isSea (char * isSeaArray, double lat, double lon) {
   if (isSeaArray == NULL) return true;
   int iLon = round (lon * 10  + 1800);
   int iLat = round (-lat * 10  + 900);
   return isSeaArray [(iLat * 3601) + iLon];
}

/*! return lon on ]-180, 180 ] interval */
static inline double lonCanonize (double lon) {
   lon = fmod (lon, 360);
   if (lon > 180) lon -= 360;
   else if (lon <= -180) lon += 360;
   return lon;
} 

/*! if antemeridian -180 < lon < 360. Normal case : -180 < lon <= 180 */
static inline double lonNormalize (double lon, bool anteMeridian) {
   lonCanonize (lon);
   if (anteMeridian && lon < 0)
      lon += 360;
   return lon;
}

/*! true if P (lat, lon) is within the zone */
static inline bool isInZone (double lat, double lon, Zone *zone) {
   return (lat >= zone->latMin) && (lat <= zone->latMax) && (lon >= zone->lonLeft) && (lon <= zone->lonRight);
}

/*! true wind direction */
static inline double fTwd (double u, double v) {
	double val = 180 + RAD_TO_DEG * atan2 (u, v);
   return (val > 180) ? val - 360 : val;
}

/*! true wind speed. cf Pythagore */
static inline double fTws (double u, double v) {
    return MS_TO_KN * sqrt ((u * u) + (v * v));
}

/*! return TWA between -180 and 180
   note : tribord amure if twa < 0 */
static inline double fTwa (double heading, double twd) {
   double val =  fmod ((twd - heading), 360.0);
   return (val > 180) ? val - 360 : (val < -180) ? val + 360 : val;
}

/*! return AWA and AWS Apparent Wind Angle and Speed */
static inline void fAwaAws (double twa, double tws, double sog, double *awa, double *aws) {
   double a = tws * sin (DEG_TO_RAD * twa);
   double b = tws * cos (DEG_TO_RAD * twa) + sog;
   *awa = RAD_TO_DEG * atan2 (a, b);
   *aws = sqrt (a*a + b*b);
}

/*! return fx : linear interpolation */
static inline double interpolate (double x, double x0, double x1, double fx0, double fx1) {
   if (x1 == x0) return fx0;
   else return fx0 + (x-x0) * (fx1-fx0) / (x1-x0);
}

/*! return givry correction to apply to direct or loxodromic cap to get orthodromic cap  */
static inline double givry (double lat1, double lon1, double lat2, double lon2) {
   double theta = lon1 - lon2;
   double latM = (lat1 + lat2) / 2;
   return (theta/2) * sin (latM * DEG_TO_RAD);
}

/*! return loxodromic cap from origin to destination */
static inline double directCap (double lat1, double lon1, double lat2, double lon2) {
   return RAD_TO_DEG * atan2 ((lon2 - lon1) * cos (DEG_TO_RAD * (lat1+lat2)/2), lat2 - lat1);
}

/*! return initial orthodromic cap from origin to destination */
static inline double orthoCap (double lat1, double lon1, double lat2, double lon2) {
   return directCap (lat1, lon1, lat2, lon2) + givry (lat1, lon1, lat2, lon2);
}

/*! return loxodromic distance in nautical miles from origin to destination */
static inline double loxoDist (double lat1, double lon1, double lat2, double lon2) {
    // Convert degrees to radians
    double lat1_rad = DEG_TO_RAD * lat1;
    double lon1_rad = DEG_TO_RAD * lon1;
    double lat2_rad = DEG_TO_RAD * lat2;
    double lon2_rad = DEG_TO_RAD * lon2;

    // Difference in longitude
    double delta_lon = lon2_rad - lon1_rad;

    // Calculate the change in latitude
    double delta_lat = lat2_rad - lat1_rad;

    // Calculate the mean latitude
    double mean_lat = (lat1_rad + lat2_rad) / 2.0;

    // Calculate the rhumb line distance
    double q = delta_lat / log(tan(G_PI / 4 + lat2_rad / 2) / tan(G_PI / 4 + lat1_rad / 2));
    
    // Correct for delta_lat being very small
    if (isnan(q)) {
        q = cos(mean_lat);
    }

    // Distance formula
    return sqrt(delta_lat * delta_lat + (q * delta_lon) * (q * delta_lon)) * EARTH_RADIUS;
}


/*! return orthodomic distance in nautical miles from origin to destination*/
static inline double orthoDist (double lat1, double lon1, double lat2, double lon2) {
    double theta = lon1 - lon2;
    double distRad = acos(
        sin(lat1 * DEG_TO_RAD) * sin(lat2 * DEG_TO_RAD) + 
        cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * cos(theta * DEG_TO_RAD)
    );
    return fabs (60 * RAD_TO_DEG * distRad);
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

/*! find in polar boat closest int value without interpolation */
static inline int closestInPolar (double twa, double w, PolMat mat) {
   int l, c, lInf, cInf, lSup, cSup;
   if (mat.nLine == 0 || mat.nCol == 0)
      return 0;
   if (twa > 180) twa = 360 - twa;
   else if (twa < 0) twa = -twa;
   for (l = 1; l < mat.nLine; l++) 
      if (mat.t [l][0] > twa) break;
   lSup = (l < mat.nLine - 1) ? l : mat.nLine - 1;
   lInf = (l == 1) ? 1 : l - 1;
   l =  (fabs (twa - lInf) < fabs (twa - lSup)) ? lInf : lSup;

   for (c = 1; c < mat.nCol; c++) 
      if (mat.t [0][c] > w) break;
   cSup = (c < mat.nCol - 1) ? c : mat.nCol - 1;
   cInf = (c == 1) ? 1 : c - 1;
   c =  (fabs (w - cInf) < fabs (twa - cSup)) ? cInf : cSup;

   return mat.t [l][c];
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



