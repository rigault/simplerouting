/*! Virtual Regatta Dahboard utilities
   compilation: gcc -c dashboardVR.c `pkg-config --cflags glib-2.0` 
:*/
#include <glib.h>
#include <float.h>   
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include "rtypes.h"
#include "rutil.h"

/*! copy src to dest with and padding */
static void strncpyPad (char *dest, const char *src, size_t n) {
   size_t srcLen = strlen (src);
   size_t  copyLen = (srcLen < n) ? srcLen : n;
   memcpy (dest, src, copyLen);
   if (copyLen < n) { // padding
      memset (dest + copyLen, ' ', n - copyLen);
   }
   dest [n] = '\0';
}

/*! format one CSV line */
static void formatLine (bool first, char **tokens, char *line, size_t maxLen, const size_t elemSize[], size_t elemSizeLen) {
   char elem [MAX_SIZE_NAME];
   line [0] = '\0';
   for (size_t i = 1; i < g_strv_length (tokens) && i < elemSizeLen; i += 1) {
      if (elemSize  [i] != 0) {
         char *noAccents = (i == 1) ? g_str_to_ascii (tokens [i], NULL) : g_strdup (tokens [i]); // replace accents for column name
         int dec = (first && (i == 10)) ? -2 : 0;  // to take into account unicode degrees in column 10...
         strncpyPad (elem, noAccents, elemSize [i] + dec);
         g_free (noAccents);
         g_strlcat (line, elem, maxLen);
         g_strlcat (line, " ", maxLen);
      }
   }
   g_strlcat (line, "\n", maxLen);
}

/*! display virtual Regatta dashboard file */
bool dashboardFormat (const char *filename, CompetitorsList *competitors, char *report, size_t maxLen, char *footer, size_t maxLenFooter) {
   char line [MAX_SIZE_LINE];
   char reportLine [MAX_SIZE_TEXT] = "";
   int nLine = 0;
   const size_t elemSize [] = {0, 12, 9, 8, 10, 10, 0, 5, 6, 12, 31, 7, 8, 8, 8, 7, 7, 10, 10};
   const size_t elemSizeLen = sizeof (elemSize) / sizeof (size_t);
   FILE *file = fopen (filename, "r");

   if (file == NULL) {
      snprintf (report, maxLen, "In dashboardFormat, could not open file: %s\n", filename);
      fprintf (stderr, "%s", report);
      return false;
   }
   report [0] = '\0';
   while (fgets (line, sizeof (line), file)) {
      // Parse the CSV line
      nLine += 1;
      gchar **tokens = g_strsplit (line, ";", -1);
      if (!tokens || g_strv_length (tokens) < 11) {
         if ((nLine < 4) && ((strstr (tokens [0], "Name") != NULL) || strstr (tokens [0], "Export Date") != NULL)) {
            snprintf (reportLine, sizeof (reportLine), "%s: %s    ", g_strstrip (tokens [0]), g_strstrip (tokens [1]));
            g_strlcat (footer, reportLine, maxLenFooter);
         }
         g_strfreev(tokens);
         continue; // Skip invalid lines
      }
      if (nLine == 5) {
         formatLine (true, tokens, reportLine, sizeof (reportLine), elemSize, elemSizeLen);
         g_strlcat (report, reportLine, maxLen);
      }
      // Extract fields (adjust indexes based on your CSV format)
      const char *name = g_strstrip (tokens [1]);     // second column as name

      // Check if the name exists in the competitors list
      for (int i = 0; i < competitors->n; i++) {
         if (g_strcmp0 (name, competitors->t[i].name) == 0) {
            formatLine (false, tokens, reportLine, sizeof (reportLine), elemSize, elemSizeLen);
            g_strlcat (report, reportLine, maxLen);
            break;
         }
      }
      g_strfreev (tokens);
   }
   fclose (file);
   return true;
}

/*! Dashboard Virtual Regatta Import */
bool dashboardImportParam (const char *filename, CompetitorsList *competitors, char *report, size_t maxLen) {
   double lat, lon;
   char line [MAX_SIZE_TEXT];
   char reportLine [MAX_SIZE_LINE];
   int nLine = 0;

   // Open the CSV file
   FILE *file = fopen (filename, "r");
   if (file == NULL) {
      snprintf (report, maxLen, "In dashboardImportParam, could not open file: %s\n", filename);
      fprintf (stderr, "%s", report);
      return false;
   }
   g_strlcpy (report, "\n", maxLen);
   while (fgets (line, sizeof (line), file)) {
      // Parse the CSV line
      nLine += 1;
      gchar **tokens = g_strsplit (line, ";", -1);
      if (!tokens || g_strv_length (tokens) < 11) {
         if ((nLine < 4) && ((strstr (tokens [0], "Name") != NULL) || strstr (tokens [0], "Export Date") != NULL)) {
            snprintf (reportLine, sizeof (reportLine), "%s: %s\n", g_strstrip (tokens [0]), g_strstrip (tokens [1]));
            g_strlcat (report, reportLine, maxLen);
         }
         if (nLine == 4) 
            g_strlcat (report, "\nCompetitors:\n", maxLen);
         g_strfreev(tokens);
         continue; // Skip invalid lines
      }

      const char *name = g_strstrip (tokens [1]);     // second column as name
      const char *strCoord = g_strstrip(tokens [10]); // coordinates lat lon

      if (! analyseCoord (strCoord, &lat, &lon))
         continue;

      // Check if the name exists in the competitors list
      for (int i = 0; i < competitors->n; i++) {
         if (g_strcmp0 (name, competitors->t[i].name) == 0) {
            competitors->t[i].lat = lat;
            competitors->t[i].lon = lon;
            snprintf (reportLine, sizeof (reportLine), "%s\n", name);
            g_strlcat (report, reportLine, maxLen);
            break;
         }
      }
      g_strfreev (tokens);
   }
   fclose (file);
   return true;
}

