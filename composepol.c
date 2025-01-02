#include <glib.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rtypes.h"
#include "inline.h"
#include "rutil.h"
#include "option.h"
#define  MAX_POLAR_FILES 10
#define  STEP_WIND  5
#define  STEP_ANGLE 5
#define  MAX_WIND  60
 
PolMat polMatTab [MAX_POLAR_FILES], resMat;
char buffer [MAX_SIZE_BUFFER];

/*! write polar information to console as pure csv file */
char *polPrint (const PolMat *mat) {
   for (int i = 0; i < mat->nLine; i++) {
      for (int j = 0; j < mat->nCol; j++) {
         printf ("%6.2f; ", mat->t [i][j]);
      }
      printf ("\n");
   }
}

/*! check and analyse  polar information  */
char *analyse (const PolMat *mat) {
   double diffH, diffV, diffMax = -1, perc;
   for (int i = 1; i < mat->nLine; i++) {
      diffMax = -1;
      for (int j = 2; j < mat->nCol; j++) {
         diffH = fabs (mat->t [i][j] - mat->t [i] [j - 1]);
         diffMax = MAX (diffMax, diffH);
         perc = diffH / mat->t [i] [j - 1];
      }
      printf ("Row: %2d has diffMax: %5.2lf, Percentage:%5.2lf%%\n", i, diffMax, 100 * perc);
   }
   for (int j = 1; j < mat->nCol; j++) {
      diffMax = -1;
      for (int i = 2; i < mat->nLine; i++) {
         diffV = fabs (mat->t [i][j] - mat->t [i-1] [j]);
         diffMax = MAX (diffMax, diffV); 
         perc = diffV / mat->t [i-1] [j];
      }
      printf ("Column: %2d has diffMax: %5.2lf, Percentage:%5.2lf%%\n", j, diffMax, 100 * perc);
   }
}


/*! produce resulting matrix */
void compose (int nPol) {
   resMat.t [0][0] = -1;  // first cell
   resMat.nLine = 1, resMat.nCol = 1;  

   for (double tws = 0.0; tws <= MAX_WIND; tws += STEP_WIND) { // first line
      resMat.t [0][resMat.nCol] = tws;
      resMat.nCol += 1;
   }
      
   for (double twa = 0.0; twa <= 180.0; twa += STEP_ANGLE) { // first column
      resMat.t [resMat.nLine][0] = twa;
      resMat.nLine += 1;
   }
  
   for (int lig = 1; lig < resMat.nLine; lig += 1) {
      for (int col = 1; col < resMat.nCol; col += 1) {
         for (int iPol = 0; iPol < nPol; iPol += 1) {
            double v = findPolar (resMat.t [lig][0], resMat.t [0][col], polMatTab [iPol]);
            resMat.t [lig][col] = MAX (resMat.t [lig][col], v);
         }
      }
   }
}

int main (int argc, char *argv []) {
   bool verbose = false;
   char errMessage [MAX_SIZE_TEXT];
   int deb = 1;
   if (argc <= 1) {
      fprintf (stderr, "In main, Synopsys: %s <file0> <file1> <file2>...\n", argv [0]);
      exit (EXIT_FAILURE);
   }  
   if (argc == 3 && strcmp (argv [1], "-a") == 0) {
      if (! readPolar (argv [2], &polMatTab [0], errMessage, sizeof (errMessage))) {
         fprintf (stderr, "In main, Impossible to read: %s: %s\n", argv [2], errMessage);
         exit (EXIT_FAILURE);
      }
      analyse (&polMatTab [0]);
      exit (EXIT_SUCCESS);
   }

   if (strcmp (argv [1], "-v") == 0) {
      verbose = true;
      deb += 1;
   }

   if ((argc - deb) >= MAX_POLAR_FILES) {
      fprintf (stderr, "In main, Number of polar files exceed limit: %d\n", MAX_POLAR_FILES);
      exit (EXIT_FAILURE);
   }
   for (int i = deb; i < argc; i++) {
      if (! readPolar (argv [i], &polMatTab [i - deb], errMessage, sizeof (errMessage))) {
         fprintf (stderr, "In main, Impossible to read: %s: %s\n", argv [i], errMessage);
         exit (EXIT_FAILURE);
      }
      if (verbose)
         printf ("Manage: %s\n", argv [i]);
      polToStr (&polMatTab [i - deb], buffer, MAX_SIZE_BUFFER);
      if (verbose) printf ("%s\n", buffer);
      compose (argc - deb);
   }
   if (verbose) {
      polToStr (&resMat, buffer, MAX_SIZE_BUFFER);
      printf ("Result:\n%s\n", buffer);
   }
   polPrint (&resMat);
   exit (EXIT_SUCCESS);
} 
