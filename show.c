/*! \mainpage Routing for sail software
 * \brief small routing software written in c language
 * \author Rene Rigault
 * \version 0.1
 * \date 2024 
* \section Compilation
* see "ccall" file
* \section Documentation
* \li Doc produced by doxygen
* \li see also html help file
* \section Description
* \li tws = True Wind Speed
* \li twd = True Wind Direction
* \li twa = True Wind Angle - the angle of the boat to the wind
* \li sog = Speed over Ground of the boat
* \li cog = Course over Ground  of the boat
* \section Usage
* \li ./show [-<option>] [<parameterFile>] */

#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/time.h>
#include <time.h>
#include <gtk/gtk.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <webkit2/webkit2.h>
#include "shapefil.h"
#include "rtypes.h"
#include "rutil.h"
#include "shputil.h"
#include "engine.h"
#include "gpsutil.h"
#include "curlutil.h"
#include "option.h"

#define BOAT_UNICODE          "⛵"
#define ORTHO_ROUTE_PARAM     20    // for drawing orrhodromic route
#define MAX_TEXT_LENGTH       5     // in polar
#define POLAR_WIDTH           800
#define POLAR_HEIGHT          500
#define DISP_NB_LAT_STEP      10
#define DISP_NB_LON_STEP      10
#define ANIMATION_TEMPO       100
#define GRIB_TIME_OUT         2000
#define READ_GRIB_TIME_OUT    200
#define MIN_MOVE_FOR_SELECT   50 // minimum move to launch smtp grib request after selection
#define MIN_POINT_FOR_BEZIER  10 // minimum number of point to select bezier representation
#define MIN_NAME_SIZE         3  // mimimum poi name length to be considered

GdkRGBA colors [] = {{1.0,0,0,1}, {0,1.0,0,1},  {0,0,1.0,1},{0.5,0.5,0,1},{0,0.5,0.5,1},{0.5,0,0.5,1},{0.2,0.2,0.2,1},{0.4,0.4,0.4,1},{0.8,0,0.2,1},{0.2,0,0.8,1}}; // Colors for Polar curve
int nColors = 10;

#define N_WIND_COLORS 6
guint8 colorPalette [N_WIND_COLORS][3] = {{0,0,255}, {0, 255, 0}, {255, 255, 0}, {255, 153, 0}, {255, 0, 0}, {139, 0, 0}};
// blue, green, yellow, orange, red, blacked red
guint8 bwPalette [N_WIND_COLORS][3] = {{250,250,250}, {200, 200, 200}, {170, 170, 170}, {130, 130, 130}, {70, 70, 70}, {10, 10, 10}};
double tTws [] = {0.0, 15.0, 20.0, 25.0, 30.0, 40.0};

GtkWidget *statusbar;
GtkWidget *window; 
GtkWidget *spinner_window; 
GtkListStore *filter_store;
GtkWidget *filter_combo;
guint context_id;
GtkWidget *polar_drawing_area;      // polar
int selectedPol = 0;               // select polar to draw. 0 = all
GtkWidget *drawing_area;
cairo_t *globalCr;
guint gribMailTimeout;
guint gribReadTimeout;
guint currentGribReadTimeout;
int kTime = 0;
gboolean animation_active = FALSE;
bool destPressed = true;
bool gribRequestRunning = false;
gboolean selecting = FALSE;
int provider = SAILDOCS_GFS;
MyDate vStart;
MyDate *start = &vStart;    
struct tm *timeInfos;               // conversion en struct tm
long theTime;
bool updatedColors = false; 
int polarType = POLAR;
int segmentOrBezier = SEGMENT;
GtkWidget *tab_display;

// Structure pour stocker les coordonnées
typedef struct {
    double x;
    double y;
} Coordinates;

Coordinates whereWasMouse, whereIsMouse;

typedef struct {
   guint xL;
   guint xR;
   guint yB;
   guint yT;
} Disp;

Disp disp;

typedef struct {
   double latMin;
   double latMax;
   double lonLeft;
   double lonRight;
   double latStep;
   double lonStep;
   long nbLat;
   long nbLon;
   int nZoom;
} DispZone;

DispZone dispZone;

typedef struct {
   double lon;
   double lat;
   double od;     // ortho dist
   double oCap;   // ortho cap 
   double ld;     // loxo dist
   double lCap;   // loxo  cap 
} WayPoint;

typedef struct {
   int n;
   double totOrthoDist;
   double totLoxoDist;
   WayPoint t [MAX_N_WAY_POINT];
} WayRoute;

WayRoute wayRoute; // = {0, 0.0, 0.0, {0}};

/*! minimum of two integers */
static inline int min (int a, int b) {
   return (a < b) ? a : b;
}

/* draw spinner when waiting for something */
static void spinner (const char *title) {
    spinner_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(spinner_window), title);
    gtk_window_set_default_size(GTK_WINDOW(spinner_window), 200, 100);

    // Créer un spinner
    GtkWidget *spinner = gtk_spinner_new();
    gtk_container_add(GTK_CONTAINER(spinner_window), spinner);
    gtk_spinner_start(GTK_SPINNER(spinner));

    gtk_widget_show_all(spinner_window);
}

/*! confirmaion box */
static bool confirm (const char *message, char* title)  {  
   GtkWidget *dialogBox;  
   dialogBox = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, \
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message);  
   gtk_window_set_title(GTK_WINDOW(dialogBox), title);  
   switch(gtk_dialog_run(GTK_DIALOG(dialogBox)))  {  
   case GTK_RESPONSE_YES:
      gtk_widget_destroy(dialogBox);
      return true;  
   case GTK_RESPONSE_NO:
      gtk_widget_destroy(dialogBox);
      break;  
   } 
   return false;
}  

/*! draw info message GTK_MESSAGE_INFO, GTK_MESSAGE_WARNING ou GTK_MESSAGE_ERROR */
static void infoMessage (char *message, GtkMessageType typeMessage) {
   GtkWidget *dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, typeMessage, GTK_BUTTONS_OK, "%s", message);
   gtk_dialog_run (GTK_DIALOG(dialog));
   gtk_widget_destroy (dialog);
}


/*! get a password */
static bool mailPassword () {
   GtkWidget *dialog, *content_area;
   GtkWidget *password_entry;
   dialog = gtk_dialog_new_with_buttons("Mail password", NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                                        "_OK", GTK_RESPONSE_ACCEPT,
                                        "_Cancel", GTK_RESPONSE_CANCEL,
                                        NULL);

   content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));
   password_entry = gtk_entry_new();
   gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);  
   gtk_entry_set_invisible_char(GTK_ENTRY(password_entry), '*');
   // Définir la largeur minimale de la boîte de dialogue
   gtk_widget_set_size_request(dialog, 30, -1);
   gtk_box_pack_start (GTK_BOX (content_area), password_entry, FALSE, FALSE, 0);

   // Affichage de la boîte de dialogue et récupération de la réponse
   gtk_widget_show_all (dialog);
   int response = gtk_dialog_run (GTK_DIALOG(dialog));
   
   // Récupération des valeurs saisies si la réponse est GTK_RESPONSE_ACCEPT
   if (response == GTK_RESPONSE_ACCEPT) {
      const char *pt = gtk_entry_get_text (GTK_ENTRY(password_entry));
      strcpy (par.mailPw, pt);
      dollarReplace (par.mailPw);
      gtk_widget_queue_draw(drawing_area); // affiche le tout
   }
   // Fermeture de la boîte de dialogue
   gtk_widget_destroy(dialog);
   return (response == GTK_RESPONSE_ACCEPT);
}

/*! Fonction that applies bold style  */
static void apply_bold_style(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end) {
    GtkTextTag *tag = gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_apply_tag(buffer, tag, start, end);
}

/*! display text with monochrome police */
static void displayText (const char *text, const char *title) {
   GtkWidget *window, *scrolled_window, *text_view;
   GtkTextBuffer *buffer;

   // Création de la fenêtre principale
   window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), title);
   gtk_window_set_default_size(GTK_WINDOW(window), 750, 400);
   gtk_container_set_border_width(GTK_CONTAINER(window), 10);
   g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

   // Création de la zone de défilement
   scrolled_window = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(window), scrolled_window);

   // Création de la zone de texte
   text_view = gtk_text_view_new();
   gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
   // Rendre le texte en lecture seule
   gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
   gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);

   // Obtention du buffer du widget texte
   buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

   // Ajout de la chaîne de caractères au buffer
   gtk_text_buffer_insert_at_cursor(buffer, text, -1);
   
   // Obtenir l'itérateur pour le début et la fin de la première ligne
   GtkTextIter start_iter, end_iter;
   gtk_text_buffer_get_start_iter(buffer, &start_iter);
   gtk_text_buffer_get_iter_at_line(buffer, &end_iter, 1); // Ligne suivante après la première

   // Appliquer le style en gras à la première ligne
   apply_bold_style(buffer, &start_iter, &end_iter);

   // Affichage de la fenêtre
   g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
   gtk_widget_show_all(window);
   gtk_main();
}

/*! display file with monochrome police */
static void displayFile (char* fileName, char * title) {
   FILE *f;
   char line [MAX_SIZE_LINE];
   GtkWidget *window, *scrolled_window, *text_view;
   GtkTextBuffer *buffer;
   // Lecture du contenu du fichier et ajout au buffer
   if ((f = fopen (fileName, "r")) == NULL) {
      infoMessage ("Impossible to open file", GTK_MESSAGE_ERROR);
      return;
   }

   // Création de la fenêtre principale
   window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), title);
   gtk_window_set_default_size(GTK_WINDOW(window), 750, 450);
   gtk_container_set_border_width(GTK_CONTAINER(window), 10);
   g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

   // Création de la zone de défilement
   scrolled_window = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(window), scrolled_window);

   // Création de la zone de texte
   text_view = gtk_text_view_new();
   gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
   gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);

   // Obtention du buffer du widget texte
   buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
   // Lecture du contenu du fichier et ajout au buffer
   while (fgets (line, sizeof(line), f) != NULL) {
      gtk_text_buffer_insert_at_cursor(buffer, line, -1);
   }
   fclose (f);
   
   // Obtenir l'itérateur pour le début et la fin de la première ligne
   GtkTextIter start_iter, end_iter;
   gtk_text_buffer_get_start_iter(buffer, &start_iter);
   gtk_text_buffer_get_iter_at_line(buffer, &end_iter, 1); // Ligne suivante après la première

   // Appliquer le style en gras à la première ligne
   apply_bold_style(buffer, &start_iter, &end_iter);

   // Affichage de la fenêtre
   g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
   gtk_widget_show_all(window);
   gtk_main();
}

/*! init of display zone */
static void initDispZone () {
   double latCenter = (zone.latMin + zone.latMax) / 2;
   double lonCenter = (zone.lonRight + zone.lonLeft) / 2;
   double deltaLat = zone.latMax - latCenter;
   dispZone.latMin = zone.latMin;
   dispZone.latMax = zone.latMax;
   dispZone.lonLeft = lonCenter - deltaLat * 2 / cos (DEG_TO_RAD * latCenter);
   dispZone.lonRight = lonCenter + deltaLat * 2 / cos (DEG_TO_RAD * latCenter);
   dispZone.latStep = fabs ((zone.latMax - zone.latMin)) / DISP_NB_LAT_STEP;
   dispZone.lonStep = fabs ((dispZone.lonLeft - dispZone.lonRight)) / DISP_NB_LON_STEP;
   //printf ("dispZone.latStep, dispZone.lonStep : %lf %lf\n", dispZone.latStep, dispZone.lonStep);
   dispZone.nbLat = DISP_NB_LAT_STEP;
   dispZone.nbLon = DISP_NB_LON_STEP;
   dispZone.nZoom = 0;
}

/*! center DispZone on lon, lat */
static void centerDispZone (double lon, double lat) {
   double oldLatCenter = (zone.latMin + zone.latMax) / 2.0;
   double oldLonCenter = (zone.lonRight + zone.lonLeft) / 2.0; //new
   double deltaLat = zone.latMax - oldLatCenter; // recalcul du deltalat
   double deltaLon = (zone.lonRight - oldLonCenter) / cos (DEG_TO_RAD * lat); // recalcul du deltalon new
   dispZone.latMin = lat - deltaLat;
   dispZone.latMax = lat + deltaLat;
   dispZone.lonLeft = lon - deltaLon;
   dispZone.lonRight = lon + deltaLon;
   dispZone.latStep = fabs ((dispZone.latMax - dispZone.latMin)) / DISP_NB_LAT_STEP;
   dispZone.lonStep = fabs ((dispZone.lonLeft - dispZone.lonRight)) / DISP_NB_LON_STEP;
}

/*! zoom the main window */
static void dispZoom (double z) {
   double latCenter = (dispZone.latMin + dispZone.latMax) / 2.0;
   double lonCenter = (dispZone.lonLeft + dispZone.lonRight) / 2.0;
   double deltaLat = dispZone.latMax - latCenter;
   double deltaLon = dispZone.lonRight - lonCenter;
   dispZone.latMin = latCenter - deltaLat * z;
   dispZone.latMax = latCenter + deltaLat * z;
   dispZone.lonLeft = lonCenter - deltaLon * z;
   dispZone.lonRight = lonCenter + deltaLon * z;
   
   dispZone.latStep *= z;
   dispZone.lonStep *= z;
   
   dispZone.nZoom += (z > 0) ? 1 : -1; 
}

/*! horizontal and vertical translation of main window */
static void dispTranslate (double h, double v) {
   double saveLatMin = dispZone.latMin, saveLatMax = dispZone.latMax;
   double saveLonLeft = dispZone.lonLeft, saveLonRight = dispZone.lonRight;

   dispZone.latMin += h;
   dispZone.latMax += h;
   dispZone.lonLeft += v;
   dispZone.lonRight += v;

   if ((dispZone.latMin < -90) || (dispZone.latMax > 90) || (dispZone.lonLeft < -180) || (dispZone.lonRight > 180)) {
      dispZone.latMin = saveLatMin;
      dispZone.latMax = saveLatMax;
      dispZone.lonLeft = saveLonLeft;
      dispZone.lonRight = saveLonRight;
   }
}   

/*! return x as a function of longitude */
static double getX (double lon, double xLeft, double xRight) {
   double kLon = (xRight - xLeft) / (dispZone.lonRight - dispZone.lonLeft);
   return kLon * (lon - dispZone.lonLeft) + xLeft;
}

/*! return y as a function of latitude */
static double getY (double lat, double yBottom, double yTop) {
   double kLat = (yBottom - yTop) / (dispZone.latMax - dispZone.latMin);
   return kLat * (dispZone.latMax - lat) + yTop;
}

/*! return longitude as a function of x (inverse of getX) */
static double xToLon (double x, double xLeft, double xRight) {
   double kLon = (xRight - xLeft) / (dispZone.lonRight - dispZone.lonLeft);
   xLeft -= 1;
   return dispZone.lonLeft + ((x - xLeft) / kLon);
}

/*! return latitude as a function of y (inverse of getY) */
static double yToLat (double y, double yBottom, double yTop) {
   //yBottom += 119; // ATTENTIOn BUG SI VALEUR + FAIBLE
   //yTop += 119;
   yBottom += 1;
   yTop += 1;
   double kLat = (yBottom - yTop) / (dispZone.latMax - dispZone.latMin);
   //printf ("Klon, y, yBottom, lat: %lf %lf %lf %lf\n", kLat, y, yBottom, (dispZone.latMax - (y - yTop) / kLat)); 
   return dispZone.latMax - ((y - yTop) / kLat); 
}

/*! association of color r g b with wind speed twd */
static void mapColors (double tws, guint8 *r, guint8 *g, guint8 *b) {
   int i;
   double ratio;
   guint8 (*wColors)[3] = (par.showColors == B_W) ? bwPalette : colorPalette;
   for (i = 0; i < N_WIND_COLORS; i++) {
      if (tTws [i] > tws) break;
   }
   if (i <= 0) {
      *r = wColors [0][0]; *g = wColors [0][1]; *b = wColors [0][2];
      return;
   } 
   if (i >= N_WIND_COLORS) {
      *r = wColors [N_WIND_COLORS-1][0]; *g = wColors [N_WIND_COLORS-1][1]; *b = wColors [N_WIND_COLORS-1][2];
      return;
   } 
   ratio = (tws - tTws [i-1]) / (tTws [i] - tTws [i-1]);
   *r = wColors [i-1][0] + ratio * (wColors [i][0] - wColors [i-1][0]);
   *g = wColors [i-1][1] + ratio * (wColors [i][1] - wColors [i-1][1]);
   *b = wColors [i-1][2] + ratio * (wColors [i][2] - wColors [i-1][2]);
   // if (*g != 0) printf ("ratio i r g b: %.2lf %d %d %d %d\n", ratio, i, *r, *g, *b);
}
  
/*! update colors for wind */
static void paintWind (cairo_t *cr, int width, int height) {
   double tws;
   //double u, v, w;
   Pp pt;
   guint8 r, g, b;
   for (int x = 0; x < width; x++) {
      for (int y = 0; y < height; y++) {
         pt.lat = yToLat (y, disp.yB, disp.yT);
         pt.lon = xToLon (x, disp.xL, disp.xR);
         if (extIsInZone (pt, zone)) {
            //findFlow (pt, t, &u, &v, &w, zone, gribData);
            //tws = extTws (u, v);
            tws = findTwsByIt (pt, (min (kTime, zone.nTimeStamp -1)));
            mapColors (tws, &r, &g, &b);
            cairo_set_source_rgba (cr, r/255.0, g/255.0, b/255.0, 0.5);
            cairo_rectangle (cr, x, y, 1, 1);
            cairo_fill (cr);
         }
      }
   }
}

/*! callback pour palette */
static gboolean cb_draw_palette(GtkWidget *widget, cairo_t *cr, gpointer data) {
   int width, height;
   double tws;
   guint8 r, g, b;
   char label [9];
   gtk_widget_get_size_request(widget, &width, &height);

   for (int x = 0; x < width; x++) {
      tws = (double) x * 50.0 / (double) width;
      mapColors (tws, &r, &g, &b);
      cairo_set_source_rgba (cr, r/255.0, g/255.0, b/255.0, 0.5);
      cairo_rectangle(cr, x, 0, 1, height / 2);
      cairo_fill(cr);
   }
   for (tws = 0; tws < 50; tws += 5.0) {
      int x = (int) (tws * (double) width / 50.0);
      cairo_set_source_rgb (cr, 0, 0, 0);
      cairo_move_to (cr, x, height / 2);
      cairo_line_to (cr, x, height);
      cairo_stroke (cr);

      g_snprintf (label, sizeof(label), "%0.2lf", tws);
      cairo_move_to(cr, x + 5, height - 5);
      cairo_show_text(cr, label);
   }
   return FALSE;
}

/*! Palette */
static void paletteDraw (GtkWidget *widget, gpointer data) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "TWS (knots)");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 100);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 800, 100);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(cb_draw_palette), NULL);

    gtk_container_add(GTK_CONTAINER(window), drawing_area);
    gtk_widget_show_all(window);

    gtk_main();
}

