
/*! \mainpage Routing for sail software
 * \brief small routing software written in c language
 * \author Rene Rigault
 * \version 0.1
 * \date 2024 
 * \file
 * \section Compilation
 * see ccr file
 * \section Documentation
 * \li Doc produced by doxygen
 * \li see also html help file
 * \li Verified with: cppcheck 
 * \section Description
 * \li calculate best route using Isochrone method 
 * \li use polar boat (wind, waves) and grib files (wind, current)
 * \li tws = True Wind Speed
 * \li twd = True Wind Direction
 * \li twa = True Wind Angle - the angle of the boat to the wind
 * \li aws = Apparent Wind Speed - the apparent speed of the wind
 * \li awa = Appartent Wind Angle - the apparent angle of the boat to the wind
 * \li sog = Speed over Ground of the boat
 * \li cog = Course over Ground  of the boat
 * \section Usage
 * \li ./routing [-<option>] [<parameterFile>] */

#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include "rtypes.h"
#include "rutil.h"
#include "option.h"
#include "gpsutil.h"

char   parameterFileName [MAX_SIZE_FILE_NAME];

/*! make initializations and call opionManage () */
int main (int argc, char *argv[]) {
   bool ret = true;

   if (setlocale (LC_ALL, "C") == NULL) {              // very important for scanf decimal numbers
      fprintf (stderr, "Error in main: setlocale failed");
      return EXIT_FAILURE;
   }

   strncpy (parameterFileName, PARAMETERS_FILE, sizeof (parameterFileName));
   switch (argc) {
      case 1: // no parameter
         optionManage ('h'); // help
         break;
      case 2: // one parameter
         if (argv [1][0] == '-') {
            ret = readParam (parameterFileName);
            optionManage (argv [1][1]);
            exit (EXIT_SUCCESS);
         }
         else { 
            ret = readParam (argv [1]);
            strncpy (parameterFileName, argv [1], sizeof (parameterFileName) - 1);
         }
         break;
      case 3: // two parameters
         if (argv [1][0] == '-') {
            ret = readParam (argv [2]);
            strncpy (parameterFileName, argv [2], sizeof (parameterFileName) - 1);
            optionManage (argv [1][1]);
            exit (EXIT_SUCCESS);
         }
         else {
            printf ("Usage: %s [-<option>] [<par file>]\n", argv [0]);
            exit (EXIT_FAILURE);
         }
         break;
      default:
         printf ("Usage: %s [-<option>] [<par file>]\n", argv [0]);
         exit  (EXIT_FAILURE);
         break;
   }
   if (!ret) exit (EXIT_FAILURE);
}

