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
* \li Verified with: cppcheck 
* \section Description
* \li calculate best route using Isochrone method 
* \li use polar boat (wind, waves) and grib files (wind, current)
* \li tws = True Wind Speed
* \li twd = True Wind Direction
* \li twa = True Wind Angle - the angle of the boat to the wind
* \li sog = Speed over Ground of the boat
* \li cog = Course over Ground  of the boat
* \section Usage
* \li ./routing [-<option>] [<parameterFile>] */

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
#include "engine.h"
#include "option.h"

#define MIN_ZOOM_POI_VISIBLE  30
#define BOAT_UNICODE          "⛵"
#define CAT_UNICODE           "\U0001F431"
#define ORTHO_ROUTE_PARAM     20    // for drawing orthodromic route
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
#define CAIRO_SET_SOURCE_RGB_BLACK(cr)             cairo_set_source_rgb (cr,0,0,0)
#define CAIRO_SET_SOURCE_RGB_WHITE(cr)             cairo_set_source_rgb (cr,1.0,1.0,1.0)
#define CAIRO_SET_SOURCE_RGB_RED(cr)               cairo_set_source_rgb (cr,1.0,0,0)
#define CAIRO_SET_SOURCE_RGB_GREEN(cr)             cairo_set_source_rgb (cr,0,1.0,0)
#define CAIRO_SET_SOURCE_RGB_BLUE(cr)              cairo_set_source_rgb (cr,0,0,1.0)
#define CAIRO_SET_SOURCE_RGB_ORANGE(cr)            cairo_set_source_rgb (cr,1.0,165.0/255,0.0)
#define CAIRO_SET_SOURCE_RGB_YELLOW(cr)            cairo_set_source_rgb (cr, 1.0,1.0,0.8)
#define CAIRO_SET_SOURCE_RGB_PINK(cr)              cairo_set_source_rgb (cr,1.0,0,1.0)
#define CAIRO_SET_SOURCE_RGB_DARK_GRAY(cr)         cairo_set_source_rgb (cr,0.2,0.2,0.2)
#define CAIRO_SET_SOURCE_RGB_GRAY(cr)              cairo_set_source_rgb (cr,0.5,0.5,0.5)
#define CAIRO_SET_SOURCE_RGB_LIGHT_GRAY(cr)        cairo_set_source_rgb (cr,0.8,0.8,0.8)
#define CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr)  cairo_set_source_rgb (cr,0.9,0.9,0.9)
#define K_LON_LAT            (0.71)

GdkRGBA colors [] = {{1.0,0,0,1}, {0,1.0,0,1},  {0,0,1.0,1},{0.5,0.5,0,1},{0,0.5,0.5,1},{0.5,0,0.5,1},{0.2,0.2,0.2,1},{0.4,0.4,0.4,1},{0.8,0,0.2,1},{0.2,0,0.8,1}}; // Colors for Polar curve
int nColors = 10;

#define N_WIND_COLORS 6
guint8 colorPalette [N_WIND_COLORS][3] = {{0,0,255}, {0, 255, 0}, {255, 255, 0}, {255, 153, 0}, {255, 0, 0}, {139, 0, 0}};
// blue, green, yellow, orange, red, blacked red
guint8 bwPalette [N_WIND_COLORS][3] = {{250,250,250}, {200, 200, 200}, {170, 170, 170}, {130, 130, 130}, {70, 70, 70}, {10, 10, 10}};
double tTws [] = {0.0, 15.0, 20.0, 25.0, 30.0, 40.0};

char parameterFileName [MAX_SIZE_NAME];
GtkWidget *statusbar;
GtkWidget *window; 
GtkWidget *spinner_window; 
GtkListStore *filter_store;
GtkWidget *filter_combo;
guint context_id;
GtkWidget *polar_drawing_area;   // polar
int selectedPol = 0;             // select polar to draw. 0 = all
double selectedTws = 0;          // selected TWS for polar draw
GtkWidget *drawing_area;
guint gribMailTimeout;
guint gribReadTimeout;
guint currentGribReadTimeout;
int kTime = 0;
gboolean animation_active = FALSE;
bool destPressed = true;
bool polygonStarted = false;
bool gribRequestRunning = false;
bool  selecting = FALSE;
int provider = SAILDOCS_GFS;
MyDate vStart;
MyDate *start = &vStart;    
struct tm *timeInfos;               // conversion en struct tm
long theTime;
bool updatedColors = false; 
int polarType = POLAR;
int segmentOrBezier = SEGMENT;
GtkWidget *tab_display;
GtkWidget *dialog = NULL;
GtkWidget *spin_button_time_max = NULL;
GtkWidget *val_size_eval = NULL; 
int selectedPointInLastIsochrone = 0;

/*! Structure to store coordinates */
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
   double latMin;
   double latMax;
   double lonLeft;
   double lonRight;
   double latStep;
   double lonStep;
   double zoom;
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

WayRoute wayRoute;

// struct to memorize zone for mail request  
struct {
   double lat1;
   double lat2;
   double lon1;
   double lon2;
} memo;

static void meteogram ();

/*! draw spinner when waiting for something */
static void spinner (const char *title, const char* str) {
   spinner_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(spinner_window), title);
   gtk_window_set_default_size(GTK_WINDOW(spinner_window), 200, 100);

   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   GtkWidget *label = gtk_label_new (str);
   GtkWidget *spinner = gtk_spinner_new ();
   gtk_spinner_start(GTK_SPINNER(spinner));
   gtk_container_add (GTK_CONTAINER (spinner_window), vbox);
   gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 5);
   gtk_box_pack_start(GTK_BOX(vbox), spinner, FALSE, FALSE, 5);
   gtk_widget_show_all (spinner_window);
}

/*! confirmation box */
static bool confirm (const char *message, char* title)  {  
   GtkWidget *dialogBox = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT, \
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message);  
   gtk_window_set_title(GTK_WINDOW (dialogBox), title); 
   gint res = gtk_dialog_run(GTK_DIALOG (dialogBox));
   gtk_widget_destroy(dialogBox);
   return res == GTK_RESPONSE_YES;
}  

/*! draw info message GTK_MESSAGE_INFO, GTK_MESSAGE_WARNING ou GTK_MESSAGE_ERROR */
static void infoMessage (char *message, GtkMessageType typeMessage) {
   GtkWidget *dialogBox = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, typeMessage, GTK_BUTTONS_OK, "%s", message);
   gtk_dialog_run (GTK_DIALOG (dialogBox));
   gtk_widget_destroy (dialogBox);
}

/*! open map usong OSM or open sea map*/
static void openMap (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data;
   const char* OSM_URL [] = {"https://www.openstreetmap.org/export/", "https://map.openseamap.org/"};
   char mapUrl [MAX_SIZE_LINE];
   double lat = (dispZone.latMin + dispZone.latMax) / 2.0;
   double lon = (dispZone.lonLeft + dispZone.lonRight) / 2.0;
   double zoom = 43.0 / fabs(dispZone.latMax - dispZone.latMin);
   
   GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW (window), OSM_URL [*comportement]);
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

   GtkWidget* webView = webkit_web_view_new();
   gtk_container_add(GTK_CONTAINER(window), webView);

   if (*comportement == 0)
      sprintf (mapUrl, "%sembed.html?bbox=%.4lf%%2C%.4lf%%2C%.4lf%%2C%.4lf&layer=mapnik&marker=%.4lf%%2C%.4lf", 
         OSM_URL[*comportement], dispZone.lonLeft, dispZone.latMin, dispZone.lonRight, dispZone.latMax,
         par.pOr.lat, par.pOr.lon);
    
   else
      sprintf (mapUrl, "%s?lat=%.4lf&lon=%.4lf&zoom=%.2lf", OSM_URL[*comportement], lat, lon, zoom);

   printf ("Map URL: %s\n", mapUrl);
   webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webView), mapUrl);
   gtk_widget_show_all(window);
   gtk_main();
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
   double deltaLat = MAX (0.1, zone.latMax - latCenter);
   double deltaLon = deltaLat * par.dispLonLatRatio;
   dispZone.zoom = 180 / deltaLat;
   // printf ("init zoom: %.2lf\n", dispZone.zoom); 
   dispZone.latMin = zone.latMin;
   dispZone.latMax = zone.latMax;
   dispZone.lonLeft = lonCenter - deltaLon;
   dispZone.lonRight = lonCenter + deltaLon;
   dispZone.latStep = fabs ((dispZone.latMax - dispZone.latMin)) / DISP_NB_LAT_STEP;
   dispZone.lonStep = fabs ((dispZone.lonLeft - dispZone.lonRight)) / DISP_NB_LON_STEP;
}

/*! center DispZone on lon, lat */
static void centerDispZone (double lon, double lat) {
   double oldLatCenter = (dispZone.latMin + dispZone.latMax) / 2.0;
   double deltaLat = MAX (0.1, dispZone.latMax - oldLatCenter);
   double deltaLon = deltaLat * par.dispLonLatRatio;
   dispZone.zoom = 180 / deltaLat;
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
   double deltaLat = MAX (0.1, dispZone.latMax - latCenter);
   double deltaLon = deltaLat * par.dispLonLatRatio;
   dispZone.zoom = 180 / deltaLat;
   if ((z < 1.0) && (deltaLat < 0.1)) return; // stop zoom
   if ((z > 1.0) && (deltaLat > 60)) return; // stop zoom

   // printf ("dispZone.zoom = %.2lf\n", dispZone.zoom);
   dispZone.latMin = latCenter - deltaLat * z;
   dispZone.latMax = latCenter + deltaLat * z;
   dispZone.lonLeft = lonCenter - deltaLon * z;
   dispZone.lonRight = lonCenter + deltaLon * z;
   dispZone.latStep = fabs ((dispZone.latMax - dispZone.latMin)) / DISP_NB_LAT_STEP;
   dispZone.lonStep = fabs ((dispZone.lonLeft - dispZone.lonRight)) / DISP_NB_LON_STEP;
}

/*! horizontal and vertical translation of main window */
static void dispTranslate (double h, double v) {
   double saveLatMin = dispZone.latMin, saveLatMax = dispZone.latMax;
   double saveLonLeft = dispZone.lonLeft, saveLonRight = dispZone.lonRight;
   const double K_TRANSLATE = (dispZone.latMax - dispZone.latMin) / 10.0;

   dispZone.latMin += h * K_TRANSLATE;
   dispZone.latMax += h * K_TRANSLATE;
   dispZone.lonLeft += v * K_TRANSLATE;
   dispZone.lonRight += v * K_TRANSLATE;

   if ((dispZone.latMin < -180) || (dispZone.latMax > 180) || (dispZone.lonLeft < -330) || (dispZone.lonRight > 330)) {
      dispZone.latMin = saveLatMin;
      dispZone.latMax = saveLatMax;
      dispZone.lonLeft = saveLonLeft;
      dispZone.lonRight = saveLonRight;
   }
}   

/*! return x as a function of longitude */
static double getX (double lon) {
   double kLat = (dispZone.yB - dispZone.yT) / (dispZone.latMax - dispZone.latMin);
   //double kLon = (dispZone.xR - dispZone.xL) / (dispZone.lonRight - dispZone.lonLeft);
   double kLon = kLat * K_LON_LAT; //cos (DEG_TO_RAD * (dispZone.latMax + dispZone.latMin)/2.0);
   return kLon * (lon - dispZone.lonLeft) + dispZone.xL;
}

/*! return y as a function of latitude */
static double getY (double lat) {
   double kLat = (dispZone.yB - dispZone.yT) / (dispZone.latMax - dispZone.latMin);
   return kLat * (dispZone.latMax - lat) + dispZone.yT;
}

/*! return longitude as a function of x (inverse of getX) */
static double xToLon (double x) {
   //double kLon = (dispZone.xR - dispZone.xL) / (dispZone.lonRight - dispZone.lonLeft);
   double kLat = (dispZone.yB - dispZone.yT) / (dispZone.latMax - dispZone.latMin);
   double kLon = kLat * K_LON_LAT; // cos (DEG_TO_RAD * (dispZone.latMax + dispZone.latMin)/2.0);
   //printf ("kLon = %.2lf\n", kLon);
   double lon = dispZone.lonLeft + ((x - dispZone.xL -1) / kLon);
   return lonCanonize (lon);
}

/*! return latitude as a function of y (inverse of getY) */
static double yToLat (double y) {
   double kLat = (dispZone.yB - dispZone.yT) / (dispZone.latMax - dispZone.latMin);
   //printf ("kLat = %.2lf\n", kLat);
   //printf ("Klon, y, yBottom, lat: %lf %lf %lf %lf\n", kLat, y, yBottom, (dispZone.latMax - (y - yTop) / kLat)); 
   return dispZone.latMax - ((y - dispZone.yT - 1) / kLat); 
}

/*! draw a polygon */
static void drawPolygon (cairo_t *cr, Polygon po) {
   double x = getX (po.points[0].lon);
   double y = getY (po.points[0].lat);
   cairo_move_to (cr, x, y);
   for (int k = 1; k < po.n; k++) {
      x = getX (po.points [k].lon);
      y = getY (po.points [k].lat);
      cairo_line_to (cr, x, y);
   }
   cairo_close_path(cr);
   cairo_fill(cr);
}

/*! exclusion zone is an array of polygons */
static void drawForbidArea (cairo_t *cr) {
   cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.5);
   for (int i = 0; i < par.nForbidZone; i++)
      drawPolygon (cr, forbidZones [i]);
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
 
/*! true if P is both within the zone and the display zone */
bool isInDispZone (Pp pt) {
   if (par.constWindTws > 0) return true;
   return (pt.lat >= zone.latMin) && (pt.lat <= zone.latMax) && (pt.lon >= zone.lonLeft) && (pt.lon <= zone.lonRight) &&
          (pt.lat >= dispZone.latMin) && (pt.lat <= dispZone.latMax) && (pt.lon >= dispZone.lonLeft) && (pt.lon <= dispZone.lonRight);
}
 