/*! make calculation over Ortho route */
static void calculateOrthoRoute () {
   wayRoute.t [0].lCap =  loxCap (par.pOr.lat, par.pOr.lon,  wayRoute.t [0].lat,  wayRoute.t [0].lon);
   wayRoute.t [0].oCap = wayRoute.t [0].lCap + givry (par.pOr.lat, par.pOr.lon, wayRoute.t [0].lat, wayRoute.t [0].lon);
   wayRoute.t [0].ld = loxDist (par.pOr.lat, par.pOr.lon,  wayRoute.t [0].lat,  wayRoute.t [0].lon);
   wayRoute.t [0].od = orthoDist (par.pOr.lat, par.pOr.lon,  wayRoute.t [0].lat,  wayRoute.t [0].lon);
   wayRoute.totLoxoDist = wayRoute.t [0].ld; 
   wayRoute.totOrthoDist = wayRoute.t [0].od; 
   
   for (int i = 0; i <  wayRoute.n; i++) {
      wayRoute.t [i+1].lCap = loxCap (wayRoute.t [i].lat, wayRoute.t [i].lon, wayRoute.t [i+1].lat, wayRoute.t [i+1].lon); 
      wayRoute.t [i+1].oCap = wayRoute.t [i+1].lCap + \
         givry (wayRoute.t [i].lat,  wayRoute.t [i].lon, wayRoute.t [i+1].lat, wayRoute.t [i+1].lon);
      wayRoute.t [i+1].ld = loxDist (wayRoute.t [i].lat, wayRoute.t [i].lon, wayRoute.t [i+1].lat, wayRoute.t [i+1].lon);
      wayRoute.t [i+1].od = orthoDist (wayRoute.t [i].lat, wayRoute.t [i].lon, wayRoute.t [i+1].lat, wayRoute.t [i+1].lon);
      wayRoute.totLoxoDist += wayRoute.t [i+1].ld; 
      wayRoute.totOrthoDist += wayRoute.t [i+1].od; 
   }
}

/*! draw orthoPoints */
static void orthoPoints (cairo_t *cr, double lat1, double lon1, double lat2, double lon2, int n) {
   double lSeg = orthoDist (lat1, lon1, lat2, lon2) / n;
   double lat = lat1;
   double lon = lon1;
   double angle, x1, y1, x2, y2;
   double x = getX (lon1, disp.xL, disp.xR);
   double y = getY (lat1, disp.yB, disp.yT);
   cairo_move_to (cr, x, y);
   cairo_set_source_rgb (cr, 0, 1, 0);
   n = 10;
   
   for (int i = 0; i < n - 2; i ++) {
      angle = loxCap (lat, lon, lat2, lon2) + givry (lat, lon, lat2, lon2);
      lSeg = orthoDist (lat, lon, lat2, lon2) / (n - i);
      lat += lSeg * cos (angle * M_PI/180) / 60;
      lon += (lSeg * sin (angle * M_PI/180) / cos (DEG_TO_RAD * lat)) / 60;
      x = getX (lon, disp.xL, disp.xR);
      y = getY (lat, disp.yB, disp.yT);
      
      angle = loxCap (lat, lon, lat2, lon2) + givry (lat, lon, lat2, lon2);
      lSeg = orthoDist (lat, lon, lat2, lon2) / (n - i - 1);
      lat += lSeg * cos (angle * M_PI/180) / 60;
      lon += (lSeg * sin (angle * M_PI/180) / cos (DEG_TO_RAD * lat)) / 60;
      x1 = getX (lon, disp.xL, disp.xR);
      y1 = getY (lat, disp.yB, disp.yT);

      angle = loxCap (lat, lon, lat2, lon2) + givry (lat, lon, lat2, lon2);
      lSeg = orthoDist (lat, lon, lat2, lon2) / (n - i - 2);
      lat += lSeg * cos (angle * M_PI/180) / 60;
      lon += (lSeg * sin (angle * M_PI/180) / cos (DEG_TO_RAD * lat)) / 60;
      x2 = getX (lon, disp.xL, disp.xR);
      y2 = getY (lat, disp.yB, disp.yT);

      cairo_curve_to(cr, x, y, x1, y1, x2, y2);
   }
   x = getX (lon2, disp.xL, disp.xR);
   y = getY (lat2, disp.yB, disp.yT);
   cairo_line_to (cr, x, y);
   cairo_stroke (cr);
   // printf ("point %d: %.1lf° %.2lf° %.2lf° %.2lf NM\n", i, lat2, lon2, angle, lSeg);
}

/*! dump orthodomic route into str */
static void wayPointToStr (char * str) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   sprintf (str, " Point  Lat        Lon       Ortho cap   Ortho Dist   Loxo Cap   Loxo Dist\n");
   sprintf (line, " pOr:   %-12s%-12s%7.2lf°      %7.2lf   %7.2lf°     %7.2lf \n", \
      latToStr (par.pOr.lat, par.dispDms, strLat), lonToStr (par.pOr.lon, par.dispDms, strLon), \
      wayRoute.t [0].oCap,  wayRoute.t [0].od, wayRoute.t [0].oCap,  wayRoute.t [0].od);
   strcat (str, line);
   for (int i=0; i <  wayRoute.n; i++) {
      sprintf (line, " WP %02d: %-12s%-12s%7.2lf°      %7.2lf   %7.2lf°     %7.2lf \n", i + 1,  \
         latToStr (wayRoute.t[i].lat, par.dispDms, strLat), lonToStr (wayRoute.t[i].lon, par.dispDms, strLon),\
         wayRoute.t [i+1].oCap,  wayRoute.t [i+1].od, wayRoute.t [i+1].lCap,  wayRoute.t [i+1].ld);
      strcat (str, line);
   }
   sprintf (line, " pDest: %-12s%-12s\n\n", latToStr (par.pDest.lat, par.dispDms, strLat), lonToStr (par.pDest.lon, par.dispDms, strLon));
   strcat (str, line);
   sprintf (line, " Total orthodromic distance: %.2lf NM\n", wayRoute.totOrthoDist);
   strcat (str, line);
   sprintf (line, " Total loxodromic distance : %.2lf NM\n", wayRoute.totLoxoDist);
   strcat (str, line);
}

/*! draw loxodromie */
static void drawLoxoRoute (cairo_t *cr) {
   double x = getX (par.pOr.lon, disp.xL, disp.xR);
   double y = getY (par.pOr.lat, disp.yB, disp.yT);
   cairo_set_source_rgb (cr, 0.8, 0.8, 0.8);
   cairo_move_to (cr, x, y);
   for (int i = 0; i <  wayRoute.n; i++) {
      x = getX (wayRoute.t [i].lon, disp.xL, disp.xR);
      y = getY (wayRoute.t [i].lat, disp.yB, disp.yT);
      cairo_line_to (cr, x, y);
   }
   if (destPressed) {
      x = getX (par.pDest.lon, disp.xL, disp.xR);
      y = getY (par.pDest.lat, disp.yB, disp.yT);
      cairo_line_to (cr, x, y);
   }
   cairo_stroke (cr);
}

/*! draw orthodromic routes from waypoints to next waypoints */
static void drawOrthoRoute (cairo_t *cr, int n) {
   double prevLat = par.pOr.lat;
   double prevLon = par.pOr.lon;
   for (int i = 0; i <  wayRoute.n; i++) {
      orthoPoints (cr, prevLat, prevLon,  wayRoute.t [i].lat,  wayRoute.t [i].lon, n);
      prevLat =  wayRoute.t [i].lat;
      prevLon =  wayRoute.t [i].lon;
   }
   if (destPressed)
      orthoPoints (cr, prevLat, prevLon, par.pDest.lat, par.pDest.lon, n);
}

/*! draw a circle */
static void circle (cairo_t *cr, double lon, double lat, int red, int green, int blue) {
   cairo_arc (cr, getX (lon, disp.xL, disp.xR), 
       getY (lat, disp.yB, disp.yT), 4.0, 0, 2 * G_PI);
   cairo_set_source_rgb (cr, red, green, blue);
   // cairo_stroke (cr);
   cairo_fill (cr);
}

/*! draw the boat */
static void showBoat (cairo_t *cr, double lon, double lat) {
   PangoLayout *layout;
   PangoFontDescription *desc;

   layout = pango_cairo_create_layout(cr);

   desc = pango_font_description_from_string("DejaVuSans 16");
   pango_layout_set_font_description(layout, desc);
   pango_font_description_free(desc);

   //pango_layout_set_text(layout, "⛵", -1);
   pango_layout_set_text(layout, BOAT_UNICODE, -1);

   cairo_move_to (cr, getX (lon, disp.xL, disp.xR),
       getY (lat, disp.yB, disp.yT));
   pango_cairo_show_layout(cr, layout);
   g_object_unref(layout);
}

/*! draw all isochrones stored in isocArray as points */
static gboolean drawAllIsochrones0  (cairo_t *cr) {
   double x, y;
   Pp pt;
   for (int i = 0; i < nIsoc; i++) {
      GdkRGBA color = colors [i % nColors];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = isocArray [i][k]; 
         x = getX (pt.lon, disp.xL, disp.xR); 
         y = getY (pt.lat, disp.yB, disp.yT); 
	      // printf ("i lon lat x y %d %lf %lf %lf %lf \n", i, pt.lon, pt.lat, x, y);
         cairo_arc (cr, x, y, 1.0, 0, 2 * G_PI);
         cairo_fill (cr);
         //cairo_stroke (cr);
      }
   }
   return FALSE;
}

/*! draw closest points to pDest in each isochrones */
static gboolean drawClosest  (cairo_t *cr) {
   double x, y;
   Pp pt;
   cairo_set_source_rgb (cr, 1.0, 0, 0);
   for (int i = 0; i < nIsoc; i++) {
      pt = isocArray [i][isoDesc [i].closest];
      //printf ("lat: %.2lf, lon: %.2lf\n", pt.lat, pt.lon); 
      x = getX (pt.lon, disp.xL, disp.xR); 
      y = getY (pt.lat, disp.yB, disp.yT); 
      cairo_arc (cr, x, y, 2, 0, 2 * G_PI);
      cairo_fill (cr);
   }
   return FALSE;
} 

/*! draw focal points */
static gboolean drawFocal  (cairo_t *cr) {
   double x, y, lat, lon;
   cairo_set_source_rgb (cr, 0, 1.0, 0);
   for (int i = 0; i < nIsoc; i++) {
      lat = isoDesc [i].focalLat;
      lon = isoDesc [i].focalLon;
      //printf ("lat: %.2lf, lon: %.2lf\n", pt.lat, pt.lon); 
      x = getX (lon, disp.xL, disp.xR); 
      y = getY (lat, disp.yB, disp.yT); 
      cairo_arc (cr, x, y, 2, 0, 2 * G_PI);
      cairo_fill (cr);
   }
   return FALSE;
} 

/*! draw all isochrones stored in isocArray as segments or curves */
static gboolean drawAllIsochrones  (cairo_t *cr, int style) {
   double x, y, x1, y1, x2, y2;
   //GtkWidget *drawing_area = gtk_drawing_area_new();
   Pp pt;
   Isoc  newIsoc;
   int index;
   if (par.closestDisp) drawClosest (cr);
   if (par.focalDisp) drawFocal (cr);
   // printf ("DrawAllIsochrones :%d\n", style);
   if (style == NOTHING) return true;
   if (style == POINT) return drawAllIsochrones0 (cr);
   cairo_set_source_rgb (cr, 0, 0, 1.0); // blue
   cairo_set_line_width(cr, 1.0);
   for (int i = 0; i < nIsoc; i++) {
      index = isoDesc [i].first;
      for (int j = 0; j < isoDesc [i].size; j++) {
         newIsoc [j] = isocArray [i][index];
         index += 1;
         if (index == isoDesc [i].size) index = 0;
      }
      pt = newIsoc [0];
	   x = getX (pt.lon, disp.xL, disp.xR);
	   y = getY (pt.lat, disp.yB, disp.yT);
      /*if (i == nIsoc - 1)
         cairo_set_source_rgb (cr, 1.0, 0, 0);
      else 
         cairo_set_source_rgb (cr, 0, 0, 1.0);*/
      cairo_move_to (cr, x, y);
	   if ((isoDesc [i].size < MIN_POINT_FOR_BEZIER) || style == SEGMENT) { 	// segment if number of points < 4 or style == 2
         for (int k = 1; k < isoDesc [i].size; k++) {
            pt = newIsoc [k]; 
            x = getX (pt.lon, disp.xL, disp.xR); 
            y = getY (pt.lat, disp.yB, disp.yT); 
            cairo_line_to (cr, x, y);
	      }
         cairo_stroke(cr);

     }
	  else { 									// curve if number of point >= 4 and style == 2
         int k;
         for (k = 1; k < isoDesc [i].size - 2; k += 3) {
            pt = newIsoc [k]; 
            x = getX (pt.lon, disp.xL, disp.xR); 
            y = getY (pt.lat, disp.yB, disp.yT); 
            pt = newIsoc [k+1]; 
            x1 = getX (pt.lon, disp.xL, disp.xR); 
            y1 = getY (pt.lat, disp.yB, disp.yT); 
            pt = newIsoc [k+2]; 
            x2 = getX (pt.lon, disp.xL, disp.xR); 
            y2 = getY (pt.lat, disp.yB, disp.yT); 
		      cairo_curve_to(cr, x, y, x1, y1, x2, y2);
		   }
         int deb = k;
         for (int k = deb; k < isoDesc [i].size; k++) { // restant en segment
            pt = newIsoc [k]; 
            x = getX (pt.lon, disp.xL, disp.xR); 
            y = getY (pt.lat, disp.yB, disp.yT); 
            cairo_line_to(cr, x, y);
         }
         cairo_stroke(cr);
      }
      cairo_stroke(cr);
   }
   return FALSE;
}

/*! give the focus on point in route at kTime */
static void focusOnPointInRoute (cairo_t *cr)  {
   int i;
   double lat, lon, deltaKtime;
   double deltaTimeStamp = (double) (zone.timeStamp [1] - zone.timeStamp [0]);
   if (route.n == 0) return;
   if (kTime < route.kTime0) i = 0;
   else {
      deltaKtime = (double) (kTime - route.kTime0);
      i = (int) (deltaKtime * deltaTimeStamp / par.tStep);
   }  
   // printf ("kTime: %d, index %lf, par.tStep: %d, deltaTimeStamp: %ld\n", kTime, index, par.tStep, deltaTimeStamp);
   if (i >= route.n) i = route.n -1;
   lat = route.t[i].lat;
   lon = route.t[i].lon;
   showBoat (cr, lon, lat);
   circle (cr, lon, lat, 1.0, 0.0, 0.0); // red
}

/*! draw the routes based on route table, from pOr to pDest or best point */
static void drawRoute (cairo_t *cr) {
   double x = getX (par.pOr.lon, disp.xL, disp.xR);
   double y = getY (par.pOr.lat, disp.yB, disp.yT);
   cairo_move_to (cr, x, y);
   cairo_set_source_rgb (cr, 1, 0, 1);

   for (int i = 1; i < route.n; i++) {
      x = getX (route.t[i].lon, disp.xL, disp.xR);
      y = getY (route.t[i].lat, disp.yB, disp.yT);
      cairo_line_to (cr, x, y);
   }
   cairo_stroke (cr);
}

/*! draw Point of Interests */
static void drawPoi (cairo_t *cr) {
   double x, y;
   for (int i = 0; i < nPoi; i++) {
      if (tPoi [i].type == VISIBLE) {
         circle (cr, tPoi [i].lon, tPoi [i].lat, 0.0, 0.0, 0.0);
         x = getX (tPoi [i].lon, disp.xL, disp.xR);
         y = getY (tPoi [i].lat, disp.yB, disp.yT);
         cairo_move_to (cr, x+10, y);
         cairo_show_text (cr, g_strdup_printf("%s", tPoi [i].name));
      }
   }
   cairo_stroke (cr);
}

/*! draw shapefile with different color for sea and earth */
static gboolean draw_shp_map (GtkWidget *widget, cairo_t *cr, gpointer data) {
   double lon, lat, lon0, lat0;
   double x0, y0, x, y;
   double alpha;
   int width, height;
   gtk_widget_get_size_request (widget, &width, &height);

   // Dessiner les entités
   for (int i = 0; i < nTotEntities; i++) {
      lon0 = entities[i].points[0].lon;
      lat0 = entities[i].points[0].lat;
      x0 = getX (lon0, disp.xL, disp.xR);
      y0 = getY (lat0, disp.yB, disp.yT);
      switch (entities [i].nSHPType) {
      case SHPT_POLYGON : // case SHPT_POLYGONZ : case SHPT_ARC : case SHPT_ARCZ :
         // Traitement pour les lignes (arcs)
         cairo_set_source_rgb (cr, 0.5, 0.5, 0.5); // Lignes en gris
         cairo_move_to(cr, x0, y0);

         for (int j = 1; j < entities[i].numPoints; j++) {
            lon = entities[i].points[j].lon;
            lat = entities[i].points[j].lat;
            x = getX (lon, disp.xL, disp.xR);
            y = getY (lat, disp.yB, disp.yT);
            cairo_line_to (cr, x, y);
         }
         // cairo_stroke(cr);
         // case SHPT_POLYGON : case SHPT_POLYGONZ Traitement pour les polygones
         alpha = (par.showColors >= 1) ? 0.5 : 1.0;
         cairo_set_source_rgba (cr, 157.0/255.0, 162.0/255.0, 12.0/255.0, alpha); // Polygones en jaune
         cairo_move_to(cr, x0, y0);
         for (int j = 1; j < entities[i].numPoints; j++) {
            lon = entities[i].points[j].lon;
            lat = entities[i].points[j].lat;
            x = getX (lon, disp.xL, disp.xR);
            y = getY (lat, disp.yB, disp.yT);
            cairo_line_to (cr, x, y);
         }
         cairo_close_path(cr);
         cairo_fill(cr);
        break;
      default: fprintf (stderr, "In draw_shp_map, impossible type\n"); 
      }
   }
   return FALSE;
}

/*! draw something that represent the waves */
static void showWaves (cairo_t *cr, Pp pt, double w) {
   double x = getX (pt.lon, disp.xL, disp.xR);
   double y = getY (pt.lat, disp.yB, disp.yT);
   if ((w <= 0) || (w > 100)) return;
   cairo_set_source_rgb (cr, 0.5, 0.5, 0.5); // orange
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 6);
   cairo_move_to (cr, x, y);
   cairo_show_text (cr, g_strdup_printf("%.2lf", w));
}

/*! draw arrow that represent the direction of the wind */
static void arrow (cairo_t *cr, Pp pt, double u, double v, double twd, double tws, int typeFlow) {
   const double arrowSize = 10.0;
   if (tws == 0 || u <= (MISSING + 1) || v <= (MISSING +1) || fabs (u) > 100 || fabs (v) > 100) 
      return;
   double head_x = getX (pt.lon, disp.xL, disp.xR); 
   double head_y = getY (pt.lat, disp.yB, disp.yT); 
   // Calculer les coordonnées de la queue
   double tail_x = head_x - 30 * u/tws;
   double tail_y = head_y + 30 * v/tws;
   if (typeFlow == WIND) cairo_set_source_rgb (cr, 0, 0, 0);   // black
   else cairo_set_source_rgb (cr, 1.0, 165.0/255.0, 0);        // orange
   cairo_set_line_width (cr, 1.0);
   cairo_set_font_size (cr, 6);

   if (tws < 1) {  // no wind : circle
      cairo_move_to (cr, head_x, head_y);
      cairo_show_text (cr, g_strdup_printf("%s", "o"));
      return;
   }
    // Dessiner la ligne de la flèche
   cairo_move_to(cr, head_x, head_y);
   cairo_line_to(cr, tail_x, tail_y);
   cairo_stroke(cr);

   // Dessiner le triangle de la flèche à l'extrémité
   cairo_move_to (cr, head_x, head_y);
   cairo_line_to (cr, head_x + arrowSize * sin (DEG_TO_RAD  * twd - M_PI/6), head_y - arrowSize * cos(DEG_TO_RAD * twd - M_PI/6));
   cairo_stroke(cr);
   cairo_move_to (cr, head_x, head_y);
   cairo_line_to (cr, head_x + arrowSize * sin(DEG_TO_RAD * twd + M_PI/6), head_y - arrowSize * cos(DEG_TO_RAD * twd + M_PI/6));
   cairo_stroke(cr);
}


