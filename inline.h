/*! this file contains small inlines functions
 to be included in s source files */

/*! true if P (lat, lon) is within the zone */
static inline bool isInZone (double lat, double lon, Zone *zone) {
   return (lat >= zone->latMin) && (lat <= zone->latMax) && (lon >= zone->lonLeft) && (lon <= zone->lonRight);
}

/*! return lon on ]-180, 180 ] interval */
static inline double lonCanonize (double lon) {
   lon = fmod (lon, 360);
   if (lon > 180) lon -= 360;
   else if (lon <= -180) lon += 360;
   return lon;
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
   //if (twa < 0) twa += 360; 
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

/*! direct loxodromic cap from origin to dest */
static inline double directCap (double lat1, double lon1, double lat2, double lon2) {
   return RAD_TO_DEG * atan2 ((lon2 - lon1) * cos (DEG_TO_RAD * (lat1+lat2)/2), lat2 - lat1);
}

/*! direct loxodromic cap from origin to dest */
static inline double orthoCap (double lat1, double lon1, double lat2, double lon2) {
   return directCap (lat1, lon1, lat2, lon2) + givry (lat1, lon1, lat2, lon2);
}

/*! direct loxodromic dist from origi to dest */
static inline double loxoDist (double lat1, double lon1, double lat2, double lon2) {
   double coeffLat = cos (DEG_TO_RAD * (lat1 + lat2)/2);
   double dLat = lat2 - lat1;
   double dLon = lon2 - lon1;
   return 60 * sqrt (dLat * dLat + dLon *dLon) * coeffLat;
}

/*! return orthodomic distance in nautical miles */
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