/*! update colors for wind */
static void paintWind (cairo_t *cr, int width, int height) {
   double tws;
   Pp pt;
   guint8 r, g, b;
   for (int x = 0; x < width; x++) {
      for (int y = 0; y < height; y++) {
         memset (&pt, 0, sizeof (Pp));
         pt.lat = yToLat (y);
         pt.lon = xToLon (x);
         if (isInDispZone (pt)) {
            //findFlow (pt, t, &u, &v, &gust, &w, zone, gribData);
            //tws = extTws (u, v);
            tws = findTwsByIt (pt, (MIN (kTime, zone.nTimeStamp -1)));
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
      CAIRO_SET_SOURCE_RGB_BLACK(cr);
      cairo_move_to (cr, x, height / 2);
      cairo_line_to (cr, x, height);
      cairo_stroke (cr);

      snprintf (label, sizeof(label), "%0.2lf", tws);
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
   double lSeg;
   double lat = lat1;
   double lon = lon1;
   double angle, x1, y1, x2, y2;
   double x = getX (lon1);
   double y = getY (lat1);
   cairo_move_to (cr, x, y);
   CAIRO_SET_SOURCE_RGB_GREEN(cr);
   n = 10;
   
   for (int i = 0; i < n - 2; i ++) {
      angle = loxCap (lat, lon, lat2, lon2) + givry (lat, lon, lat2, lon2);
      lSeg = orthoDist (lat, lon, lat2, lon2) / (n - i);
      lat += lSeg * cos (angle * DEG_TO_RAD) / 60;
      lon += (lSeg * sin (angle * DEG_TO_RAD) / cos (DEG_TO_RAD * lat)) / 60;
      x = getX (lon);
      y = getY (lat);
      
      angle = loxCap (lat, lon, lat2, lon2) + givry (lat, lon, lat2, lon2);
      lSeg = orthoDist (lat, lon, lat2, lon2) / (n - i - 1);
      lat += lSeg * cos (angle * DEG_TO_RAD) / 60;
      lon += (lSeg * sin (angle * DEG_TO_RAD) / cos (DEG_TO_RAD * lat)) / 60;
      x1 = getX (lon);
      y1 = getY (lat);

      angle = loxCap (lat, lon, lat2, lon2) + givry (lat, lon, lat2, lon2);
      lSeg = orthoDist (lat, lon, lat2, lon2) / (n - i - 2);
      lat += lSeg * cos (angle * DEG_TO_RAD) / 60;
      lon += (lSeg * sin (angle * DEG_TO_RAD) / cos (DEG_TO_RAD * lat)) / 60;
      x2 = getX (lon);
      y2 = getY (lat);

      cairo_curve_to(cr, x, y, x1, y1, x2, y2);
   }
   x = getX (lon2);
   y = getY (lat2);
   cairo_line_to (cr, x, y);
   cairo_stroke (cr);
   // printf ("point %d: %.1lf° %.2lf° %.2lf° %.2lf NM\n", i, lat2, lon2, angle, lSeg);
}

/*! return label associated to x */
GtkWidget *doubleToLabel (double x) {
   char str [MAX_SIZE_LINE];
   sprintf (str, "%.2lf", x);
   GtkWidget *label = gtk_label_new (str);
   gtk_label_set_yalign(GTK_LABEL(label), 0);
   gtk_label_set_xalign(GTK_LABEL(label), 0);   
   return label;
}

/*! return label associated to str with optionnally i*/
GtkWidget *strToLabel (char *str0, int i) {
   char str [MAX_SIZE_LINE];
   if (i >= 0) sprintf (str, "%s %d", str0, i);
   else strcpy (str, str0);
   GtkWidget *label = gtk_label_new (str);
   gtk_label_set_yalign(GTK_LABEL(label), 0);
   gtk_label_set_xalign(GTK_LABEL(label), 0);   
   return label;
}

/*! return label in bold associated to str */
GtkWidget *strToLabelBold (char *str) {
   GtkWidget *label = gtk_label_new (str);
   PangoAttrList *attrs = pango_attr_list_new();
   PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
   pango_attr_list_insert(attrs, attr);
   gtk_label_set_attributes(GTK_LABEL(label), attrs);
   pango_attr_list_unref(attrs);
   gtk_label_set_yalign(GTK_LABEL(label), 0);
   gtk_label_set_xalign(GTK_LABEL(label), 0);   
   return label;
}

/*! dump orthodomic route into str */
static void niceWayPointReport () {
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   GtkWidget *dialog = gtk_dialog_new_with_buttons ("Orthodomic and Loxdromic Waypoint routes", NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                          NULL, NULL, NULL, NULL, NULL);

   gtk_widget_set_size_request(dialog, 400, -1);
   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

   GtkWidget *grid = gtk_grid_new();   
   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_container_add(GTK_CONTAINER(content_area), grid);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
   
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Point"),      0, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Lat."),       1, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Lon."),       2, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Ortho Cap."), 3, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Ortho Dist."),4, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Loxo Cap."),  5, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Loxo Dist."), 6, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), separator,                                                     0, 1, 7, 1);
  
   int i;
   for (i = -1; i <  wayRoute.n; i++) {
      if (i == -1) {
         gtk_grid_attach (GTK_GRID (grid), strToLabel("Origin", -1),                                       0, 2, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel(latToStr (par.pOr.lat, par.dispDms, strLat), -1), 1, 2, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel(lonToStr (par.pOr.lon, par.dispDms, strLon), -1), 2, 2, 1, 1);
      }
      else {
         gtk_grid_attach (GTK_GRID (grid), strToLabel("Waypoint", i),                                               0, i+3, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel(latToStr (wayRoute.t[i].lat, par.dispDms, strLat), -1), 1, i+3, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel(lonToStr (wayRoute.t[i].lon, par.dispDms, strLon), -1), 2, i+3, 1, 1);
      }
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayRoute.t [i+1].oCap),                                3, i+3, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayRoute.t [i+1].od),                                  4, i+3, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayRoute.t [i+1].lCap),                                5, i+3, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayRoute.t [i+1].ld),                                  6, i+3, 1, 1);
   }
   gtk_grid_attach (GTK_GRID (grid), strToLabel("Destination", -1),                                       0, i+3, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabel(latToStr (par.pDest.lat, par.dispDms, strLat), -1), 1, i+3, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabel(lonToStr (par.pDest.lon, par.dispDms, strLon), -1), 2, i+3, 1, 1);
   
   i += 1;
   separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID(grid), separator,                                                     0, i+3, 7, 1);
   i += 1;
   gtk_grid_attach (GTK_GRID (grid), strToLabel("Total Orthodomic Distance", -1),                 0, i+3, 3, 1);
   gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayRoute.totOrthoDist),                       3, i+3, 1, 1);
   i += 1;
   separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID(grid), separator,                                                     0, i+3, 7, 1);
   i += 1;
   gtk_grid_attach (GTK_GRID (grid), strToLabel("Total Loxodromic Distance", -1),                 0, i+3, 3, 1);
   gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayRoute.totLoxoDist),                        3, i+3, 1, 1);

   gtk_widget_show_all (dialog);
   gtk_dialog_run (GTK_DIALOG (dialog));
   gtk_widget_destroy (dialog);
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
   strncat (str, line, MAX_SIZE_LINE);
   for (int i=0; i <  wayRoute.n; i++) {
      sprintf (line, " WP %02d: %-12s%-12s%7.2lf°      %7.2lf   %7.2lf°     %7.2lf \n", i + 1,  \
         latToStr (wayRoute.t[i].lat, par.dispDms, strLat), lonToStr (wayRoute.t[i].lon, par.dispDms, strLon),\
         wayRoute.t [i+1].oCap,  wayRoute.t [i+1].od, wayRoute.t [i+1].lCap,  wayRoute.t [i+1].ld);
      strncat (str, line, MAX_SIZE_LINE);
   }
   sprintf (line, " pDest: %-12s%-12s\n\n", latToStr (par.pDest.lat, par.dispDms, strLat), lonToStr (par.pDest.lon, par.dispDms, strLon));
   strncat (str, line, MAX_SIZE_LINE);
   sprintf (line, " Total orthodromic distance: %.2lf NM\n", wayRoute.totOrthoDist);
   strncat (str, line, MAX_SIZE_LINE);
   sprintf (line, " Total loxodromic distance : %.2lf NM\n", wayRoute.totLoxoDist);
   strncat (str, line, MAX_SIZE_LINE);
}

/*! draw loxodromie */
static void drawLoxoRoute (cairo_t *cr) {
   double x = getX (par.pOr.lon);
   double y = getY (par.pOr.lat);
   CAIRO_SET_SOURCE_RGB_LIGHT_GRAY(cr);
   cairo_move_to (cr, x, y);
   for (int i = 0; i <  wayRoute.n; i++) {
      x = getX (wayRoute.t [i].lon);
      y = getY (wayRoute.t [i].lat);
      cairo_line_to (cr, x, y);
   }
   if (destPressed) {
      x = getX (par.pDest.lon);
      y = getY (par.pDest.lat);
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
static void circle (cairo_t *cr, double lon, double lat, double red, double green, double blue) {
   cairo_arc (cr, getX (lon), 
       getY (lat), 4.0, 0, 2 * G_PI);
   cairo_set_source_rgb (cr, red, green, blue);
   cairo_fill (cr);
}

/*! draw the boat or cat or ... */
static void showUnicode (cairo_t *cr, const char *unicode, double lon, double lat) {
   PangoLayout *layout;
   PangoFontDescription *desc;

   layout = pango_cairo_create_layout(cr);

   desc = pango_font_description_from_string("DejaVuSans 16");
   pango_layout_set_font_description(layout, desc);
   pango_font_description_free(desc);

   pango_layout_set_text(layout, unicode, -1);

   cairo_move_to (cr, getX (lon), getY (lat));
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
         x = getX (pt.lon); 
         y = getY (pt.lat); 
	      // printf ("i lon lat x y %d %lf %lf %lf %lf \n", i, pt.lon, pt.lat, x, y);
         cairo_arc (cr, x, y, 1.0, 0, 2 * G_PI);
         cairo_fill (cr);
      }
   }
   return FALSE;
}

/*! draw closest points to pDest in each isochrones */
static gboolean drawClosest  (cairo_t *cr) {
   double x, y;
   Pp pt;
   CAIRO_SET_SOURCE_RGB_RED(cr);
   for (int i = 0; i < nIsoc; i++) {
      pt = isocArray [i][isoDesc [i].closest];
      //printf ("lat: %.2lf, lon: %.2lf\n", pt.lat, pt.lon); 
      x = getX (pt.lon); 
      y = getY (pt.lat); 
      cairo_arc (cr, x, y, 2, 0, 2 * G_PI);
      cairo_fill (cr);
   }
   return FALSE;
} 

