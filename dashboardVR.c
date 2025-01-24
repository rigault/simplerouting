/*! Virtual Regatta Dahboard utilities
   compilation: gcc -c dashboardVR.c `pkg-config --cflags glib-2.0` 
:*/
#include <gtk/gtk.h>
#include <float.h>   
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include "rtypes.h"
#include "rutil.h"

#define MAX_N_SHIP_TYPE          2
#define MAX_STAMINA_MANOEUVRE    3
#define MAX_TWS_STAMINA          30
#define MAX_ENERGY_STAMINA       100
#define MAX_COL_VR_DASHBOARD     20
#define MIN_COL_VR_DASHBOARD     11

struct {
   char name [MAX_SIZE_NAME];
   double cShip;  
   double tMin [3];
   double tMax [3];
} shipParam [MAX_N_SHIP_TYPE] = {
   {"Imoca", 1.2, {300, 300, 420}, {660, 660, 600}}, 
   {"Normal", 1.0, {300, 300, 336}, {660, 660, 480}}
};

struct {
   int index;
   double tws;
   double energy;
   bool fullPack;
   GtkWidget *wPenalty [MAX_STAMINA_MANOEUVRE];
   GtkWidget *wEnergyCoeff;
   GtkWidget *wLoss [MAX_STAMINA_MANOEUVRE];
   GtkWidget *wRecup;
} shipData;

/*! return penalty in seconds for manoeuvre tyopr. Depend on tws and energy. Give also sTamina coefficient */
static double fPenalty (int shipIndex, int type, double tws, double energy, double *cStamina) {
   if (type < 0 || type > 2) {
      fprintf (stderr, "In fPenalty, type unknown: %d\n", type);
      return -1;
   };
   const double kPenalty = 0.015;
   double cShip = shipParam [shipIndex].cShip;
   *cStamina = 2 - fmin (energy, 100.0) * kPenalty;
   double tMin = shipParam [shipIndex].tMin [type];
   double tMax = shipParam [shipIndex].tMax [type];
   double fTws = 50.0 - 50.0 * cos (G_PI * ((fmax (10.0, fmin (tws, 30.0))-10.0)/(30.0 - 10.0)));
   //printf ("cShip: %.2lf, cStamina: %.2lf, tMin: %.2lf, tMax: %.2lf, fTws: %.2lf\n", cShip, cStamina, tMin, tMax, fTws);
   return  cShip * (*cStamina) * (tMin + fTws * (tMax - tMin) / 100.0);
}

/*! return point loss with manoeuvre types. Depends on tws and fullPack */
static double fPointLoss (int shipIndex, int type, double tws, bool fullPack) {
   const double fPCoeff = (type == 2 && fullPack) ? 0.8 : 1;
   double loss = (type == 2) ? 0.2 : 0.1;
   double cShip = shipParam [shipIndex].cShip;
   double fTws = (tws <= 10.0) ? 0.02 * tws + 1 : 
                 (tws <= 20.0) ? 0.03 * tws + 0.9 : 
                 (tws <= 30) ? 0.05 * tws + 0.5 :  
                 2.0;
   return fPCoeff * loss * cShip * fTws;
}

/*! return type in second to get bacl on energy point */
static double fTimeToRecupOnePoint (double tws) {
   const double timeToRecupLow = 5;   // minutes
   const double timeToRecupHigh = 15; // minutes
   double fTws = 1.0 - cos (G_PI * (fmin (tws, 30.0)/30.0));
   return 60 * (timeToRecupLow + fTws * (timeToRecupHigh - timeToRecupLow) / 2.0);
}

/*! make calculation (penalty, point moss, time to recover for every type of manoeuvre */
static void calculation () {
   double cStamina;
   for (int i = 0; i < MAX_STAMINA_MANOEUVRE; i += 1) {
      double penalty = fPenalty (shipData.index, i, shipData.tws, shipData.energy, &cStamina);
      char*strInfo = g_strdup_printf ("%04.0lf s",  penalty);
      gtk_label_set_text (GTK_LABEL (shipData.wPenalty [i]), strInfo);

      double loss = fPointLoss (shipData.index, i, shipData.tws, shipData.fullPack);
      strInfo = g_strdup_printf ("%3.0lf",  100 * loss);
      gtk_label_set_text (GTK_LABEL (shipData.wLoss [i]), strInfo);

      g_free (strInfo);
   }
   double recup = fTimeToRecupOnePoint (shipData.tws);
   char *strInfo = g_strdup_printf ("%02d mn %02d s", (int) (recup / 60.0), (int) fmod (recup, 60.0));
   gtk_label_set_text (GTK_LABEL (shipData.wRecup), strInfo);
   g_free (strInfo);

   strInfo = g_strdup_printf ("(x %4.2lf)", cStamina);
   gtk_label_set_text (GTK_LABEL (shipData.wEnergyCoeff), strInfo);
   g_free (strInfo);
}

