extern void   bestVmg (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed);
extern void   bestVmgBack (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed);
extern double maxValInPol (const PolMat *mat);
extern char   *polToStr (const PolMat *mat, char *str, size_t maxLen);
extern bool   readPolar (const char *fileName, PolMat *mat, char *errMessage, size_t maxLen);