/*! draw barbule that represent the wind */
static void barbule (cairo_t *cr, Pp pt, double u, double v, double tws, int typeFlow) {
   int i, j, k;
   double barb0_x, barb0_y, barb1_x, barb1_y, barb2_x, barb2_y;
   if (tws == 0 || u <= (MISSING + 1) || v <= (MISSING +1) || fabs (u) > 100 || fabs (v) > 100) {
      //printf ("In barbule, strange value lat: %.2lf, lon: %.2lf, u: %.2lf, v: %.2lf\n", pt.lat, pt.lon, u, v);
      return;
   }
   // Calculer les coordonnées de la tête
   double head_x = getX (pt.lon, disp.xL, disp.xR); 
   double head_y = getY (pt.lat, disp.yB, disp.yT); 
   // Calculer les coordonnées de la queue
   double tail_x = head_x - 30 * u/tws;
   double tail_y = head_y + 30 * v/tws;
   if (typeFlow == WIND) cairo_set_source_rgb (cr, 0, 0, 0);   // black
   else cairo_set_source_rgb (cr, 1.0, 165.0/255.0, 0);        // orange
   cairo_set_line_width (cr, 1.0);
   cairo_set_font_size (cr, 6);

   if (tws < 1) {  // no wind : circle
      cairo_move_to (cr, head_x, head_y);
      cairo_show_text (cr, g_strdup_printf("%s", "o"));
      //cairo_arc (cr, head_x, head_y, 3, 0, 2*G_PI);
      //cairo_stroke (cr);
      return;
   }

   // Dessiner la ligne de la flèche
   cairo_move_to (cr, tail_x, tail_y);
   cairo_line_to (cr, head_x, head_y);
   cairo_stroke (cr);
   cairo_arc (cr, head_x, head_y, 1, 0, 2*G_PI);
   cairo_fill (cr);

   // determiner le nombre de barbules de chaque type
   tws += 2; // pour arrondi
   int barb50 = (int)tws/50;  
   int barb10 = ((int)tws % 50)/10;  
   int barb5 =  ((int)(tws) % 10) / 5; 
 
   // signes de u et v
   int signU = (u >= 0) ? 1 : -1;
   int signV = (v >= 0) ? 1 : -1;

   k = 25; // hauteur
   for (i = 0; i < barb50; i++) {
      barb0_x = tail_x + (10 * i) * u/tws; 
      barb0_y = tail_y - (10 * i) * v/tws;
      barb1_x = barb0_x - k * signU * signV * fabs(v/tws);
      barb1_y = barb0_y - k * fabs (u/tws);
      barb2_x = tail_x + (10 * (i+1)) * u/tws; 
      barb2_y = tail_y - (10 * (i+1)) * v/tws;
      cairo_move_to (cr, barb0_x, barb0_y);
      cairo_line_to (cr, barb1_x, barb1_y);
      cairo_line_to (cr, barb2_x, barb2_y);
      cairo_close_path (cr);
      cairo_fill (cr);
   }

   k = 20; // hauteur
   for (j = 0; j < barb10; j++) {
      barb0_x = tail_x + (12 * i + 8 * j) * u/tws; 
      barb0_y = tail_y - (12 * i + 8 * j) * v/tws;
      // printf ("u = %lf v = %lf \n", u, v);
      barb1_x = barb0_x - k * signU * signV * fabs(v/tws);
      barb1_y = barb0_y - k * fabs (u/tws);
      cairo_move_to (cr, barb0_x, barb0_y);
      cairo_line_to (cr, barb1_x, barb1_y);
      cairo_stroke (cr);
   }

   k = 10; // hauteur
   if (barb5 != 0) {
      barb0_x = tail_x + (12 * i + 8 * j) * u/tws; 
      barb0_y = tail_y - (12 * i + 8 * j) * v/tws;
      barb1_x = barb0_x - k * signU * signV * fabs(v/tws);
      barb1_y = barb0_y - k * fabs (u/tws);
      cairo_move_to (cr, barb0_x, barb0_y);
      cairo_line_to (cr, barb1_x, barb1_y);
      cairo_stroke (cr);
   }
}

/*! update status bar */
static void statusBarUpdate () {
   char totalDate [MAX_SIZE_DATE]; 
   char *pDate = &totalDate [0];
   char sStatus [MAX_SIZE_BUFFER];
   Pp pt;
   double u = 0, v = 0, w = 0, uCurr = 0, vCurr = 0, bidon;
   char seaEarth [10];
   char strLat [MAX_SIZE_NAME] = "", strLon [MAX_SIZE_NAME] = "";
   double tDeltaCurrent = zoneTimeDiff (currentZone, zone);
   pt.lat = yToLat (whereIsMouse.y, disp.yB, disp.yT);
   pt.lon = xToLon (whereIsMouse.x, disp.xL, disp.xR);
   if (pt.lon > 180) pt.lon -= 360;
   if (par.constWindTws != 0) {
	   u = - KN_TO_MS * par.constWindTws * sin (DEG_TO_RAD * par.constWindTwd);
	   v = - KN_TO_MS * par.constWindTws * cos (DEG_TO_RAD * par.constWindTwd);
   }
	else findFlow (pt, zone.timeStamp [kTime], &u, &v, &w, zone, gribData);
	//else findFlow (pt, theTime, &u, &v, &w, zone, gribData);
   strcpy (seaEarth, (extIsSea (pt.lon, pt.lat)) ? "Sea" : "Earth");
   if (par.constCurrentS != 0) {
      uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
      vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
   }
   else {
	   findFlow (pt, currentZone.timeStamp [kTime] - tDeltaCurrent, &uCurr, &vCurr, &bidon, currentZone, currentGribData);
   }
   
   sprintf (sStatus, "%s         %d/%ld %-80s %s, %s\
      Wind: %03d° %05.2lf Knots   Waves: %5.2lf  Current: %03d° %05.2lf Knots         %s      %s",
      newDate (zone.dataDate [0], (zone.dataTime [0]/100)+ zone.timeStamp [kTime], pDate),\
      kTime + 1, zone.nTimeStamp, " ",\
      latToStr (pt.lat, par.dispDms, strLat),\
      lonToStr (pt.lon, par.dispDms, strLon),\
      (int) (extTwd (u,v) + 360) % 360, extTws (u,v), w, 
      (int) (extTwd (uCurr, vCurr) + 360) % 360, extTws (uCurr, vCurr), 
      seaEarth,
      (gribRequestRunning || readGribRet == -1) ? "WAITING GRIB" : "");
  
   gtk_statusbar_push(GTK_STATUSBAR(statusbar), context_id, sStatus);

}

/*! draw the main window */
static gboolean drawGribCallback (GtkWidget *widget, cairo_t *cr, gpointer data) {
   //double x, y;
   double u, v, w, twd, tws, uCurr, vCurr;
   char str [MAX_SIZE_LINE] ="";
   //strLat [MAX_SIZE_LINE]= "", strLon [MAX_SIZE_LINE] = "";
   double tDeltaCurrent = zoneTimeDiff (currentZone, zone);
   Pp pt;
   globalCr = cr;
   int width, height;
   guint8 r, g, b;
   sprintf (str, "%s %s %s", PROG_NAME, PROG_VERSION, par.gribFileName);
   gtk_window_set_title (GTK_WINDOW (window), str); // window is a global variable
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel(widget)), &width, &height);
//printf ("width: %d, height: %d\n", width, height);
   disp.xL = 0;      // right
   disp.xR = width ; // left
   disp.yT = 0;      // top
   disp.yB = height; // bottom
   theTime = zone.timeStamp [kTime];

   cairo_set_source_rgb(cr, 1, 1, 1); // Couleur blanche
   cairo_paint(cr);                   // clear all
   if (par.showColors != 0) {
      if (par.constWindTws != 0) {
         mapColors (par.constWindTws, &r, &g, &b);
         cairo_set_source_rgba (cr, r/255.0, g/255.0, b/255.0, 0.5);
         cairo_rectangle (cr, 1, 1, width, height);
         cairo_fill(cr);
      }
      else paintWind (cr, width, height);
   }
   cairo_stroke(cr);

   draw_shp_map (widget, cr, data);
   cairo_set_source_rgb (cr, 0, 0, 0);
   /*
   // Dessiner la ligne horizontale pour les longitudes avec des libellés
   cairo_move_to (cr, disp.xR, disp.yB);
   cairo_line_to (cr, disp.xL, disp.yB);
   cairo_stroke (cr);

   // Dessiner la ligne verticale pour les latitudes avec des libelles
   cairo_move_to(cr, disp.xL+3, disp.yT);
   cairo_line_to(cr, disp.xL+3, disp.yB);
   cairo_stroke(cr);

   // Dessiner les libelles des longitudes
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 6);

   double lon = dispZone.lonLeft + dispZone.lonStep;
   while (lon <= dispZone.lonRight) {
      x = getX (lon, disp.xL, disp.xR);
      cairo_move_to (cr, x, height - 130);
      cairo_show_text (cr, g_strdup_printf("%s", lonToStr (lon, par.dispDms, strLon)));
      lon += dispZone.lonStep;
   }
   cairo_stroke(cr);

   // Dessiner les libelles des latitudes
   double lat = dispZone.latMin;
   while (lat <= dispZone.latMax) {
      y = getY (lat, disp.yB, disp.yT);
      cairo_move_to (cr, 5, y);
      cairo_show_text (cr, g_strdup_printf("%s", latToStr (lat, par.dispDms, strLat)));
      lat += dispZone.latStep;
   }
   */
   // dessiner les barbules ou fleches de vent
   pt.lat = dispZone.latMin;
   while (pt.lat <= dispZone.latMax) {
      pt.lon = dispZone.lonLeft;
      while (pt.lon <= dispZone.lonRight) {
	      u = 0; v = 0; w = 0; uCurr = 0; vCurr = 0;
         if (extIsInZone (pt, zone)) {
            if (par.constWindTws != 0) {
	            u = - KN_TO_MS * par.constWindTws * sin (DEG_TO_RAD * par.constWindTwd);
	            v = - KN_TO_MS * par.constWindTws * cos (DEG_TO_RAD * par.constWindTwd);
            }
	         else {
	            findFlow (pt, theTime, &u, &v, &w, zone, gribData);
               //printf ("twd : %lf\n", twd);
            }
            twd = extTwd (u, v);
            tws = extTws (u, v);
    	      // printf ("%lf %lf %lf %lf\n", pt.lat,pt.lon, u, v);
	         if (par.windDisp == BARBULE) barbule (cr, pt, u, v, tws, WIND);
            else if (par.windDisp == ARROW) arrow (cr, pt, u, v, twd, tws, WIND);
            if (par.constWave != 0) w = par.constWave;
            if (par.waveDisp) showWaves (cr, pt, w);

            if (par.currentDisp) {
               if (par.constCurrentS != 0) {
                  uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
                  vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
               }
               else {
	               findFlow (pt, theTime - tDeltaCurrent, &uCurr, &vCurr, &w, currentZone, currentGribData);
               }
               if ((uCurr != 0) || (vCurr !=0)) {
                  twd = extTwd (uCurr, vCurr);
                  tws = extTws (uCurr, vCurr);
                  barbule (cr, pt, uCurr, vCurr, tws, CURRENT);
               }
            }
	      }
         pt.lon += (dispZone.lonStep) / 2;
      }
      pt.lat += (dispZone.latStep) / 2;
   }
   //orthoPoints (cr, par.pOr.lat, par.pOr.lon, par.pDest.lat, par.pDest.lon, 20);
   calculateOrthoRoute ();
   drawOrthoRoute (cr, ORTHO_ROUTE_PARAM);
   drawLoxoRoute (cr);
   circle (cr, par.pOr.lon, par.pOr.lat, 0.0, 1.0, 0.0);
   if (destPressed) circle (cr, par.pDest.lon, par.pDest.lat, 0.0, 0.0, 1.0);
   if (! isnan (my_gps_data.lon) && ! isnan (my_gps_data.lat))
      circle (cr, my_gps_data.lon, my_gps_data.lat , 1.0, 0.0, 0.0);
   if ((route.n != 0) && (isfinite(route.totDist)) && (route.totDist > 0)) { 
      drawAllIsochrones (cr, par.style);
      drawRoute (cr);
      focusOnPointInRoute (cr);
   }
   drawPoi (cr);
   // Dessin egalement le rectangle de selection si en cours de selection
   if (selecting) {
      cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 0.5); // Rouge semi-transparent
      cairo_rectangle(cr, whereWasMouse.x, whereWasMouse.y, whereIsMouse.x - whereWasMouse.x, whereIsMouse.y - whereWasMouse.y);
      cairo_fill(cr);
   }
   statusBarUpdate ();
   return FALSE;
}

/*! draw the polar circles and scales */
static void polarTarget (cairo_t *cr, int type, double width, double height, double rStep) {
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double nStep = ceil (maxValInPol (*ptMat));
   // printf ("rStep = %.2lf\n", rStep);
   if (type == WAVE_POLAR) {
      nStep = nStep/10;
      rStep = rStep *10;
   }
   int rMax = rStep * nStep;
   double centerX = width / 2;
   double centerY = height / 2;
   const int MIN_R_STEP_SHOW = 12;
   cairo_set_source_rgb (cr, 0.9, 0.9, 0.9);
  
   for (int i = 1; i <= nStep; i++)
      cairo_arc (cr, centerX, centerY, i * rStep, -G_PI/2, G_PI/2);  //trace cercle
   
   for (double angle = -90; angle <= 90; angle += 22.5) {            // segments
      cairo_move_to (cr, centerX, centerY);
      cairo_rel_line_to (cr, rMax * cos (DEG_TO_RAD * angle),
         rMax * sin (DEG_TO_RAD * angle));
   }
   cairo_stroke(cr);
   cairo_set_source_rgb (cr, 0.2, 0.2, 0.2);

   // libelles vitesses
   for (int i = 1; i <= nStep; i++) {
      cairo_move_to (cr, centerX - 40, centerY - i * rStep);
      if (type == WAVE_POLAR) {
         if ((i % 2) == 0) {// only pair values 
            cairo_show_text (cr, g_strdup_printf("%2d %%", i * 10));
            cairo_move_to (cr, centerX - 40, centerY + i * rStep);
            cairo_show_text (cr, g_strdup_printf("%2d %%", i * 10));
         }
      }
   
      else {
         if ((rStep > MIN_R_STEP_SHOW) || ((i % 2) == 0)) { // only pair values if too close
            cairo_show_text (cr, g_strdup_printf("%2d kn", i));
            cairo_move_to (cr, centerX - 40, centerY + i * rStep);
            cairo_show_text (cr, g_strdup_printf("%2d kn", i));
         }
      }
   }
   // libelles angles
   for (double angle = -90; angle <= 90; angle += 22.5) {
      cairo_move_to (cr, centerX + rMax * cos (DEG_TO_RAD * angle) * 1.05,
         centerY + rMax * sin (DEG_TO_RAD * angle) * 1.05);
         //cairo_show_text (cr, g_strdup_printf("%.2f°", angle + 90));
   }
   cairo_stroke(cr);
}

/*! draw the polar legends */
static void polarLegend (cairo_t *cr, int type) {
   double xLeft = 100;
   double y = 5;
   double hSpace = 18;
   GdkRGBA color;
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   cairo_set_line_width (cr, 1);
   cairo_set_source_rgb (cr, 0.5, 0.5, 0.5);
   cairo_rectangle (cr, xLeft, y, 120, ptMat->nCol * hSpace);
   cairo_stroke(cr);
   cairo_set_line_width (cr, 1);
   xLeft += 20;
   y += hSpace;
   for (int c = 1; c < ptMat->nCol; c++) {
      color = colors [c % nColors];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      cairo_move_to (cr, xLeft, y);
      cairo_show_text (cr, g_strdup_printf((type == WAVE_POLAR) ? "Height at %.2lf m" : "Wind at %.2lf kn", ptMat->t [0][c]));
      y += hSpace;
   }
   cairo_stroke(cr);
}

/*! return x and y in polar */
static void getPolarXY (int type, int l, int c, double width, double height, double radiusFactor, double *x, double *y) {
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double angle = (90 - ptMat->t [l][0]) * DEG_TO_RAD;  // Convertir l'angle en radians
   double radius = ptMat->t [l][c] * radiusFactor;
   *x = width / 2 + radius * cos(angle);
   *y = height / 2 - radius * sin(angle);
}

/*! Draw polar from polar matrix */
static void on_draw_polar_event (GtkWidget *widget, cairo_t *cr, gpointer user_data) {
   double x, y, x1, x2, y1, y2;
   cairo_set_line_width(cr, 2);
   // Obtenir la largeur et la hauteur de la zone de dessin
   int width, height;
   PolMat *ptMat = (polarType == WAVE_POLAR) ? &wavePolMat : &polMat; 
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel(widget)), &width, &height);
   //double radiusFactor = (polarType == WAVE_POLAR) ? width / 40 : width / (maxValInPol (*ptMat) * 5); // Ajustez la taille de la courbe
   double radiusFactor;
   radiusFactor = width / (maxValInPol (*ptMat) * 6); // Ajustez la taille de la courbe
   // printf ("RAD : %.2lf\n", radiusFactor);
   int minCol = (selectedPol == 0) ? 1 : selectedPol;
   int maxCol = (selectedPol == 0) ? ptMat->nCol : selectedPol + 1;

   polarTarget (cr, polarType, width, height, radiusFactor);
   polarLegend (cr, polarType);
   cairo_set_line_width (cr, 1);

   for (int c = minCol; c < maxCol; c++) {
      GdkRGBA color = colors [c % nColors];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      getPolarXY (polarType, 1, c, width, height, radiusFactor, &x, &y);
      cairo_move_to (cr, x, y);

      // segments
      if (segmentOrBezier == SEGMENT) {
         for (int l = 2; l < ptMat->nLine; l += 1) {
            getPolarXY (polarType, l, c, width, height, radiusFactor, &x, &y);
            cairo_line_to(cr, x, y);
         }
         cairo_stroke(cr);
      }
      // Tracer la courbe de Bézier si nb de points suffisant
      else {
         for (int l = 2; l < ptMat->nLine - 2; l += 3) {
            getPolarXY (polarType, l, c, width, height, radiusFactor, &x, &y);
            getPolarXY (polarType, l+1, c, width, height, radiusFactor, &x1, &y1);
            getPolarXY (polarType, l+2, c, width, height, radiusFactor, &x2, &y2);
            cairo_curve_to(cr, x, y, x1, y1, x2, y2);
         }
         getPolarXY (polarType, ptMat->nLine -1, c, width, height, radiusFactor, &x, &y);
         cairo_line_to(cr, x, y);
         
      }
      cairo_stroke(cr);
   }
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/*! find index in polar to draw */
static void on_filter_changed(GtkComboBox *widget, gpointer user_data) {
   selectedPol = gtk_combo_box_get_active(GTK_COMBO_BOX(filter_combo));
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/* combo filter zone for polar */
static void create_filter_combo(int type) {
   char str [MAX_SIZE_LINE];
   GtkTreeIter iter;
   // Créez un modèle de liste pour la liste déroulante
   filter_store = gtk_list_store_new(1, G_TYPE_STRING);
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   
   // Ajoutez vos filtres au modèle
   gtk_list_store_append(filter_store, &iter);
   gtk_list_store_set(filter_store, &iter, 0, "All", -1);  
   for (int c = 1; c < ptMat->nCol; c++) {
      gtk_list_store_append(filter_store, &iter);
      sprintf (str, "%.2lf %s", ptMat->t [0][c], (type == WAVE_POLAR) ? "m. Wave Height" : "Knots. Wind Speed.");
      gtk_list_store_set(filter_store, &iter, 0, str, -1);
   }

   // Créez la liste déroulante
   filter_combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(filter_store));

   // Créez et configurez le renderer pour afficher le texte dans la liste déroulante
   GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(filter_combo), renderer, TRUE);
   gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(filter_combo), renderer, "text", 0, NULL);
   g_signal_connect(G_OBJECT(filter_combo), "changed", G_CALLBACK(on_filter_changed), NULL);
   // Sélectionnez la première entrée par défaut
   gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 0);
}