/*! draw focal points */
static gboolean drawFocal  (cairo_t *cr) {
   double x, y, lat, lon;
   CAIRO_SET_SOURCE_RGB_GREEN(cr);
   for (int i = 0; i < nIsoc; i++) {
      lat = isoDesc [i].focalLat;
      lon = isoDesc [i].focalLon;
      //printf ("lat: %.2lf, lon: %.2lf\n", pt.lat, pt.lon); 
      x = getX (lon); 
      y = getY (lat); 
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
   CAIRO_SET_SOURCE_RGB_BLUE(cr);
   cairo_set_line_width(cr, 1.0);
   for (int i = 0; i < nIsoc; i++) {
      // GdkRGBA color = colors [i % nColors];
      // cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      
      index = isoDesc [i].first;
      for (int j = 0; j < isoDesc [i].size; j++) {
         newIsoc [j] = isocArray [i][index];
         index += 1;
         if (index == isoDesc [i].size) index = 0;
      }
      pt = newIsoc [0];
	   x = getX (pt.lon);
	   y = getY (pt.lat);
      cairo_move_to (cr, x, y);
	   if ((isoDesc [i].size < MIN_POINT_FOR_BEZIER) || style == SEGMENT) { 	// segment if number of points < 4 or style == 2
         for (int k = 1; k < isoDesc [i].size; k++) {
            pt = newIsoc [k]; 
            x = getX (pt.lon); 
            y = getY (pt.lat); 
            cairo_line_to (cr, x, y);
	      }
         cairo_stroke(cr);

     }
	  else { 									// curve if number of point >= 4 and style == 2
         int k;
         for (k = 1; k < isoDesc [i].size - 2; k += 3) {
            pt = newIsoc [k]; 
            x = getX (pt.lon); 
            y = getY (pt.lat); 
            pt = newIsoc [k+1]; 
            x1 = getX (pt.lon); 
            y1 = getY (pt.lat); 
            pt = newIsoc [k+2]; 
            x2 = getX (pt.lon); 
            y2 = getY (pt.lat); 
		      cairo_curve_to(cr, x, y, x1, y1, x2, y2);
		   }
         for (int kk = k; kk < isoDesc [i].size; kk++) { // restant en segment
            pt = newIsoc [kk]; 
            x = getX (pt.lon); 
            y = getY (pt.lat); 
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
   showUnicode (cr, BOAT_UNICODE, lon, lat);
   circle (cr, lon, lat, 1.0, 0.0, 0.0); // red
}

/*! draw the routes based on route table, from pOr to pDest or best point */
static void drawRoute (cairo_t *cr) {
   double x = getX (par.pOr.lon);
   double y = getY (par.pOr.lat);
   cairo_move_to (cr, x, y);
   CAIRO_SET_SOURCE_RGB_PINK(cr);

   for (int i = 1; i < route.n; i++) {
      x = getX (route.t[i].lon);
      y = getY (route.t[i].lat);
      cairo_line_to (cr, x, y);
   }
   cairo_stroke (cr);
}

/*! draw Point of Interests */
static void drawPoi (cairo_t *cr) {
   double x, y;
   for (int i = 0; i < nPoi; i++) {
      x = getX (tPoi [i].lon);
      y = getY (tPoi [i].lat);
      if (tPoi [i].level <= par.maxPoiVisible) {
         switch (tPoi [i].type) {
         case UNVISIBLE:
            break;
         case CAT:
            showUnicode (cr, CAT_UNICODE, tPoi [i].lon, tPoi [i].lat);
            break;
         case NORMAL: case NEW:
            circle (cr, tPoi [i].lon, tPoi [i].lat, 0.0, 0.0, 0.0);
            cairo_move_to (cr, x+10, y);
            cairo_show_text (cr, g_strdup_printf("%s", tPoi [i].name));
         case PORT:
            if (dispZone.zoom > MIN_ZOOM_POI_VISIBLE) {
               CAIRO_SET_SOURCE_RGB_BLACK (cr); // points
               cairo_move_to (cr, x+10, y);
               gchar *utf8String = g_locale_to_utf8(tPoi [i].name, -1, NULL, NULL, NULL);
               cairo_show_text (cr, g_strdup_printf("%s", utf8String));
               free (utf8String);
               cairo_rectangle (cr, x, y, 1, 1);
               cairo_fill (cr);
            }
            break;
         }
      }
   }
   cairo_stroke (cr);
}


/*! return true if rectangle defined by parameters interects with display zone */
bool isRectangleIntersecting (double latMin, double latMax, double lonMin, double lonMax) {
    return (latMin <= dispZone.latMax && latMax >= dispZone.latMin && lonMin <= dispZone.lonRight && lonMax >= dispZone.lonLeft);
           //latMin <=zone.latMax && latMax >= zone.latMin && lonMin <= zone.lonRight && lonMax >= zone.lonLeft);
}

/*! draw shapefile with different color for sea and earth */
inline static gboolean draw_shp_map (cairo_t *cr) {
   register double x, y;
   register double alpha;
   register int iPart;
   register int step = 1;

   // Dessiner les entités
   for (int i = 0; i < nTotEntities; i++) {
      if ((entities [i].nSHPType != POINT) && (! isRectangleIntersecting (entities[i].latMin, entities[i].latMax, entities[i].lonMin, entities[i].lonMax)))
         continue;
      x = getX (entities[i].points[0].lon);
      y = getY (entities[i].points[0].lat);
      step = (dispZone.zoom < 5) ? 128 : (dispZone.zoom < 20) ? 64 : (dispZone.zoom < 50) ? 32 : (dispZone.zoom < 100) ? 16 : (dispZone.zoom < 500) ? 8 : 1;
      
      //printf ("draw_sh_map zoom: %.2lf, step: %d\n", dispZone.zoom, step);
      switch (entities [i].nSHPType) {
      case SHPT_POLYGON: // case SHPT_POLYGONZ:// Traitement pour les polygones
         alpha = (par.showColors >= 1) ? 0.5 : 1.0;
         cairo_set_source_rgba (cr, 157.0/255.0, 162.0/255.0, 12.0/255.0, alpha); // Polygones en jaune
         cairo_move_to (cr, x, y);
         iPart = 0;
         
         for (int j = 1; j < entities[i].numPoints; j += step) {
            x = getX (entities[i].points[j].lon);
            y = getY (entities[i].points[j].lat);
            if (j >= entities[i].index [iPart]) {
               //cairo_stroke(cr);
               cairo_close_path(cr);
               //CAIRO_SET_SOURCE_RGB_BLACK (cr); //lignes
               cairo_fill(cr);
               //j = entities[i].index [iPart];
               iPart += 1;
            }
            else cairo_line_to (cr, x, y);
         }
         cairo_close_path(cr);
         cairo_fill(cr);
        break;
      case SHPT_ARC: //case SHPT_ARCZ: // Traitement pour les lignes (arcs)
         CAIRO_SET_SOURCE_RGB_RED(cr); //lignes
         cairo_move_to (cr, x, y);
         iPart = 0;
         for (int j = 1; j < entities[i].numPoints; j += step) {
            x = getX (entities[i].points[j].lon);
            y = getY (entities[i].points[j].lat);
            cairo_line_to (cr, x, y);
            if (j >= entities[i].index [iPart]) {
               cairo_stroke(cr);
               //j = entities[i].index [iPart];
               iPart += 1;
            }
         }
         cairo_stroke(cr);
         break;
      case POINT:
         CAIRO_SET_SOURCE_RGB_RED(cr); // points
         cairo_rectangle (cr, x, y, 1, 1);
         cairo_fill (cr);
         break;
      case SHPT_NULL: // empty
         break;
      default: fprintf (stderr, "In draw_shp_map, SHPtype unknown: %d\n", entities [i].nSHPType); 
      }
   }
   return FALSE;
}

/*! draw something that represent the waves */
static void showWaves (cairo_t *cr, Pp pt, double w) {
   double x = getX (pt.lon);
   double y = getY (pt.lat);
   if ((w <= 0) || (w > 100)) return;
   CAIRO_SET_SOURCE_RGB_GRAY(cr);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 6);
   cairo_move_to (cr, x, y);
   cairo_show_text (cr, g_strdup_printf("%.2lf", w));
}

/*! draw arrow that represent the direction of the wind */
static void arrow (cairo_t *cr, Pp pt, double head_x, double head_y, double u, double v, double twd, double tws, int typeFlow) {
   const double arrowSize = 10.0;
   if (tws == 0 || fabs (u) > 100 || fabs (v) > 100) return;
   // Calculer les coordonnées de la queue
   double tail_x = head_x - 30 * u/tws;
   double tail_y = head_y + 30 * v/tws;

   if (typeFlow == WIND) CAIRO_SET_SOURCE_RGB_BLACK (cr);
   else CAIRO_SET_SOURCE_RGB_ORANGE (cr); 
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
   double head_x = getX (pt.lon); 
   double head_y = getY (pt.lat); 
   // Calculer les coordonnées de la queue
   double tail_x = head_x - 30 * u/tws;
   double tail_y = head_y + 30 * v/tws;
   if (typeFlow == WIND) CAIRO_SET_SOURCE_RGB_BLACK(cr);
   else CAIRO_SET_SOURCE_RGB_ORANGE(cr);
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
   memset (&pt, 0, sizeof (Pp));
   double u = 0, v = 0, g = 0, w = 0, uCurr = 0, vCurr = 0, bidon;
   char seaEarth [10];
   char strLat [MAX_SIZE_NAME] = "", strLon [MAX_SIZE_NAME] = "";
   double tDeltaCurrent = zoneTimeDiff (currentZone, zone);
   pt.lat = yToLat (whereIsMouse.y);
   pt.lon = xToLon (whereIsMouse.x);
   pt.lon = lonCanonize (pt.lon);
   if (par.constWindTws != 0) {
	   u = - KN_TO_MS * par.constWindTws * sin (DEG_TO_RAD * par.constWindTwd);
	   v = - KN_TO_MS * par.constWindTws * cos (DEG_TO_RAD * par.constWindTwd);
   }
	else findFlow (pt, zone.timeStamp [kTime], &u, &v, &g, &w, zone, gribData);
	//else findFlow (pt, theTime, &u, &v, &w, zone, gribData);
   strcpy (seaEarth, (extIsSea (pt.lon, pt.lat)) ? "OK" : "KO");
   if (par.constCurrentS != 0) {
      uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
      vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
   }
   else {
	   findFlow (pt, currentZone.timeStamp [kTime] - tDeltaCurrent, &uCurr, &vCurr, &bidon, &bidon, currentZone, currentGribData);
   }
   
   sprintf (sStatus, "%s         %d/%ld      %s %s, %s\
      Wind: %03d° %05.2lf Knots  Gust: %05.2lf Knots  Waves: %05.2lf  Current: %03d° %05.2lf Knots         %s      Zoom: %.2lf       %s",
      newDate (zone.dataDate [0], (zone.dataTime [0]/100)+ zone.timeStamp [kTime], pDate),\
      kTime + 1, zone.nTimeStamp, " ",\
      latToStr (pt.lat, par.dispDms, strLat),\
      lonToStr (pt.lon, par.dispDms, strLon),\
      (int) (extTwd (u,v) + 360) % 360, extTws (u,v), MS_TO_KN * g, w, 
      (int) (extTwd (uCurr, vCurr) + 360) % 360, extTws (uCurr, vCurr), 
      seaEarth,
      dispZone.zoom,
      (gribRequestRunning || readGribRet == -1) ? "WAITING GRIB" : "");
  
   gtk_statusbar_push(GTK_STATUSBAR(statusbar), context_id, sStatus);

}

/*! draw the main window */
static gboolean drawGribCallback (GtkWidget *widget, cairo_t *cr, gpointer data) {
   double u, v, gust, w, twd, tws, uCurr, vCurr, bidon, head_x, head_y;
   char str [MAX_SIZE_LINE] = "";
   double tDeltaCurrent = zoneTimeDiff (currentZone, zone);
   Pp pt;
   memset (&pt, 0, sizeof (Pp));
   int width, height;
   guint8 r, g, b;
   sprintf (str, "%s %s %s", PROG_NAME, PROG_VERSION, par.gribFileName);
   gtk_window_set_title (GTK_WINDOW (window), str); // window is a global variable
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel(widget)), &width, &height);
   dispZone.xL = 0;      // right
   dispZone.xR = width;  // left
   dispZone.yT = 0;      // top
   dispZone.yB = height; // bottom
   theTime = zone.timeStamp [kTime];

   // printf ("drawGribCallback 1\n");
   CAIRO_SET_SOURCE_RGB_WHITE(cr);
   cairo_paint(cr);                   // clear all
   if (par.showColors != 0) {
      if (par.constWindTws != 0) {
         mapColors (par.constWindTws, &r, &g, &b);
         cairo_set_source_rgba (cr, r/255.0, g/255.0, b/255.0, 0.5);
         cairo_rectangle (cr, 1, 1, width, height);
         cairo_fill(cr);
      }
      else {
         paintWind (cr, width, height);
      }
   }

   draw_shp_map (cr);
   CAIRO_SET_SOURCE_RGB_LIGHT_GRAY (cr);
   cairo_set_line_width(cr, 0.5);
   // Quelques meridiens
   for (int intLon = -90; intLon <= 180; intLon += 90) {
      cairo_move_to (cr, getX (intLon), getY (85.0));
      cairo_line_to (cr, getX (intLon), getY (-85.0));
      cairo_stroke(cr);
   }
   // Equateur
   cairo_move_to (cr, dispZone.xR, getY (0.0));
   cairo_line_to (cr, dispZone.xL, getY (0.0));
   cairo_stroke(cr);

   //printf ("disp.latMin: %.2lf, disp.latMax: %.2lf, disp.lonLeft: %.2lf, disp.lonRight: %.2lf\ndisp.latStep: %.2lf, disp.latStep: %.2lf", dispZone.latMin, dispZone.latMax, dispZone.lonLeft, dispZone.lonRight, dispZone.latStep, dispZone.lonStep); 
   
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
	            findFlow (pt, theTime, &u, &v, &gust, &w, zone, gribData);
            }
            twd = extTwd (u, v);
            tws = extTws (u, v);
	         if (par.windDisp == BARBULE) barbule (cr, pt, u, v, tws, WIND);
            else if (par.windDisp == ARROW) {
               head_x = getX (pt.lon); 
               head_y = getY (pt.lat); 
               arrow (cr, pt, head_x, head_y, u, v, twd, tws, WIND);
            }
            if (par.constWave != 0) w = par.constWave;
            if (par.waveDisp) showWaves (cr, pt, w);

            if (par.currentDisp) {
               if (par.constCurrentS != 0) {
                  uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
                  vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
               }
               else {
	               findFlow (pt, theTime - tDeltaCurrent, &uCurr, &vCurr, &bidon, &w, currentZone, currentGribData);
               }
               if ((uCurr != 0) || (vCurr !=0)) {
                  tws = extTws (uCurr, vCurr);
                  barbule (cr, pt, uCurr, vCurr, tws, CURRENT);
               }
            }
	      }
         pt.lon += (dispZone.lonStep) / 2;
      }
      pt.lat += (dispZone.latStep) / 2;
   }
   calculateOrthoRoute ();
   drawOrthoRoute (cr, ORTHO_ROUTE_PARAM);
   drawLoxoRoute (cr);
   circle (cr, par.pOr.lon, par.pOr.lat, 0.0, 1.0, 0.0);
   if (! isnan (my_gps_data.lon) && ! isnan (my_gps_data.lat))
      circle (cr, my_gps_data.lon, my_gps_data.lat , 1.0, 0.0, 0.0);
   if ((route.n != 0) && (isfinite(route.totDist)) && (route.totDist > 0)) { 
      drawAllIsochrones (cr, par.style);
      drawRoute (cr);
      focusOnPointInRoute (cr);
      Pp selectedPt = isocArray [nIsoc - 1][selectedPointInLastIsochrone]; // delected point in last isochrone
      circle (cr, selectedPt.lon, selectedPt.lat, 1.0, 0.0, 1.1);
   }
   if (destPressed) circle (cr, par.pDest.lon, par.pDest.lat, 0.0, 0.0, 1.0);
   if (polygonStarted) {
      for (int i = 0; i < forbidZones [par.nForbidZone].n ; i++) {
         // printf ("polygonStart i = %d\n", i);
         circle (cr, forbidZones [par.nForbidZone].points [i].lon, forbidZones [par.nForbidZone].points [i].lat, 1.0, 0, 0);
      }
      // cairo_fill_preserve(cr);
   }
   drawPoi (cr);
   // Dessin egalement le rectangle de selection si en cours de selection
   if (selecting) {
      cairo_set_source_rgba (cr, 1.0, 0.0, 0.0, 0.5); // Rouge semi-transparent
      cairo_rectangle (cr, whereWasMouse.x, whereWasMouse.y, whereIsMouse.x - whereWasMouse.x, whereIsMouse.y - whereWasMouse.y);
      cairo_fill(cr);
   }
   drawForbidArea (cr);

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
   CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr);
  
   for (int i = 1; i <= nStep; i++)
      cairo_arc (cr, centerX, centerY, i * rStep, -G_PI/2, G_PI/2);  //trace cercle
   
   for (double angle = -90; angle <= 90; angle += 22.5) {            // segments
      cairo_move_to (cr, centerX, centerY);
      cairo_rel_line_to (cr, rMax * cos (DEG_TO_RAD * angle),
         rMax * sin (DEG_TO_RAD * angle));
   }
   cairo_stroke(cr);
   CAIRO_SET_SOURCE_RGB_DARK_GRAY(cr);

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
   CAIRO_SET_SOURCE_RGB_GRAY(cr);
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
static void getPolarXYbyValue (int type, int l, double w, double width, double height, double radiusFactor, double *x, double *y) {
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double angle = (90 - ptMat->t [l][0]) * DEG_TO_RAD;  // Convertir l'angle en radians
   double val = extFindPolar (ptMat->t [l][0], w, *ptMat);
   double radius = val * radiusFactor;
   *x = width / 2 + radius * cos(angle);
   *y = height / 2 - radius * sin(angle);
}

/*! return x and y in polar */
static void getPolarXYbyCol (int type, int l, int c, double width, double height, double radiusFactor, double *x, double *y) {
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double angle = (90 - ptMat->t [l][0]) * DEG_TO_RAD;  // Convertir l'angle en radians
   double radius = ptMat->t [l][c] * radiusFactor;
   *x = width / 2 + radius * cos(angle);
   *y = height / 2 - radius * sin(angle);
}

/*! Draw polar from polar matrix */
static void drawPolarBySelectedTws (cairo_t *cr, int polarType, PolMat ptMat,
   double selectedTws, double width, double height, double radiusFactor) {
   
   double x, y, x1, x2, y1, y2;
   cairo_set_line_width (cr, 5);
   cairo_set_source_rgba (cr, 1.0, 0, 0, 1.0);
   getPolarXYbyValue (polarType, 1, selectedTws, width, height, radiusFactor, &x, &y);
   cairo_move_to (cr, x, y);

   // segments
   if (segmentOrBezier == SEGMENT) {
      for (int l = 2; l < ptMat.nLine; l += 1) {
         getPolarXYbyValue (polarType, l, selectedTws, width, height, radiusFactor, &x, &y);
         cairo_line_to(cr, x, y);
      }
   }
   // Tracer la courbe de Bézier si nb de points suffisant
   else {
      for (int l = 2; l < ptMat.nLine - 2; l += 3) {
         getPolarXYbyValue (polarType, l, selectedTws, width, height, radiusFactor, &x, &y);
         getPolarXYbyValue (polarType, l+1, selectedTws, width, height, radiusFactor, &x1, &y1);
         getPolarXYbyValue (polarType, l+2, selectedTws, width, height, radiusFactor, &x2, &y2);
         cairo_curve_to(cr, x, y, x1, y1, x2, y2);
      }
      getPolarXYbyValue (polarType, ptMat.nLine -1, selectedTws, width, height, radiusFactor, &x, &y);
      cairo_line_to(cr, x, y);
   }
   cairo_stroke(cr);
}

/*! Draw polar from polar matrix */
static void drawPolarAll (cairo_t *cr, int polarType, PolMat ptMat, 
   double width, double height, double radiusFactor) {

   double x, y, x1, x2, y1, y2;
   cairo_set_line_width (cr, 1);
   int minCol = (selectedPol == 0) ? 1 : selectedPol;
   int maxCol = (selectedPol == 0) ? ptMat.nCol : selectedPol + 1;

   for (int c = minCol; c < maxCol; c++) {
      GdkRGBA color = colors [c % nColors];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      getPolarXYbyCol (polarType, 1, c, width, height, radiusFactor, &x, &y);
      cairo_move_to (cr, x, y);

      // segments
      if (segmentOrBezier == SEGMENT) {
         for (int l = 2; l < ptMat.nLine; l += 1) {
            getPolarXYbyCol (polarType, l, c, width, height, radiusFactor, &x, &y);
            cairo_line_to(cr, x, y);
         }
      }
      // Tracer la courbe de Bézier si nb de points suffisant
      else {
         for (int l = 2; l < ptMat.nLine - 2; l += 3) {
            getPolarXYbyCol (polarType, l, c, width, height, radiusFactor, &x, &y);
            getPolarXYbyCol (polarType, l+1, c, width, height, radiusFactor, &x1, &y1);
            getPolarXYbyCol (polarType, l+2, c, width, height, radiusFactor, &x2, &y2);
            cairo_curve_to(cr, x, y, x1, y1, x2, y2);
         }
         getPolarXYbyCol (polarType, ptMat.nLine -1, c, width, height, radiusFactor, &x, &y);
         cairo_line_to(cr, x, y);
      }
      cairo_stroke(cr);
   }
}