/*! Callback fullpack or not */
static void onCheckBoxToggled (GtkWidget *checkbox, gpointer user_data) {
   gboolean *flag = (gboolean *) user_data;
   *flag = gtk_check_button_get_active ((GtkCheckButton *) checkbox);
   calculation ();
}

/*! Callback Ship index selection */
static void cbDropDownShip (GObject *dropDown) {
   shipData.index =  gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   calculation ();
}

/*! Callback TWS change */
static void onTwsValueChanged (GtkScale *scale, gpointer data) {
   GtkWidget *label = GTK_WIDGET (data);
   shipData.tws =  round (gtk_range_get_value (GTK_RANGE (scale)));
   char *strInfo = g_strdup_printf ("%02.0lf Kn", shipData.tws);
   gtk_label_set_text (GTK_LABEL(label), strInfo);
   calculation ();
   g_free (strInfo);
}

/*! Callback Energy change */
static void onEnergyValueChanged (GtkScale *scale, gpointer data) {
   GtkWidget *label = GTK_WIDGET (data);
   shipData.energy = round (gtk_range_get_value (GTK_RANGE (scale)));
   char *strInfo = g_strdup_printf ("%02.0lf ", shipData.energy);
   gtk_label_set_text (GTK_LABEL(label), strInfo);
   calculation ();
   g_free (strInfo);
}