/* display polar matrix */
static void polarDump (int type) {
   //int *comportement = (int *) data;
   //printf ("ATT 0 comportement %d\n", *comportement);
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   GtkWidget *label;
   char str [MAX_SIZE_LINE] = "Polar Grid: ";
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   strcat (str, (type == WAVE_POLAR) ? par.wavePolFileName : par.polarFileName);
   gtk_window_set_title(GTK_WINDOW(window), str);
   gtk_container_set_border_width(GTK_CONTAINER(window), 10);
   gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

   // Scroll 
   GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(window), scrolled_window);
   gtk_widget_set_size_request(scrolled_window, 800, 400);

   // grid creation
   GtkWidget *grid = gtk_grid_new();   
   gtk_container_add(GTK_CONTAINER(scrolled_window), grid);
   if ((ptMat->nCol == 0) || (ptMat->nLine == 0)) {
      infoMessage ("No polar information", GTK_MESSAGE_ERROR);
      return;
   }
   
   for (int line = 0; line < ptMat->nLine; line++) {
      for (int col = 0; col < ptMat->nCol; col++) {
	      if ((col == 0) && (line == 0)) strcpy (str, (type == WAVE_POLAR) ? "Angle/Height" : "TWA/TWS");
         else sprintf(str, "%6.2f", ptMat->t [line][col]);
         label = gtk_label_new(str);
	      gtk_grid_attach(GTK_GRID (grid), label, col, line, 1, 1);
         gtk_widget_set_size_request (label, MAX_TEXT_LENGTH * 10, -1);
         // Ajouter des propriétés distinctes pour la première ligne et la première colonne
         if (line == 0 || col == 0) {
           // Mettre en gras ou changer la couleur pour la première ligne
            PangoAttrList *attrs = pango_attr_list_new();
            PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
            pango_attr_list_insert(attrs, attr);
            gtk_label_set_attributes(GTK_LABEL(label), attrs);
            pango_attr_list_unref(attrs);
        }
     }
   }
   g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
   gtk_widget_show_all(window);
   gtk_main();
}

/* display wave polar matrix */
static void wavePolarDump (GtkWidget *widget, cairo_t *cr, gpointer data) {
   polarType = WAVE_POLAR;
   polarDump (WAVE_POLAR);
}

/* callBack polar dumo */
static void cbPolarDump (GtkWidget *widget, cairo_t *cr, gpointer data) {
   polarType = POLAR;
   polarDump (POLAR);
}

/*! callback function in polar for edit */
static void on_edit_button_polar_clicked(GtkWidget *widget, gpointer data) {
   char line [MAX_SIZE_LINE] = "";
   char fileName [MAX_SIZE_NAME] = "";
   strcpy (fileName, (polarType == WAVE_POLAR) ? par.wavePolFileName : par.polarFileName);
   sprintf (line, "%s %s \n", par.editor, fileName);
   printf ("%s\n", line);
   if (system (line) != 0) {
      fprintf (stderr, "Error in editing Polar. System call: %s\n", line);
      return;
   }
   if (confirm (fileName, "Confirm reloading file below"))
      readPolar (fileName, (polarType == WAVE_POLAR) ? &wavePolMat : &polMat);
}

/*! Callback radio button selecting provider for segment or bezier */
static void segmentOrBezierButtonToggled(GtkToggleButton *button, gpointer user_data) {
   segmentOrBezier = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/*! Draw polar from polar file */
static void polarDraw (int type) {
   char line [MAX_SIZE_LINE] = "Polar: "; 
   GtkWidget *box, *hbox;
   char sStatus [MAX_SIZE_LINE];
   GtkWidget *statusbar;
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   if ((ptMat->nCol == 0) || (ptMat->nLine == 0)) {
      infoMessage ("No polar information", GTK_MESSAGE_ERROR);
      return;
   }

   // Créer la fenêtre principale
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
   gtk_window_set_default_size(GTK_WINDOW(window), POLAR_WIDTH, POLAR_HEIGHT);
   strcat (line, (type == WAVE_POLAR) ? par.wavePolFileName : par.polarFileName);
   gtk_window_set_title(GTK_WINDOW(window), line);

   // Créer un conteneur de type boîte
   box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   gtk_container_add(GTK_CONTAINER(window), box);
   
   // Créer une zone de dessin
   polar_drawing_area = gtk_drawing_area_new();
   gtk_widget_set_size_request(polar_drawing_area, -1, -1);
   g_signal_connect(G_OBJECT(polar_drawing_area), "draw", G_CALLBACK(on_draw_polar_event), NULL);
   
   // create zone for combo box
   create_filter_combo (type);
  
   GtkWidget *dumpButton = gtk_button_new_from_icon_name ("x-office-spreadsheet-symbolic", GTK_ICON_SIZE_BUTTON);
   g_signal_connect(G_OBJECT(dumpButton), "clicked", G_CALLBACK((type == WAVE_POLAR) ? wavePolarDump : cbPolarDump), NULL);
   
   GtkWidget *editButton = gtk_button_new_from_icon_name ("document-edit-symbolic", GTK_ICON_SIZE_BUTTON);
   g_signal_connect(G_OBJECT(editButton), "clicked", G_CALLBACK(on_edit_button_polar_clicked), NULL);
   
   // radio button for bezier or segment choice
   GtkWidget *segmentRadio = gtk_radio_button_new_with_label (NULL, "segment");
   GtkWidget *bezierRadio = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (segmentRadio), "Bézier");
   g_object_set_data (G_OBJECT(segmentRadio), "index", GINT_TO_POINTER (SEGMENT));
   g_object_set_data (G_OBJECT(bezierRadio), "index", GINT_TO_POINTER (BEZIER));
   g_signal_connect (segmentRadio, "toggled", G_CALLBACK (segmentOrBezierButtonToggled), NULL);
   g_signal_connect (bezierRadio, "toggled", G_CALLBACK (segmentOrBezierButtonToggled), NULL);
   segmentOrBezier = (ptMat->nLine < MIN_POINT_FOR_BEZIER) ? SEGMENT : BEZIER;
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON ((segmentOrBezier == SEGMENT) ? segmentRadio : bezierRadio), TRUE);
  
   hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_box_pack_start(GTK_BOX (hbox), filter_combo, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), segmentRadio, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), bezierRadio, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), dumpButton, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), editButton, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (box), hbox, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (box), polar_drawing_area, TRUE, TRUE, 0);

   // Connecter le signal de fermeture de la fenêtre à gtk_main_quit
   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

   // Création de la barre d'état
   statusbar = gtk_statusbar_new ();
   guint context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "Statusbar");
   sprintf (sStatus, "nCol: %2d   nLig: %2d   max: %2.2lf", \
      ptMat->nCol, ptMat->nLine, maxValInPol (*ptMat));
   gtk_statusbar_push (GTK_STATUSBAR(statusbar), context_id, sStatus);
   gtk_box_pack_start(GTK_BOX(box), statusbar, FALSE, FALSE, 0);

   gtk_widget_show_all(window);
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
   gtk_main();
}

/* callBack polar dump */
static void cbPolarDraw (GtkWidget *widget, cairo_t *cr, gpointer data) {
   polarType = POLAR;
   polarDraw (POLAR);
}

/* callBack polar dumo */
static void cbWavePolarDraw (GtkWidget *widget, cairo_t *cr, gpointer data) {
   polarType = WAVE_POLAR;
   polarDraw (WAVE_POLAR);
}

/*! write routing report in str */ 
static void reportToStr (char* buffer) {
   char totalDate [MAX_SIZE_DATE]; 
   char *pDate = &totalDate [0];
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   sprintf (line, "Departure Date and Time                 : %4d/%02d/%02d %02d:%02d\n", 
        start->year, start->mon+1, start->day, start->hour, start->min);
   strcat (buffer, line);
   sprintf (line, "Nb hours after origin of grib           : %.2lf\n", par.startTimeInHours);
   strcat (buffer, line); 
   sprintf (line, "Nb of Isochrones                        : %d\n", nIsoc);
   strcat (buffer, line);
   if (route.destinationReached) {
      sprintf (line, "Number of steps                         : %d\n", nIsoc + 1);
      strcat (buffer, line);
   }
   else {
      sprintf (line, "Best point reached                      : ");
      strcat (buffer, line);
      sprintf (line, "%s %s\n", latToStr (lastClosest.lat, par.dispDms, strLat), lonToStr (lastClosest.lon, par.dispDms, strLon));
      strcat (buffer, line);
      sprintf (line, "Distance to destination                 : %.2lf\n", orthoDist (lastClosest.lat, lastClosest.lon,\
         par.pDest.lat, par.pDest.lon));
      strcat (buffer, line);
   }
   sprintf (line, "Arrival Date and Time                   : %s\n",\
      newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route.duration, pDate));
   strcat (buffer, line);

   sprintf (line, "Total distance in Nautical miles        : %.2lf\n", route.totDist);
   strcat (buffer, line);
   sprintf (line, "Duration in hours                       : %.2lf\n", route.duration);
   strcat (buffer, line);
   sprintf (line, "Mean Speed Over Ground                  : %.2lf\n", route.totDist/route.duration);
   strcat (buffer, line);
}

static void initStart (MyDate *start) {
   long intDate = zone.dataDate [0];
   long intTime = zone.dataTime [0];
   double forecastTime = par.startTimeInHours;
   //double forecastTime = zone.timeStamp [route.kTime0];
   time_t seconds = 0;
   struct tm *tm0 = localtime (&seconds);;
   tm0->tm_year = ((int ) intDate / 10000) - 1900;
   tm0->tm_mon = ((int) (intDate % 10000) / 100) - 1;
   tm0->tm_mday = (int) (intDate % 100);
   tm0->tm_hour = 0; tm0->tm_min = 0; tm0->tm_sec = 0;
   time_t theTime = mktime (tm0);                        // converion struct tm en time_t
   theTime += 3600 * (intTime/100 + forecastTime);       // calcul ajout en secondes
   timeInfos = localtime (&theTime);                     // conversion en struct tm
   start->year = timeInfos->tm_year + 1900;
   start->mon = timeInfos->tm_mon;
   start->day=  timeInfos->tm_mday;
   start->hour = timeInfos->tm_hour;
   start->min = timeInfos->tm_min;
   start->sec = 0;
}

/* calculate difference in hours between departure time and time 0 */
static double getDepartureTimeInHour (MyDate *start) {
   time_t seconds = 0;
   struct tm *tmStart = localtime (&seconds);
   time_t theTime0 = dateToTime_t (zone.dataDate [0]) + 3600 * (zone.dataTime [0])/100;
  
   tmStart->tm_year = start->year - 1900;
   tmStart->tm_mon = start->mon;
   tmStart->tm_mday = start->day;
   tmStart->tm_hour = start->hour;; 
   tmStart->tm_min = start->min; 
   tmStart->tm_sec = start->sec;;
   time_t startTime = mktime (tmStart);                   // converion struct tm en time_t
   return (startTime - theTime0)/3600.0;                  // calcul ajout en secondes
} 

/*! get start date and time for routing. Returns true if OK Response */
static bool calendar (MyDate *start) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Pick a Date", NULL, 
     GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);

   GtkWidget *calendar = gtk_calendar_new();

   gtk_calendar_select_month(GTK_CALENDAR(calendar), start->mon, start->year);
   gtk_calendar_select_day(GTK_CALENDAR(calendar), start->day);

   // Ajouter le calendrier à la boîte de dialogue
   GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_add(GTK_CONTAINER(content_area), calendar);

   // Créer les libellés et les boutons de sélection
   GtkWidget *labelHour = gtk_label_new("Hour");
   GtkWidget *spinHour = gtk_spin_button_new_with_range(0, 23, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinHour), start->hour);

   GtkWidget *labelMinutes = gtk_label_new("Minutes");
   GtkWidget *spinMinutes = gtk_spin_button_new_with_range(0, 59, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinMinutes), start->min);

   // Créer une boîte de disposition horizontale
   GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_box_pack_start(GTK_BOX(hbox), labelHour, FALSE, FALSE, 5);
   gtk_box_pack_start(GTK_BOX(hbox), spinHour, FALSE, FALSE, 5);
   gtk_box_pack_start(GTK_BOX(hbox), labelMinutes, FALSE, FALSE, 5);
   gtk_box_pack_start(GTK_BOX(hbox), spinMinutes, FALSE, FALSE, 5);

   gtk_container_add(GTK_CONTAINER(content_area), hbox);

   gtk_widget_show_all(dialog);

   int result = gtk_dialog_run(GTK_DIALOG(dialog));

   // Récupérer la date sélectionnée après la fermeture de la boîte de dialogue
   if (result == GTK_RESPONSE_OK) {
      guint selected_year, selected_month, selected_day;
      gtk_calendar_get_date(GTK_CALENDAR(calendar), &selected_year, &selected_month, &selected_day);
      start -> hour = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinHour));
      start -> min = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinMinutes));
      start -> year = selected_year;
      start -> mon = selected_month;
      start -> day = selected_day;
      start -> sec = 0;  
   } else {
      gtk_widget_destroy(dialog);
      return false;
   }
   gtk_widget_destroy(dialog);
   return true;
}

/*! launch routing */
static void on_run_button_clicked (GtkWidget *widget, gpointer data) {
   int ret;
   struct timeval t0, t1;
   long ut0, ut1;
   char buffer [1024] = "";
   char line [1024] = "";
   double lastStepDuration = 0.0;
   par.pOr.id = -1;
   par.pOr.father = -1;
   par.pDest.id = 0;
   par.pDest.father = 0;
   if (! extIsInZone (par.pOr, zone)) {
     infoMessage ("Origin point not in wind zone", GTK_MESSAGE_WARNING);
     return;
   }
   if (! extIsInZone (par.pDest, zone)) {
     infoMessage ("Destination point not in wind zone", GTK_MESSAGE_WARNING);
     return;
   }
   initStart (start);
   if (!calendar (start)) return;
   par.startTimeInHours = getDepartureTimeInHour (start); 
   if ((par.startTimeInHours < 0) || (par.startTimeInHours > zone.timeStamp [zone.nTimeStamp -1])) {
      infoMessage ("start time should be within grib window time !", GTK_MESSAGE_WARNING);
      return;
   }
   gettimeofday (&t0, NULL);
   ut0 = t0.tv_sec * MILLION + t0.tv_usec;
   lastClosest = par.pOr;
   route.kTime0 = (int) (par.startTimeInHours / (zone.timeStamp [1] - zone.timeStamp [0]));
   ret = routing (par.pOr, par.pDest, par.startTimeInHours, par.tStep, &lastStepDuration);
   if (ret == -1) {
      infoMessage ("Too many points in isochrone", GTK_MESSAGE_ERROR);
      return;
   }
   // printf ("lastStepDuration: %lf\n", lastStepDuration);
   gettimeofday (&t1, NULL);
   ut1 = t1.tv_sec * MILLION + t1.tv_usec;
   sprintf (line, "Process time in seconds                 : %.2f\n", ((double) ut1-ut0)/MILLION);
   strcat (buffer, line);
   route.destinationReached = (ret != NIL);
   if (ret != NIL) {
      storeRoute (par.pDest, lastStepDuration);
      if (strlen (par.dumpRFileName) > 0) dumpRoute (par.dumpRFileName, par.pDest);
   }
   else {
      storeRoute (lastClosest, lastStepDuration);
      if (strlen (par.dumpRFileName) > 0) dumpRoute (par.dumpRFileName, lastClosest);
   }
   if (strlen (par.dumpIFileName) > 0) dumpAllIsoc (par.dumpIFileName);
   if (! isfinite (route.totDist) || (route.totDist <= 1)) {
      infoMessage ("No route calculated. Check if wind !", GTK_MESSAGE_WARNING);
   }
   else {
      reportToStr (buffer);
      displayText (buffer, (ret != NIL) ? "Destination reached" : "Destination not reached");
   }
}

/*! Callback for stop button */
static void on_stop_button_clicked(GtkWidget *widget, gpointer data) {
   animation_active = FALSE;
}

/*! Callback for animation update */
static gboolean on_play_timeout(gpointer user_data) {
   //long deltaPlus = 3 * par.tStep / (zone.timeStamp [1]-zone.timeStamp [0]);
   //if (kTime < zone.nTimeStamp - 1 + deltaPlus) kTime += 1;
   if (kTime < zone.nTimeStamp - 1) kTime += 1;
   else animation_active = FALSE;
   gtk_widget_queue_draw(drawing_area);
   statusBarUpdate ();
   return animation_active;
}

/*! Callback for play button (animation) */
static void on_play_button_clicked(GtkWidget *widget, gpointer user_data) {
   if (!animation_active) {
      animation_active = TRUE;
      g_timeout_add (ANIMATION_TEMPO, (GSourceFunc)on_play_timeout, NULL);
   } else {
      animation_active = FALSE;
   }
   statusBarUpdate ();
}

/*! Callback for to start button */
static void on_to_start_button_clicked(GtkWidget *widget, gpointer data) {
   kTime = 0;
   theTime = zone.timeStamp [0];
   statusBarUpdate ();
   gtk_widget_queue_draw(drawing_area);
}

static void on_to_end_button_clicked(GtkWidget *widget, gpointer data) {
   kTime = zone.nTimeStamp - 1;
   theTime = zone.timeStamp [zone.nTimeStamp - 1];
   statusBarUpdate ();
   gtk_widget_queue_draw(drawing_area);
}

/*! Callback for reward button */
static void on_reward_button_clicked(GtkWidget *widget, gpointer data) {
   if (kTime > 0) kTime -= 1; 
   statusBarUpdate ();
   gtk_widget_queue_draw(drawing_area);
}

/*! Callback for Forward button */
static void on_forward_button_clicked(GtkWidget *widget, gpointer data) {
   //long deltaPlus = 3 * par.tStep / (zone.timeStamp [1]-zone.timeStamp [0]);
   //if (kTime < zone.nTimeStamp - 1 + deltaPlus) kTime += 1; 
   if (kTime < zone.nTimeStamp - 1) kTime += 1; 
   statusBarUpdate ();
   gtk_widget_queue_draw(drawing_area);
}