/*! Draw polar from polar matrix */
static void on_draw_polar_event (GtkWidget *widget, cairo_t *cr, gpointer user_data) {
   int width, height;
   PolMat *ptMat = (polarType == WAVE_POLAR) ? &wavePolMat : &polMat; 
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel(widget)), &width, &height);
   //double radiusFactor = (polarType == WAVE_POLAR) ? width / 40 : width / (maxValInPol (*ptMat) * 5); // Ajustez la taille de la courbe
   double radiusFactor;
   radiusFactor = width / (maxValInPol (*ptMat) * 6); // Ajustez la taille de la courbe
   polarTarget (cr, polarType, width, height, radiusFactor);
   polarLegend (cr, polarType);
   drawPolarAll (cr, polarType, *ptMat, width, height, radiusFactor);
   drawPolarBySelectedTws (cr, polarType, *ptMat, selectedTws, width, height, radiusFactor);
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/*! find index in polar to draw */
static void on_filter_changed(GtkComboBox *widget, gpointer user_data) {
   selectedPol = gtk_combo_box_get_active(GTK_COMBO_BOX(filter_combo));
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/*! combo filter zone for polar */
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

/*! display polar matrix */
static void polarDump (int type) {
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

/*! callBack polar dumo */
static void cbPolarDump (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data;
   polarType = *comportement;
   polarDump (polarType);
}

/*! callback function in polar for edit */
static void on_edit_button_polar_clicked(GtkWidget *widget, gpointer data) {
   char line [MAX_SIZE_LINE] = "";
   char fileName [MAX_SIZE_NAME] = "";
   strcpy (fileName, (polarType == WAVE_POLAR) ? par.wavePolFileName : par.polarFileName);
   sprintf (line, "%s %s \n", par.spreadsheet, fileName);
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

/*! Callback for polar to change scale calue */
static void on_scale_value_changed (GtkScale *scale, gpointer user_data) {
   selectedTws = gtk_range_get_value(GTK_RANGE(scale));
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/*! Draw polar from polar file */
static void polarDraw (int type) {
   char line [MAX_SIZE_LINE] = "Polar: "; 
   int polar = POLAR, wavePolar = WAVE_POLAR;         // distinction polar and wavePolar for polarDraw call
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
   g_signal_connect(G_OBJECT(dumpButton), "clicked", G_CALLBACK(cbPolarDump), (type == WAVE_POLAR) ? &wavePolar : &polar);
   
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

   // Créer le widget GtkScale
   int maxScale = ptMat->t[0][ptMat->nCol-1]; // max value of wind or waves
   GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, maxScale, 1);
   gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_TOP);
   gtk_widget_set_size_request (scale, 300, -1);  // Ajuster la largeur du GtkScale
   g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_value_changed), NULL);
  
   hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_box_pack_start(GTK_BOX (hbox), filter_combo, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), segmentRadio, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), bezierRadio, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), dumpButton, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), editButton, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX (hbox), scale, FALSE, FALSE, 0);
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

/*! callBack polar draw */
static void cbPolarDraw (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data;
   polarType = *comportement;
   polarDraw (polarType);
}

/*! time management init */
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

/*! calculate difference in hours between departure time and time 0 */
static double getDepartureTimeInHour (MyDate *start) {
   time_t seconds = 0;
   struct tm *tmStart = gmtime (&seconds);
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

/*! one line for niceReport () */
static void lineReport (GtkWidget *grid, int l, const char* iconName, const char *libelle, const char *value) {
   GtkWidget *icon = gtk_button_new_from_icon_name (iconName, GTK_ICON_SIZE_BUTTON);
   GtkWidget *label = gtk_label_new (libelle);
   gtk_grid_attach(GTK_GRID (grid), icon, 0, l, 1, 1);
   gtk_label_set_yalign(GTK_LABEL(label), 0);
   gtk_label_set_xalign(GTK_LABEL(label), 0);   
   gtk_grid_attach(GTK_GRID (grid), label,1, l, 1, 1);
   GtkWidget *labelValue = gtk_label_new (value);
   gtk_label_set_yalign(GTK_LABEL(labelValue), 0);
   gtk_label_set_xalign(GTK_LABEL(labelValue), 1);   
   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    // Ajouter le séparateur à la grille
   gtk_grid_attach (GTK_GRID (grid), labelValue, 2, l, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), separator,    0, l + 1, 3, 1);
   gtk_widget_set_margin_end(GTK_WIDGET(labelValue), 20);
}

/*! display nice report */
static void niceReport (double computeTime) {
   char totalDate [MAX_SIZE_DATE]; 
   char *pDate = &totalDate [0];
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   if (computeTime > 0)
      sprintf (line,"%s      Compute Time:%.2lf sec.", (route.destinationReached) ? 
       "Destination reached" : "Destination unreached", computeTime);
   else 
      sprintf (line,"%s", (route.destinationReached) ? "Destination reached" : "Destination unreached");

   GtkWidget *dialog = gtk_dialog_new_with_buttons (line, NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                          NULL, NULL, NULL, NULL, NULL);

   gtk_widget_set_size_request(dialog, 400, -1);
   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

   GtkWidget *grid = gtk_grid_new();   
   gtk_container_add(GTK_CONTAINER(content_area), grid);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
   
   sprintf (line, "%4d/%02d/%02d %02d:%02d\n", start->year, start->mon+1, start->day, start->hour, start->min);
   lineReport (grid, 0, "document-open-recent", "Departure Date and Time", line);

   sprintf (line, "%.2lf\n", par.startTimeInHours);
   lineReport (grid, 2, "dialog-information-symbolic", "Nb hours after origin of grib", line);
   
   sprintf (line, "%d\n", nIsoc);
   lineReport (grid, 4, "accessories-text-editor-symbolic", "Nb of isochrones", line);

   sprintf (line, "%s %s\n", latToStr (lastClosest.lat, par.dispDms, strLat), lonToStr (lastClosest.lon, par.dispDms, strLon));
   lineReport (grid, 6, "emblem-ok-symbolic", "Best Point Reached", line);

   sprintf (line, "%.2lf\n", (route.destinationReached) ? 0 : orthoDist (lastClosest.lat, lastClosest.lon,\
         par.pDest.lat, par.pDest.lon));
   lineReport (grid, 8, "mail-forward-symbolic", "Distance To Destination", line);

   sprintf (line, "%s\n",\
      newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route.duration, pDate));
   lineReport (grid, 10, "alarm-symbolic", "Arrival Date and Time", line);

   sprintf (line, "%.2lf\n", route.totDist);
   lineReport (grid, 12, "emblem-important-symbolic", "Total Distance in Nautical Miles", line);
   
   sprintf (line, "%.2lf\n", route.duration);
   lineReport (grid, 14, "user-away", "Duration in Hours", line);
   
   sprintf (line, "%.2lf\n", route.totDist/route.duration);
   lineReport (grid, 16, "utilities-system-monitor-symbolic", "Mean Speed Over Ground", line);

   gtk_widget_show_all (dialog);
   gtk_dialog_run (GTK_DIALOG (dialog));
   gtk_widget_destroy (dialog);
}

/*! launch routing */
static void on_run_button_clicked (GtkWidget *widget, gpointer data) {
   int ret;
   struct timeval t0, t1;
   long ut0, ut1;
   double lastStepDuration = 0.0;
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
      niceReport (((double) ut1-ut0)/MILLION);
   }
   selectedPointInLastIsochrone = (nIsoc <= 1) ? 0 : isoDesc [nIsoc -1].closest; 
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

/*! display isochrones */
static void isocDump (GtkWidget *widget, gpointer data) {
   char *buffer = NULL;
   const int N_POINT_FACTOR = 1000;
   if (nIsoc == 0)
      infoMessage ("No isochrone", GTK_MESSAGE_INFO); 
   else {
      if ((buffer = (char *) malloc (nIsoc * MAX_SIZE_LINE * N_POINT_FACTOR)) == NULL)
         infoMessage ("Not enough memory", GTK_MESSAGE_ERROR); 
      else {
         allIsocToStr (buffer); 
         displayText (buffer, "Isochrones");
         free (buffer);
      }
   }
}

/*! display isochrone descriptors */
static void isocDescDump (GtkWidget *widget, gpointer data) {
   char *buffer = NULL;
   if (route.n <= 0)
      infoMessage ("No route calculated", GTK_MESSAGE_INFO);
   else {
      if ((buffer = (char *) malloc (nIsoc * MAX_SIZE_LINE)) == NULL)
         infoMessage ("Not enough memory", GTK_MESSAGE_ERROR); 
      else {
         if (isoDectToStr (buffer, nIsoc * MAX_SIZE_LINE))
            displayText (buffer, "Isochrone Descriptor");
         else infoMessage ("Not enough space", GTK_MESSAGE_ERROR);
      }
   }
}

/*! display Points of Interest information */
static void poiDump (GtkWidget *widget, gpointer data) {
   char line [MAX_SIZE_LINE] = "Point of Interests";
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   int count = 0;
   int l = 2;
   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   GtkWidget *label;

   GtkWidget *dialog = gtk_dialog_new_with_buttons (line, NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                          NULL, NULL, NULL, NULL, NULL);

   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

   GtkWidget *grid = gtk_grid_new();   
   gtk_container_add (GTK_CONTAINER(content_area), grid);
   gtk_grid_set_column_spacing (GTK_GRID(grid), 20);
   gtk_grid_set_row_spacing (GTK_GRID(grid), 5);

   gtk_grid_set_row_homogeneous (GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous (GTK_GRID(grid), FALSE);
   
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Lat."),       0, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Lon."),       1, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Type"),       2, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Level"),      3, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Name"),       4, 0, 1, 1);
   separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID(grid), separator,                      0, 1, 5, 1);

   for (int i = 0; i < nPoi; i++) {
      if ((tPoi [i].type != UNVISIBLE) && (tPoi [i].type != PORT)) {
         label = gtk_label_new (latToStr (tPoi[i].lat, par.dispDms, strLat));
         gtk_grid_attach(GTK_GRID (grid), label, 0, l, 1, 1);
         
         label = gtk_label_new (lonToStr (tPoi[i].lon, par.dispDms, strLon));
         gtk_grid_attach (GTK_GRID (grid), label, 1, l, 1, 1);
         
         sprintf (line, "%d", tPoi [i].type);
         label = gtk_label_new (line);
         gtk_grid_attach (GTK_GRID (grid), label, 2, l, 1, 1);
         
         sprintf (line, "%d", tPoi [i].level);
         label = gtk_label_new (line);
         gtk_grid_attach (GTK_GRID (grid), label, 3, l, 1, 1);
         
         label = gtk_label_new (tPoi [i].name);
         gtk_grid_attach (GTK_GRID (grid), label, 4, l, 1, 1);

         separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
         gtk_grid_attach (GTK_GRID(grid), separator, 0, l + 1, 5, 1);
         gtk_widget_set_margin_end (GTK_WIDGET(label), 20);
         gtk_label_set_yalign(GTK_LABEL(label), 0);
         gtk_label_set_xalign(GTK_LABEL(label), 0);
         l += 2;
         count += 1;
      }
   }
   sprintf (line, "Points of Interest (Number: %d)", count);
   gtk_window_set_title (GTK_WINDOW(dialog), line);
   gtk_widget_show_all (dialog);
   gtk_dialog_run (GTK_DIALOG (dialog));
   gtk_widget_destroy (dialog);
}

/*! print the report of route calculation */
static void rteReport (GtkWidget *widget, gpointer data) {
   if (route.n <= 0)
      infoMessage ("No route calculated", GTK_MESSAGE_INFO);
   else {
      niceReport (0);
   }
}

/*! print the route from origin to destination or best point */
static void rteDump (GtkWidget *widget, gpointer data) {
   char buffer [MAX_SIZE_BUFFER];
   if (route.n <= 0)
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

/*! edit poi or port */
static void poiEdit (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data;
   char line [MAX_SIZE_LINE] = "";
   sprintf (line, "%s %s\n", par.spreadsheet, ((*comportement) == POI_SEL) ? par.poiFileName: par.portFileName);
   if (system (line) != 0) {
      fprintf (stderr, "Error in edit Poi. System call: %s\n", line);
      return;
   }
   if (confirm (((*comportement) == POI_SEL) ? par.poiFileName : par.portFileName, "Confirm loading file below")) {
      nPoi = 0;
      nPoi = readPoi (par.poiFileName);
      nPoi += readPoi (par.portFileName);
   }
}

/*! save Points of Interest information in file */
static void poiSave (GtkWidget *widget, gpointer data) {
   if (confirm (par.poiFileName, "Write"))
      writePoi (par.poiFileName);
}

/*! show GPS information */
static void gpsDump (GtkWidget *widget, gpointer data) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   if (isnan (my_gps_data.lon) || isnan (my_gps_data.lat)) {
      infoMessage ("No GPS Data available", GTK_MESSAGE_INFO);
      return;
   }

   GtkWidget *dialog = gtk_dialog_new_with_buttons ("GPS Information", NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                          NULL, NULL, NULL, NULL, NULL);

   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

   GtkWidget *grid = gtk_grid_new();   
   gtk_container_add(GTK_CONTAINER(content_area), grid);

   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);

   sprintf (line, "%s %s\n", \
      latToStr (my_gps_data.lat, par.dispDms, strLat), lonToStr (my_gps_data.lon, par.dispDms, strLon));
   lineReport (grid, 0, "network-workgroup-symbolic", "Position", line);

   sprintf (line, "%.2f\n", my_gps_data.alt);
   lineReport (grid, 2, "airplane-mode-symbolic", "Altitude", line);

   sprintf (line, "%d\n", my_gps_data.status);
   lineReport (grid, 4, "dialog-information-symbolic", "Status", line);

   sprintf (line, "%d\n", my_gps_data.nSat);
   lineReport (grid, 6, "preferences-system-network-symbolic", "Number of satellites", line);

   struct tm *timeinfo = gmtime (&my_gps_data.timestamp.tv_sec);
   sprintf(line, "%d-%02d-%02d %02d:%02d:%02d UTC\n", 
      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
   lineReport (grid, 8, "document-open-recent", "GPS Time", line);

   gtk_widget_show_all (dialog);
   gtk_dialog_run (GTK_DIALOG (dialog));
   gtk_widget_destroy (dialog);
}

/*! for HTML home */
static void web_home_button_clicked (GtkWidget *widget, gpointer data) {
   WebKitWebView *webview = WEBKIT_WEB_VIEW(data);
   webkit_web_view_load_uri (webview, par.helpFileName);
}

/*! for HTML back */
static void web_back_button_clicked (GtkButton *button, gpointer data) {
   WebKitWebView *webview = WEBKIT_WEB_VIEW (data);
   webkit_web_view_go_back (webview);
}

/*! for HTML forward  */
static void web_forward_button_clicked (GtkWidget *widget, gpointer data) {
   WebKitWebView *webview = WEBKIT_WEB_VIEW (data);
   webkit_web_view_go_forward (webview);
}

