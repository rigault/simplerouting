/*! compilation: gcc -c polar.c `pkg-config --cflags glib-2.0` */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <locale.h>
#include "rtypes.h"
#include "inline.h"
#include "rutil.h"

/* str to double accepting both . or , as decimal separator */
static bool strtodNew (const char *str, double *v) {
   if (str == NULL)
      return false;
   if (!isNumber (str))
      return false;
   char *mutableStr = g_strdup (str);
   g_strdelimit (mutableStr, ",", '.');

   char *endptr;
   *v = strtod (mutableStr, &endptr);
   if (endptr == mutableStr)
      return false;
   g_free (mutableStr);
   
   return true;
}

/*! Check polar and return false and a report if something to say */
static bool polarCheck (PolMat *mat, char *report, size_t maxLen) {
   double maxInRow, maxInCol;
   int cMax, rowMax;
   report [0] ='\0';
   for (int c = 1; c < mat->nCol; c += 1) {  // check values in line 0 progress
      if (mat->t [0][c] < mat->t[0][c-1]) {
         snprintf (report, maxLen, "Report: values in row 0 should progress, col: %d\n", c);
      }
   }
   for (int row = 1; row < mat->nLine; row += 1) {
      if ((mat->t [row][0] < mat->t[row-1] [0])) {
         snprintf (report, maxLen, "Report: values in col 0 should progress, row: %d\n", row);
      }
   }
   for (int row = 1; row < mat->nLine; row += 1) {
      maxInRow = -1.0;
      cMax = -1;
      for (int c = 1; c < mat->nCol; c += 1) { // find maxInRow
         if (mat->t[row][c] > maxInRow) {
            maxInRow = mat->t[row][c];
            cMax = c;
         }
      }
      for (int c = 2; c <= cMax; c += 1) {
         if (mat->t [row][c] < mat->t[row][c-1]) {
            snprintf (report, maxLen, "Report: values in row: %d should progress at col: %d up to maxInRow: %.2lf\n", row, c, maxInRow);
         }
      }
      for (int c = cMax + 1; c < mat->nCol; c += 1) {
         if ((mat->t [row][c] > mat->t[row][c-1])) {
            snprintf (report, maxLen, "Report: values in row: %d should regress at col: %d after maxInRow: %.2lf\n", row, c, maxInRow);
         }
      }
   }

   for (int c = 1; c < mat->nCol; c += 1) {
      maxInCol = -1.0;
      rowMax = -1;
      for (int row = 1; row < mat->nLine; row += 1) { // find maxInRow
         if (mat->t[row][c] > maxInCol) {
            maxInCol = mat->t[row][c];
            rowMax = row;
         }
      }
      for (int row = 2; row <= rowMax; row += 1) {
         if (mat->t [row][c] < mat->t[row-1][c]) {
            snprintf (report, maxLen, "Report: values in col: %d should progress at row: %d up to maxInCol: %.2lf\n", c, row, maxInCol);
         }
      }
      for (int row = rowMax + 1; row < mat->nLine; row += 1) {
         if ((mat->t [row][c] > mat->t[row-1][c])) {
            snprintf (report, maxLen, "Report: values in col: %d should regress at row: %d after maxInCol: %.2lf\n", c, row, maxInCol);
         }
      }
   }
   return (report [0] == '\0'); // True if no error found
}

/*! read polar file and fill poLMat matrix 
   if check then polarCheck */
bool readPolar (bool check, const char *fileName, PolMat *mat, char *errMessage, size_t maxLen) {
   FILE *f = NULL;
   char buffer [MAX_SIZE_TEXT];
   char *pLine = &buffer [0];
   char *strToken;
   double v = 0;
   int c;

   errMessage [0] = '\0';
   mat->nLine = 0;
   mat->nCol = 0;
   if ((f = fopen (fileName, "r")) == NULL) {
      snprintf (errMessage, maxLen,  "Error in readPolar: cannot open: %s\n", fileName);
      return false;
   }
   while (fgets (pLine, MAX_SIZE_TEXT, f) != NULL) {
      c = 0;
      if (pLine [0] == '#') continue;                       // ignore if comment
      if (strpbrk (pLine, CSV_SEP_POLAR) == NULL) continue; // ignore if no separator
         
      strToken = strtok (pLine, CSV_SEP_POLAR);
      while ((strToken != NULL) && (c < MAX_N_POL_MAT_COLS)) {
         if (strtodNew (strToken, &v)) {
            mat->t [mat->nLine][c] = v;
            c += 1;
         }
         if ((mat->nLine == 0) && (c == 0))
            c += 1;
         strToken = strtok (NULL, CSV_SEP_POLAR);
      }
      if (c <= 2) continue; // line is not validated if not at least 2 columns
      if (c >= MAX_N_POL_MAT_COLS) {
         snprintf (errMessage, maxLen, "Error in readPolar: max number of colums: %d\n", MAX_N_POL_MAT_COLS);
         fclose (f);
         return false;
      }
      mat->nLine += 1;
      
      if (mat->nLine >= MAX_N_POL_MAT_LINES) {
         snprintf (errMessage, maxLen, "Error in readPolar: max number of line: %d\n", MAX_N_POL_MAT_LINES);
         fclose (f);
         return false;
      }
      if (mat->nLine == 1) mat->nCol = c; // max
   }
   mat->t [0][0] = -1;
   fclose (f);
   if (check)
      polarCheck (mat, errMessage, maxLen);
   return true;
}