/*! display isochrone file */
static void isocDump (GtkWidget *widget, gpointer data) {
   char *buffer = NULL;
   const int N_POINT_FACTOR = 1000;
   if (nIsoc == 0)
      infoMessage ("No isochrone", GTK_MESSAGE_INFO); 
   else {
      //if ((buffer = (char *) malloc (nIsoc * N_POINT_IN_ISOC * MAX_SIZE_LINE)) == NULL)
      if ((buffer = (char *) malloc (nIsoc * MAX_SIZE_LINE * N_POINT_FACTOR)) == NULL)
         infoMessage ("Not enough memory", GTK_MESSAGE_ERROR); 
      else {
         allIsocToStr (buffer); 
         displayText (buffer, "Isochrones");
         free (buffer);
      }
   }
}

/*! print the report of route calculation */
static void rteReport (GtkWidget *widget, gpointer data) {
   char buffer [MAX_SIZE_BUFFER] = "";
   if (route.n <= 0 && route.destinationReached == false)
      infoMessage ("No route calculated", GTK_MESSAGE_INFO);
   else {
      /*if ((!isfinite (route.totDist)) || (route.totDist <= 0))
         infoMessage ("No route calculated: check if wind", GTK_MESSAGE_WARNING);
      else {*/
         reportToStr (buffer);  
         displayText (buffer, (route.destinationReached) ? "Destination reached" :\
            "Destination unreached");
      //}
   }
}

/*! print the route from origin to destination or best point */
static void rteDump (GtkWidget *widget, gpointer data) {
   char buffer [MAX_SIZE_BUFFER];
   if (route.n <= 0 && route.destinationReached == false)
      infoMessage ("No route calculated", GTK_MESSAGE_INFO);
   else {
      routeToStr (route, buffer); 
      displayText (buffer, (route.destinationReached) ? "Destination reached" :\
         "Destination unreached. Route to best point");
   }
}

/*! print the orthodromic and loxo routes through waypoints */
static void orthoDump (GtkWidget *widget, cairo_t *cr, gpointer data) {
   char buffer [MAX_SIZE_BUFFER] = "";
   calculateOrthoRoute ();
   wayPointToStr (buffer);
   displayText (buffer, "Orthodomic and Loxdromic Waypoint routes");
}

/*! show parameters information */
static void parDump (GtkWidget *widget, gpointer data) {
   char str [MAX_SIZE_LINE];
   writeParam (buildRootName (TEMP_FILE_NAME, str), true);
   displayFile (buildRootName (TEMP_FILE_NAME, str), "Parameter Dump");
}

/*! show Points of Interest information */
static void poiDump (GtkWidget *widget, gpointer data) {
   char buffer [MAX_SIZE_BUFFER] = "";
   poiToStr (buffer);
   displayText (buffer, "Point of Interest Table");
}

/*! edit poi */
static void poiEdit (GtkWidget *widget, gpointer data) {
   char line [MAX_SIZE_LINE] = "";
   sprintf (line, "%s %s\n", par.editor, par.poiFileName);
   printf ("poiEdit: %s\n", line);
   if (system (line) != 0) {
      fprintf (stderr, "Error in edit Poi. System call: %s\n", line);
      return;
   }
   if (confirm (par.poiFileName, "Confirm loading file below"))
      nPoi = readPoi (par.poiFileName);
}

/*! save Points of Interest information in file */
static void poiSave (GtkWidget *widget, gpointer data) {
   if (confirm (par.poiFileName, "Write"))
      writePoi (par.poiFileName);
}

/*! show GPS information */
static void gpsDump (GtkWidget *widget, gpointer data) {
   char buffer [MAX_SIZE_BUFFER] = "";
   if (gpsToStr (buffer))
      infoMessage (buffer, GTK_MESSAGE_INFO);
   else infoMessage ("No GPS data avalaible", GTK_MESSAGE_WARNING);
}

static void web_home_button_clicked (GtkWidget *widget, gpointer data) {
   WebKitWebView *webview = WEBKIT_WEB_VIEW(data);
   webkit_web_view_load_uri(webview, par.helpFileName);
}

void web_back_button_clicked (GtkButton *button, gpointer data) {
   WebKitWebView *webview = WEBKIT_WEB_VIEW (data);
   webkit_web_view_go_back (webview);
}

static void web_forward_button_clicked (GtkWidget *widget, gpointer data) {
   WebKitWebView *webview = WEBKIT_WEB_VIEW (data);
   webkit_web_view_go_forward (webview);
}

static void web_cli_button_clicked (GtkWidget *widget, gpointer data) {
   char str [MAX_SIZE_BUFFER] = "";
   WebKitWebView *webview = WEBKIT_WEB_VIEW (data);
   if (fileToStr (par.cliHelpFileName, str))
      webkit_web_view_load_plain_text (webview, str);
   else infoMessage ("cli text fle not found", GTK_MESSAGE_ERROR);
}

/*! launch help HTML file */
static void help (GtkWidget *widget, gpointer data) {
   char title [MAX_SIZE_LINE];
   sprintf  (title, "Help: %s", par.helpFileName);
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
   gtk_window_set_title(GTK_WINDOW(window), title);
   
   // Créer un conteneur de type boîte
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   gtk_container_add(GTK_CONTAINER(window), box);
   
   GtkWidget *homeButton = gtk_button_new_from_icon_name ("go-home", GTK_ICON_SIZE_BUTTON);
   GtkWidget *backButton = gtk_button_new_from_icon_name ("go-previous", GTK_ICON_SIZE_BUTTON);
   GtkWidget *forwardButton = gtk_button_new_from_icon_name ("go-next", GTK_ICON_SIZE_BUTTON);
   GtkWidget *cliButton = gtk_button_new_from_icon_name ("text-x-generic", GTK_ICON_SIZE_BUTTON);
   
   GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   WebKitWebView *webView = WEBKIT_WEB_VIEW(webkit_web_view_new());
   gtk_box_pack_start(GTK_BOX (hbox), homeButton, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), backButton, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), forwardButton, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), cliButton, FALSE, FALSE, 0);
   
   gtk_box_pack_start(GTK_BOX (box), hbox, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (box), GTK_WIDGET(webView), TRUE, TRUE, 0);
   
   g_signal_connect(G_OBJECT(homeButton), "clicked", G_CALLBACK(web_home_button_clicked), webView);
   g_signal_connect(G_OBJECT(backButton), "clicked", G_CALLBACK(web_back_button_clicked), webView);
   g_signal_connect(G_OBJECT(forwardButton), "clicked", G_CALLBACK(web_forward_button_clicked), webView);
   g_signal_connect(G_OBJECT(cliButton), "clicked", G_CALLBACK(web_cli_button_clicked), webView);

   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

   webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webView), par.helpFileName);
   gtk_widget_show_all(window);
   gtk_main();
}

/*! display info on the program */
static void helpInfo(GtkWidget *p_widget, gpointer user_data) {
   char str [MAX_SIZE_LINE];
   int major = gtk_get_major_version();
   int minor = gtk_get_minor_version();
   int micro = gtk_get_micro_version();
   char strVersion [MAX_SIZE_LINE];
   sprintf (strVersion, "%s\nGTK version: %d.%d.%d\nCompilation date: %s\n", PROG_VERSION, major, minor, micro, __DATE__);
   GtkWidget *p_about_dialog = gtk_about_dialog_new ();
   gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (p_about_dialog), strVersion);
   gtk_about_dialog_set_program_name  (GTK_ABOUT_DIALOG (p_about_dialog), PROG_NAME);
   const char *authors[2] = {PROG_AUTHOR, NULL};
   gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (p_about_dialog), authors);
   gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (p_about_dialog), PROG_WEB_SITE);
   GdkPixbuf *p_logo = p_logo = gdk_pixbuf_new_from_file (buildRootName (PROG_LOGO, str), NULL);
   gtk_about_dialog_set_logo (GTK_ABOUT_DIALOG (p_about_dialog), p_logo);
   gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG (p_about_dialog), DESCRIPTION);
   gtk_dialog_run (GTK_DIALOG (p_about_dialog));
}

/*! url selection */ 
static bool urlChange (char *url) {
   GtkWidget *dialog, *content_area;
   GtkWidget *url_entry;
   GtkDialogFlags flags;

   // Création de la boîte de dialogue
   flags = GTK_DIALOG_DESTROY_WITH_PARENT;
   dialog = gtk_dialog_new_with_buttons("URL", NULL, flags,
                                        "_OK", GTK_RESPONSE_ACCEPT,
                                        "_Cancel", GTK_RESPONSE_CANCEL,
                                        NULL);

   content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

   //url_label = gtk_label_new("");
   url_entry = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(url_entry), url);
   
   int min_width = MAX (300, strlen (url) * 10); 

   // Définir la largeur minimale de la boîte de dialogue
   gtk_widget_set_size_request(dialog, min_width, -1);
   //gtk_box_pack_start (GTK_BOX (content_area), url_label, FALSE, FALSE, 0);
   gtk_box_pack_start (GTK_BOX (content_area), url_entry, FALSE, FALSE, 0);

   // Affichage de la boîte de dialogue et récupération de la réponse
   gtk_widget_show_all (dialog);
   int response = gtk_dialog_run (GTK_DIALOG(dialog));
   
   // Récupération des valeurs saisies si la réponse est GTK_RESPONSE_ACCEPT
   if (response == GTK_RESPONSE_ACCEPT) {
       const char *pt = gtk_entry_get_text (GTK_ENTRY(url_entry));
       strcpy (url, pt);
       gtk_widget_queue_draw(drawing_area); // affiche le tout
   }
   // Fermeture de la boîte de dialogue
   gtk_widget_destroy(dialog);
   return (response == GTK_RESPONSE_ACCEPT);
}

/*! check if readGrib is terminated */
static gboolean readGribCheck (gpointer data) {
   statusBarUpdate ();
   // printf ("readGribCheck: %d\n", readGribRet);
   if (readGribRet == -1) { // not terminated
      return TRUE;
   }
   
   g_source_remove (gribReadTimeout); // timer stopped
   gtk_widget_destroy (spinner_window);
   if (readGribRet == 0) {
      infoMessage ("Error in readGribCheck (wind)", GTK_MESSAGE_ERROR);
   }
   else {
      kTime = 0;
      par.constWindTws = 0;
      initDispZone ();
      updatedColors = false; 
   }
   gtk_widget_queue_draw (drawing_area);
   return TRUE;
}

/*! check if readGrib is terminated */
static gboolean readCurrentGribCheck (gpointer data) {
   statusBarUpdate ();
   gtk_widget_queue_draw (drawing_area);
   if (readCurrentGribRet == -1) { // not terminated
      return TRUE;
   }
   g_source_remove (currentGribReadTimeout); // timer stopped
   gtk_widget_destroy (spinner_window);
   if (readCurrentGribRet == 0) {
      infoMessage ("Error in readCurrentGribCheck (current)", GTK_MESSAGE_ERROR);
   }
   return TRUE;
}
   
/*! open selected file */
static void loadGribFile (int type, char *fileName) {
   if (type == WIND) { // wind
      strncpy (par.gribFileName, fileName, MAX_SIZE_FILE_NAME - 1);
      readGribRet = -1; // Global variable
      g_thread_new("readGribThread", readGrib, NULL);
      gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readGribCheck, NULL);
      spinner ("Grib File decoding");
   }
   else {                 
      strncpy (par.currentGribFileName, fileName, MAX_SIZE_FILE_NAME - 1);
      readCurrentGribRet = -1; // Global variable
      g_thread_new("readCurrentGribThread", readCurrentGrib, NULL);
      currentGribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readCurrentGribCheck, NULL);
      spinner ("Current Grib File decoding");
   }
}

/*! Select grib file */
static void downloadGrib (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data;
   time_t t;
   struct tm *info;
   char url [MAX_SIZE_BUFFER];
   char fileName [MAX_SIZE_FILE_NAME];
   char outputFileName [MAX_SIZE_FILE_NAME] = WORKING_DIR;
   char remotePath [] = "mc2020/int/cartes/marine/grib/";
   time (&t);
   info = localtime (&t);
   strcat (outputFileName, "grib");
   if (*comportement == WIND)
      sprintf (fileName, "METEOCONSULT06Z_VENT_%02d%02d_Centre_Atlantique.grb", info->tm_mon + 1, info->tm_mday);  
   else
      sprintf (fileName, "METEOCONSULT00Z_COURANT_%02d%02d_Centre_Atlantique.grb", info->tm_mon + 1, info->tm_mday);  
   sprintf (url, "%s%s%s", ROOT_GRIB_SERVER, remotePath, fileName);
   printf ("url %s\n", url);
   if (urlChange (url)) {
      printf ("new url %s\n", url);
      strcat (outputFileName, strrchr (url, '/')); // le nom du fichier
      if (curlGet (url, outputFileName))
         loadGribFile (*comportement, outputFileName);
      else infoMessage ("Error dowloading file", GTK_MESSAGE_ERROR);
   }
}

/*! Select grib file and read grib file, fill gribData */
static void openGrib (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data;
   char directory [MAX_SIZE_LINE] = WORKING_DIR;
   strcat (directory, "grib");

   GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open Grib", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, \
    "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), directory);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Grib Files");
   gtk_file_filter_add_pattern(filter, "*.gr*");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      char *fileName;
      GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
      fileName = gtk_file_chooser_get_filename(chooser);
      gtk_widget_destroy(dialog);

      GtkWidget *window;
      GtkWidget *scrolled_window;
      GtkWidget *text_view;
      // GtkTextBuffer *buffer;
      window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(window), "GRIB Files");
      gtk_window_set_default_size(GTK_WINDOW(window), 200, 200);

      scrolled_window = gtk_scrolled_window_new (NULL, NULL);
      gtk_container_add(GTK_CONTAINER(window), scrolled_window);

      text_view = gtk_text_view_new();
      gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);
      loadGribFile (*comportement, fileName);
   }
   else gtk_widget_destroy(dialog);
}

/*! Select polar file and read polar file, fill polMat */
static void openPolar (GtkWidget *widget, gpointer data) {
   char *fileName;
   char directory [MAX_SIZE_LINE] = WORKING_DIR;
   strcat (directory, "pol");
   GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Polar", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, \
    "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

   gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Polar Files");
   gtk_file_filter_add_pattern(filter, "*.pol");
   gtk_file_filter_add_pattern(filter, "*.csv");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), directory);

   if (gtk_dialog_run (GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
      fileName = gtk_file_chooser_get_filename(chooser);
      gtk_widget_destroy(dialog);

      GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(window), "Polar Files");
      gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);

      GtkWidget *scrolled_window = gtk_scrolled_window_new (NULL, NULL);
      gtk_container_add (GTK_CONTAINER(window), scrolled_window);

      GtkWidget *text_view = gtk_text_view_new();
      gtk_container_add (GTK_CONTAINER(scrolled_window), text_view);

      if (strstr (fileName, "polwave.csv") == NULL) {
         readPolar (fileName, &polMat);
         strcpy (par.polarFileName, fileName);
         polarType = POLAR;
         polarDraw (POLAR); 
      }
      else {
         readPolar (fileName, &wavePolMat);
         strcpy (par.wavePolFileName, fileName);
         polarType = WAVE_POLAR;
         polarDraw (WAVE_POLAR); 
      }
    
  }
  else {
     gtk_widget_destroy (dialog);
  }
}

/*! Write parameter file and initialize context */
static void saveScenario (GtkWidget *widget, gpointer data) {
   char directory [MAX_SIZE_NAME] = WORKING_DIR;
   strcat (directory, "par");
   GtkWidget *dialog = gtk_file_chooser_dialog_new("Save As",
                                        NULL,
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        "_Cancel",
                                        GTK_RESPONSE_CANCEL,
                                        "_Save",
                                        GTK_RESPONSE_ACCEPT,
                                        NULL);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), directory);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name (filter, "Parameter Files");
   gtk_file_filter_add_pattern(filter, "*.par");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
   GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);

   // Désactive la confirmation d'écrasement du fichier existant
   gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

   // Définit un nom de fichier par défaut (peut être vide)
   gtk_file_chooser_set_current_name(chooser, "new_file.par");

   int result = gtk_dialog_run(GTK_DIALOG(dialog));

   if (result == GTK_RESPONSE_ACCEPT) {
      char *fileName = gtk_file_chooser_get_filename(chooser);
      writeParam (fileName, false);
      g_free(fileName);
   }

   gtk_widget_destroy(dialog);
}

/*! Edit parameter file  */
static void editScenario (GtkWidget *widget, gpointer data) {
   char line [MAX_SIZE_LINE] = "";
   sprintf (line, "%s %s\n", par.editor, PARAMETERS_FILE);
   if (system (line) != 0) {
      fprintf (stderr, "Error in editScenario. System call: %s\n", line);
      return;
   }
   if (confirm (PARAMETERS_FILE, "Confirm loading file below")) {
      readParam (PARAMETERS_FILE);
      readGrib (NULL);
      if (readGribRet == 0) {
         infoMessage ("Error in readgrib", GTK_MESSAGE_ERROR);
         return;
      }
      //theTime = zone.timeStamp [0];
      updatedColors = false; 
         
      initDispZone ();
      readPolar (par.polarFileName, &polMat);
      readPolar (par.wavePolFileName, &wavePolMat);
   }
}

/*! Read parameter file and initialize context */
static void openScenario (GtkWidget *widget, gpointer data) {
   char directory [MAX_SIZE_LINE] = WORKING_DIR;
   char *fileName;
   strcat (directory, "par");
   GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Parameters", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, \
      "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), directory);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Parameter Files");
   gtk_file_filter_add_pattern(filter, "*.par");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

   if (gtk_dialog_run (GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
      fileName = gtk_file_chooser_get_filename(chooser);
      gtk_widget_destroy(dialog);
      readParam (fileName);
      printf ("openScenario: %s\n", fileName);
      readGrib (NULL);
      if (readGribRet == 0) {
         infoMessage ("Error in readgrib", GTK_MESSAGE_ERROR);
         return;
      }
      //theTime = zone.timeStamp [0];
      updatedColors = false; 
         
      initDispZone ();
      readPolar (par.polarFileName, &polMat);
      readPolar (par.wavePolFileName, &wavePolMat);
   }
   else {
      gtk_widget_destroy (dialog);
   }
}

/* provides meta information about grib file */
static void gribInfo (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data; // either WIND or CURRENT
   char buffer [MAX_SIZE_BUFFER] = "";
   char buffer1 [MAX_SIZE_BUFFER] = "";
   char line [MAX_SIZE_LINE] = "";
   if (*comportement == WIND) {
      if (zone.nbLat == 0)
         infoMessage ("No wind data grib available", GTK_MESSAGE_ERROR);
      else {
         gribToStr (buffer, zone);
         sprintf (line, "Size of grib file %s: %ld bytes\n\n", par.gribFileName, \
            getFileSize (par.gribFileName));
         strcat (buffer, line); 
         if ((!zone.wellDefined ) || (zone.nShortName < 2))
            strcat (buffer, "No wind data grib check possible");
            else {
               checkGribToStr (buffer1, zone, gribData);
               strcat (buffer, buffer1);
            } 
         // printf ("%s\n", buffer);
         displayText (buffer, "Grib Wind Information");
      }
   }
   else { // current
      if (currentZone.nbLat == 0)
         infoMessage ("No current data grib available", GTK_MESSAGE_ERROR);
      else {
         gribToStr (buffer, currentZone);
         sprintf (line, "Size of grib file %s: %ld bytes\n\n", par.currentGribFileName, \
            getFileSize (par.currentGribFileName));
         strcat (buffer, line); 
         if ((!currentZone.wellDefined) || (currentZone.nShortName < 2)) 
            strcat (buffer, "No current data grib check possible");
         else {
            strcat (buffer, "value check...\n");
            checkGribToStr (buffer1, currentZone, currentGribData);
            strcat (buffer, buffer1);
         }
         displayText (buffer, "Grib Current Information");
      }
   }
}