/*! Staina calculator based on Virtual Regatta model */
void staminaCalculator (GtkApplication *application) {
   GtkWidget *label;
   GtkWidget *staminaWindow = gtk_application_window_new (application);
   gtk_window_set_title (GTK_WINDOW (staminaWindow), "Stamina Calculator");
   gtk_widget_set_size_request (staminaWindow, 500, -1);
   
   GtkWidget *grid = gtk_grid_new();   

   gtk_window_set_child (GTK_WINDOW (staminaWindow), grid);

   gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
   gtk_grid_set_row_spacing (GTK_GRID (grid), 5);
   gtk_widget_set_margin_start (grid, 10);
   gtk_widget_set_margin_top (grid, 10);
   
   // Create combo box for ship Selection
   label = gtk_label_new ("Ship");
   gtk_widget_set_halign (label, GTK_ALIGN_START);
   gtk_grid_attach (GTK_GRID(grid), label, 0, 0, 1, 1);
   const char *arrayShip [] = {"Imoca", "Normal", NULL};
   GtkWidget *shipDropDown = gtk_drop_down_new_from_strings (arrayShip);
   gtk_drop_down_set_selected ((GtkDropDown *) shipDropDown, 0);
   gtk_grid_attach (GTK_GRID (grid), shipDropDown, 1, 0, 1, 1);
   g_signal_connect (shipDropDown, "notify::selected", G_CALLBACK (cbDropDownShip), NULL);

   // checkbox: "full pack"
   GtkWidget *checkboxFP = gtk_check_button_new_with_label ("FP");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxFP, shipData.fullPack);
   gtk_grid_attach (GTK_GRID (grid), checkboxFP,  3, 0, 1, 1);
   g_signal_connect (G_OBJECT (checkboxFP), "toggled", G_CALLBACK (onCheckBoxToggled), &shipData.fullPack);

   // GtkScale widget creation for tws
   label = gtk_label_new ("Tws");
   gtk_widget_set_halign (label, GTK_ALIGN_START);
   gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);
   GtkWidget *twsScale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, MAX_TWS_STAMINA, 1);
   gtk_widget_set_size_request (twsScale, 100, -1);  // Adjust GtkScale size
   gtk_grid_attach (GTK_GRID(grid), twsScale, 1, 1, 2, 1);
   GtkWidget *twsInfo = gtk_label_new ("0");
   gtk_grid_attach (GTK_GRID (grid), twsInfo, 3, 1, 1, 1);
   g_signal_connect (twsScale, "value-changed", G_CALLBACK (onTwsValueChanged), twsInfo);

   // GtkScale widget creation for Energy
   label = gtk_label_new ("Energy");
   gtk_widget_set_halign (label, GTK_ALIGN_START);
   gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);
   GtkWidget *energyScale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, MAX_ENERGY_STAMINA, 1);
   gtk_widget_set_size_request (energyScale, 150, -1);  // Adjust GtkScale size
   gtk_grid_attach (GTK_GRID(grid), energyScale, 1, 2, 2, 1);
   GtkWidget *energyInfo = gtk_label_new ("");
   gtk_grid_attach (GTK_GRID (grid), energyInfo, 3, 2, 1, 1);
   shipData.wEnergyCoeff = gtk_label_new ("");
   gtk_grid_attach (GTK_GRID (grid), shipData.wEnergyCoeff, 4, 2, 1, 1);
   g_signal_connect (energyScale, "value-changed", G_CALLBACK (onEnergyValueChanged), energyInfo);

   gtk_grid_attach (GTK_GRID (grid),  gtk_label_new ("Tack"), 1, 3, 1, 1);
   gtk_grid_attach (GTK_GRID (grid),  gtk_label_new ("Gybe"), 2, 3, 1, 1);
   gtk_grid_attach (GTK_GRID (grid),  gtk_label_new ("Sail"), 3, 3, 1, 1);

   GtkWidget *separator0 = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach (GTK_GRID (grid),  separator0, 0, 4, 5, 1);

   // result for time manoeuvre
   label = gtk_label_new ("Time To Manoeuvre");
   gtk_widget_set_halign (label, GTK_ALIGN_START);
   gtk_grid_attach (GTK_GRID (grid),  label , 0, 5, 1, 1);
   for (int i = 0; i < MAX_STAMINA_MANOEUVRE; i += 1) {
      shipData.wPenalty [i] = gtk_label_new ("");
      gtk_grid_attach (GTK_GRID (grid), shipData.wPenalty [i], i+1, 5, 1, 1);
   }

   GtkWidget *separator1 = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach (GTK_GRID (grid),  separator1, 0, 6, 5, 1);

   label = gtk_label_new ("Energy Points lossed");
   gtk_widget_set_halign (label, GTK_ALIGN_START);
   gtk_grid_attach (GTK_GRID (grid),  label , 0, 7, 1, 1);
   
   for (int i = 0; i < MAX_STAMINA_MANOEUVRE; i += 1) {
      shipData.wLoss [i] = gtk_label_new ("0");
      gtk_grid_attach (GTK_GRID (grid), shipData.wLoss [i], i+1, 7, 1, 1);
   }
   GtkWidget *separator2 = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach (GTK_GRID (grid),  separator2,     0, 8, 5, 1);

   label = gtk_label_new ("Time to recover one point");
   gtk_widget_set_halign (label, GTK_ALIGN_START);
   gtk_grid_attach (GTK_GRID (grid),  label , 0, 9, 1, 1);
   shipData.wRecup = gtk_label_new ("0");
   gtk_grid_attach (GTK_GRID (grid),  shipData.wRecup, 1, 9, 1, 1);

   label =  gtk_label_new (""); // for space at bottom
   gtk_grid_attach (GTK_GRID (grid),  label, 0, 10, 1, 1);

   onTwsValueChanged (GTK_SCALE (twsScale), twsInfo); 
   onEnergyValueChanged (GTK_SCALE (energyScale), energyInfo);
   calculation ();
   gtk_window_present (GTK_WINDOW (staminaWindow));
}

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
struct tm dashboardImportParam (const char *fileName, CompetitorsList *competitors, 
                                char *report, size_t maxLen, char *footer, size_t maxLenFooter) {
   double lat, lon;
   char line [MAX_SIZE_LINE];
   char reportLine [MAX_SIZE_TEXT] = "";
   int nLine = 0;
   const size_t elemSize [] = {0, 12, 9, 8, 10, 10, 0, 5, 0, 0, 31, 7, 8, 8, 8, 7, 7, 10, 10}; // 0 means not shown
   const size_t elemSizeLen = sizeof (elemSize) / sizeof (size_t);
   FILE *file = fopen (fileName, "r");
   struct tm tm0 = {0}; 
   char strDate [MAX_SIZE_NAME], strTime [MAX_SIZE_NAME];
   int nCol = 0;

   if (file == NULL) {
      snprintf (report, maxLen, "In dashboardFormat, could not open file: %s\n", fileName);
      fprintf (stderr, "%s", report);
      return tm0;
   }
   report [0] = '\0';
   while (fgets (line, sizeof (line), file)) {
      // Parse the CSV line
      nLine += 1;
      gchar **tokens = g_strsplit (line, ";", -1);
      nCol = g_strv_length (tokens);
      if (! tokens || (nCol > MAX_COL_VR_DASHBOARD))
         continue;

      switch (nLine) {
      case 1: 
         if ((nCol > 1) && (strstr (tokens [0], "Name") != NULL))
            snprintf (footer, maxLenFooter, "%s   ", g_strstrip (tokens [1]));
         break;
      case 3:
         if ((nCol > 1) &&  (strstr (tokens [0], "Export Date") != NULL))
            g_strlcpy (strDate, tokens [1], sizeof (strDate));
         break;
      case 5:
         if ((nCol > MIN_COL_VR_DASHBOARD)) {
            formatLine (true, tokens, "UTC", reportLine, sizeof (reportLine), elemSize, elemSizeLen);
            g_strlcat (report, reportLine, maxLen);
         }
         break;
      default:
         if ((nCol > MIN_COL_VR_DASHBOARD)) {
            g_strstrip (tokens [1]);                           // second column as name
            if (! analyseCoord (tokens [10], &lat, &lon)) {    // 11th column with lat, lon
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