/*! for text read file */
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
   const char *authors[2] = {PROG_AUTHOR, NULL};
   char str [MAX_SIZE_LINE];
   char strVersion [MAX_SIZE_LINE];
  
   sprintf (strVersion, "%s\nGTK version: %d.%d.%d\nWebKit version: %d.%d.%d\nGlib version: %d.%d.%d\nCompilation date: %s\n", 
      PROG_VERSION, gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
      webkit_get_major_version (), webkit_get_minor_version (),webkit_get_micro_version (), 
      GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION, 
      __DATE__);
   GtkWidget *p_about_dialog = gtk_about_dialog_new ();
   gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (p_about_dialog), strVersion);
   gtk_about_dialog_set_program_name  (GTK_ABOUT_DIALOG (p_about_dialog), PROG_NAME);
   gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (p_about_dialog), authors);
   gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (p_about_dialog), PROG_WEB_SITE);
   GdkPixbuf *p_logo = gdk_pixbuf_new_from_file (buildRootName (PROG_LOGO, str), NULL);
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
      spinner ("Grib File decoding", " ");
   }
   else {                 
      strncpy (par.currentGribFileName, fileName, MAX_SIZE_FILE_NAME - 1);
      readCurrentGribRet = -1; // Global variable
      g_thread_new("readCurrentGribThread", readCurrentGrib, NULL);
      currentGribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readCurrentGribCheck, NULL);
      spinner ("Current Grib File decoding", " ");
   }
}

/*! build url then download it and load the grib file */ 
static void urlDownloadGrib (int type, int indice) {
   char url [MAX_SIZE_BUFFER];
   char outputFileName [MAX_SIZE_LINE];
   sprintf (outputFileName, "%sgrib", par.workingDir);
   buildMeteoUrl (type, indice, url);
   if (urlChange (url)) {
      printf ("new url %s\n", url);
      strcat (outputFileName, strrchr (url, '/')); // le nom du fichier
      if (curlGet (url, outputFileName))
         loadGribFile (type, outputFileName);
      else infoMessage ("Error dowloading file", GTK_MESSAGE_ERROR);
   }
}

/*! Select grib file and read grib file, fill gribData */
static void openGrib (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data;
   char directory [MAX_SIZE_DIR_NAME];
   //strcat (directory, "grib");
   sprintf (directory, "%sgrib", par.workingDir);

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
   char directory [MAX_SIZE_DIR_NAME];
   sprintf (directory, "%spol", par.workingDir);
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
   char directory [MAX_SIZE_DIR_NAME];
   sprintf (directory, "%spar", par.workingDir);
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
   sprintf (line, "%s %s\n", par.editor, parameterFileName);
   if (system (line) != 0) {
      fprintf (stderr, "Error in editScenario. System call: %s\n", line);
      return;
   }
   if (confirm (parameterFileName, "Confirm loading file below")) {
      readParam (parameterFileName);
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
   char directory [MAX_SIZE_DIR_NAME];
   char *fileName;
   sprintf (directory, "%spar", par.workingDir);
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

/*! provides meta information about grib file, called by gribInfo*/
static void gribInfoDisplay (const char *fileName, Zone zone) {
   char buffer [MAX_SIZE_BUFFER];
   char line [MAX_SIZE_LINE] = "";
   char str [MAX_SIZE_NAME] = "";
   char str1 [MAX_SIZE_NAME] = "";
   char strTmp [MAX_SIZE_LINE] = "";
   char strLat0 [MAX_SIZE_NAME] = "", strLon0 [MAX_SIZE_NAME] = "";
   char strLat1 [MAX_SIZE_NAME] = "", strLon1 [MAX_SIZE_NAME] = "";
   char centreName [MAX_SIZE_NAME] = "";
   int l = 0;
   long timeStep;
   bool isTimeStepOK = true;
 
   // for (size_t i = 0; i < sizeof(dicTab) / sizeof(dicTab[0]); i++) // search name of center
   for (size_t i = 0; i < 4; i++) // search name of center
     if (dicTab [i].id == zone.centreId) strcpy (centreName, dicTab [i].name);
         
   sprintf (line,  "Centre ID: %ld %s   Ed. number: %ld", zone.centreId, centreName, zone.editionNumber);

   GtkWidget *dialog = gtk_dialog_new_with_buttons (line, NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                          NULL, NULL, NULL, NULL, NULL);
   gtk_widget_set_size_request(dialog, 400, -1);
   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

   GtkWidget *grid = gtk_grid_new();   
   gtk_container_add(GTK_CONTAINER(content_area), grid);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);

   // Définir l'espacement entre les colonnes (10 pixels)

   newDate (zone.dataDate [0], zone.dataTime [0]/100, strTmp);
   lineReport (grid, 0, "document-open-recent", "Date From", strTmp);
   
   newDate (zone.dataDate [0], zone.dataTime [0]/100 + zone.timeStamp [zone.nTimeStamp -1], strTmp);
   lineReport (grid, l+=2, "document-open-recent", "Date To", strTmp);

   sprintf (line, "%d", zone.nMessage);
   lineReport (grid, l+=2, "zoom-original-symbolic", "Nb. Messages", line);

   sprintf (line, "%ld", zone.stepUnits);
   lineReport (grid, l+=2, "document-page-setup", "Step Unit", line);

   sprintf (line, "%ld", zone.numberOfValues);
   lineReport (grid, l+=2, "document-page-setup-symbolic", "Nb. of Values", line);

   sprintf (line, "From: %s, %s To: %s %s", latToStr (zone.latMax, par.dispDms, strLat0), lonToStr (zone.lonLeft, par.dispDms, strLon0),
      latToStr (zone.latMin, par.dispDms, strLat1), lonToStr (zone.lonRight, par.dispDms, strLon1));
   lineReport (grid, l+=2, "network-workgroup-symbolic", "Zone ", line);

   sprintf (line, "%.3f° - %.3f°\n", zone.latStep, zone.lonStep);
   lineReport (grid, l+=2, "dialog-information-symbolic", "Lat Step - Lon Step", line);

   sprintf (line, "%ld - %ld\n", zone.nbLat, zone.nbLon);
   lineReport (grid, l+=2 , "preferences-desktop-locale-symbolic", "Nb. Lat - Nb. Lon", line);

   timeStep = zone.timeStamp [1] - zone.timeStamp [0];
   // check if regular
   for (size_t i = 1; i < zone.nTimeStamp - 1; i++)
      if ((zone.timeStamp [i] - zone.timeStamp [i-1]) != timeStep) {
         isTimeStepOK = false;
         printf ("timeStep: %ld other timeStep: %ld\n", timeStep, zone.timeStamp [i] - zone.timeStamp [i-1]);
      }

   sprintf (strTmp, "TimeStamp List of %s %ld", (isTimeStepOK)? "regular" : "UNREGULAR", zone.nTimeStamp);
   if (zone.nTimeStamp < 8 || ! isTimeStepOK) {
      strcpy (buffer, "[ ");
      for (size_t k = 0; k < zone.nTimeStamp; k++) {
         if ((k > 0) && ((k % 20) == 0)) 
            strcat (buffer, "\n");
         sprintf (str, "%ld ", zone.timeStamp [k]);
         strcat (buffer, str);
      }
      strcat (buffer, "]\n");
   }
   else {
      sprintf (buffer, "[%ld, %ld, ..%ld]\n", zone.timeStamp [0], zone.timeStamp [1], zone.timeStamp [zone.nTimeStamp - 1]);
   }
   lineReport (grid, l+=2, "view-list-symbolic", strTmp, buffer);
   
   sprintf (line, "[ ");
   for (size_t k = 0; k < zone.nShortName -1; k++) {
      sprintf (str, "%s ", zone.shortName [k]);
      strcat (line, str);
   }
   if (zone.nShortName > 0) {
      sprintf (str, "%s ]\n", zone.shortName [zone.nShortName -1]);
      strcat (line, str);
   }
   lineReport (grid, l+=2, "non-starred-symbolic", "ShortName List", line);

   sprintf (line, "%s\n", (zone.wellDefined) ? "Well defined" : "Undefined");
   lineReport (grid, l+=2 , (zone.wellDefined) ? "weather-clear" : "weather-showers", "Zone is", line);
   
   lineReport (grid, l+=2 , "mail-attachment-symbolic", "Grib File Name", fileName);
   
   formatThousandSep (str1, getFileSize (fileName));
    
   lineReport (grid, l+=2, "document-properties-symbolic", "Grib File size", str1);

   if ((zone.nDataDate > 1) || (zone.nDataTime > 1)) {
      sprintf (line, "Date: %ld, Time: %ld\n", zone.nDataDate, zone.nDataTime);
      lineReport (grid, l+=2, "software-update-urgent-symbolic", "Warning number of", line);
   }

   gtk_widget_show_all (dialog);
   gtk_dialog_run (GTK_DIALOG (dialog));
   gtk_widget_destroy (dialog);
}

/*! provides meta information about grib file */
static void gribInfo (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data; // either WIND or CURRENT
   char buffer [MAX_SIZE_BUFFER] = "";
   if (*comportement == WIND) {
      if (zone.nbLat == 0)
         infoMessage ("No wind data grib available", GTK_MESSAGE_ERROR);
      else {
         gribInfoDisplay (par.gribFileName, zone);
         if ((!zone.wellDefined ) || (zone.nShortName < 2))
            infoMessage ("No wind data grib check possible", GTK_MESSAGE_WARNING);
            else {
               if (checkGribToStr (buffer, zone, gribData))
                  displayText (buffer, "Grib Wind Check");
            }
      }
   }
   else { // current
      if (currentZone.nbLat == 0)
         infoMessage ("No current data grib available", GTK_MESSAGE_ERROR);
      else {
         gribInfoDisplay (par.currentGribFileName, currentZone);
         if ((!currentZone.wellDefined) || (currentZone.nShortName < 2)) 
            infoMessage ("No current data grib check possible", GTK_MESSAGE_WARNING);
         else {
            if (checkGribToStr (buffer, currentZone, currentGribData))
               displayText (buffer, "Grib Current Check");
         }
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
   sprintf (command, "%s %sgrib %s", par.imapScript, par.workingDir, par.mailPw); 
   fp = popen (command, "r");
   if (fp == NULL) {
      fprintf (stderr, "mailGribRead Error opening: %s\n", command);
      gribRequestRunning = false;
      gtk_widget_destroy (spinner_window);
      return TRUE;
   }
   while (fgets(line, sizeof(line)-1, fp) != NULL) {
      n += 1;
      strncat (buffer, line, MAX_SIZE_LINE);
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

/*! return maxTimeRange of GFS model based on timeStep and resolution */
static int maxTimeRange () {
   if (par.gribResolution <= 0.25)
      return 120;
   else if (par.gribResolution < 1.0)
      return (par.gribTimeStep < 6) ? 192 : 240;
   else return (par.gribTimeStep < 6) ? 192 : (par.gribTimeStep < 12) ? 240 : 384;
}

/*! return number of values x n message wich give an evaluation
   of grib file size in bytes */
static int evalSize () {
   const int nShortName = 4;
   int nValue = ((abs (memo.lat2-memo.lat1) / par.gribResolution) + 1) * ((abs (memo.lon2-memo.lon1) / par.gribResolution) + 1);
   int nMessage = nShortName * (1 + (par.gribTimeMax / par.gribTimeStep));
   return nMessage * nValue;
}

/*! Callback Time Step value */
static void time_step_changed(GtkSpinButton *spin_button, gpointer user_data) {
   char str [MAX_SIZE_LINE] = "";
   par.gribTimeStep = gtk_spin_button_get_value_as_int(spin_button);
   par.gribTimeMax = maxTimeRange ();
   if (spin_button_time_max != NULL) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin_button_time_max), par.gribTimeMax / 24);
      formatThousandSep (str, evalSize ());
      gtk_label_set_text(GTK_LABEL (val_size_eval), str);
   } 
   gtk_widget_queue_draw(dialog);
}

/*! Callback Time Max value */
static void time_max_changed(GtkSpinButton *spin_button, gpointer user_data) {
   char str [MAX_SIZE_LINE] = "";
   par.gribTimeMax = gtk_spin_button_get_value_as_int(spin_button) * 24;
   par.gribTimeMax = MIN (par.gribTimeMax, maxTimeRange ());
   if (spin_button_time_max != NULL) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin_button_time_max), par.gribTimeMax / 24);
      formatThousandSep (str, evalSize ());
      gtk_label_set_text(GTK_LABEL (val_size_eval), str);
   } 
   gtk_widget_queue_draw(dialog);
}

/*! Callback resolution */
static void resolution_changed(GtkSpinButton *spin_button, gpointer user_data) {
   char str [MAX_SIZE_LINE] = "";
   par.gribResolution = gtk_spin_button_get_value (spin_button);
   par.gribTimeMax = maxTimeRange ();
   if (spin_button_time_max != NULL) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin_button_time_max), par.gribTimeMax / 24);
      formatThousandSep (str, evalSize ());
      gtk_label_set_text(GTK_LABEL (val_size_eval), str);
   } 
   gtk_widget_queue_draw(dialog);
}

/*! Callback model combo box indice */
static void model_combo_changed (GtkComboBox *widget, gpointer user_data) {
   provider = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
}

/*! mail request dialog box */
static int mailRequestBox (double lat1, double lon1, double lat2, double lon2) {
   char strLat1 [MAX_SIZE_NAME] = "", strLon1 [MAX_SIZE_NAME] = "";
   char strLat2 [MAX_SIZE_NAME] = "", strLon2 [MAX_SIZE_NAME] = "";
   char str [MAX_SIZE_LINE] = "",  str1 [MAX_SIZE_LINE] = "";
   sprintf (str, "%s, %s to  %s, %s", latToStr (lat1, par.dispDms, strLat1), lonToStr (lon1, par.dispDms, strLon1),
         latToStr (lat2, par.dispDms, strLat2), lonToStr (lon2, par.dispDms, strLon2));

   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

   // Création de la boîte de dialogue ATT Global variable
   dialog = gtk_dialog_new_with_buttons ("Launch mail Grib request", \
      GTK_WINDOW(window), GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);

    // Récupération de la zone de contenu de la boîte de dialogue
   gtk_widget_set_size_request(dialog, 400, -1);
   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

   GtkWidget *grid = gtk_grid_new();   
   gtk_container_add(GTK_CONTAINER(content_area), grid);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   //gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   //gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
   
   GtkWidget *label_zone = gtk_label_new ("Zone");
   gtk_label_set_xalign(GTK_LABEL(label_zone), 0);
   gtk_grid_attach(GTK_GRID (grid), label_zone,                 0, 0, 1, 1);
   GtkWidget *label_buffer = gtk_label_new (str);
   gtk_grid_attach(GTK_GRID (grid), label_buffer,               1, 0, 3, 1);
   
   GtkWidget *label_model = gtk_label_new ("Model"); 
   GtkListStore *liststore = gtk_list_store_new (1, G_TYPE_STRING);
   // Ajouter des éléments à la liste
   
   GtkTreeIter iter;
   for (int i = 0;  i < N_PROVIDERS; i++) {
      gtk_list_store_append (liststore, &iter);
      gtk_list_store_set (liststore, &iter, 0, providerTab [i].libelle, -1);
   }

   // Créer une combobox et définir son modèle
   GtkWidget *model_combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
   g_object_unref(liststore);

   // Créer un rendu de texte pour afficher le texte dans la combobox
   GtkCellRenderer *myRenderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(model_combo_box), myRenderer, TRUE);
   gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(model_combo_box), myRenderer, "text", 0, NULL);
   gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo_box), provider);

   g_signal_connect(model_combo_box, "changed", G_CALLBACK (model_combo_changed), NULL);

   gtk_label_set_xalign(GTK_LABEL(label_model), 0);
   gtk_grid_attach(GTK_GRID(grid), label_model,          0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), model_combo_box,      1, 1, 3, 1);

   // Création des champs Time Step et Time Max
   GtkWidget *label_resolution = gtk_label_new("Resolution");
   // gtk_adjustment_new(gdouble value, gdouble lower, gdouble upper, gdouble step_increment, gdouble page_increment, gdouble page_size);
   GtkAdjustment *adjustment = gtk_adjustment_new (0.5, 0.25, 1.0, 0.25, 1.0, 0.0);

    // Création du GtkSpinButton avec GtkAdjustment
   GtkWidget *spin_button_resolution = gtk_spin_button_new(adjustment, 0.25, 2); // 0.25 resolution, 2 nombre de chiffres après la virgule
   gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin_button_resolution), par.gribResolution);
   g_signal_connect (spin_button_resolution, "value-changed", G_CALLBACK(resolution_changed), NULL);
   gtk_label_set_xalign(GTK_LABEL (label_resolution), 0);
   gtk_grid_attach(GTK_GRID (grid), label_resolution,           0, 2, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), spin_button_resolution,     1, 2, 1, 1);
   
   GtkWidget *label_time_step = gtk_label_new("Time Step");
   GtkWidget *spin_button_time_step = gtk_spin_button_new_with_range(3, 24, 3);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin_button_time_step), par.gribTimeStep);
   g_signal_connect (spin_button_time_step, "value-changed", G_CALLBACK(time_step_changed), NULL);
   gtk_label_set_xalign(GTK_LABEL (label_time_step), 0);
   gtk_grid_attach(GTK_GRID (grid), label_time_step,            0, 3, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), spin_button_time_step,      1, 3, 1, 1);

   sprintf (str, "Forecast time in days");
   GtkWidget *label_time_max = gtk_label_new (str);
   spin_button_time_max = gtk_spin_button_new_with_range (1, 16, 1); // ATT : Global variable
   gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin_button_time_max), par.gribTimeMax / 24);
   g_signal_connect (spin_button_time_max, "value-changed", G_CALLBACK(time_max_changed), NULL);
   gtk_label_set_xalign(GTK_LABEL (label_time_max), 0);
   gtk_grid_attach(GTK_GRID (grid), label_time_max,             0, 4, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), spin_button_time_max,       1, 4, 1, 1);

   GtkWidget *label_size_eval = gtk_label_new ("Number of values ");
   gtk_label_set_xalign(GTK_LABEL (label_size_eval), 0);

   memo.lat1 = lat1, memo.lat2 = lat2, memo.lon1 = lon1, memo.lon2 = lon2; 
   
   formatThousandSep (str1, evalSize ());
   val_size_eval = gtk_label_new (str1);
   gtk_widget_set_halign (val_size_eval, GTK_ALIGN_START);

   gtk_grid_attach(GTK_GRID (grid), label_size_eval, 0, 5, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), val_size_eval,   1, 5, 1, 1);
   
   gtk_grid_attach(GTK_GRID (grid), separator, 0, 6, 3, 1);
   
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