/*! read mail grib received from provider */
static gboolean mailGribRead (gpointer data) {
   FILE *fp;
   char command [MAX_SIZE_LINE *  2];
   if (! gribRequestRunning) {
      return TRUE;
   }
   char line[MAX_SIZE_LINE] = "";
   char buffer[MAX_SIZE_BUFFER] = "\n";
   int n = 0;
   char *fileName, *end;
   sprintf (command, "%s %sgrib %s", par.imapScript, WORKING_DIR, par.mailPw); 
   fp = popen (command, "r");
   if (fp == NULL) {
      fprintf (stderr, "mailGribRead Error opening: %s\n", command);
      gribRequestRunning = false;
      gtk_widget_destroy (spinner_window);
      return TRUE;
   }
   while (fgets(line, sizeof(line)-1, fp) != NULL) {
      n += 1;
      strcat (buffer, line);
   }
   pclose(fp);
   if (n > 0) {
      g_source_remove (gribMailTimeout); // timer stopped
      if (confirm (buffer, "Confirm loading file below")) {
         if ((fileName = strstr (buffer, "File: /") + 6) != NULL) {
            while (isspace (*fileName)) fileName += 1;
            if ((end = strstr (fileName, " ")) != NULL)
               *(end) = '\0';
            if (spinner_window != NULL) gtk_widget_destroy (spinner_window);
            loadGribFile ((provider == SAILDOCS_CURR) ? CURRENT: WIND, fileName);
         }
      }
      else if (spinner_window != NULL) gtk_widget_destroy (spinner_window);
      gribRequestRunning = false;
   }
   return TRUE;
}

/*! Callback radio button selecting provider for mail request */
static void radio_button_toggled(GtkToggleButton *button, gpointer user_data) {
   provider = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   printf ("Provider: %d\n", provider);
}

GtkWidget *createRadioButtonProvider (char *name, GtkWidget *hbox, GtkWidget *from, int i) {
   GtkWidget *choice;
   if (from == NULL) choice = gtk_radio_button_new_with_label(NULL, name );
   else choice = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(from), name);
   g_object_set_data(G_OBJECT(choice), "index", GINT_TO_POINTER(i));
   g_signal_connect(choice, "toggled", G_CALLBACK(radio_button_toggled), NULL);
   if (i == 0)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(choice), TRUE);
   gtk_box_pack_start(GTK_BOX(hbox), choice, FALSE, FALSE, 0);
   return choice;
}

/*! Callback Time Step value */
static void time_step_changed(GtkSpinButton *spin_button, gpointer user_data) {
    par.gribTimeStep = gtk_spin_button_get_value_as_int(spin_button);
}

/*! Callback Time Max value */
static void time_max_changed(GtkSpinButton *spin_button, gpointer user_data) {
    par.gribTimeMax = gtk_spin_button_get_value_as_int(spin_button);
}

/*! mail request dialog box */
static int mailRequestBox (const char *buffer) {
   GtkWidget *dialog;
   GtkWidget *content_area;
   GtkWidget *label_buffer;
   GtkWidget *hbox0, *hbox1, *hbox2;
   GtkWidget *label_time_step, *label_time_max;
   GtkWidget *spin_button_time_step, *spin_button_time_max;
   GtkWidget *choice [MAX_N_SMTP_TO];
   provider = 0;

   // Création de la boîte de dialogue
   dialog = gtk_dialog_new_with_buttons("Launch mail Grib request", \
      GTK_WINDOW(window), GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);

    // Récupération de la zone de contenu de la boîte de dialogue
   content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

   label_buffer = gtk_label_new (buffer);
   
   // Création des champs Time Step et Time Max
   label_time_step = gtk_label_new("Time Step:");
   label_time_max = gtk_label_new("Time Max:");
   spin_button_time_step = gtk_spin_button_new_with_range(0, 24, 3);
   spin_button_time_max = gtk_spin_button_new_with_range(24, 384, 6);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_button_time_step), par.gribTimeStep);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_button_time_max), par.gribTimeMax);
   g_signal_connect(spin_button_time_step, "value-changed", G_CALLBACK(time_step_changed), NULL);
   g_signal_connect(spin_button_time_max, "value-changed", G_CALLBACK(time_max_changed), NULL);

   // Placement des éléments dans la boîte de dialogue
   hbox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

   gtk_box_pack_start(GTK_BOX(hbox0), label_buffer, FALSE, FALSE, 0);
   
   // Création des boutons radio
   if (par.nSmtp > 0)
      choice [0] = createRadioButtonProvider (par.smtpName [0], hbox1, NULL, 0);
   for (int i = 1; i < par.nSmtp; i++)
      choice [i] = createRadioButtonProvider (par.smtpName [i], hbox1, choice [i-1], i);

   gtk_box_pack_start(GTK_BOX(hbox2), label_time_step, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox2), spin_button_time_step, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox2), label_time_max, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox2), spin_button_time_max, FALSE, FALSE, 0);

   gtk_box_pack_start(GTK_BOX(content_area), hbox0, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(content_area), hbox1, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(content_area), hbox2, FALSE, FALSE, 0);

   gtk_widget_show_all(dialog);
   int result = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
   return result;
}

/*! display label in col c and ligne l of tab*/
static void labelCreate (GtkWidget *tab, char * name, int c, int l) {
   GtkWidget *label = gtk_label_new (name);
   gtk_grid_attach(GTK_GRID(tab), label, c, l, 1, 1);
   gtk_widget_set_margin_start(label, 10);
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
}

/*! For point of origin */
void on_entry_focus_out_event(GtkWidget *entry, GdkEvent *event, gpointer user_data) {
   const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
   double lat, lon;
   int poiIndex = -1;
   if ((strlen (name) >= MIN_NAME_SIZE) && ((poiIndex = findPoiByName (name, &lat, &lon)) != -1))
      gtk_entry_set_text(GTK_ENTRY(entry), tPoi [poiIndex].name);
   route.n = 0;
}

void radio_button_Colors (GtkRadioButton *button, gpointer user_data) {
   par.showColors = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw(drawing_area); // affiche le tout
}

GtkWidget *createRadioButtonColors (char *name, GtkWidget *hbox, GtkWidget *from, int i) {
   GtkWidget *choice;
   if (from == NULL) choice = gtk_radio_button_new_with_label(NULL, name );
   else choice = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(from), name);
   g_object_set_data(G_OBJECT(choice), "index", GINT_TO_POINTER(i));
   g_signal_connect(choice, "toggled", G_CALLBACK(radio_button_Colors), NULL);
   gtk_grid_attach(GTK_GRID(tab_display), choice,           i+1, 1, 1, 1);
   if (i == par.showColors)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(choice), TRUE);
   return choice;
}

// wind representation
void radio_button_Wind_Disp (GtkRadioButton *button, gpointer user_data) {
   par.windDisp = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw(drawing_area); // affiche le tout
}

GtkWidget *createRadioButtonWindDisp (char *name, GtkWidget *hbox, GtkWidget *from, int i) {
   GtkWidget *choice;
   if (from == NULL) choice = gtk_radio_button_new_with_label(NULL, name );
   else choice = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(from), name);
   g_object_set_data(G_OBJECT(choice), "index", GINT_TO_POINTER(i));
   g_signal_connect(choice, "toggled", G_CALLBACK(radio_button_Wind_Disp), NULL);
   gtk_grid_attach(GTK_GRID(tab_display), choice,           i+1, 2, 1, 1);
   if (i == par.windDisp)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(choice), TRUE);
   return choice;
}

void radio_button_Isoc (GtkRadioButton *button, gpointer user_data) {
   par.style = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw(drawing_area); // affiche le tout
 }

GtkWidget *createRadioButtonIsoc (char *name, GtkWidget *hbox, GtkWidget *from, int i) {
   GtkWidget *choice;
   if (from == NULL) choice = gtk_radio_button_new_with_label(NULL, name );
   else choice = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(from), name);
   g_object_set_data(G_OBJECT(choice), "index", GINT_TO_POINTER(i));
   g_signal_connect(choice, "toggled", G_CALLBACK(radio_button_Isoc), NULL);
   gtk_grid_attach(GTK_GRID(tab_display), choice,           i+1, 3, 1, 1);
   if (i == par.style)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(choice), TRUE);
   return choice;
}

void radio_button_Dms (GtkRadioButton *button, gpointer user_data) {
par.dispDms = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw(drawing_area); // affiche le tout
   statusBarUpdate ();
}

GtkWidget *createRadioButtonDms (char *name, GtkWidget *hbox, GtkWidget *from, int i) {
   GtkWidget *choice;
   if (from == NULL) choice = gtk_radio_button_new_with_label(NULL, name );
   else choice = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(from), name);
   g_object_set_data(G_OBJECT(choice), "index", GINT_TO_POINTER(i));
      g_signal_connect(choice, "toggled", G_CALLBACK(radio_button_Dms), NULL);
      gtk_grid_attach(GTK_GRID(tab_display), choice,           i+1, 4, 1, 1);
      if (i == par.dispDms)
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(choice), TRUE);
      return choice;
   }

// Fonction appelée lorsqu'une boîte à cocher est activée/désactivée
void on_checkbox_toggled(GtkWidget *checkbox, gpointer user_data) {
   gboolean *flag = (gboolean *)user_data;
   *flag = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox));
   gtk_widget_queue_draw(drawing_area);
}
  
/*! change parameters and pOr and pDest coordinates */ 
static void change (GtkWidget *widget, gpointer data) {
   char str0 [50], str1 [50];
   GtkWidget *dialog, *content_area;
   GtkWidget *entry_origin_lat, *entry_origin_lon;
   GtkWidget *entry_dest_lat, *entry_dest_lon;
   GtkWidget *entry_start_time, *entry_time_step;
   GtkWidget *entry_wind_twd, *entry_wind_tws;
   GtkWidget *entry_current_twd, *entry_current_tws;
   GtkWidget *entry_wave;
   GtkWidget *entry_penalty0, *entry_penalty1;
   GtkWidget *entry_sog, *entry_threshold;
   GtkWidget *entry_efficiency;
   
   GtkCellRenderer *myRenderer;

   // Création de la boîte de dialogue
   dialog = gtk_dialog_new_with_buttons("Change", NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_CANCEL, NULL);

   content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

   // Créer une boîte à onglets
   GtkWidget *notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(content_area), notebook);

   // Onglet "Technique"
   GtkWidget *tab_tec = gtk_grid_new();
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_tec, gtk_label_new("Technical"));
   
   gtk_grid_set_row_spacing(GTK_GRID(tab_tec), 5);
   gtk_grid_set_column_spacing(GTK_GRID(tab_tec), 5);

   // Onglet "Display"
   tab_display = gtk_grid_new();
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_display, gtk_label_new("Display"));
   gtk_widget_set_halign(GTK_WIDGET(tab_display), GTK_ALIGN_START);
   gtk_widget_set_valign(GTK_WIDGET(tab_display), GTK_ALIGN_START);
   gtk_grid_set_row_spacing(GTK_GRID(tab_display), 20);
   gtk_grid_set_column_spacing(GTK_GRID(tab_display), 5);

   // Création des éléments pour l'origine
   entry_origin_lat = gtk_entry_new();
   entry_origin_lon = gtk_entry_new();
   char originLatStr[64], originLonStr[64];
   if (strlen (par.pOrName) > 0) {
      snprintf (originLatStr, sizeof(originLatStr), "%s", par.pOrName);
      snprintf (originLonStr, sizeof(originLonStr), "%s", "NA");
   }      
   else {
      snprintf(originLatStr, sizeof(originLatStr), "%.2f", par.pOr.lat);
      snprintf(originLonStr, sizeof(originLonStr), "%.2f", par.pOr.lon);
   }
   gtk_entry_set_text(GTK_ENTRY(entry_origin_lat), originLatStr);
   gtk_entry_set_text(GTK_ENTRY(entry_origin_lon), originLonStr);

   labelCreate (tab_tec, "Origin Lat",                  0, 0);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_origin_lat, 1, 0, 1, 1);
   labelCreate (tab_tec, "Lon",                         2, 0);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_origin_lon, 3, 0, 1, 1);
   g_signal_connect(G_OBJECT(entry_origin_lat), "focus-out-event", G_CALLBACK(on_entry_focus_out_event), NULL);

   // Création des éléments pour la destination
   entry_dest_lat = gtk_entry_new();
   entry_dest_lon = gtk_entry_new();
   char destLatStr[64], destLonStr[64];
   if (strlen (par.pDestName) > 0) {
      snprintf (destLatStr, sizeof(destLatStr), "%s", par.pDestName);
      snprintf (destLonStr, sizeof(destLonStr), "%s", "NA");
   }
   else {
      snprintf(destLatStr, sizeof(destLatStr), "%.2f", par.pDest.lat);
      snprintf(destLonStr, sizeof(destLonStr), "%.2f", par.pDest.lon);
   }
   gtk_entry_set_text(GTK_ENTRY(entry_dest_lat), destLatStr);
   gtk_entry_set_text(GTK_ENTRY(entry_dest_lon), destLonStr);
   
   labelCreate (tab_tec, "Destination Lat",             0, 1);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_dest_lat,   1, 1, 1, 1);
   labelCreate (tab_tec, "Lon",                         2, 1);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_dest_lon,   3, 1, 1, 1);
   // Connexion de la fonction de rappel au signal "focus-out-event"
   g_signal_connect(G_OBJECT(entry_dest_lat), "focus-out-event", G_CALLBACK(on_entry_focus_out_event), NULL);

   // Création des éléments pour cog et cog step
   GtkWidget *spin_cog = gtk_spin_button_new_with_range(1, 20, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_cog), par.cogStep);
   GtkWidget *spin_range = gtk_spin_button_new_with_range(50, 100, 5);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_range), par.rangeCog);
   labelCreate (tab_tec, "Cog Step",                   0, 2);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_cog,        1, 2, 1, 1);
   labelCreate (tab_tec, "Cog Range",                  2, 2);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_range,      3, 2, 1, 1);

   // Création des éléments pour start time et time step
   entry_start_time = gtk_entry_new();
   entry_time_step = gtk_entry_new();
   labelCreate (tab_tec, "Start Time in hours",         0, 3);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_start_time, 1, 3, 1, 1);
   labelCreate (tab_tec, "Time Step",                   2, 3);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_time_step,  3, 3, 1, 1);
   
   // Pré-remplissage avec les valeurs de départ
   char startTimeStr [50], timeStep [50];
   snprintf(startTimeStr, sizeof(startTimeStr), "%.2lf", par.startTimeInHours);
   snprintf(timeStep, sizeof(timeStep), "%.2lf", par.tStep);
   gtk_entry_set_text(GTK_ENTRY(entry_start_time), startTimeStr);
   gtk_entry_set_text(GTK_ENTRY(entry_time_step), timeStep);

   // Création des éléments pour Opt
   GtkWidget *spin_opt = gtk_spin_button_new_with_range(0, 4, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_opt), par.opt);
   labelCreate (tab_tec, "Opt",                 0, 4);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_opt, 1, 4, 1, 1);
   
   // Création des éléments pour minPt et Isoc
   labelCreate (tab_tec, "minPt/sector",                         0, 5);
   
   GtkListStore *liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
   // Ajouter des éléments à la liste
   GtkTreeIter iter;
   char str [2];
   for (int minPt = 0; minPt < 10; minPt++) {
      gtk_list_store_append(liststore, &iter);
      sprintf (str, "%d", minPt);
      gtk_list_store_set(liststore, &iter, 0, str,         1, minPt, -1);
   }

   // Créer une combobox et définir son modèle
   GtkWidget *opt_combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
   g_object_unref(liststore);

   // Créer un rendu de texte pour afficher le texte dans la combobox
   myRenderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(opt_combo_box), myRenderer, TRUE);
   gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(opt_combo_box), myRenderer, "text", 0, NULL);
   gtk_combo_box_set_active(GTK_COMBO_BOX(opt_combo_box), par.minPt);
   gtk_grid_attach(GTK_GRID(tab_tec), opt_combo_box,      1, 5, 1, 1);

   labelCreate (tab_tec, "Max Isoc",                      2, 5);
   GtkWidget *spin_max_iso = gtk_spin_button_new_with_range(0, MAX_N_ISOC, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_max_iso), par.maxIso);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_max_iso,       3, 5, 1, 1);
   
   // Création des éléments pour sector
   GtkWidget *spin_kFactor = gtk_spin_button_new_with_range(5, 200, 5);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_kFactor), par.kFactor);
   GtkWidget *spin_n_sectors = gtk_spin_button_new_with_range(10, 1000, 10);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_n_sectors), par.nSectors);
   labelCreate (tab_tec, "k Factor",                    0, 6);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_kFactor,     1, 6, 1, 1);
   labelCreate (tab_tec, "N sectors",                   2, 6);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_n_sectors,   3, 6, 1, 1);
   
   // Création des éléments pour constWind
   entry_wind_twd = gtk_entry_new();
   entry_wind_tws = gtk_entry_new();
   labelCreate (tab_tec, "Const Wind Twd",            0, 7);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_wind_twd, 1, 7, 1, 1);
   labelCreate (tab_tec, "Const Wind Tws",            2, 7);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_wind_tws, 3, 7, 1, 1);
   
   // Pré-remplissage avec les valeurs de départ
   snprintf(str0, sizeof(str0), "%.2f", par.constWindTwd);
   snprintf(str1, sizeof(str1), "%.2f", par.constWindTws);
   gtk_entry_set_text(GTK_ENTRY(entry_wind_twd), str0);
   gtk_entry_set_text(GTK_ENTRY(entry_wind_tws), str1);

   // Création des éléments pour constCurrent
   entry_current_twd = gtk_entry_new();
   entry_current_tws = gtk_entry_new();
   labelCreate (tab_tec, "Const Current Twd",            0, 8);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_current_twd, 1, 8, 1, 1);
   labelCreate (tab_tec, "Const Current Tws",            2, 8);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_current_tws, 3, 8, 1, 1);
   
   // Pré-remplissage avec les valeurs de départ
   snprintf(str0, sizeof(str0), "%.2f", par.constCurrentD);
   snprintf(str1, sizeof(str1), "%.2f", par.constCurrentS);
   gtk_entry_set_text(GTK_ENTRY(entry_current_twd), str0);
   gtk_entry_set_text(GTK_ENTRY(entry_current_tws), str1);

   // Création des éléments pour penalty
   entry_penalty0 = gtk_entry_new();
   entry_penalty1 = gtk_entry_new();
   labelCreate (tab_tec, "Virement de bord",          0, 9);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_penalty0, 1, 9, 1, 1);
   labelCreate (tab_tec, "Empannage",                 2, 9);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_penalty1, 3, 9, 1, 1);
   
   // Pré-remplissage avec les valeurs de départ
   snprintf(str0, sizeof(str0), "%.2f", par.penalty0);
   snprintf(str1, sizeof(str1), "%.2f", par.penalty1);
   gtk_entry_set_text(GTK_ENTRY(entry_penalty0), str0);
   gtk_entry_set_text(GTK_ENTRY(entry_penalty1), str1);

   // Création des éléments pour motorSpeed, threshold
   entry_sog = gtk_entry_new ();
   entry_threshold = gtk_entry_new();
   
   // Pré-remplissage avec les valeurs de départ
   snprintf(str0, sizeof(str0), "%.2f", par.motorSpeed);
   gtk_entry_set_text(GTK_ENTRY(entry_sog), str0);
   snprintf(str1, sizeof(str1), "%.2f", par.threshold);
   gtk_entry_set_text(GTK_ENTRY(entry_threshold), str1);

   labelCreate (tab_tec, "Motor Speed          ",          0, 10);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_sog,           1, 10, 1, 1);
   labelCreate (tab_tec, "Threshold for Motor",            2, 10);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_threshold,     3, 10, 1, 1);

   // Création des éléments pour constWave, efficiency
   entry_wave = gtk_entry_new();
   entry_efficiency = gtk_entry_new();

   snprintf(str0, sizeof(str0), "%.2f", par.constWave);
   gtk_entry_set_text(GTK_ENTRY(entry_wave), str0);
   snprintf(str1, sizeof(str1), "%.2f", par.efficiency);
   gtk_entry_set_text(GTK_ENTRY(entry_efficiency), str1);

   labelCreate (tab_tec, "Const Wave Height",              0, 11);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_wave,          1, 11, 1, 1);
   labelCreate (tab_tec, "Efficiency",                     2, 11);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_efficiency,    3, 11, 1, 1);

   GtkWidget *hbox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

   labelCreate (tab_display, "",              0, 0); // just for spoce on top
   // showColor

   hbox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   labelCreate (tab_display, "Colors",                     0, 1);
   GtkWidget *choice0 = createRadioButtonColors ("None", hbox0, NULL, 0);
   GtkWidget *choice1 = createRadioButtonColors ("B.& W.", hbox0, choice0, 1);
   GtkWidget *choice2 = createRadioButtonColors ("Colored", hbox0, choice1, 2);
   
   hbox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   labelCreate (tab_display, "Wind",                       0, 2);
   choice0 = createRadioButtonWindDisp ("None", hbox0, NULL, 0);
   choice1 = createRadioButtonWindDisp ("Arrow", hbox0, choice0, 1);
   createRadioButtonWindDisp ("Barbule", hbox0, choice1, 2);
   
   labelCreate (tab_display, "Isochrones",                 0, 3);
   choice0 = createRadioButtonIsoc ("None", hbox0, NULL, 0);
   choice1 = createRadioButtonIsoc ("Points", hbox0, choice0, 1);
   choice2 = createRadioButtonIsoc ("Segment", hbox0, choice1, 2);
   createRadioButtonIsoc ("Bézier", hbox0, choice2, 3);
   
   hbox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   labelCreate (tab_display, "DMS",                        0, 4);
   choice0 = createRadioButtonDms ("Basic", hbox0, NULL, 0);
   choice1 = createRadioButtonDms ("Degree", hbox0, choice0, 1);
   choice2 = createRadioButtonDms ("Deg Min", hbox0, choice1, 2);
   createRadioButtonDms ("Deg. Min. Sec.", hbox0, choice2, 3);

   GtkWidget *checkboxWave = gtk_check_button_new_with_label("Waves");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkboxWave), par.waveDisp);
   g_signal_connect(G_OBJECT(checkboxWave), "toggled", G_CALLBACK(on_checkbox_toggled), &par.waveDisp);
   gtk_grid_attach(GTK_GRID(tab_display), checkboxWave,           0, 5, 1, 1);

   // Boîte à cocher pour "courant"
   GtkWidget *checkboxCurrent = gtk_check_button_new_with_label("Current");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkboxCurrent), par.currentDisp);
   g_signal_connect(G_OBJECT(checkboxCurrent), "toggled", G_CALLBACK(on_checkbox_toggled), &par.currentDisp);
   gtk_grid_attach(GTK_GRID(tab_display), checkboxCurrent,        2, 5, 1, 1);

   // Boîte à cocher pour "closest"
   GtkWidget *checkboxClosest = gtk_check_button_new_with_label("Closest");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkboxClosest), par.closestDisp);
   g_signal_connect(G_OBJECT(checkboxClosest), "toggled", G_CALLBACK(on_checkbox_toggled), &par.closestDisp);
   gtk_grid_attach(GTK_GRID(tab_display), checkboxClosest,        0, 6, 1, 1);

   // Boîte à cocher pour "focal point"
   GtkWidget *checkboxFocal = gtk_check_button_new_with_label("Focal Point");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkboxFocal), par.focalDisp);
   g_signal_connect(G_OBJECT(checkboxFocal), "toggled", G_CALLBACK(on_checkbox_toggled), &par.focalDisp);
   gtk_grid_attach(GTK_GRID(tab_display), checkboxFocal,          2, 6, 1, 1);

   // Affichage de la boîte de dialogue et récupération de la réponse
   gtk_widget_show_all(dialog);
   int response = gtk_dialog_run(GTK_DIALOG(dialog));
   
   // Récupération des valeurs saisies si la réponse est GTK_RESPONSE_ACCEPT
   if (response == GTK_RESPONSE_ACCEPT) {
      const char *ptOr = gtk_entry_get_text (GTK_ENTRY(entry_origin_lat));
      int poiIndex = -1;
      
      if (! isNumber (ptOr)) {
         if ((poiIndex = findPoiByName (ptOr, &par.pOr.lat, &par.pOr.lon)) != -1)
            strcpy (par.pOrName, tPoi [poiIndex].name);
         else par.pOrName [0]= '\0';
      }  
      else if (strlen (ptOr) >= MIN_NAME_SIZE) {
         par.pOr.lat = atof (gtk_entry_get_text(GTK_ENTRY(entry_origin_lat)));
         par.pOr.lon= atof (gtk_entry_get_text(GTK_ENTRY(entry_origin_lon)));
      }
      if (par.pOr.lon > 180) par.pOr.lon -= 360;

      const char *ptDest = gtk_entry_get_text (GTK_ENTRY(entry_dest_lat));
      if (! isNumber (ptDest)) {
         if ((poiIndex = findPoiByName (ptDest, &par.pDest.lat, &par.pDest.lon)) != -1)
            strcpy (par.pDestName, tPoi [poiIndex].name);
         else par.pDestName [0]= '\0';
      }  
      else {
         par.pDest.lat = atof (gtk_entry_get_text(GTK_ENTRY(entry_dest_lat)));
         par.pDest.lon = atof (gtk_entry_get_text(GTK_ENTRY(entry_dest_lon)));
      }
      if (par.pDest.lon > 180) par.pDest.lon -= 360;
      par.cogStep = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_cog));
      par.rangeCog = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_range));
      par.startTimeInHours = atof (gtk_entry_get_text(GTK_ENTRY(entry_start_time)));
      par.tStep = atof (gtk_entry_get_text(GTK_ENTRY(entry_time_step)));
      par.opt = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_opt));
      par.minPt = gtk_combo_box_get_active(GTK_COMBO_BOX(opt_combo_box));
      par.maxIso = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_max_iso));
      if (par.maxIso > MAX_N_ISOC) par.maxIso = MAX_N_ISOC;
      par.kFactor = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_kFactor));
      par.nSectors = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_n_sectors));
      par.constWindTwd = atof (gtk_entry_get_text(GTK_ENTRY(entry_wind_twd)));
      par.constWindTws = atof (gtk_entry_get_text(GTK_ENTRY(entry_wind_tws)));
      if (par.constWindTws != 0)
	      initConst ();
      par.constCurrentD = atof (gtk_entry_get_text(GTK_ENTRY(entry_current_twd)));
      par.constCurrentS = atof (gtk_entry_get_text(GTK_ENTRY(entry_current_tws)));
      par.penalty0 = atof (gtk_entry_get_text(GTK_ENTRY(entry_penalty0)));
      par.penalty1 = atof (gtk_entry_get_text(GTK_ENTRY(entry_penalty1)));
      par.motorSpeed = atof (gtk_entry_get_text(GTK_ENTRY(entry_sog)));
      par.threshold = atof (gtk_entry_get_text(GTK_ENTRY(entry_threshold)));
      par.efficiency = atof (gtk_entry_get_text(GTK_ENTRY(entry_efficiency)));
      par.constWave = atof (gtk_entry_get_text(GTK_ENTRY(entry_wave)));
      gtk_widget_queue_draw(drawing_area); // affiche le tout
   }
   // Fermeture de la boîte de dialogue
   gtk_widget_destroy(dialog);
}
   
