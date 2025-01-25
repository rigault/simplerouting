#include <gtk/gtk.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#define STAMINA_ID "com.stamina"  
#define MAX_SIZE_NAME 50
#define MAX_N_SHIP_TYPE 2
#define MAX_STAMINA_MANOEUVRE 3
#define MAX_TWS_STAMINA 30
#define MAX_ENERGY_STAMINA 100

struct {
   char name [MAX_SIZE_NAME];
   double cShip;  
   double tMin [3];
   double tMax [3];
} shipParam [MAX_N_SHIP_TYPE] =
{
   "Imoca", 1.2, {300, 300, 420}, {660, 660, 600},
   "Normal", 1.0, {300, 300, 336}, {660, 660, 480} 
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

static double fTimeToRecupOnePoint (double tws) {
   const double minTws = 0.0;
   const double maxTws = 30.0;
   const double timeToRecupLow = 5;   // minutes
   const double timeToRecupHigh = 15; // minutes
   double fTws = 1.0 - cos (G_PI * (fmin (tws, 30.0)/30.0));
   return 60 * (timeToRecupLow + fTws * (timeToRecupHigh - timeToRecupLow) / 2.0);
   //double fTws = (timeToRecupHigh - timeToRecupLow) / (maxTws - minTws);
   //return 60 * (timeToRecupLow + (fmin (tws, maxTws) - minTws) * fTws);
}

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

/*! Callback fullpack or not  */
static void onCheckBoxToggled (GtkWidget *checkbox, gpointer user_data) {
   gboolean *flag = (gboolean *) user_data;
   *flag = gtk_check_button_get_active ((GtkCheckButton *) checkbox);
   calculation ();
}

static void cbDropDownShip (GObject *dropDown, GParamSpec *pspec, gpointer user_data) {
   shipData.index =  gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   calculation ();
}

static void onTwsValueChanged (GtkScale *scale, gpointer data) {
   GtkWidget *label = GTK_WIDGET (data);
   shipData.tws =  round (gtk_range_get_value (GTK_RANGE (scale)));
   char *strInfo = g_strdup_printf ("%02.0lf Kn", shipData.tws);
   gtk_label_set_text (GTK_LABEL(label), strInfo);
   calculation ();
   g_free (strInfo);
}

static void onEnergyValueChanged (GtkScale *scale, gpointer data) {
   GtkWidget *label = GTK_WIDGET (data);
   shipData.energy = round (gtk_range_get_value (GTK_RANGE (scale)));
   char *strInfo = g_strdup_printf ("%02.0lf ", shipData.energy);
   gtk_label_set_text (GTK_LABEL(label), strInfo);
   calculation ();
   g_free (strInfo);
}

static void appActivate (GApplication *application) {
   GtkWidget *label;
   GtkWidget *staminaWindow = gtk_application_window_new (GTK_APPLICATION (application));
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

int main () {
   int ret;
   GtkApplication *app = gtk_application_new (STAMINA_ID, G_APPLICATION_DEFAULT_FLAGS);
   g_signal_connect (app, "activate", G_CALLBACK (appActivate), NULL);
   ret = g_application_run (G_APPLICATION (app), 0, NULL);
   g_object_unref (app);
   return ret;
}