/*! For point of origin or point of destination */
static void on_entry_focus_out_event (GtkWidget *entry, GdkEvent *event, gpointer user_data) {
   const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
   double lat, lon;
   int poiIndex = -1;
   if ((strlen (name) >= MIN_NAME_SIZE) && ((poiIndex = findPoiByName (name, &lat, &lon)) != -1))
      gtk_entry_set_text(GTK_ENTRY(entry), tPoi [poiIndex].name);
   route.n = 0;
}

/*! Callback Radio button for Wind colors */ 
static void radio_button_Colors (GtkRadioButton *button, gpointer user_data) {
   par.showColors = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw(drawing_area); // affiche le tout
}

/*! Radio button for Wind Colar */
static GtkWidget *createRadioButtonColors (char *name, GtkWidget *hbox, GtkWidget *from, int i) {
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

/*!  Callback wind representation */
static void radio_button_Wind_Disp (GtkRadioButton *button, gpointer user_data) {
   par.windDisp = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw(drawing_area); // affiche le tout
}

/*! Radio button for Wind disp */
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

/*!  Callback Isochrone representation  */
static void radio_button_Isoc (GtkRadioButton *button, gpointer user_data) {
   par.style = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw(drawing_area); // affiche le tout
 }

/*! Choice Isochrone representation  */
static GtkWidget *createRadioButtonIsoc (char *name, GtkWidget *hbox, GtkWidget *from, int i) {
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

/*! Callback Degree Minutes Seconde choice */
static void radio_button_Dms (GtkRadioButton *button, gpointer user_data) {
   par.dispDms = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw(drawing_area); // affiche le tout
   statusBarUpdate ();
}

/*! Choice Degree Minutes Seconde choice */
static GtkWidget *createRadioButtonDms (char *name, GtkWidget *hbox, GtkWidget *from, int i) {
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

/*! Callback activated or not  */
static void on_checkbox_toggled(GtkWidget *checkbox, gpointer user_data) {
   gboolean *flag = (gboolean *)user_data;
   *flag = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox));
   gtk_widget_queue_draw(drawing_area);
}

/*! Callback for poi visibility change level */
static void on_level_poi_visible_changed (GtkScale *scale, gpointer user_data) {
   par.maxPoiVisible = gtk_range_get_value(GTK_RANGE(scale));
   gtk_widget_queue_draw (drawing_area);
}

/*! change parameters and pOr and pDest coordinates */ 
static void change (GtkWidget *widget, gpointer data) {
   char str0 [64], str1 [64];
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
      latToStr (par.pOr.lat, par.dispDms, originLatStr);
      lonToStr (par.pOr.lon, par.dispDms, originLonStr);
   }
   gtk_entry_set_text(GTK_ENTRY(entry_origin_lat), originLatStr);
   gtk_entry_set_text(GTK_ENTRY(entry_origin_lon), originLonStr);

   labelCreate (tab_tec, "Origin Lat",                  0, 0);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_origin_lat, 1, 0, 1, 1);
   labelCreate (tab_tec, "Lon",                         2, 0);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_origin_lon, 3, 0, 1, 1);
   g_signal_connect(G_OBJECT(entry_origin_lat), "focus-out-event", G_CALLBACK (on_entry_focus_out_event), NULL);

   // Création des éléments pour la destination
   entry_dest_lat = gtk_entry_new();
   entry_dest_lon = gtk_entry_new();
   char destLatStr[64], destLonStr[64];
   if (strlen (par.pDestName) > 0) {
      snprintf (destLatStr, sizeof(destLatStr), "%s", par.pDestName);
      snprintf (destLonStr, sizeof(destLonStr), "%s", "NA");
   }
   else {
      latToStr (par.pDest.lat, par.dispDms, destLatStr);
      lonToStr (par.pDest.lon, par.dispDms, destLonStr);
   }
   gtk_entry_set_text(GTK_ENTRY(entry_dest_lat), destLatStr);
   gtk_entry_set_text(GTK_ENTRY(entry_dest_lon), destLonStr);
   
   labelCreate (tab_tec, "Destination Lat",             0, 1);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_dest_lat,   1, 1, 1, 1);
   labelCreate (tab_tec, "Lon",                         2, 1);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_dest_lon,   3, 1, 1, 1);
   // Connexion de la fonction de rappel au signal "focus-out-event"
   g_signal_connect(G_OBJECT(entry_dest_lat), "focus-out-event", G_CALLBACK (on_entry_focus_out_event), NULL);

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
   GtkWidget *spin_opt = gtk_spin_button_new_with_range (0, 4, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_opt), par.opt);
   labelCreate (tab_tec, "Opt",                          0, 4);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_opt,          1, 4, 1, 1);
   
   labelCreate (tab_tec, "Max Isoc",                     2, 4);
   GtkWidget *spin_max_iso = gtk_spin_button_new_with_range(0, MAX_N_ISOC, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_max_iso), par.maxIso);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_max_iso,      3, 4, 1, 1);

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

   labelCreate (tab_tec, "j % Factor",                    2, 5);
   GtkWidget *spin_jFactor = gtk_spin_button_new_with_range (0, 100, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_jFactor), par.jFactor);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_jFactor,       3, 5, 1, 1);
   
   // Création des éléments pour sector
   GtkWidget *spin_kFactor = gtk_spin_button_new_with_range(0, 200, 5);
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

   labelCreate (tab_display, "",              0, 0); // just for space on top
   // showColor
   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    // Ajouter le séparateur à la grille

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

   gtk_grid_attach(GTK_GRID(tab_display), separator, 0, 7, 10, 1);
   // Créer le widget GtkScale
   const int MAX_LEVEL_POI_VISIBLE = 5; 
   labelCreate (tab_display, "level Poi Visible",     0, 8);
   GtkWidget *levelVisible = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, MAX_LEVEL_POI_VISIBLE, 1);
   gtk_range_set_value(GTK_RANGE(levelVisible), par.maxPoiVisible);
   gtk_scale_set_value_pos (GTK_SCALE(levelVisible), GTK_POS_TOP);
   gtk_widget_set_size_request (levelVisible, 200, -1);  // Ajuster la largeur du GtkScale
   g_signal_connect(levelVisible, "value-changed", G_CALLBACK(on_level_poi_visible_changed), NULL);
   gtk_grid_attach(GTK_GRID(tab_display), levelVisible,1, 8, 2, 1);
  
   // Affichage de la boîte de dialogue et récupération de la réponse
   gtk_widget_show_all(dialog);
   int response = gtk_dialog_run(GTK_DIALOG(dialog));
   
   // Récupération des valeurs saisies si la réponse est GTK_RESPONSE_ACCEPT
   if (response == GTK_RESPONSE_ACCEPT) {
      const char *ptOr = gtk_entry_get_text (GTK_ENTRY(entry_origin_lat));
      int poiIndex;
      
      if (! isNumber (ptOr)) {
         if ((poiIndex = findPoiByName (ptOr, &par.pOr.lat, &par.pOr.lon)) != -1)
            strcpy (par.pOrName, tPoi [poiIndex].name);
         else par.pOrName [0]= '\0';
      }  
      else {
         par.pOr.lat = getCoord (gtk_entry_get_text(GTK_ENTRY(entry_origin_lat)));
         par.pOr.lon = getCoord (gtk_entry_get_text(GTK_ENTRY(entry_origin_lon)));
      }
      par.pOr.lon = lonCanonize (par.pOr.lon);

      const char *ptDest = gtk_entry_get_text (GTK_ENTRY(entry_dest_lat));
      if (! isNumber (ptDest)) {
         if ((poiIndex = findPoiByName (ptDest, &par.pDest.lat, &par.pDest.lon)) != -1)
            strcpy (par.pDestName, tPoi [poiIndex].name);
         else par.pDestName [0]= '\0';
      }  
      else {
         par.pDest.lat = getCoord (gtk_entry_get_text(GTK_ENTRY(entry_dest_lat)));
         par.pDest.lon = getCoord (gtk_entry_get_text(GTK_ENTRY(entry_dest_lon)));
      }
      par.pDest.lon = lonCanonize (par.pDest.lon);
      par.cogStep = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_cog));
      par.rangeCog = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_range));
      par.startTimeInHours = atof (gtk_entry_get_text(GTK_ENTRY(entry_start_time)));
      par.tStep = atof (gtk_entry_get_text(GTK_ENTRY(entry_time_step)));
      par.opt = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_opt));
      par.minPt = gtk_combo_box_get_active(GTK_COMBO_BOX(opt_combo_box));
      par.maxIso = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_max_iso));
      if (par.maxIso > MAX_N_ISOC) par.maxIso = MAX_N_ISOC;
      par.jFactor = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_jFactor));
      par.kFactor = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_kFactor));
      par.nSectors = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_n_sectors));
      par.constWindTwd = atof (gtk_entry_get_text(GTK_ENTRY(entry_wind_twd)));
      par.constWindTws = atof (gtk_entry_get_text(GTK_ENTRY(entry_wind_tws)));
      if (par.constWindTws != 0)
	      initConst (&zone);
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
   dispTranslate (1.0, 0);
   gtk_widget_queue_draw (drawing_area);
}

/*! go down button */ 
static void on_down_button_clicked (GtkWidget *widget, gpointer data) {
   dispTranslate (-1.0, 0);
   gtk_widget_queue_draw (drawing_area);
}

/*! go left button */ 
static void on_left_button_clicked (GtkWidget *widget, gpointer data) {
   dispTranslate (0, -1.0);
   gtk_widget_queue_draw (drawing_area);
}

/*! go right button */ 
static void on_right_button_clicked (GtkWidget *widget, gpointer data) {
   dispTranslate (0, 1.0);
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
   // Extraction des coordonnées de la structure
   Coordinates *coords = (Coordinates *)g_object_get_data(G_OBJECT(widget), "coordinates");
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel (window)), &width, &height);
   //printf ("lat: %.2lf, lon: %.2lf\n", yToLat (coords->y), xToLon (coords->x));
   if (strcmp(selection, "Meteogram") == 0) {
      meteogram ();
   }
   else if (strcmp(selection, "Waypoint") == 0) {
      if (wayRoute.n < MAX_N_WAY_POINT) {
         destPressed = false;
         wayRoute.t [wayRoute.n].lat = yToLat (coords->y);
         wayRoute.t [wayRoute.n].lon = xToLon (coords->x);
         wayRoute.n += 1;
      }
      else (infoMessage ("number of waypoints exceeded", GTK_MESSAGE_ERROR));
   }
   else if (strcmp(selection, "Origin") == 0) {
      destPressed = false;
      par.pOr.lat = yToLat (coords->y);
      par.pOr.lon = xToLon (coords->x);
      route.n = 0;
      wayRoute.n = 0;
      wayRoute.totOrthoDist = 0;
      wayRoute.totLoxoDist = 0;
      par.pOrName [0] = '\0';
   } 
   else if (strcmp(selection, "Destination") == 0) {
      destPressed = true;
      route.n = 0;
      par.pDest.lat = yToLat (coords->y);
      par.pDest.lon = xToLon (coords->x);
      wayRoute.t [wayRoute.n].lat = par.pDest.lat;
      wayRoute.t [wayRoute.n].lon = par.pDest.lon;
      calculateOrthoRoute ();
      niceWayPointReport (); 
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
            tPoi [nPoi].lon = xToLon (coords->x);
            tPoi [nPoi].lat = yToLat (coords->y);
            tPoi [nPoi].level = 1;
            tPoi [nPoi].type = NEW;
            nPoi += 1;
         }
      }
      else (infoMessage ("Number of poi exceeded", GTK_MESSAGE_ERROR));
   }
   else if (strcmp(selection, "StartPolygon") == 0) {
      printf ("start polygon\n");
      if ((forbidZones [par.nForbidZone].points = (Point *) malloc (MAX_SIZE_FORBID_ZONE * sizeof(Point))) == NULL) {
         fprintf (stderr, "in on_popup_menu_selection Error malloc forbidZones with k = %d\n", par.nForbidZone);
         infoMessage ("in on_popup_menu_selection Error malloc forbidZones", GTK_MESSAGE_ERROR);
         return;
      } 
      polygonStarted = true;
      if (par.nForbidZone < MAX_N_FORBID_ZONE) {
         forbidZones [par.nForbidZone].points [0].lat = yToLat (coords->y);
         forbidZones [par.nForbidZone].points [0].lon = xToLon (coords->x);
         forbidZones [par.nForbidZone].n = 1;
      }
   }
   else if ((strcmp(selection, "VertexPolygon") == 0) && (polygonStarted)) {
      if (forbidZones [par.nForbidZone].n < MAX_SIZE_FORBID_ZONE-1) {
         printf ("vertex polygon %d %d\n", par.nForbidZone, forbidZones [par.nForbidZone].n);
         forbidZones [par.nForbidZone].points [forbidZones [par.nForbidZone].n].lat = yToLat (coords->y);
         forbidZones [par.nForbidZone].points [forbidZones [par.nForbidZone].n].lon = xToLon (coords->x);
         forbidZones [par.nForbidZone].n += 1;
      }
   }
   else if (strcmp(selection, "ClosePolygon") == 0  && (polygonStarted)) {
      if ((forbidZones [par.nForbidZone].n < MAX_SIZE_FORBID_ZONE) && (forbidZones [par.nForbidZone].n > 2)) {
         forbidZones [par.nForbidZone].points [forbidZones [par.nForbidZone].n].lat = forbidZones [par.nForbidZone].points [0].lat;
         forbidZones [par.nForbidZone].points [forbidZones [par.nForbidZone].n].lon = forbidZones [par.nForbidZone].points [0].lon;
         forbidZones [par.nForbidZone].n += 1;
         par.nForbidZone += 1;
         updateIsSeaWithForbiddenAreas ();
      }
      //else free (forbidZones [par.nForbidZone].points);
      polygonStarted = false;
      
      printf ("close polygon\n");
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

/*! double clic for last point in isochrone select */
static void on_double_click (GtkWidget *widget, int x, int y) {
   double dXy, xLon, yLat, minDxy = DBL_MAX;

   if (route.n == 0) return; // we care only if route exist
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Last Isochone point select",
      NULL, GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_ACCEPT, "Cancel",
      GTK_RESPONSE_CANCEL, NULL);

   for (int i = 0; i < isoDesc [nIsoc - 1 ].size; i++) {
      xLon = getX (isocArray [nIsoc - 1][i].lon);
      yLat = getY (isocArray [nIsoc - 1][i].lat);
      dXy = (xLon - x)*(xLon -x) + (yLat - y) * (yLat - y);
      if (dXy < minDxy) {
         minDxy = dXy;
         selectedPointInLastIsochrone = i;
      }
   }
   lastClosest = isocArray [nIsoc - 1][selectedPointInLastIsochrone];
   storeRoute (lastClosest, 0);
   niceReport (0);
   gtk_widget_destroy(dialog);
}