/*! zoom in */ 
static void on_zoom_in_button_clicked (GtkWidget *widget, gpointer data) {
   dispZoom (0.8);
   gtk_widget_queue_draw (drawing_area);
}

/*! zoom out */ 
static void on_zoom_out_button_clicked (GtkWidget *widget, gpointer data) {
   dispZoom (1.2);
   gtk_widget_queue_draw (drawing_area);
}

/*! zoom original */ 
static void on_zoom_original_button_clicked (GtkWidget *widget, gpointer data) {
   initDispZone ();
   gtk_widget_queue_draw (drawing_area);
}

/*! go up button */ 
static void on_up_button_clicked (GtkWidget *widget, gpointer data) {
   dispTranslate (0.5, 0);
   gtk_widget_queue_draw (drawing_area);
}

/*! go down button */ 
static void on_down_button_clicked (GtkWidget *widget, gpointer data) {
   dispTranslate (-0.5, 0);
   gtk_widget_queue_draw (drawing_area);
}

/*! go left button */ 
static void on_left_button_clicked (GtkWidget *widget, gpointer data) {
   dispTranslate (0, -0.5);
   gtk_widget_queue_draw (drawing_area);
}

/*! go right button */ 
static void on_right_button_clicked (GtkWidget *widget, gpointer data) {
   dispTranslate (0, 0.5);
   gtk_widget_queue_draw (drawing_area);
}

/*! center the map on GPS position */ 
static void on_gps_button_clicked (GtkWidget *widget, gpointer data) {
   if (isnan (my_gps_data.lon) || isnan (my_gps_data.lat))
      infoMessage ("No GPS position available", GTK_MESSAGE_WARNING);
   else {
      centerDispZone (my_gps_data.lon, my_gps_data.lat);
      gtk_widget_queue_draw (drawing_area);
   }
}

/*! poi name */ 
static bool poiNameChoose (char *poiName) {
   GtkWidget *dialog, *content_area;
   GtkWidget *poiName_entry;
   GtkDialogFlags flags;

   // Création de la boîte de dialogue
   flags = GTK_DIALOG_DESTROY_WITH_PARENT;
   dialog = gtk_dialog_new_with_buttons("Poi Name", NULL, flags,
                                        "_OK", GTK_RESPONSE_ACCEPT,
                                        "_Cancel", GTK_RESPONSE_CANCEL,
                                        NULL);

   content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

   //poiName_label = gtk_label_new("");
   poiName_entry = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(poiName_entry), poiName);
   
   // Définir la largeur minimale de la boîte de dialogue
   gtk_widget_set_size_request(dialog, 20, -1);
   //gtk_box_pack_start (GTK_BOX (content_area), poiName_label, FALSE, FALSE, 0);
   gtk_box_pack_start (GTK_BOX (content_area), poiName_entry, FALSE, FALSE, 0);

   // Affichage de la boîte de dialogue et récupération de la réponse
   gtk_widget_show_all (dialog);
   int response = gtk_dialog_run (GTK_DIALOG(dialog));
   
   // Récupération des valeurs saisies si la réponse est GTK_RESPONSE_ACCEPT
   if (response == GTK_RESPONSE_ACCEPT) {
       const char *pt = gtk_entry_get_text (GTK_ENTRY(poiName_entry));
       strcpy (poiName, pt);
       gtk_widget_queue_draw(drawing_area); // affiche le tout
   }
   // Fermeture de la boîte de dialogue
   gtk_widget_destroy(dialog);
   return (response == GTK_RESPONSE_ACCEPT);
}

/*! Callback for popup menu */
static void on_popup_menu_selection (GtkWidget *widget, gpointer data) {
   const char *selection = (const char *)data;
   int width, height;
   char buffer [1024] = "";
   //printf ("selection: %s\n", selection);
   // Extraction des coordonnées de la structure
   Coordinates *coords = (Coordinates *)g_object_get_data(G_OBJECT(widget), "coordinates");
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel (window)), &width, &height);
   //printf ("lat: %.2lf, lon: %.2lf\n", yToLat (coords->y, disp.yB, disp.yT), xToLon (coords->x, disp.xL, disp.xR));
   if (strcmp(selection, "Waypoint") == 0) {
      if (wayRoute.n < MAX_N_WAY_POINT) {
         destPressed = false;
         wayRoute.t [wayRoute.n].lat = yToLat (coords->y, disp.yB, disp.yT);
         wayRoute.t [wayRoute.n].lon = xToLon (coords->x, disp.xL, disp.xR);
         wayRoute.n += 1;
      }
      else (infoMessage ("number of waypoints exceeded", GTK_MESSAGE_ERROR));
   }
   else if (strcmp(selection, "Origin") == 0) {
      destPressed = false;
      par.pOr.lat = yToLat (coords->y, disp.yB, disp.yT);
      par.pOr.lon = xToLon (coords->x, disp.xL, disp.xR);
      route.n = 0;
      wayRoute.n = 0;
      wayRoute.totOrthoDist = 0;
      wayRoute.totLoxoDist = 0;
      par.pOrName [0] = '\0';
   } 
   else if (strcmp(selection, "Destination") == 0) {
      destPressed = true;
      route.n = 0;
      par.pDest.lat = yToLat (coords->y, disp.yB, disp.yT);
      par.pDest.lon = xToLon (coords->x, disp.xL, disp.xR);
      wayRoute.t [wayRoute.n].lat = par.pDest.lat;
      wayRoute.t [wayRoute.n].lon = par.pDest.lon;
      calculateOrthoRoute ();
      wayPointToStr (buffer);
      displayText (buffer, "Orthodomic and Loxdromic Waypoint routes");
      par.pOr.id = -1;
      par.pOr.father = -1;
      par.pDest.id = 0;
      par.pDest.father = 0;
      par.pDestName [0] = '\0';
   }  
   else if (strcmp(selection, "Poi") == 0)  {
      printf ("Poi selected\n");
      if (nPoi < MAX_N_POI) {
         if (poiNameChoose (tPoi[nPoi].name)) {
            tPoi [nPoi].lon = xToLon (coords->x, disp.xL, disp.xR);
            tPoi [nPoi].lat = yToLat (coords->y, disp.yB, disp.yT);
            tPoi [nPoi].type = VISIBLE;
            nPoi += 1;
         }
      }
      else (infoMessage ("number of poi exceeded", GTK_MESSAGE_ERROR));
   }
   gtk_widget_queue_draw (drawing_area); // affiche tout
}

/*! callback when mouse move */
static gboolean on_motion (GtkWidget *widget, GdkEventButton *event, gpointer data) {
   whereIsMouse.x = event->x;
   whereIsMouse.y = event->y;
   if (selecting) {
      gtk_widget_queue_draw(widget); // Redessiner pour mettre à jour le rectangle de sélection
   }
   else statusBarUpdate ();
   return TRUE;
}

/*!  callback for clic mouse */
static gboolean on_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   char str [MAX_SIZE_LINE];
   GtkWidget *menu, *item_origine, *item_destination, *item_waypoint, *item_poi;
   if (event->button == GDK_BUTTON_PRIMARY) { // left clic
      selecting = !selecting;
      whereWasMouse.x = whereIsMouse.x = event->x;
      whereWasMouse.y = whereIsMouse.y = event->y;
      gtk_widget_queue_draw(widget); // Redessiner pour mettre à jour le rectangle de sélection
   }
   if (event->button == 3) { // right clic
      menu = gtk_menu_new();
      item_origine = gtk_menu_item_new_with_label("Origin");
      sprintf (str, "Waypoint no: %d",  wayRoute.n + 1);
      item_waypoint = gtk_menu_item_new_with_label (str);
      item_destination = gtk_menu_item_new_with_label("Destination");
      item_poi = gtk_menu_item_new_with_label("New Poi");

      // Création d'une structure pour stocker les coordonnées
      Coordinates *coords = g_new(Coordinates, 1);
      coords->x = event->x;
      coords->y = event->y;

      // Ajout des coordonnées à la propriété de l'objet widget
      g_object_set_data(G_OBJECT(item_origine), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_waypoint), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_destination), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_poi), "coordinates", coords);

      g_signal_connect(item_origine, "activate", G_CALLBACK (on_popup_menu_selection), "Origin");
      g_signal_connect(item_waypoint, "activate", G_CALLBACK(on_popup_menu_selection), "Waypoint");
      g_signal_connect(item_destination, "activate", G_CALLBACK(on_popup_menu_selection), "Destination");
      g_signal_connect(item_poi, "activate", G_CALLBACK(on_popup_menu_selection), "Poi");

      GtkWidget *item_separator = gtk_separator_menu_item_new();

      // Ajout des éléments au menu
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_origine);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_waypoint);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_destination);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_separator);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_poi);

      gtk_widget_show_all(menu);
      gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
      return TRUE;
   }
   return FALSE;
}

/*! zoomm associated to scoll event */
static gboolean on_scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
   // Gérer l'événement de la roulette de la souris
   if (event->direction == GDK_SCROLL_UP) {
      dispZoom (0.8);
   } else if (event->direction == GDK_SCROLL_DOWN) {
	dispZoom (1.2);
   }
   gtk_widget_queue_draw (drawing_area);
   return TRUE;
}

/*! Function called if a key is pressed in tool bar */
static gboolean on_toolbar_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   // Empêcher la propagation de l'événement dans la barre d'outils
   return TRUE;
}   

/*! Translate screen on key arrow pressed */
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   switch (event->keyval) {
   case GDK_KEY_Up:
	   dispTranslate (0.5, 0);
      break;
   case GDK_KEY_Down:
	   dispTranslate (-0.5, 0);
      break;
   case GDK_KEY_Left:
	   dispTranslate (0, -0.5);
      break;
   case GDK_KEY_Right:
	   dispTranslate (0, 0.5);
      break;
   default:
      break;
   }
   gtk_widget_queue_draw (drawing_area);
   return FALSE; 
}

