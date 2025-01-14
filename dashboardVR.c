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
    //snprintf (dest, n + 1, "%s;", src); 
    g_strlcpy (dest, src, n + 1);
    size_t len = strlen (dest);
    memset (dest + len, ' ', n - len);
    dest[n] = '\0';
}


/*! decode tm time based on date & time found in VR dashboard 
   date format: 01_04_2025_210 last field 210 ignored because ambiguous (2 october or 21:00 ?)
   time format: 12:48:00 */
struct tm getTmTime (const char *strDate, const char *strTime) {
   struct tm tm0 = {0};
   gchar **dateTokens = g_strsplit (strDate, "_", -1);
   if (!dateTokens || g_strv_length(dateTokens) < 3) {
      g_strfreev(dateTokens);
      return tm0;
   }

   gchar **timeTokens = g_strsplit(strTime, ":", -1);
   if (!timeTokens || g_strv_length(timeTokens) < 3) {
      g_strfreev(dateTokens);
      g_strfreev(timeTokens);
      return tm0;
   }

   tm0.tm_year = strtol (dateTokens [2], NULL, 10) - 1900;
   tm0.tm_mon = strtol (dateTokens [0], NULL, 10) - 1;
   tm0.tm_mday = strtol (dateTokens [1], NULL, 10);
   tm0.tm_hour = strtol (timeTokens [0], NULL, 10);
   tm0.tm_min   = strtol (timeTokens [1], NULL, 10);
   tm0.tm_sec   = strtol (timeTokens [2], NULL, 10);

   if (!par.dashboardUTC)
      tm0.tm_sec -= offsetLocalUTC () ;            // to get UTC time if not already UTC
   mktime (&tm0);                                  // adjust with seconds modified
   return tm0;
}

/*! format one CSV line */
static void formatLine (bool first, char **tokens, const char *strTime, char *line, size_t maxLen, 
                        const size_t elemSize[], size_t elemSizeLen) {
   char elem [MAX_SIZE_NAME];
   line [0] = '\0';
   int dec = 0;
   char *newToken;
   for (size_t i = 1; i < g_strv_length (tokens) && i < elemSizeLen; i += 1) {
      dec = 0;
      if (elemSize  [i] == 0) continue; // ignore column
      switch (i) {
      case 1: // name
         newToken = g_str_to_ascii (tokens [i], NULL); // suppress accents
         break;
      case 2: // date & time
         newToken = g_strdup (strTime);
         break;
      case 10: 
         newToken = g_strdup (tokens [i]);
         if (first) dec = -2;
         break;
      default:
         newToken = g_strdup (tokens [i]);
      }
      strncpyPad (elem, newToken, elemSize [i] + dec);
      g_free (newToken);
      g_strlcat (line, elem, maxLen);
      g_strlcat (line, " ", maxLen);
   }
   g_strstrip (line);
   if (strlen (line) > 0)
      g_strlcat (line, "\n", maxLen);
}

/*! Import virtual Regatta dashboard file
   return: tm last competitor update time conveted in UTC
   competitors: updated with lat, lon found in dashboard file
   report: text
   footer: with metadata
*/
struct tm dashboardImportParam (const char *filename, CompetitorsList *competitors, 
                                char *report, size_t maxLen, char *footer, size_t maxLenFooter) {
   double lat, lon;
   char line [MAX_SIZE_LINE];
   char reportLine [MAX_SIZE_TEXT] = "";
   int nLine = 0;
   const size_t elemSize [] = {0, 12, 9, 8, 10, 10, 0, 5, 0, 0, 31, 7, 8, 8, 8, 7, 7, 10, 10}; // 0 means not shown
   const size_t elemSizeLen = sizeof (elemSize) / sizeof (size_t);
   FILE *file = fopen (filename, "r");
   struct tm tm0 = {0}; 
   char strDate [MAX_SIZE_NAME], strTime [MAX_SIZE_NAME];

   if (file == NULL) {
      snprintf (report, maxLen, "In dashboardFormat, could not open file: %s\n", filename);
      fprintf (stderr, "%s", report);
      return tm0;
   }
   report [0] = '\0';
   while (fgets (line, sizeof (line), file)) {
      // Parse the CSV line
      nLine += 1;
      gchar **tokens = g_strsplit (line, ";", -1);
      if (! tokens)
         continue;
      switch (nLine) {
      case 1: 
         if ((g_strv_length (tokens) > 1) && (strstr (tokens [0], "Name") != NULL))
            snprintf (footer, maxLenFooter, "%s   ", g_strstrip (tokens [1]));
         break;
      case 3:
         if ((g_strv_length (tokens) > 1) &&  (strstr (tokens [0], "Export Date") != NULL))
            g_strlcpy (strDate, tokens [1], sizeof (strDate));
         break;
      case 5:
         if ((g_strv_length (tokens) > 11)) {
            formatLine (true, tokens, "UTC", reportLine, sizeof (reportLine), elemSize, elemSizeLen);
            g_strlcat (report, reportLine, maxLen);
         }
         break;
      default:
         if ((g_strv_length (tokens) > 11)) {
            g_strstrip (tokens [1]);                        // second column as name
            if (! analyseCoord (tokens [10], &lat, &lon)) {  // 11th column with lat, lon
               g_strfreev (tokens);
               break;
            }
            // Check if the name exists in the competitors list
            for (int i = 0; i < competitors->n; i++) {
               if (g_strcmp0 (tokens [1], competitors->t[i].name) == 0) {
                  competitors->t[i].lat = lat;
                  competitors->t[i].lon = lon;
                  tm0 = getTmTime (strDate, tokens [2]); // find tm time based on date and time in column 2
                  snprintf (strTime, sizeof (strTime), "%02d:%02d:%02d", tm0.tm_hour, tm0.tm_min, tm0.tm_sec);
                  formatLine (false, tokens, strTime, reportLine, sizeof (reportLine), elemSize, elemSizeLen);
                  g_strlcat (report, reportLine, maxLen);
                  break;
               }
            }
         }
      }
      g_strfreev (tokens);
   }
   fclose (file);
   snprintf (strDate, sizeof (strDate), "%4d/%02d/%02d %02d:%02d:%02d UTC",
      tm0.tm_year + 1900, tm0.tm_mon + 1, tm0.tm_mday, tm0.tm_hour, tm0.tm_min, tm0.tm_sec);

   g_strlcat (footer, strDate, maxLenFooter);
   
   return tm0; 
}