/*! callback for clic mouse */
static gboolean on_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer data) {
   char str [MAX_SIZE_LINE];
   GtkWidget *menu, *item_meteogram, *item_origine, *item_destination, *item_waypoint, *item_poi;
   GtkWidget *item_start_polygon, *item_vertex_polygon, *item_close_polygon;
   if (event->button == GDK_BUTTON_PRIMARY) { // left clic
      if (event->type == GDK_2BUTTON_PRESS) { // double clic
         on_double_click (widget, event->x, event->y);
         selecting = false;
      }   
      else if (event->type == GDK_BUTTON_PRESS) { // simple clic
         selecting = !selecting;
         whereWasMouse.x = whereIsMouse.x = event->x;
         whereWasMouse.y = whereIsMouse.y = event->y;
         gtk_widget_queue_draw(widget); // Redessiner pour mettre à jour le rectangle de sélection
      }
   }
   else if (event->button == GDK_BUTTON_SECONDARY) { // right clic
      menu = gtk_menu_new();
      item_meteogram = gtk_menu_item_new_with_label("Meteogram");
      item_origine = gtk_menu_item_new_with_label("Origin");
      sprintf (str, "Waypoint no: %d",  wayRoute.n + 1);
      item_waypoint = gtk_menu_item_new_with_label (str);
      item_destination = gtk_menu_item_new_with_label("Destination");
      item_poi = gtk_menu_item_new_with_label("New Poi");
      item_start_polygon = gtk_menu_item_new_with_label("Start Polygon");
      item_vertex_polygon = gtk_menu_item_new_with_label("Vertex Polygon");
      item_close_polygon = gtk_menu_item_new_with_label("Close Polygon");


      // Création d'une structure pour stocker les coordonnées
      Coordinates *coords = g_new (Coordinates, 1);
      coords->x = event->x;
      coords->y = event->y;

      // Ajout des coordonnées à la propriété de l'objet widget
      g_object_set_data(G_OBJECT(item_origine), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_waypoint), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_destination), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_poi), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_start_polygon), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_vertex_polygon), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_close_polygon), "coordinates", coords);

      g_signal_connect(item_meteogram, "activate", G_CALLBACK (on_popup_menu_selection), "Meteogram");
      g_signal_connect(item_origine, "activate", G_CALLBACK (on_popup_menu_selection), "Origin");
      g_signal_connect(item_waypoint, "activate", G_CALLBACK(on_popup_menu_selection), "Waypoint");
      g_signal_connect(item_destination, "activate", G_CALLBACK(on_popup_menu_selection), "Destination");
      g_signal_connect(item_poi, "activate", G_CALLBACK(on_popup_menu_selection), "Poi");
      g_signal_connect(item_start_polygon, "activate", G_CALLBACK(on_popup_menu_selection), "StartPolygon");
      g_signal_connect(item_vertex_polygon, "activate", G_CALLBACK(on_popup_menu_selection), "VertexPolygon");
      g_signal_connect(item_close_polygon, "activate", G_CALLBACK(on_popup_menu_selection), "ClosePolygon");


      // Ajout des éléments au menu
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_meteogram);

      GtkWidget *item_separator = gtk_separator_menu_item_new();
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_separator);

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_origine);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_waypoint);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_destination);

      GtkWidget *item_separator1 = gtk_separator_menu_item_new();
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_separator1);

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_poi);

      GtkWidget *item_separator2 = gtk_separator_menu_item_new();
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_separator2);

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_start_polygon);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_vertex_polygon);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_close_polygon);

      gtk_widget_show_all (menu);
      gtk_menu_popup_at_pointer (GTK_MENU(menu), NULL);
      return TRUE;
   }
   return FALSE;
}

/*! zoom associated to scoll event */
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
	   dispTranslate (1.0, 0);
      break;
   case GDK_KEY_Down:
	   dispTranslate (-1.0, 0);
      break;
   case GDK_KEY_Left:
	   dispTranslate (0, -1.0);
      break;
   case GDK_KEY_Right:
	   dispTranslate (0, 1.0);
      break;
   default:
      break;
   }
   gtk_widget_queue_draw (drawing_area);
   return FALSE; 
}

/*! Draw meteogram */
static void on_meteogram_event (GtkWidget *widget, cairo_t *cr, gpointer user_data) {
   int width, height, x, y;
   double u, v, g = 0, w, twd, tws, head_x, maxTws = 0, maxG = 0, maxMax = 0, maxWave = 0;
   double uCurr, vCurr, currTwd, currTws, maxCurrTws = 0, bidon; // current
   long tMax = zone.timeStamp [zone.nTimeStamp -1];
   double tDeltaCurrent = zoneTimeDiff (currentZone, zone);
   double tDeltaNow = 0; // time between beginning og grib and now in hours
   Pp pt;
   memset (&pt, 0, sizeof (Pp));
   char totalDate [MAX_SIZE_DATE], oldDay [MAX_SIZE_DATE] = ""; 
   char *pDate = &totalDate [0], *pOldDay = &oldDay [0];
   int timeMeteo;
   pt.lat = yToLat (whereIsMouse.y);
   pt.lon = xToLon (whereIsMouse.x);

   pt.lon = lonCanonize (pt.lon);
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel(widget)), &width, &height);

   if (zone.nTimeStamp < 2) return;
   cairo_set_line_width (cr, 1);
   const int X_LEFT = 30;
   const int X_RIGHT = width - 20;
   const int Y_TOP = 40;
   const int Y_BOTTOM = height - 25;
   const int HEAD_Y = 20;
   const int XK = (X_RIGHT - X_LEFT) / tMax;  
   const int DELTA = 5;
   const int DAY_LG = 10;
   // graphical difference between up to now and after now 
   tDeltaNow = diffNowGribTime0 (zone) / 3600.0;
   CAIRO_SET_SOURCE_RGB_YELLOW(cr);
   if (tDeltaNow > 0) {
      x = X_LEFT + XK * tDeltaNow;
      x = MIN (x, X_RIGHT);
      cairo_rectangle (cr, X_LEFT, Y_TOP, x - X_LEFT, Y_BOTTOM - Y_TOP); // past is gray
      cairo_fill (cr);
      if (x < X_RIGHT -10) {
         CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr); // line for now
         cairo_move_to (cr, x, Y_BOTTOM);
         cairo_line_to (cr, x, Y_TOP);
         cairo_stroke (cr);
      }
   } 
   
   // Dessiner la ligne horizontale avec fleches
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_move_to (cr, X_LEFT,  Y_BOTTOM );
   cairo_line_to (cr, X_RIGHT, Y_BOTTOM);
   cairo_line_to (cr, X_RIGHT - DELTA, Y_BOTTOM + DELTA); // for arrow
   cairo_stroke (cr);
   cairo_move_to (cr, X_RIGHT, Y_BOTTOM );
   cairo_line_to (cr, X_RIGHT - DELTA, Y_BOTTOM - DELTA);
   cairo_stroke (cr);
   
   // Dessiner la ligne verticale avec fleches
   cairo_move_to (cr, X_LEFT, Y_BOTTOM);
   cairo_line_to (cr, X_LEFT, Y_TOP);
   cairo_line_to (cr, X_LEFT - DELTA, Y_TOP + DELTA);
   cairo_stroke (cr);
   cairo_move_to (cr, X_LEFT, Y_TOP);
   cairo_line_to (cr, X_LEFT + DELTA, Y_TOP + DELTA);
   cairo_stroke (cr);

   // find max Tws max gust max waves max current and draw twa arrows
   for (int i = 0; i < tMax; i += 1) {
      findFlow (pt, i, &u, &v, &g, &w, zone, gribData);
	   findFlow (pt, i - tDeltaCurrent, &uCurr, &vCurr, &bidon, &bidon, currentZone, currentGribData);
      tws = extTws (u, v);
      twd = extTwd (u, v);
      currTws = extTws (uCurr, vCurr);
      currTwd = extTwd (uCurr, vCurr);

      head_x = X_LEFT + XK * i;
      if ((i % (tMax / 24)) == 0) {
         arrow (cr, pt, head_x, HEAD_Y, u, v, twd, tws, WIND);
         arrow (cr, pt, head_x, HEAD_Y + 20, uCurr, vCurr, currTwd, currTws, CURRENT);
      }
      if (tws > maxTws) maxTws = tws;
      if (w > maxWave) maxWave = w;
      if (g > maxG) maxG = g;
      if (currTws > maxCurrTws) maxCurrTws = currTws;
   }
   maxG *= MS_TO_KN; 
   maxMax = ceil (MAX (maxG, MAX (maxTws, MAX (maxWave, (MAX (maxCurrTws, 10)))))); 
   
   // Dessiner les libelles temps et lignes verticales
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 10);
   
   for (int i = 0; i <= tMax; i+= (par.gribTimeMax <= 120) ? 6 : 12) {
      timeMeteo = (zone.dataTime [0]/100) + i;
      if ((par.gribTimeMax > 240) && (timeMeteo % 24) != 0) continue;
      x = X_LEFT + XK * i;
      cairo_move_to (cr, x, Y_BOTTOM + 10);
      newDate (zone.dataDate [0], timeMeteo, pDate);
      cairo_show_text (cr, g_strdup_printf("%s", strrchr (pDate, ' ') + 1));
      if ((timeMeteo % ((par.gribTimeMax <= 240) ? 24 : 48)) == 0) { // only year month day considered
         cairo_move_to (cr, x, Y_BOTTOM + 20);
         pDate [DAY_LG] = '\0'; 
         cairo_show_text (cr, g_strdup_printf("%s", pDate));
         strcpy (pOldDay, pDate);
         CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr);
         cairo_move_to (cr, x, Y_BOTTOM);
         cairo_line_to (cr, x, Y_TOP);
         cairo_stroke (cr);
         CAIRO_SET_SOURCE_RGB_BLACK(cr);
      }
   }
   cairo_stroke(cr);

   const int YK = (Y_BOTTOM - Y_TOP) / maxMax;
   CAIRO_SET_SOURCE_RGB_BLACK(cr);

   // Dessiner les libelles des vitesses et lignes horizontales
   cairo_set_font_size (cr, 10);
   double step = (maxMax > 50) ? 10 : 5;
   for (double speed = step; speed <= maxMax; speed += step) {
      y = Y_BOTTOM - YK * speed;
      cairo_move_to (cr, X_LEFT - 20, y);
      cairo_show_text (cr, g_strdup_printf("%02.0lf", speed));
      CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr); // lignes horizontales
      cairo_move_to (cr, X_LEFT, y);
      cairo_line_to (cr, X_RIGHT, y);
      cairo_stroke (cr);
      CAIRO_SET_SOURCE_RGB_BLACK(cr);
   }
   cairo_stroke(cr);
  
   CAIRO_SET_SOURCE_RGB_BLUE(cr);
   // draw tws 
   for (int i = 0; i < tMax; i += 1) {
      findFlow (pt, i, &u, &v, &g, &w, zone, gribData);
      tws = extTws (u,v);
      x = X_LEFT + XK * i;
      y = Y_BOTTOM - YK * tws;
      if (i == 0) cairo_move_to (cr, x, y);
      else cairo_line_to (cr, x, y);
   }
   cairo_stroke(cr);
   
   // draw gust
   if (maxG > 0) { 
      CAIRO_SET_SOURCE_RGB_RED(cr);
      for (int i = 0; i < tMax; i += 1) {
         findFlow (pt, i, &u, &v, &g, &w, zone, gribData);
         x = X_LEFT + XK * i;
         y = Y_BOTTOM - YK * g * MS_TO_KN;
         if (i == 0) cairo_move_to (cr, x, y);
         else cairo_line_to (cr, x, y);
      }
      cairo_stroke(cr);
   }

   // draw waves
   if (maxWave > 0) { 
      CAIRO_SET_SOURCE_RGB_GREEN(cr);
      for (int i = 0; i < tMax; i += 1) {
         findFlow (pt, i, &u, &v, &g, &w, zone, gribData);
         x = X_LEFT + XK * i;
         y = Y_BOTTOM - YK * w;
         if (i == 0) cairo_move_to (cr, x, y);
         else cairo_line_to (cr, x, y);
      }
      cairo_stroke(cr);
   }
   // draw uTws (current) 
   if (maxCurrTws > 0) {
      CAIRO_SET_SOURCE_RGB_ORANGE(cr);
      for (int i = 0; i < tMax; i += 1) {
	      findFlow (pt, i - tDeltaCurrent, &uCurr, &vCurr, &bidon, &bidon, currentZone, currentGribData);
         currTws = extTws (uCurr, vCurr);
         x = X_LEFT + XK * i;
         y = Y_BOTTOM - YK * currTws;
         if (i == 0) cairo_move_to (cr, x, y);
         else cairo_line_to (cr, x, y);
      }
      cairo_stroke(cr);
   }
}