/*! Action after selection. Launch smtp request for grib file */
static gboolean on_button_release_event(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   double lat1, lat2, lon1, lon2;
   char buffer [MAX_SIZE_BUFFER] = "";
   char command [MAX_SIZE_LINE * 2] = "";
   if (event->button == GDK_BUTTON_PRIMARY && selecting && \
      ((whereIsMouse.x -whereWasMouse.x) > MIN_MOVE_FOR_SELECT) && \
      ((whereIsMouse.x -whereWasMouse.x) > MIN_MOVE_FOR_SELECT)) {

      lat1 = yToLat (whereIsMouse.y, disp.yB, disp.yT);
      lon1 = xToLon (whereWasMouse.x, disp.xL, disp.xR);
      lat2 = yToLat (whereWasMouse.y, disp.yB, disp.yT);
      lon2 = xToLon (whereIsMouse.x, disp.xL, disp.xR);
      sprintf (buffer, "%.2lf%c°, %.2lf%c° to %.2lf%c°, %.2lf%c°\n\n", 
         fabs (lat1), (lat1 >0)?'N':'S', fabs (lon1), (lon1 >= 0)?'E':'W',
         fabs (lat2), (lat2 >0)?'N':'S', fabs (lon2), (lon2 >= 0)?'E':'W');
      provider = SAILDOCS_GFS; // default 
      if ((mailRequestBox (buffer) == GTK_RESPONSE_OK) &&
         ((par.mailPw [0] != '\0') || mailPassword ())) { 
         if (smtpGribRequestPython (provider, lat1, lon1, lat2, lon2)) {
            spinner ("Waiting for grib Mail response");
            sprintf (command, "%s %s", par.imapToSeen, par.mailPw);
            if (system (command) != 0) {
               infoMessage ("Error running imapToSeen script", GTK_MESSAGE_ERROR);;
               return TRUE;
            }
            gribRequestRunning = true;
            gribMailTimeout = g_timeout_add (GRIB_TIME_OUT, mailGribRead, NULL);
         }
         else {
            infoMessage ("Error SMTP request Python", GTK_MESSAGE_ERROR);
            par.mailPw [0] = '\0';
         }
      }
      statusBarUpdate ();
   }
   selecting = FALSE;
   gtk_widget_queue_draw(widget); // Redessiner pour effacer le rectangle de sélection
   return TRUE;
}

/*! submenu with label and icon */
static GtkWidget *mySubMenu (const char *str, const char *iconName) {
   GtkWidget *my_item= gtk_menu_item_new();
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *icon = gtk_image_new_from_icon_name(iconName, GTK_ICON_SIZE_MENU);
   GtkWidget *label = gtk_label_new(str);
   gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

   GtkWidget *spacer = gtk_label_new(" ");
   gtk_box_pack_start(GTK_BOX(box), spacer, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
   gtk_container_add(GTK_CONTAINER(my_item), box);
   return my_item;
}

/*! Display main menu and make initializations */
int main (int argc, char *argv[]) {
   // initialisations
   int forWind = WIND, forCurrent = CURRENT;          // distinction wind current for openGrib call
   char str [MAX_SIZE_LINE];
   my_gps_data.ret = initGPS ();
   printf ("initGPS : %d\n", my_gps_data.ret);
   
   gtk_init(&argc, &argv);
   g_set_application_name (PROG_NAME);
   if (setlocale(LC_ALL, "C") == NULL) {              // very important for scanf decimal numbers
      fprintf (stderr, "main () Error: setlocale");
      return EXIT_FAILURE;
   }
   
   if (argc <= 1)                                     // no parameter on cmd pLine
      readParam (PARAMETERS_FILE);
   else if ((argc >= 2) && (argv [1][0] != '-'))      // file name is the only arg of cmd pLine
      readParam (argv [1]);
   else if (argv [1][0] == '-') {                     // there is an option
      if (argc >= 3)
         readParam (argv [2]);
      else
         readParam (PARAMETERS_FILE);
      optionManage (argv [1][1]);
      exit (EXIT_SUCCESS);
   }

   readGrib (NULL);
   updatedColors = false;
   if (readGribRet == 0) {
      fprintf (stderr, "main: unable to read grib file: %s\n ", par.gribFileName);
   }
   theTime = zone.timeStamp [0];
      
   initDispZone ();
   readPolar (par.polarFileName, &polMat);
   readPolar (par.wavePolFileName, &wavePolMat);
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   wayRoute.n = 0;
   wayRoute.t[0].lat = par.pDest.lat;
   wayRoute.t[0].lon = par.pDest.lon;

   // Création de la fenêtre principale
   window = gtk_window_new (GTK_WINDOW_TOPLEVEL); // Main window : global variable
   gtk_window_set_title(GTK_WINDOW(window), PROG_NAME);
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 400);
   gtk_window_maximize (GTK_WINDOW (window)); // full screen
   GError *error = NULL;
   if (!gtk_window_set_icon_from_file(GTK_WINDOW(window), buildRootName (PROG_LOGO, str), &error)) {
      g_warning("In main () Impossible to load icon: %s", error->message);
      g_error_free(error);
   }

   // Création du layout vertical
   GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_container_add(GTK_CONTAINER (window), vbox);

   // Création de la barre de menu
   GtkWidget *menubar = gtk_menu_bar_new();
   GtkWidget *file_menu = gtk_menu_new();
   GtkWidget *polar_menu = gtk_menu_new();
   GtkWidget *scenario_menu = gtk_menu_new();
   GtkWidget *dump_menu = gtk_menu_new();
   GtkWidget *poi_menu = gtk_menu_new();
   GtkWidget *help_menu = gtk_menu_new();

   GtkWidget *file_menu_item = gtk_menu_item_new_with_mnemonic("_Grib"); // Alt G
   GtkWidget *polar_menu_item = gtk_menu_item_new_with_mnemonic ("_Polar"); //...
   GtkWidget *scenario_menu_item = gtk_menu_item_new_with_mnemonic ("_Scenarios");
   GtkWidget *dump_menu_item = gtk_menu_item_new_with_mnemonic ("_Display");
   GtkWidget *poi_menu_item = gtk_menu_item_new_with_mnemonic ("PO_I");
   GtkWidget *help_menu_item = gtk_menu_item_new_with_mnemonic ("_Help");

   gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(polar_menu_item), polar_menu);
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(scenario_menu_item), scenario_menu);
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(dump_menu_item), dump_menu);
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(poi_menu_item), poi_menu);
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_menu_item), help_menu);

   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_menu_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), polar_menu_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), scenario_menu_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), dump_menu_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), poi_menu_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_menu_item);

   // Ajout d'éléments dans le menu "Grib"
   GtkWidget *file_item_open_wind = mySubMenu ("Wind: Open Grib", "folder");
   GtkWidget *file_item_wind_info = mySubMenu ("Wind: Grib Info", "applications-engineering-symbolic");
   GtkWidget *file_item_download = mySubMenu ("Wind: Download", "network-transmit-receive");
   GtkWidget *file_item_open_current = mySubMenu ("Current: Open Grib", "folder");
   GtkWidget *file_item_current_info = mySubMenu ("Current: Grib Info", "applications-engineering-symbolic");
   GtkWidget *file_item_current_download = mySubMenu ("Current: Download", "network-transmit-receive");
   GtkWidget *file_item_exit = mySubMenu ("Quit", "application-exit-symbolic");

   GtkWidget *item_separator = gtk_separator_menu_item_new();

   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_open_wind);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_wind_info);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_download);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), item_separator);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_open_current);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_current_info);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_current_download);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_exit);

   // Ajout d'éléments dans le menu "Polar"
   GtkWidget *polar_item_open = mySubMenu ("Polar or Wave Polar open", "folder-symbolic");
   GtkWidget *polar_item_draw = mySubMenu ("Polar Draw", "utilities-system-monitor-symbolic");
   GtkWidget *polar_item_wave = mySubMenu ("Wave Polar Draw", "x-office-spreadsheet-symbolic");

   gtk_menu_shell_append(GTK_MENU_SHELL(polar_menu), polar_item_open);
   gtk_menu_shell_append(GTK_MENU_SHELL(polar_menu), polar_item_draw);
   gtk_menu_shell_append(GTK_MENU_SHELL(polar_menu), polar_item_wave);
   
   // Ajout d'éléments dans le menu "Scenarios"
   GtkWidget *scenario_item_open = mySubMenu ("Open", "folder-symbolic");
   GtkWidget *scenario_item_change = mySubMenu ("Settings", "preferences-desktop");
   GtkWidget *scenario_item_get = mySubMenu ("Show", "document-open-symbolic");
   GtkWidget *scenario_item_save = mySubMenu ("Save", "media-floppy-symbolic");
   GtkWidget *scenario_item_edit = mySubMenu ("Edit", "document-edit-symbolic");
   
   gtk_menu_shell_append(GTK_MENU_SHELL(scenario_menu), scenario_item_open);
   gtk_menu_shell_append(GTK_MENU_SHELL(scenario_menu), scenario_item_change);
   gtk_menu_shell_append(GTK_MENU_SHELL(scenario_menu), scenario_item_get);
   gtk_menu_shell_append(GTK_MENU_SHELL(scenario_menu), scenario_item_save);
   gtk_menu_shell_append(GTK_MENU_SHELL(scenario_menu), scenario_item_edit);

   // Ajout d'éléments dans le menu dump "Display"
   GtkWidget *dump_item_isoc = gtk_menu_item_new_with_label("Isochrone");
   GtkWidget *dump_item_ortho = gtk_menu_item_new_with_label("Ortho and Loxo Routes");
   GtkWidget *dump_item_rte = gtk_menu_item_new_with_label("Sail Route");
   GtkWidget *dump_item_report = gtk_menu_item_new_with_label("Sail Report");
   GtkWidget *dump_item_gps = gtk_menu_item_new_with_label("GPS");

   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_isoc);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_ortho);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_rte);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_report);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_gps);

   // Ajout d'éléments dans le menu "Poi"
   GtkWidget *poi_item_dump = mySubMenu ("Dump", "document-open-symbolic");
   GtkWidget *poi_item_save = mySubMenu ("Save", "media-floppy-symbolic");
   GtkWidget *poi_item_edit = mySubMenu ("Edit", "document-edit-symbolic");
   
   gtk_menu_shell_append(GTK_MENU_SHELL(poi_menu), poi_item_dump);
   gtk_menu_shell_append(GTK_MENU_SHELL(poi_menu), poi_item_save);
   gtk_menu_shell_append(GTK_MENU_SHELL(poi_menu), poi_item_edit);

   // Ajout d'éléments dans le menu "Help"
   GtkWidget *help_item_html = mySubMenu ("Help", "help-browser-symbolic");
   GtkWidget *help_item_info = mySubMenu ("About", "help-about-symbolic");

   gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_item_html);
   gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_item_info);

   // fonctions de CallBack
   g_signal_connect(G_OBJECT(file_item_open_wind), "activate", G_CALLBACK(openGrib), &forWind);
   g_signal_connect(G_OBJECT(file_item_wind_info), "activate", G_CALLBACK(gribInfo), &forWind);
   g_signal_connect(G_OBJECT(file_item_download), "activate", G_CALLBACK(downloadGrib), &forWind);
   g_signal_connect(G_OBJECT(file_item_open_current), "activate", G_CALLBACK(openGrib), &forCurrent);
   g_signal_connect(G_OBJECT(file_item_current_info), "activate", G_CALLBACK(gribInfo), &forCurrent);
   g_signal_connect(G_OBJECT(file_item_current_download), "activate", G_CALLBACK(downloadGrib), &forCurrent);
   g_signal_connect(G_OBJECT(file_item_exit), "activate", G_CALLBACK(gtk_main_quit), NULL);
   g_signal_connect(G_OBJECT(polar_item_open), "activate", G_CALLBACK(openPolar), NULL);
   g_signal_connect(G_OBJECT(polar_item_draw), "activate", G_CALLBACK(cbPolarDraw), NULL);
   g_signal_connect(G_OBJECT(polar_item_wave), "activate", G_CALLBACK(cbWavePolarDraw), NULL);
   g_signal_connect(G_OBJECT(scenario_item_open), "activate", G_CALLBACK(openScenario), NULL);
   g_signal_connect(G_OBJECT(scenario_item_change), "activate", G_CALLBACK(change), NULL);
   g_signal_connect(G_OBJECT(scenario_item_get), "activate", G_CALLBACK(parDump), NULL);
   g_signal_connect(G_OBJECT(scenario_item_save), "activate", G_CALLBACK(saveScenario), NULL);
   g_signal_connect(G_OBJECT(scenario_item_edit), "activate", G_CALLBACK(editScenario), NULL);
   g_signal_connect(G_OBJECT(dump_item_isoc), "activate", G_CALLBACK(isocDump), NULL);
   g_signal_connect(G_OBJECT(dump_item_ortho), "activate", G_CALLBACK(orthoDump), NULL);
   g_signal_connect(G_OBJECT(dump_item_rte), "activate", G_CALLBACK(rteDump), NULL);
   g_signal_connect(G_OBJECT(dump_item_report), "activate", G_CALLBACK(rteReport), NULL);
   g_signal_connect(G_OBJECT(dump_item_gps), "activate", G_CALLBACK(gpsDump), NULL);
   g_signal_connect(G_OBJECT(poi_item_dump), "activate", G_CALLBACK(poiDump), NULL);
   g_signal_connect(G_OBJECT(poi_item_save), "activate", G_CALLBACK(poiSave), NULL);
   g_signal_connect(G_OBJECT(poi_item_edit), "activate", G_CALLBACK(poiEdit), NULL);
   g_signal_connect(G_OBJECT(help_item_html), "activate", G_CALLBACK(help), NULL);
   g_signal_connect(G_OBJECT(help_item_info), "activate", G_CALLBACK(helpInfo), NULL);


   // Ajout de la barre d'outil
   GtkWidget *toolbar = gtk_toolbar_new();
   gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);

   // Ajouter les événements de clavier à la barre d'outils
   gtk_widget_add_events(toolbar, GDK_KEY_PRESS_MASK);

   // Connecter la fonction on_toolbar_key_press à l'événement key-press-event de la barre d'outils
   g_signal_connect(toolbar, "key-press-event", G_CALLBACK(on_toolbar_key_press), NULL);

   GtkToolItem *run_button = gtk_tool_button_new(NULL, NULL);
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(run_button), "system-run"); 
   
   GtkToolItem *change_button = gtk_tool_button_new(NULL, "Change");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(change_button), "preferences-desktop"); 
   
   GtkToolItem *polar_button = gtk_tool_button_new(NULL, "Polar");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(polar_button), "utilities-system-monitor-symbolic"); 
   
   GtkToolItem *stop_button = gtk_tool_button_new(NULL, "Stop");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(stop_button), "media-playback-pause"); 
   
   GtkToolItem *play_button = gtk_tool_button_new(NULL, "Play");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(play_button), "media-playback-start"); 
   
   GtkToolItem *to_start_button = gtk_tool_button_new(NULL, "<<");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_start_button), "media-skip-backward"); 
   
   GtkToolItem *reward_button = gtk_tool_button_new(NULL, "<");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(reward_button), "media-seek-backward"); 
   
   GtkToolItem *forward_button = gtk_tool_button_new(NULL, ">");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(forward_button), "media-seek-forward"); 
   
   GtkToolItem *to_end_button = gtk_tool_button_new(NULL, ">>");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_end_button), "media-skip-forward"); 
   
   GtkToolItem *to_zoom_in_button = gtk_tool_button_new(NULL, "zoomIn");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_zoom_in_button), "zoom-in-symbolic"); 
   
   GtkToolItem *to_zoom_out_button = gtk_tool_button_new(NULL, "zoomOut");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_zoom_out_button), "zoom-out-symbolic"); 
   
   GtkToolItem *to_zoom_original_button = gtk_tool_button_new(NULL, "zoomOriginal");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_zoom_original_button), "zoom-original"); 
   
   GtkToolItem *to_left_button = gtk_tool_button_new(NULL, "left");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_left_button), "pan-start-symbolic"); 
   
   GtkToolItem *to_up_button = gtk_tool_button_new(NULL, "up");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_up_button), "pan-up-symbolic"); 
   
   GtkToolItem *to_down_button = gtk_tool_button_new(NULL, "down");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_down_button), "pan-down-symbolic"); 
   
   GtkToolItem *to_right_button = gtk_tool_button_new(NULL, "right");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_right_button), "pan-end-symbolic"); 
   
   GtkToolItem *to_gps_pos_button = gtk_tool_button_new(NULL, "gpsPos");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(to_gps_pos_button), "find-location-symbolic"); 
  
   GtkToolItem *palette_button = gtk_tool_button_new(NULL, "palette");
   gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(palette_button), "edit-select-all"); 

   g_signal_connect(run_button, "clicked", G_CALLBACK (on_run_button_clicked), NULL);
   g_signal_connect(change_button, "clicked", G_CALLBACK (change), NULL);
   g_signal_connect(polar_button, "clicked", G_CALLBACK (cbPolarDraw), NULL);
   g_signal_connect(stop_button, "clicked", G_CALLBACK (on_stop_button_clicked), NULL);
   g_signal_connect(play_button, "clicked", G_CALLBACK (on_play_button_clicked), NULL);
   g_signal_connect(to_start_button, "clicked", G_CALLBACK (on_to_start_button_clicked), NULL);
   g_signal_connect(reward_button, "clicked", G_CALLBACK (on_reward_button_clicked), NULL);
   g_signal_connect(forward_button, "clicked", G_CALLBACK (on_forward_button_clicked), NULL);
   g_signal_connect(to_end_button, "clicked", G_CALLBACK (on_to_end_button_clicked), NULL);
   g_signal_connect(to_zoom_in_button, "clicked", G_CALLBACK (on_zoom_in_button_clicked), NULL);
   g_signal_connect(to_zoom_out_button, "clicked", G_CALLBACK (on_zoom_out_button_clicked), NULL);
   g_signal_connect(to_zoom_original_button, "clicked", G_CALLBACK (on_zoom_original_button_clicked), NULL);
   g_signal_connect(to_left_button, "clicked", G_CALLBACK (on_left_button_clicked), NULL);
   g_signal_connect(to_up_button, "clicked", G_CALLBACK (on_up_button_clicked), NULL);
   g_signal_connect(to_down_button, "clicked", G_CALLBACK (on_down_button_clicked), NULL);
   g_signal_connect(to_right_button, "clicked", G_CALLBACK (on_right_button_clicked), NULL);
   g_signal_connect(to_gps_pos_button, "clicked", G_CALLBACK (on_gps_button_clicked), NULL);
   g_signal_connect(palette_button, "clicked", G_CALLBACK (paletteDraw), NULL);

   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), run_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), change_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), polar_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), stop_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), play_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_start_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), reward_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), forward_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_end_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_zoom_in_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_zoom_out_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_zoom_original_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_left_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_up_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_down_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_right_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), to_gps_pos_button, -1);
   gtk_toolbar_insert(GTK_TOOLBAR(toolbar), palette_button, -1);

   // Création de la barre d'état
   statusbar = gtk_statusbar_new();
   context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "Statusbar");
   statusBarUpdate ();

   // Créer une zone de dessin
   drawing_area = gtk_drawing_area_new();
   gtk_widget_set_size_request(drawing_area, -1, -1);

   g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(drawGribCallback), NULL);
   g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_button_press_event), NULL);
   g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_motion), NULL);
   g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(on_button_release_event), NULL);
   gtk_widget_set_events(drawing_area, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK);

   // Ajout des éléments au layout vertical
   gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), drawing_area, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), statusbar, FALSE, FALSE, 0);

   // Connecter l'événement de la roulette de la souris à la fonction de gestion
   g_signal_connect(G_OBJECT(window), "scroll-event", G_CALLBACK (on_scroll_event), NULL);

   // Permettre la réception d'événements de la souris
   gtk_widget_add_events(window, GDK_SCROLL_MASK);

   // Ajouter les événements de clavier à la fenêtre
   gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
   // Connecter la fonction on_key_press à l'événement key_press
   g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), window);

   // Connexion du signal de fermeture de la fenêtre
   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
   g_signal_connect(window, "delete-event", G_CALLBACK(gtk_main_quit), NULL);

   // g_timeout_add (10000, mailGribRead, NULL);
   
   gtk_widget_show_all(window);
   gtk_main();
   freeSHP ();
   free (tIsSea);
   closeGPS ();
   return 0;
}

