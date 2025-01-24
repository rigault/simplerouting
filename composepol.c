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
#include "polar.h"
#define MAX_POLAR_FILES 10
#define OUTPUT_RES "VRrespol.csv"
#define OUTPUT_SAIL "VRrespol.sailpol"

 
int sailID [MAX_N_SAIL] = {1, 2, 3, 4, 5, 6, 7, 8};
int sailCount [MAX_N_SAIL];

PolMat polMatTab [MAX_POLAR_FILES], resMat, sailMat;
char buffer [MAX_SIZE_BUFFER];

/*! write polar information to console as pure csv file */
static void polPrint (const PolMat *mat) {
   for (int i = 0; i < mat->nLine; i++) {
      for (int j = 0; j < mat->nCol; j++) {
         printf ("%6.2f; ", mat->t [i][j]);
      }
      printf ("\n");
   }
}

/*! write polar information to console as pure csv file */
static void polWrite (const char *fileName, const PolMat *mat) {
   FILE *f;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr,  "In poiWrite, Unable to open %s\n", fileName);
      return;
   }
   
   for (int i = 0; i < mat->nLine; i++) {
      for (int j = 0; j < mat->nCol; j++) {
         if ((i == 0) || (j == 0))
            fprintf (f, "%6.0f; ", mat->t [i][j]);
         else
            fprintf (f, "%6.2f; ", mat->t [i][j]);
      }
      fprintf (f, "\n");
   }
   fclose (f);
}

/*! check and analyse  polar information  */
static void analyse (const PolMat *mat) {
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

/*! mix matrix n  matrix */
static void compose (int nPol) {
   sailPolMat.t [0][0] = resMat.t [0][0] = -1;  // first cell
   int nLine = polMatTab [0].nLine;
   int nCol = polMatTab [0].nCol;
   sailMat.nLine = resMat.nLine = nLine;
   sailMat.nCol = resMat.nCol = nCol;

   for (int c = 0; c < nCol; c += 1) { // first line
      sailMat.t [0][c] = resMat.t [0][c] = polMatTab [0].t[0][c];
   }
   for (int l = 1; l < nLine; l += 1) { // first column
      sailMat.t [l][0] = resMat.t [l][0] = polMatTab [0].t[l][0];
   }
   for (int lig = 1; lig < nLine; lig += 1) {
      for (int col = 1; col < nCol; col += 1) {
         double v = polMatTab [nPol].t[lig][col];
         if (v > resMat.t [lig][col]) {
            resMat.t [lig][col] = v;
            sailMat.t [lig][col] = sailID [nPol];
         }
      }
   }
}

/* check that input polar are consistent (nLine, nCol, twa, tws) */
static bool checkConsistency (int nPol) {
   int nLine = polMatTab [0].nLine;
   int nCol = polMatTab [0].nCol;
   printf ("checkConsistency nPol: %d\n", nPol);
   if (polMatTab [nPol].nLine != nLine) {
      fprintf (stderr, "In checkConsistency,  Polar: %d, Number of line: %d Incorrect\n", nPol, polMatTab [nPol].nLine);  
      return false;
   }
   if (polMatTab [nPol].nCol != nCol) {
      fprintf (stderr, "In checkConsistency,  Polar: %d, Number of Col: %d Incorrect\n", nPol, polMatTab [nPol].nCol);  
      return false;
   }
   for (int c = 1; c < nCol; c += 1) { // check line 0 is the same
      if (polMatTab [nPol].t[0][c] != polMatTab [0].t[0][c]) {
         fprintf (stderr, "In checkConsistency,  Polar: %d, Incorrect at col %d\n", nPol, c);  
         return false;
      }
   }
   for (int l = 1; l < nLine; l += 1) { // check column 0 is the same
      if (polMatTab [nPol].t[l][0] != polMatTab [0].t[l][0]) {
         fprintf (stderr, "In checkConsistency,  Polar: %d, Incorrect at line %d\n", nPol, l);  
         return false;
      }
   }
   return true;
}

/*! check after composition the sail Matrix */
static void countSail (const PolMat *mat) {
   for (int lig = 1; lig < mat->nLine; lig += 1) {
      for (int col = 1; col < mat->nCol; col += 1) {
         int v = (int) mat->t [lig][col];
         if (v >= 0 && v < MAX_N_SAIL) 
            sailCount [v] += 1;
         else
            fprintf (stderr, "In countSail, Error stange value: %d\n", v);
      }
   }
}

/* write small sail report */
static void reportSail () {
   char str [MAX_SIZE_NAME];
   int total = 0;
   printf ("\nnline except 0: %d, nCol except 0: %d, total cell: %d\n",
      sailMat.nLine -1, sailMat.nCol -1, (sailMat.nLine - 1) * (sailMat.nCol - 1)); 
   printf ("\nindex  Count Name\n");
   for (int i = 0; i < MAX_N_SAIL; i++) {
      printf ("%6d %5d %s\n", i, sailCount [i], fSailName (i, str, sizeof (str)));
      total += sailCount [i];
   }
   printf ("Total: %5d\n", total);
}

int main (int argc, char *argv []) {
   bool verbose = false;
   char errMessage [MAX_SIZE_TEXT];
   int deb = 1;
   int nPol = 0;
   if (argc <= 2) {
      fprintf (stderr, "In main, Synopsys: %s -c <file0> <file1> <file2>...\n", argv [0]);
      exit (EXIT_FAILURE);
   }  
   if (argc == 3 && strcmp (argv [1], "-a") == 0) {
      if (! readPolar (false, argv [2], &polMatTab [0], errMessage, sizeof (errMessage))) {
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

   if (strcmp (argv [1], "-c") != 0) {
      fprintf (stderr, "In main, Synopsys: %s -c <file0> <file1> <file2>...\n", argv [0]);
      exit (EXIT_FAILURE);
   }
   deb += 1;

   if ((argc - deb) >= MAX_POLAR_FILES) {
      fprintf (stderr, "In main, Number of polar files exceed limit: %d\n", MAX_POLAR_FILES);
      exit (EXIT_FAILURE);
   }
   for (int i = deb; i < argc; i++) {
      nPol = i - deb ;
      // printf ("Analyse Polar; %d\n", nPol);
      if (! readPolar (false, argv [nPol + 2], &polMatTab [nPol], errMessage, sizeof (errMessage))) {
         fprintf (stderr, "In main, Impossible to read: %s: %s\n", argv [i], errMessage);
         exit (EXIT_FAILURE);
      }
      if (verbose)
         printf ("Manage: %s\n", argv [i]);
      polToStr (&polMatTab [nPol], buffer, MAX_SIZE_BUFFER);
      if (verbose) printf ("%s\n", buffer);
      if (checkConsistency (nPol));
      compose (nPol);
   }
   if (verbose) {
      polToStr (&resMat, buffer, MAX_SIZE_BUFFER);
      printf ("Result:\n%s\n", buffer);
   }
   countSail (&sailMat);
   reportSail ();
   //printf ("Resulting Polar:\n");
   //polPrint (&resMat);
   polWrite (OUTPUT_RES, &resMat);
   //printf ("Resulting SailPolar:\n");
   polWrite (OUTPUT_SAIL, &sailMat);
   printf ("resulting polar and sail polar names: %s %s\n", OUTPUT_RES, OUTPUT_SAIL);

   exit (EXIT_SUCCESS);
} 