/*! meteogram */
static void meteogram () {
   char totalDate [MAX_SIZE_DATE]; 
   char *pDate = &totalDate [0];
   char strLat [MAX_SIZE_NAME], strLon [MAX_SIZE_NAME];
   char line [MAX_SIZE_LINE]; 
   if (par.constWindTws > 0) {
      infoMessage ("Wind is constant !", GTK_MESSAGE_INFO);
      return;
   }
   Pp pt;
   pt.lat = yToLat (whereIsMouse.y);
   pt.lon = xToLon (whereIsMouse.x);
   sprintf (line, "Meteogram (Wind, Gust, Wave, Current) for %s %s beginning %s during %ld hours", latToStr (pt.lat, par.dispDms, strLat), 
      lonToStr (pt.lon, par.dispDms, strLon),
      newDate (zone.dataDate [0], (zone.dataTime [0]/100)+ zone.timeStamp [kTime], pDate),
      zone.timeStamp [zone.nTimeStamp -1]);

   GtkWidget *dialog = gtk_dialog_new_with_buttons (line, NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                          NULL, NULL, NULL, NULL, NULL);

   gtk_widget_set_size_request(dialog, 1200, 400);
   GtkWidget *meteogram_drawing_area = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

   g_signal_connect(G_OBJECT(meteogram_drawing_area), "draw", G_CALLBACK(on_meteogram_event), NULL);
   
   gtk_widget_show_all (dialog);
   gtk_dialog_run (GTK_DIALOG (dialog));
   gtk_widget_destroy (dialog);
}

/*! Action after selection. Launch smtp request for grib file */
static gboolean on_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer data) {
   double lat1, lat2, lon1, lon2;
   char buffer [MAX_SIZE_BUFFER] = "";
   char *ptBuffer = &buffer [0], *ptBuffer1;
   char command [MAX_SIZE_LINE * 2] = "";

   if ((event->button == GDK_BUTTON_PRIMARY) && selecting && \
      ((whereIsMouse.x - whereWasMouse.x) > MIN_MOVE_FOR_SELECT) && \
      ((whereIsMouse.x - whereWasMouse.x) > MIN_MOVE_FOR_SELECT)) {

      lat1 = yToLat (whereIsMouse.y);
      lon1 = xToLon (whereWasMouse.x);
      lat2 = yToLat (whereWasMouse.y);
      lon2 = xToLon (whereIsMouse.x);
      lon1 = lonCanonize (lon1);
      lon2 = lonCanonize (lon2);

      provider = SAILDOCS_GFS; // default 
      if ((mailRequestBox (lat1, lon1, lat2, lon2) == GTK_RESPONSE_OK) &&
         ((par.mailPw [0] != '\0') || mailPassword ())) { 
         if (smtpGribRequestPython (provider, lat1, lon1, lat2, lon2, buffer)) { 
            ptBuffer = strchr (buffer, ' ');          // delete first words 
            ptBuffer1 = strchr (ptBuffer + 1, ' ');  // delete second word 
            spinner ("Waiting for grib Mail response", ptBuffer1);
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
   gtk_widget_queue_draw (widget); // Redessiner pour effacer le rectangle de sélection
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

/*! callback function to select URL for wind */
static void wind_url_selected(GtkMenuItem *menu_item, gpointer user_data) {
   int index = GPOINTER_TO_INT(user_data);
   urlDownloadGrib (WIND, index);
}

/*! callback function to select URL for current */
static  void current_url_selected(GtkMenuItem *menu_item, gpointer user_data) {
   int index = GPOINTER_TO_INT(user_data);
   urlDownloadGrib (CURRENT, index);
}

/*! callback function when widow size change
void on_window_change(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data) {
   gint width = event->width;
   gint height = event->height;
   g_print("Nouvelle taille de la fenêtre - Largeur: %d, Hauteur: %d\n", width, height);
}*/

/*! Window settings with menus and buuton */
static void windowSettings () {
   int forWind = WIND, forCurrent = CURRENT;          // distinction wind current for openGrib call
   int polar = POLAR, wavePolar = WAVE_POLAR;         // distinction polar and wavePolar for polarDraw call
   int poiSelect = POI_SEL, portSelect = PORT_SEL;    // distinction poi and port for editPoi call
   int osmSelect = 0, openSeaSelect = 1;              // distinction Open Steet Map or Open sea
   char str [MAX_SIZE_LINE];

   // Création de la fenêtre principale
   window = gtk_window_new (GTK_WINDOW_TOPLEVEL); // Main window : global variable
   gtk_window_set_title(GTK_WINDOW(window), PROG_NAME);
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 400);
   gtk_window_maximize (GTK_WINDOW (window)); // full screen
   GError *error = NULL;
   if (!gtk_window_set_icon_from_file(GTK_WINDOW(window), buildRootName (PROG_LOGO, str), &error)) {
      fprintf (stderr, "In windowSetting () Impossible to load icon: %s", error->message);
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
   GtkWidget *file_item_wind_url = gtk_menu_item_new_with_label ("Wind: Meteoconsult");
   GtkWidget *file_item_open_current = mySubMenu ("Current: Open Grib", "folder");
   GtkWidget *file_item_current_info = mySubMenu ("Current: Grib Info", "applications-engineering-symbolic");
   GtkWidget *file_item_current_url = gtk_menu_item_new_with_label ("Current: Meteoconsult");
   GtkWidget *file_item_exit = mySubMenu ("Quit", "application-exit-symbolic");

   GtkWidget *item_separator = gtk_separator_menu_item_new();

   // Création du sous-menu "Grib" avec une liste d'URL
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_open_wind);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_wind_info);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_wind_url);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), item_separator);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_open_current);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_current_info);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_current_url);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_exit);

   // sous sous menu wind URL 
   GtkWidget *wind_url_submenu = gtk_menu_new();
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item_wind_url), wind_url_submenu);
   for (int i = 0; i < N_WIND_URL; i++) {
       GtkWidget *url_menu_item = gtk_menu_item_new_with_label(WIND_URL [i * 2]);
       g_signal_connect(url_menu_item, "activate", G_CALLBACK(wind_url_selected), GINT_TO_POINTER(i));
       gtk_menu_shell_append(GTK_MENU_SHELL(wind_url_submenu), url_menu_item);
   }
   
   // sous sous menu current URL 
   GtkWidget *current_url_submenu = gtk_menu_new();
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item_current_url), current_url_submenu);
   for (int i = 0; i < N_CURRENT_URL; i++) {
       GtkWidget *url_menu_item = gtk_menu_item_new_with_label(CURRENT_URL [i * 2]);
       g_signal_connect(url_menu_item, "activate", G_CALLBACK(current_url_selected), GINT_TO_POINTER(i));
       gtk_menu_shell_append(GTK_MENU_SHELL(current_url_submenu), url_menu_item);
   }

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
   GtkWidget *dump_item_isoc = gtk_menu_item_new_with_label("Isochrones");
   GtkWidget *dump_item_desc_isoc = gtk_menu_item_new_with_label("Isochrone Descriptors");
   GtkWidget *dump_item_ortho = gtk_menu_item_new_with_label("Ortho and Loxo Routes");
   GtkWidget *report_item_ortho = gtk_menu_item_new_with_label("Ortho and Loxo Report");
   GtkWidget *dump_item_rte = gtk_menu_item_new_with_label("Sail Route");
   GtkWidget *report_item_rte = gtk_menu_item_new_with_label("Sail Report");
   GtkWidget *dump_item_gps = gtk_menu_item_new_with_label("GPS");

   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_isoc);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_desc_isoc);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_ortho);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), report_item_ortho);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_rte);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), report_item_rte);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_gps);

   // Ajout d'éléments dans le menu "Poi"
   GtkWidget *poi_item_dump = mySubMenu ("Dump", "document-open-symbolic");
   GtkWidget *poi_item_save = mySubMenu ("Save", "media-floppy-symbolic");
   GtkWidget *poi_item_edit = mySubMenu ("Edit PoI", "document-edit-symbolic");
   GtkWidget *port_item_edit = mySubMenu ("Edit Ports", "document-edit-symbolic");
   
   gtk_menu_shell_append(GTK_MENU_SHELL(poi_menu), poi_item_dump);
   gtk_menu_shell_append(GTK_MENU_SHELL(poi_menu), poi_item_save);
   gtk_menu_shell_append(GTK_MENU_SHELL(poi_menu), poi_item_edit);
   gtk_menu_shell_append(GTK_MENU_SHELL(poi_menu), port_item_edit);

   // Ajout d'éléments dans le menu "Help"
   GtkWidget *help_item_osm = mySubMenu ("OpenStreeMap", "preferences-system-network-symbolic");
   GtkWidget *help_item_o_sea_m = mySubMenu ("OpenSeaMap", "emblem-web");
   GtkWidget *help_item_html = mySubMenu ("Help", "help-browser-symbolic");
   GtkWidget *help_item_info = mySubMenu ("About", "help-about-symbolic");

   gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_item_osm);
   gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_item_o_sea_m);
   gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_item_html);
   gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_item_info);

   
   // fonctions de CallBack
   g_signal_connect(G_OBJECT(file_item_open_wind), "activate", G_CALLBACK(openGrib), &forWind);
   g_signal_connect(G_OBJECT(file_item_wind_info), "activate", G_CALLBACK(gribInfo), &forWind);
   g_signal_connect(G_OBJECT(file_item_open_current), "activate", G_CALLBACK(openGrib), &forCurrent);
   g_signal_connect(G_OBJECT(file_item_current_info), "activate", G_CALLBACK(gribInfo), &forCurrent);
   g_signal_connect(G_OBJECT(file_item_exit), "activate", G_CALLBACK(gtk_main_quit), NULL);
   g_signal_connect(G_OBJECT(polar_item_open), "activate", G_CALLBACK(openPolar), NULL);
   g_signal_connect(G_OBJECT(polar_item_draw), "activate", G_CALLBACK(cbPolarDraw), &polar);
   g_signal_connect(G_OBJECT(polar_item_wave), "activate", G_CALLBACK(cbPolarDraw), &wavePolar);
   g_signal_connect(G_OBJECT(scenario_item_open), "activate", G_CALLBACK(openScenario), NULL);
   g_signal_connect(G_OBJECT(scenario_item_change), "activate", G_CALLBACK(change), NULL);
   g_signal_connect(G_OBJECT(scenario_item_get), "activate", G_CALLBACK(parDump), NULL);
   g_signal_connect(G_OBJECT(scenario_item_save), "activate", G_CALLBACK(saveScenario), NULL);
   g_signal_connect(G_OBJECT(scenario_item_edit), "activate", G_CALLBACK(editScenario), NULL);
   g_signal_connect(G_OBJECT(dump_item_isoc), "activate", G_CALLBACK(isocDump), NULL);
   g_signal_connect(G_OBJECT(dump_item_desc_isoc), "activate", G_CALLBACK(isocDescDump), NULL);
   g_signal_connect(G_OBJECT(dump_item_ortho), "activate", G_CALLBACK(orthoDump), NULL);
   g_signal_connect(G_OBJECT(report_item_ortho), "activate", G_CALLBACK(niceWayPointReport), NULL);
   g_signal_connect(G_OBJECT(dump_item_rte), "activate", G_CALLBACK(rteDump), NULL);
   g_signal_connect(G_OBJECT(report_item_rte), "activate", G_CALLBACK(rteReport), NULL);
   g_signal_connect(G_OBJECT(dump_item_gps), "activate", G_CALLBACK(gpsDump), NULL);
   g_signal_connect(G_OBJECT(poi_item_dump), "activate", G_CALLBACK(poiDump), NULL);
   g_signal_connect(G_OBJECT(poi_item_save), "activate", G_CALLBACK(poiSave), NULL);
   g_signal_connect(G_OBJECT(poi_item_edit), "activate", G_CALLBACK(poiEdit), &poiSelect);
   g_signal_connect(G_OBJECT(port_item_edit), "activate", G_CALLBACK(poiEdit), &portSelect);
   g_signal_connect(G_OBJECT(help_item_osm), "activate", G_CALLBACK(openMap), &osmSelect);
   g_signal_connect(G_OBJECT(help_item_o_sea_m), "activate", G_CALLBACK(openMap), &openSeaSelect);
   g_signal_connect(G_OBJECT(help_item_html), "activate", G_CALLBACK(help), NULL);
   g_signal_connect(G_OBJECT(help_item_info), "activate", G_CALLBACK(helpInfo), NULL);

   // when window is resized
   // g_signal_connect(G_OBJECT(window), "configure-event", G_CALLBACK(on_window_change), NULL);

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
   g_signal_connect(polar_button, "clicked", G_CALLBACK (cbPolarDraw), &polar);
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
   printf ("End windowSetting\n");
   gtk_main();
}

/*! Display main menu and make initializations */
int main (int argc, char *argv[]) {
   char fileName [MAX_SIZE_FILE_NAME];
   // char buffer [MAX_SIZE_BUFFER];
   bool ret = true;
   // initialisations
   my_gps_data.ret = initGPS ();
   printf ("initGPS        : %d\n", my_gps_data.ret);
   
   gtk_init(&argc, &argv);

   g_set_application_name (PROG_NAME);
   if (setlocale(LC_ALL, "C") == NULL) {              // very important for scanf decimal numbers
      fprintf (stderr, "main () Error: setlocale");
      return EXIT_FAILURE;
   }

   strcpy (parameterFileName, PARAMETERS_FILE);
   switch (argc) {
      case 1: // no parameter
         ret = readParam (parameterFileName);
         break;
      case 2: // one parameter
         if (argv [1][0] == '-') {
            ret = readParam (parameterFileName);
            optionManage (argv [1][1]);
            exit (EXIT_SUCCESS);
         }
         else { 
            ret = readParam (argv [1]);
            strcpy (parameterFileName, argv [1]);
         }
         break;
      case 3: // two parameters
         if (argv [1][0] == '-') {
            ret = readParam (argv [2]);
            strcpy (parameterFileName, argv [2]);
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

   printf ("Editor         : %s\n", par.editor); 
   printf ("Spreadsheet    : %s\n", par.spreadsheet); 
   printf ("Working dir    : %s\n", par.workingDir); 
   printf ("nPoi           : %d\n", nPoi);
   initConst (&zone);
   initDispZone ();
   
   if (par.gribFileName [0] == '\0') { // no grib file found in .par file
      strcpy (par.gribFileName, par.workingDir);
      strcat (par.gribFileName, "grib/");
      mostRecentFile (par.gribFileName, ".gr", fileName); // most recent grib file found
      strcat (par.gribFileName, fileName);
   }
      
   if (par.gribFileName [0] != '\0') {
      readGrib (NULL);
      updatedColors = false;
      if (readGribRet == 0) {
         fprintf (stderr, "main: unable to read grib file: %s\n ", par.gribFileName);
      }
      theTime = zone.timeStamp [0];
      initDispZone ();
      printf ("grib file done : %s\n", par.gribFileName);
   }
   //gribToStr (buffer, zone);
   //printf ("%s\n", buffer);
   

   if (par.currentGribFileName [0] != '\0') {
      readCurrentGrib (NULL);
      printf ("current grib file name done: %s\n", par.gribFileName);
   }

   if (par.isSeaFileName [0] != '\0')
      readIsSea (par.isSeaFileName);
   printf ("readIsSea done : %s\n", par.isSeaFileName);
   updateIsSeaWithForbiddenAreas ();
   printf ("update Forbid Areas done\n");
   
   for (int i = 0; i < par.nShpFiles; i++) {
      initSHP (par.shpFileName [i]);
      printf ("SHP file loaded: %s\n", par.shpFileName [i]);
   }
   if (readPolar (par.polarFileName, &polMat))
      printf ("Polar loaded   : %s\n", par.polarFileName);
      
   if (readPolar (par.wavePolFileName, &wavePolMat))
      printf ("Polar loaded   : %s\n", par.wavePolFileName);
   
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   wayRoute.n = 0;
   wayRoute.t[0].lat = par.pDest.lat;
   wayRoute.t[0].lon = par.pDest.lon;
   printf ("Launch windowSettings\n");

   windowSettings ();
   
   freeSHP ();
   free (tIsSea);
   closeGPS ();
   return 0;
}

