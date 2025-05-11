extern void    bestVmg (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed);
extern void    bestVmgBack (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed);
extern double  maxValInPol (const PolMat *mat);
extern bool    readPolar (bool check, const char *fileName, PolMat *mat, char *errMessage, size_t maxLen);
extern char    *polToStr (const PolMat *mat, char *str, size_t maxLen);
extern GString *polToJson (const char *fileName, const char *objName);
extern GString *sailLegendToJson (const char *sailName [], size_t len);