/*! return the max value found in polar */
double maxValInPol (const PolMat *mat) {
   double max = 0.0;
   for (int i = 1; i < mat->nLine; i++)
      for (int j = 1; j < mat->nCol; j++) 
         if (mat->t [i][j] > max) max = mat->t [i][j];
   return max;
}

/*! return VMG: angle and speed at TWS : pres */
void bestVmg (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed) {
   *vmgSpeed = -1;
   double vmg;
   for (int i = 1; i < mat->nLine; i++) {
      if (mat->t [i][0] > 90) break; // useless over 90°
      vmg = findPolar (mat->t [i][0], tws, *mat) * cos (DEG_TO_RAD * mat->t [i][0]);
      if (vmg > *vmgSpeed) {
         *vmgSpeed = vmg;
         *vmgAngle = mat->t [i][0];
      }
   }
}

/*! return VMG back: angle and speed at TWS : vent arriere */
void bestVmgBack (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed) {
   *vmgSpeed = -1;
   double vmg;
   for (int i = 1; i < mat->nLine; i++) {
      if (mat->t [i][0] < 90) continue; // useless under  90° for Vmg Back
      vmg = fabs (findPolar (mat->t [i][0], tws, *mat) * cos (DEG_TO_RAD * mat->t [i][0]));
      if (vmg > *vmgSpeed) {
         *vmgSpeed = vmg;
         *vmgAngle = mat->t [i][0];
      }
   }
}

/*! write polar information in string */
char *polToStr (const PolMat *mat, char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE] = "";
   str [0] = '\0';
   for (int i = 0; i < mat->nLine; i++) {
      for (int j = 0; j < mat->nCol; j++) {
         snprintf (line, MAX_SIZE_LINE, "%6.2f ", mat->t [i][j]);
         g_strlcat (str, line, maxLen);
      }
      g_strlcat (str, "\n", maxLen);
   }
   snprintf (line, MAX_SIZE_LINE, "Number of rows in polar : %d\n", mat->nCol);
   g_strlcat (str, line, maxLen);
   snprintf (line, MAX_SIZE_LINE, "Number of lines in polar: %d\n", mat->nLine);
   g_strlcat (str, line, maxLen);
   snprintf (line, MAX_SIZE_LINE, "Max                     : %.2lf\n", maxValInPol (mat));
   g_strlcat (str, line, maxLen);
   return str;
}

/*! write polar information in GString */
GString *polToJson (const char *fileName, const char *objName) {
   char polarName [MAX_SIZE_FILE_NAME];
   char errMessage [MAX_SIZE_LINE];
   PolMat mat;
   GString *jString = g_string_new ("");
   buildRootName (fileName, polarName, sizeof (polarName));

   if (!readPolar (false, polarName, &mat, errMessage, sizeof (errMessage))) {
      fprintf (stderr, "In polToJson Error: %s\n", errMessage);
      g_string_append_printf (jString, "{}\n");
      return jString;
   }
   if ((mat.nLine < 2) || (mat.nCol < 2)) {
      fprintf (stderr, "In polToJson Error: no value in: %s\n", polarName);
      g_string_append_printf (jString, "{}\n");
      return jString;
   }
   g_string_append_printf (jString, "{\"%s\": \"%s\", \"nLine\": %d, \"nCol\":%d, \"max\":%.2lf, \"array\":\n[\n", 
                           objName, polarName, mat.nLine, mat.nCol, maxValInPol (&mat));

   for (int i = 0; i < mat.nLine ; i++) {
      g_string_append_printf (jString, "[");
      for (int j = 0; j < mat.nCol - 1; j++) {
         g_string_append_printf (jString, "%.4f, ", mat.t [i][j]);
      }
      g_string_append_printf (jString, "%.4f]%s\n", mat.t [i][mat.nCol -1], (i < mat.nLine - 1) ? "," : "");
   }
   g_string_append_printf (jString, "]}\n");
   return jString;
}

/*! write legend about sail in Gstring */
GString *sailLegendToJson (const char *sailName [], const char *colorStr [], size_t len) {
   GString *jString = g_string_new ("{\"legend\": [");
   for (size_t i = 0; i < len; i += 1) {
      g_string_append_printf (jString, "[\"%s\", \"%s\"]%s", sailName [i], colorStr [i], (i < len -1) ? "," : "");
   }
   g_string_append_printf (jString, "]}\n");
   return jString;
}

