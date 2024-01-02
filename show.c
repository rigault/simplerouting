/*! compilation gcc -c `pkg-config gtk+-2.0 --cflags` show.c `pkg-config gtk+-3.0 --libs` */
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
* \li ./show [<parameterFile>] */

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
#define make CFLAGS+="-DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED"

#define MAX_TEXT_LENGTH 5 // in polar
#define POLAR_WIDTH 800
#define POLAR_HEIGHT 500
#define ARROW_SIZE 10
#define DISP_NB_LAT_STEP 10
#define DISP_NB_LON_STEP 10
#define ANIMATION_TEMPO 100
#define GRIB_TIME_OUT 2000
#define READ_GRIB_TIME_OUT 200
#define MIN_MOVE_FOR_SELECT 50 // minimum move to launch smtp grib request after selection

GdkRGBA colors [] = {{1.0,0,0,1}, {0,1.0,0,1},  {0,0,1.0,1},{0.5,0.5,0,1},{0,0.5,0.5,1},{0.5,0,0.5,1},{0.2,0.2,0.2,1},{0.4,0.4,0.4,1},{0.8,0,0.2,1},{0.2,0,0.8,1}}; // Colors for Polar curve
int nColors = 10;

GtkWidget *statusbar;
GtkWidget *window; 
GtkWidget *spinner_window; 
GtkListStore *filter_store;
GtkWidget *filter_combo;
guint context_id;
GtkWidget *polar_drawing_area; // polar
gint selectedPol = 0;            // select polar to draw. 0 = all
GtkWidget *drawing_area;
cairo_t *globalCr;
guint gribMailTimeout;
guint gribReadTimeout;
guint currentGribReadTimeout;
int kTime = 0;
gboolean animation_active = FALSE;
bool destPressed = true;
int indexBest = -1;  // index of best point reached if pDest not reached
bool gribRequestRunning = false;
gboolean selecting = FALSE;
int provider = SAILDOCS;
MyDate vStart;
MyDate *start = &vStart;    
struct tm *timeInfos;               // conversion en struct tm

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
   double d;
   double cap;
} WayPoint;

typedef struct {
   int n;
   double totDist;
   WayPoint t [MAX_N_WAY_POINT];
} WayRoute;

WayRoute wayRoute = {0, 0.0};

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

/*! make calculation over Ortho route */
static void calculateOrthoRoute () {
   wayRoute.t [0].cap = loxCap (par.pOr.lat, par.pOr.lon,  wayRoute.t [0].lat,  wayRoute.t [0].lon) \
     + givry (par.pOr.lat, par.pOr.lon,  wayRoute.t [0].lat,  wayRoute.t [0].lon);
   wayRoute.t [0].d = orthoDist (par.pOr.lat, par.pOr.lon,  wayRoute.t [0].lat,  wayRoute.t [0].lon);
   wayRoute.totDist = wayRoute.t [0].d; 
   
   for (int i = 0; i <  wayRoute.n; i++) {
      wayRoute.t [i+1].cap = loxCap ( wayRoute.t [i].lat,  wayRoute.t [i].lon,  wayRoute.t [i+1].lat,  wayRoute.t [i+1].lon) \
         + givry ( wayRoute.t [i].lat,  wayRoute.t [i].lon,  wayRoute.t [i+1].lat,  wayRoute.t [i+1].lon);
      wayRoute.t [i+1].d = orthoDist ( wayRoute.t [i].lat,  wayRoute.t [i].lon,  wayRoute.t [i+1].lat,  wayRoute.t [i+1].lon);
      wayRoute.totDist += wayRoute.t [i+1].d; 
   }
}

/*! draw orthoPoints */
static void orthoPoints (cairo_t *cr, double lat1, double lon1, double lat2, double lon2, int n) {
   double lSeg = orthoDist (lat1, lon1, lat2, lon2) / n;
   double lat = lat1;
   double lon = lon1;
   double angle;
   int i;
   double x1, y1, x2, y2;
   double x = getX (lon1, disp.xL, disp.xR);
   double y = getY (lat1, disp.yB, disp.yT);
   cairo_move_to (cr, x, y);
   cairo_set_source_rgb (cr, 0, 1, 0);
   n = 10;
   
   for (i = 0; i < n - 2; i ++) {
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
   sprintf (str, " Point      Lon       Lat     Cap     Dist \n");
   sprintf (line, " pOr:   %7.2lf° %7.2lf° %7.2lf° %7.2lf \n", par.pOr.lon, par.pOr.lat,  wayRoute.t [0].cap,  wayRoute.t [0].d);
   strcat (str, line);
   for (int i=0; i <  wayRoute.n; i++) {
      sprintf (line, " WP %02d: %7.2lf° %7.2lf° %7.2lf° %7.2lf \n", i + 1,  wayRoute.t[i].lon, \
          wayRoute.t[i].lat,  wayRoute.t [i+1].cap,  wayRoute.t [i+1].d);
      strcat (str, line);
   }
   sprintf (line, " pDest: %7.2lf° %7.2lf° \n\n", par.pDest.lon, par.pDest.lat);
   strcat (str, line);
   sprintf (line, " Total orthodomic distance: %.2lf NM\n", wayRoute.totDist);
   strcat (str, line);
}

/*! draw orthodromic routes from waypoints to next waypoints */
static void wayPointRoute (cairo_t *cr, int n) {
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

/*! draw all isochrones stored in isocArray as points */
static gboolean drawAllIsochrones0  (cairo_t *cr) {
   double x, y;
   Pp pt;
   for (int i = 0; i < nIsoc; i++) {
      GdkRGBA color = colors [i % nColors];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      for (int k = 0; k < sizeIsoc [i]; k++) {
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

/*! draw all isochrones stored in isocArray as segments or curves */
static gboolean drawAllIsochrones  (cairo_t *cr, int style) {
   double x, y, x1, y1, x2, y2;
   //GtkWidget *drawing_area = gtk_drawing_area_new();
   Pp pt;
   int next;
   if (style == 0) return true;
   if (style == 1) return drawAllIsochrones0 (cr);
   cairo_set_line_width(cr, 1.0);
   for (int i = 0; i < nIsoc; i++) {
      // GdkRGBA color = colors [i];
      next = firstInIsoc [i];
      pt = isocArray [i][next];
	   x = getX (pt.lon, disp.xL, disp.xR);
	   y = getY (pt.lat, disp.yB, disp.yT);
      //cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      cairo_set_source_rgb (cr, 0, 0, 1.0);
      cairo_move_to (cr, x, y);
      cairo_set_source_rgb (cr, 0, 0, 1.0);
	   if ((sizeIsoc [i] < 4) || style == 2) { 					// segment if number of points < 4 or style == 2
         for (int k = 1; k < sizeIsoc [i]; k++) {
            next += 1;
            if (next >= sizeIsoc [i]) next = 0;
            pt = isocArray [i][next]; 
            x = getX (pt.lon, disp.xL, disp.xR); 
            y = getY (pt.lat, disp.yB, disp.yT); 
	         // printf ("i lon lat x y %d %lf %lf %lf %lf \n", i, pt.lon, pt.lat, x, y);
		      cairo_line_to (cr, x, y);
	     }
     }
	  else { 									// curve if number of point >= 4 and style == 2
         for (int k = 1; k < sizeIsoc [i] - 2; k++) {
            next += 1;
            if (next >= sizeIsoc [i]) next = 0;
            pt = isocArray [i][next]; 
            x = getX (pt.lon, disp.xL, disp.xR); 
            y = getY (pt.lat, disp.yB, disp.yT); 
	         // printf ("i lon lat x y %d %lf %lf %lf %lf \n", i, pt.lon, pt.lat, x, y);
            if (next == (sizeIsoc [i] - 1)) { 
               pt = isocArray [i][0];
               x1 = getX (pt.lon, disp.xL, disp.xR); 
               y1 = getY (pt.lat, disp.yB, disp.yT);
               pt = isocArray [i][1];
            } 
		      else if (next == (sizeIsoc [i] - 2)) {
               pt = isocArray [i][next + 1]; 
               x1 = getX (pt.lon, disp.xL, disp.xR); 
               y1 = getY (pt.lat, disp.yB, disp.yT); 
               pt = isocArray [i][0];
            }
            else {
               pt = isocArray [i][next + 1]; 
               x1 = getX (pt.lon, disp.xL, disp.xR); 
               y1 = getY (pt.lat, disp.yB, disp.yT); 
               pt = isocArray [i][next + 2];
            }
            x2 = getX (pt.lon, disp.xL, disp.xR); 
            y2 = getY (pt.lat, disp.yB, disp.yT); 
		      cairo_curve_to(cr, x, y, x1, y1, x2, y2);
            //cairo_arc (cr, x, y, 2.0, 0, 2 * G_PI);
            //cairo_fill (cr);
		   }
      }
      cairo_stroke(cr);
   }
   return FALSE;
}

/*! draw loxodromie */
static void drawDirectRoute (cairo_t *cr) {
   double x = getX (par.pOr.lon, disp.xL, disp.xR);
   double y = getY (par.pOr.lat, disp.yB, disp.yT);
   cairo_set_source_rgb (cr, 0.9, 0.9, 0.9);
   cairo_move_to (cr, x, y);
   x = getX (par.pDest.lon, disp.xL, disp.xR);
   y = getY (par.pDest.lat, disp.yB, disp.yT);
   cairo_line_to (cr, x, y);
   cairo_stroke (cr);
}

/*! give the focus on point in route at kTime */
static void focusOnPointInRoute (cairo_t *cr)  {
   int i;
   double index = 0;
   double lat, lon;
   long deltaTimeStamp = zone.timeStamp [1] - zone.timeStamp [0];
   if (route.n == 0) return;

   if (kTime < route.kTime0) i = 0;
   else {
      index = (double) (kTime - route.kTime0) * (double) deltaTimeStamp / par.tStep;
      i = floor (index);
   }
   // printf ("kTime: %d, index %lf, par.tStep: %d, deltaTimeStamp: %ld\n", kTime, index, par.tStep, deltaTimeStamp);
   if (i < route.n -1) {
      lat = route.t[i].lat + (index - i) * (route.t[i+1].lat - route.t[i].lat);
      lon = route.t[i].lon + (index - i) * (route.t[i+1].lon - route.t[i].lon);
   }
   else {
      lat = route.t[route.n - 1].lat;
      lon = route.t[route.n - 1].lon;
   }
   // printf ("focus i: %d\n", i);
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
      circle (cr, tPoi [i].lon, tPoi [i].lat, 0.0, 0.0, 0.0);
      x = getX (tPoi [i].lon, disp.xL, disp.xR);
      y = getY (tPoi [i].lat, disp.yB, disp.yT);
      cairo_move_to (cr, x+10, y);
      cairo_show_text (cr, g_strdup_printf("%s", tPoi [i].name));
   }
   cairo_stroke (cr);
}

/*! draw shapfile with different color for sea and earth */
static gboolean draw_shp_map (GtkWidget *widget, cairo_t *cr, gpointer data) {
    double lon, lat, lon0, lat0;
    double x0, y0, x, y;

    // Récupérer la taille de la fenêtre
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
         cairo_set_source_rgb (cr, 157.0/255.0, 162.0/255.0, 12.0/255.0); // Polygones en jaune
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

/*! draw barbule that represent the wind */
static void barbule (cairo_t *cr, Pp pt, double u, double v, double twd, double tws, int typeFlow) {
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

/*! draw the main window */
static gboolean drawGribCallback (GtkWidget *widget, cairo_t *cr, gpointer data) {
   bool found = false; 
   double x, y;
   double u, v, w, twd, tws, uCurr, vCurr;
   char str [MAX_SIZE_LINE] ="", strLat [MAX_SIZE_LINE]= "", strLon [MAX_SIZE_LINE] = "";
   double tDeltaCurrent = zoneTimeDiff (currentZone, zone);
   Pp pt;
   globalCr = cr;
   gint width, height;
   sprintf (str, "%s %s %s", PROG_NAME, PROG_VERSION, par.gribFileName);
   gtk_window_set_title (GTK_WINDOW (window), str); // window is a global variable
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel(widget)), &width, &height);
   disp.xL = 0;      // right
   disp.xR = width ; // left
   disp.yT = 0;      // top
   disp.yB = height; // bottom
   cairo_set_source_rgb(cr, 1, 1, 1); // Couleur blanche
   cairo_paint(cr);                   // clear all
   draw_shp_map (widget, cr, data);
   cairo_set_source_rgb (cr, 0, 0, 0);

   // Dessiner la ligne horizontale pour les longitudes avec des libellés
   //cairo_move_to (cr, disp.xR, disp.yB-125);
   //cairo_line_to (cr, disp.xL, disp.yB-125);
   //cairo_stroke (cr);

   // Dessiner la ligne verticale pour les latitudes avec des libelles
   //cairo_move_to(cr, disp.xL+3, disp.yT);
   //cairo_line_to(cr, disp.xL+3, disp.yB);
   //cairo_stroke(cr);

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

   // dessiner les barbules ou fleches de vent
   pt.lat = dispZone.latMin;
   while (pt.lat <= dispZone.latMax) {
      pt.lon = dispZone.lonLeft;
      while (pt.lon <= dispZone.lonRight) {
	      u = 0; v = 0; w = 0; uCurr = 0; vCurr = 0;
         if (isInZone (pt, zone)) {
            if (par.constWindTws != 0) {
	            u = - KN_TO_MS * par.constWindTws * sin (DEG_TO_RAD * par.constWindTwd);
	            v = - KN_TO_MS * par.constWindTws * cos (DEG_TO_RAD * par.constWindTwd);
            }
	         else {
	            found = findFlow (pt, zone.timeStamp [kTime], &u, &v, &w, zone, gribData);
               //printf ("twd : %lf\n", twd);
            }
            twd = fTwd (u, v);
            tws = fTws (u, v);
    	      // printf ("%lf %lf %lf %lf\n", pt.lat,pt.lon, u, v);
	         if (found) {
               barbule (cr, pt, u, v, twd, tws, WIND);
               showWaves (cr, pt, w);
            }

            if (par.constCurrentS != 0) {
               uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
               vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
            }
            else {
	            found = findFlow (pt, zone.timeStamp [kTime] - tDeltaCurrent, &uCurr, &vCurr, &w, currentZone, currentGribData);
            }
            if (found && ((uCurr != 0) || (vCurr !=0))) {
               twd = fTwd (uCurr, vCurr);
               tws = fTws (uCurr, vCurr);
               barbule (cr, pt, uCurr, vCurr, twd, tws, CURRENT);
            }
	      }
         pt.lon += (dispZone.lonStep) / 2;
      }
      pt.lat += (dispZone.latStep) / 2;
   }
   //orthoPoints (cr, par.pOr.lat, par.pOr.lon, par.pDest.lat, par.pDest.lon, 20);
   calculateOrthoRoute ();
   wayPointRoute (cr, 20);
   drawDirectRoute (cr);
   circle (cr, par.pOr.lon, par.pOr.lat, 0.0, 1.0, 0.0);
   if (destPressed) circle (cr, par.pDest.lon, par.pDest.lat, 0.0, 0.0, 1.0);
   if (! isnan (my_gps_data.lon) && ! isnan (my_gps_data.lat))
      circle (cr, my_gps_data.lon, my_gps_data.lat , 1.0, 0.0, 0.0);
   if (route.n != 0) {
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

   return FALSE;
}

/*! update status bar */
static void statusBarUpdate () {
   char totalDate [MAX_SIZE_DATE]; 
   char *pDate = &totalDate [0];
   char sStatus [MAX_SIZE_BUFFER];
   Pp pt;
   double u = 0, v = 0, w = 0, uCurr = 0, vCurr = 0;
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
   strcpy (seaEarth, (extIsSea (pt.lon, pt.lat)) ? "Sea" : "Earth");
   if (par.constCurrentS != 0) {
      uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
      vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
   }
   else {
	   findFlow (pt, currentZone.timeStamp [kTime] - tDeltaCurrent, &uCurr, &vCurr, &w, currentZone, currentGribData);
   }
   
   sprintf (sStatus, "%s         %d/%ld %-80s %s, %s\
      Wind: %06.2lf° %06.2lf Knots   Waves: %6.2lf  Current: %06.2lf° %05.2lf Knots         %s      %s",
      newDate (zone.dataDate [0], (zone.dataTime [0]/100)+ zone.timeStamp [kTime], pDate), \
      kTime + 1, zone.nTimeStamp, " ",\
      latToStr (pt.lat, par.dispDms, strLat),\
      lonToStr (pt.lon, par.dispDms, strLon),\
      fTwd (u,v), fTws (u,v), w, 
      fTwd (uCurr, vCurr), fTws (uCurr, vCurr), 
      seaEarth,
      (gribRequestRunning || readGribRet == -1) ? "WAITING GRIB" : "");
   
   gtk_statusbar_push (GTK_STATUSBAR(statusbar), context_id, sStatus);
}

/*! draw the polar circles and scales */
static void polarTarget (cairo_t *cr, double width, double height, double rStep) {
   double nStep = ceil (maxValInPol (polMat));
   int rMax = rStep * nStep;
   double centerX = width / 2;
   double centerY = height / 2;
   const int MIN_R_STEP_SHOW = 12;
   // cercles
   cairo_set_source_rgb (cr, 0.9, 0.9, 0.9);
   cairo_move_to (cr, centerX, centerY);
   for (int i = 1; i <= nStep; i++) {
      cairo_arc (cr, centerX, centerY, i * rStep, -G_PI/2, G_PI/2);
   }
  
   // segments 
   for (double angle = -90; angle <= 90; angle += 22.5) {
      cairo_move_to (cr, centerX, centerY);
      cairo_rel_line_to (cr, rMax * cos (DEG_TO_RAD * angle),
         rMax * sin (DEG_TO_RAD * angle));
   }
   cairo_stroke(cr);
   cairo_set_source_rgb (cr, 0.2, 0.2, 0.2);

   // libelles vitesses
   for (int i = 1; i <= nStep; i++) {
      cairo_move_to (cr, centerX - 40, centerY - i * rStep);
      if ((rStep > MIN_R_STEP_SHOW) || ((i % 2) == 0))  // only pair values if too close
         cairo_show_text (cr, g_strdup_printf("%2d kn", i));
   }
   // libelles angles
   for (double angle = -90; angle <= 90; angle += 22.5) {
      cairo_move_to (cr, centerX + rMax * cos (DEG_TO_RAD * angle) * 1.05,
         centerY + rMax * sin (DEG_TO_RAD * angle) * 1.05);
      cairo_show_text (cr, g_strdup_printf("%.2f°", angle + 90));
   }
   cairo_stroke(cr);
}

/*! draw he polar legends */
static void polarLegend (cairo_t *cr) {
   double xLeft = 100;
   double y = 5;
   double hSpace = 12;
   GdkRGBA color;
   cairo_set_line_width (cr, 5);
   cairo_set_source_rgb (cr, 0.5, 0.5, 0.5);
   cairo_rectangle (cr, xLeft, y, 120, polMat.nCol * hSpace);
   cairo_stroke(cr);
   cairo_set_line_width (cr, 1);
   xLeft += 20;
   y += hSpace;
   for (int c = 1; c < polMat.nCol; c++) {
      color = colors [c % nColors];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      cairo_move_to (cr, xLeft, y);
      cairo_show_text (cr, g_strdup_printf("Wind at %.2lf kn", polMat.t [0][c]));
      y += hSpace;
   }
   cairo_stroke(cr);
}

/*! return x and y in polar */
static void getPolarXY (int l, int c, double width, double height, double radiusFactor, double *x, double *y) {
   double angle = (90 - polMat.t [l][0]) * DEG_TO_RAD;  // Convertir l'angle en radians
   double radius = polMat.t [l][c] * radiusFactor;
   *x = width / 2 + radius * cos(angle);
   *y = height / 2 - radius * sin(angle);
}

/*! Draw polar from polar matrix */
static void on_draw_polar_event (GtkWidget *widget, cairo_t *cr, gpointer user_data) {
   double x, y, x1, x2, y1, y2;
   cairo_set_line_width(cr, 2);
   // Obtenir la largeur et la hauteur de la zone de dessin
   gint width, height;
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel(widget)), &width, &height);
   double radiusFactor = width / (maxValInPol (polMat) * 5); // Ajustez la taille de la courbe
   int minCol = (selectedPol == 0) ? 1 : selectedPol;
   int maxCol = (selectedPol == 0) ? polMat.nCol : selectedPol + 1;

   polarTarget (cr, width, height, radiusFactor);
   polarLegend (cr);
   cairo_set_line_width (cr, 1);

   for (int c = minCol; c < maxCol; c++) {
      GdkRGBA color = colors [c % nColors];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      getPolarXY (1, c, width, height, radiusFactor, &x, &y);
      //getPolarXY (i, 1, width, height, radiusFactor, &x, &y);
      cairo_move_to (cr, x, y);

      // Tracer la courbe de Bézier
      for (int l = 2; l < polMat.nLine; l += 3) {
         getPolarXY (l, c, width, height, radiusFactor, &x, &y);
         //cairo_line_to(cr, x, y);
         getPolarXY (l+1, c, width, height, radiusFactor, &x1, &y1);
         getPolarXY (l+2, c, width, height, radiusFactor, &x2, &y2);
         cairo_curve_to(cr, x, y, x1, y1, x2, y2);
      }
    cairo_stroke(cr);
   }
}

/*! find index in polar to draw */
static void on_filter_changed(GtkComboBox *widget, gpointer user_data) {
   selectedPol = gtk_combo_box_get_active(GTK_COMBO_BOX(filter_combo));
   gtk_widget_queue_draw (polar_drawing_area);
}

/* combo filter zone for polar */
static void create_filter_combo() {
   char str [MAX_SIZE_LINE];
   GtkTreeIter iter;
   // Créez un modèle de liste pour la liste déroulante
   filter_store = gtk_list_store_new(1, G_TYPE_STRING);
   
   // Ajoutez vos filtres au modèle
   gtk_list_store_append(filter_store, &iter);
   gtk_list_store_set(filter_store, &iter, 0, "All Wind Speeds", -1);    
   for (int c = 1; c < polMat.nCol; c++) {
      gtk_list_store_append(filter_store, &iter);
      sprintf (str, "%.2lf Knots. Wind Speed.", polMat.t [0][c]);
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

/*! Draw polar from polar file */
static void drawPolar (GtkWidget *widget, gpointer data) {
   char line [MAX_SIZE_LINE] = "Polar: "; 
   GtkWidget *box;
   char sStatus [MAX_SIZE_LINE];
   GtkWidget *statusbar;

   // Créer la fenêtre principale
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
   gtk_window_set_default_size(GTK_WINDOW(window), POLAR_WIDTH, POLAR_HEIGHT);
   strcat (line, par.polarFileName);
   gtk_window_set_title(GTK_WINDOW(window), line);

   // Créer un conteneur de type boîte
   box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   gtk_container_add(GTK_CONTAINER(window), box);

   // Créer une zone de dessin
   polar_drawing_area = gtk_drawing_area_new();
   gtk_widget_set_size_request(polar_drawing_area, -1, -1);
   g_signal_connect(G_OBJECT(polar_drawing_area), "draw", G_CALLBACK(on_draw_polar_event), NULL);
  
   // create zone for combo box
   create_filter_combo ();
   
   gtk_box_pack_start(GTK_BOX(box), filter_combo, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box), polar_drawing_area, TRUE, TRUE, 0);

   // Connecter le signal de fermeture de la fenêtre à gtk_main_quit
   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

   // Création de la barre d'état
   statusbar = gtk_statusbar_new ();
   guint context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "Statusbar");
   sprintf (sStatus, "nCol: %2d   nLig: %2d   max speed: %2.2lf", \
      polMat.nCol, polMat.nLine, maxValInPol (polMat));
   gtk_statusbar_push (GTK_STATUSBAR(statusbar), context_id, sStatus);
   gtk_box_pack_start(GTK_BOX(box), statusbar, FALSE, FALSE, 0);

   gtk_widget_show_all(window);
   gtk_main();
}

/*! write routing report in str */ 
static void reportToStr (char* buffer) {
   char totalDate [MAX_SIZE_DATE]; 
   char *pDate = &totalDate [0];
   Pp pt;
   char line [MAX_SIZE_LINE];
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
      pt = isocArray [nIsoc - 1][indexBest];     
      sprintf (line, "%.2f%c°, %.2f%c°\n", fabs(pt.lat), (pt.lat >=0) ? 'N':'S', fabs(pt.lon), (pt.lon >= 0) ? 'E':'W');
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
   // double forecastTime = par.startTimeInHours;
   double forecastTime = zone.timeStamp [route.kTime0];
   time_t seconds = 0;
   struct tm *tm0 = localtime (&seconds);;
   tm0->tm_year = ((int ) intDate / 10000) - 1900;
   tm0->tm_mon = ((int) (intDate % 10000) / 100) - 1;
   tm0->tm_mday = (int) (intDate % 100);
   tm0->tm_hour = 0; tm0->tm_min = 0; tm0->tm_sec = 0;
   time_t theTime = mktime (tm0);                      // converion struct tm en time_t
   theTime += 3600 * (intTime/100 + forecastTime);     // calcul ajout en secondes
   //struct tm *timeInfos = localtime (&theTime);        // conversion en struct tm
   timeInfos = localtime (&theTime);        // conversion en struct tm
   //sprintf (res, "%4d:%02d:%02d", timeInfos->tm_year + 1900, timeInfos->tm_mon + 1, timeInfos->tm_mday);
   
   start->year = timeInfos->tm_year + 1900;
   start->mon = timeInfos->tm_mon;
   start->day=  timeInfos->tm_mday;
   start->hour = timeInfos->tm_hour;
   start->min = timeInfos->tm_min;
   start->sec = 0;
}

/* calculate difference in hours beteen departure time and time 0 */
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

/*! get start date and time for routing. Returns Yes if OK Response */
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

   // Ajouter la boîte de disposition à la boîte de dialogue
   gtk_container_add(GTK_CONTAINER(content_area), hbox);

   // Afficher tous les widgets
   gtk_widget_show_all(dialog);

   // Exécuter la boîte de dialogue
   gint result = gtk_dialog_run(GTK_DIALOG(dialog));

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
   if (! isInZone (par.pOr, zone)) {
     infoMessage ("pOr not in wind zone", GTK_MESSAGE_WARNING);
     return;
   }
   buffer [0] = '\0';
   route.kTime0 = kTime;
   initStart (start);
   if (!calendar (start)) return;
   par.startTimeInHours = getDepartureTimeInHour (start); 
   if ((par.startTimeInHours < 0) || (par.startTimeInHours > zone.timeStamp [zone.nTimeStamp -1])) {
      infoMessage ("start time should be within grib window time !", GTK_MESSAGE_WARNING);
      return;
   }
   gettimeofday (&t0, NULL);
   ut0 = t0.tv_sec * MILLION + t0.tv_usec;
   indexBest = -1;
   ret = routing (par.pOr, par.pDest, par.startTimeInHours, par.tStep, &indexBest, &lastStepDuration);
   if (ret == -1) {
      infoMessage ("Too many points in isochrone", GTK_MESSAGE_ERROR);
      return;
   }

   // printf ("lastStepDuration: %lf\n", lastStepDuration);
   gettimeofday (&t1, NULL);
   ut1 = t1.tv_sec * MILLION + t1.tv_usec;
   sprintf (line, "Process time in seconds                 : %.2f\n", ((double) ut1-ut0)/MILLION);
   strcat (buffer, line);

   if (ret != NIL) {
      storeRoute (par.pDest, lastStepDuration);
      // storeRoute (par.pDest, 0);
      if (strlen (par.dumpRFileName) > 0) dumpRoute (par.dumpRFileName, par.pDest);
   }
   else {
      storeRoute (isocArray [nIsoc - 1][indexBest], lastStepDuration);
      // storeRoute (isocArray [nIsoc - 1][indexBest], 0);
      if (strlen (par.dumpRFileName) > 0) dumpRoute (par.dumpRFileName, isocArray [nIsoc - 1][indexBest]);
   }
   if (strlen (par.dumpIFileName) > 0) dumpAllIsoc (par.dumpIFileName);
   reportToStr (buffer);
   displayText (buffer, (route.destinationReached) ? "Destination reached" : "Destination not reached");
   printf ("%s\n", buffer);
}

/*! Callback for stop button */
static void on_stop_button_clicked(GtkWidget *widget, gpointer data) {
   animation_active = FALSE;
}

/*! Callback for animation update */
static gboolean on_play_timeout(gpointer user_data) {
   long deltaPlus = 3 * par.tStep / (zone.timeStamp [1]-zone.timeStamp [0]);
   if (kTime < zone.nTimeStamp - 1 + deltaPlus) kTime += 1;
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
   statusBarUpdate ();
   gtk_widget_queue_draw(drawing_area);
}

/*! Callback for to end button */
static void on_to_end_button_clicked(GtkWidget *widget, gpointer data) {
   kTime = zone.nTimeStamp - 1;
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
   long deltaPlus = 3 * par.tStep / (zone.timeStamp [1]-zone.timeStamp [0]);
   if (kTime < zone.nTimeStamp - 1 + deltaPlus) kTime += 1; 
   statusBarUpdate ();
   gtk_widget_queue_draw(drawing_area);
}

/* display isochrone file */
static void isocDump (GtkWidget *widget, cairo_t *cr, gpointer data) {
   if (nIsoc == 0)
      infoMessage ("No isochrone", GTK_MESSAGE_INFO); 
   else displayFile (par.dumpIFileName, "Isochrones");   
}

/* display wave polar matrix */
static void wavePolarDump (GtkWidget *widget, cairo_t *cr, gpointer data) {
   char buffer [MAX_SIZE_BUFFER] = "";
   polToStr (buffer, wavePolMat);
   displayText (buffer, "Wave Polar Matrix");
}

/* display polar matrix */
static void polarDump (GtkWidget *widget, cairo_t *cr, gpointer data) {
   GtkWidget *label;
   char str [MAX_SIZE_LINE] = "Polar Grid: ";
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   strcat (str, par.polarFileName);
   gtk_window_set_title(GTK_WINDOW(window), str);
   gtk_container_set_border_width(GTK_CONTAINER(window), 10);
   gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

   // Scroll zie creation
   GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(window), scrolled_window);
   gtk_widget_set_size_request(scrolled_window, 800, 400);

   // grid creation
   GtkWidget *grid = gtk_grid_new();   
   gtk_container_add(GTK_CONTAINER(scrolled_window), grid);
   
   for (int line = 0; line < polMat.nLine; line++) {
      for (int col = 0; col < polMat.nCol; col++) {
	      if ((col == 0) && (line == 0)) strcpy (str, "TWA/TWS");
         else sprintf(str, "%6.2f", polMat.t [line][col]);
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

/*! print the report of route calculation */
static void rteReport (GtkWidget *widget, gpointer data) {
   char buffer [MAX_SIZE_BUFFER] = "";
   if (route.n <= 0 && route.destinationReached == false)
      infoMessage ("No route calculated", GTK_MESSAGE_INFO);
   else {
      reportToStr (buffer);  
      displayText (buffer, (route.destinationReached) ? "Destination reached" :\
         "Destination unreached");
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

/*! print the orthodromic routes through waypoints */
static void orthoDump (GtkWidget *widget, cairo_t *cr, gpointer data) {
   char buffer [MAX_SIZE_BUFFER] = "";
   calculateOrthoRoute ();
   wayPointToStr (buffer);
   displayText (buffer, "orthodomic Waypoint Dump");
}

/*! show parameters information */
static void parDump (GtkWidget *widget, cairo_t *cr, gpointer data) {
   writeParam (TEMP_FILE_NAME, true);
   displayFile (TEMP_FILE_NAME, "Parameter Dump");
}

/*! show Points of Interest information */
static void poiDump (GtkWidget *widget, gpointer data) {
   char buffer [MAX_SIZE_BUFFER] = "";
   poiToStr (buffer);
   displayText (buffer, "Point of Interest Table");
}

/*! show GPS information */
static void gpsDump (GtkWidget *widget, gpointer data) {
   char buffer [MAX_SIZE_BUFFER] = "";
   if (gpsToStr (buffer))
      infoMessage (buffer, GTK_MESSAGE_INFO);
   else infoMessage ("No GPS data avalaible", GTK_MESSAGE_WARNING);
}

/*! launch help HTML file */
static void help (GtkWidget *widget, gpointer data) {
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

   GtkWidget *web_view = webkit_web_view_new();
   gtk_container_add(GTK_CONTAINER(window), web_view);

   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

   webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), par.helpFileName);
   gtk_widget_show_all(window);
   gtk_main();
}

/*! display info on the program */
static void helpInfo(GtkWidget *p_widget, gpointer user_data) {
   GtkWidget *p_about_dialog = gtk_about_dialog_new ();
   gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (p_about_dialog), PROG_VERSION);
   gtk_about_dialog_set_program_name  (GTK_ABOUT_DIALOG (p_about_dialog), PROG_NAME);
   const gchar *authors[2] = {PROG_AUTHOR, NULL};
   gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (p_about_dialog), authors);
   gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (p_about_dialog), PROG_WEB_SITE);
   GdkPixbuf *p_logo = p_logo = gdk_pixbuf_new_from_file (PROG_LOGO, NULL);
   gtk_about_dialog_set_logo (GTK_ABOUT_DIALOG (p_about_dialog), p_logo);
   gtk_dialog_run (GTK_DIALOG (p_about_dialog));
}

/*! url selection */ 
static void urlChange (char *url) {
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
   gint response = gtk_dialog_run (GTK_DIALOG(dialog));
   
   // Récupération des valeurs saisies si la réponse est GTK_RESPONSE_ACCEPT
   if (response == GTK_RESPONSE_ACCEPT) {
       const char *pt = gtk_entry_get_text (GTK_ENTRY(url_entry));
       strcpy (url, pt);
       gtk_widget_queue_draw(drawing_area); // affiche le tout
   }
   // Fermeture de la boîte de dialogue
   gtk_widget_destroy(dialog);
}
   
/*! Select grib file */
static void downloadGrib (GtkWidget *widget, cairo_t *cr, gpointer data) {
   char url [MAX_SIZE_LINE];
   char fileName [MAX_SIZE_LINE];
   char outputFileName [MAX_SIZE_LINE] = "grib/";
   char remotePath [] = "20231126/12z/0p4-beta/enfo/";
   strcpy (fileName, "20231126120000-0h-enfo-ef.index");
   strcpy (url, ROOT_GRIB_SERVER);
   strcat (url,remotePath);
   strcat (url, fileName);
   printf ("url %s\n", url);
   urlChange (url);
   printf ("new url %s\n", url);
   strcat (outputFileName, strrchr (url, '/')); // le nom du fichier
   if (curlGet (url, outputFileName))
      infoMessage ("File dowlowded. You can open it", GTK_MESSAGE_INFO);
   else infoMessage ("Error dowloading file", GTK_MESSAGE_ERROR);
}

/*! check if readGrib is terminated */
static gboolean readGribCheck (gpointer data) {
   statusBarUpdate ();
   gtk_widget_queue_draw (drawing_area);
   if (readGribRet == -1) { // not terminated
      return TRUE;
   }
   g_source_remove (gribReadTimeout); // timer stopped
   gtk_widget_destroy (spinner_window);
   if (readGribRet == 0) {
      //initConst (zone);
      infoMessage ("Error in readGrib wind", GTK_MESSAGE_ERROR);
   }
   else {
      kTime = 0;
      par.constWindTws = 0;
      initDispZone ();
   }
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
      initConst (currentZone);
      infoMessage ("Error in readGrib wind", GTK_MESSAGE_ERROR);
   }
   return TRUE;
}
   
/*! Select grib file and read grib file, fill gribData */
static void openGrib (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data;
   GtkWidget *dialog;
   GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
   gint res;

   dialog = gtk_file_chooser_dialog_new ("Open Grib", NULL, action, \
    "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), "grib");

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Grib Files");
   gtk_file_filter_add_pattern(filter, "*.gr*");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

   res = gtk_dialog_run(GTK_DIALOG(dialog));
   if (res == GTK_RESPONSE_ACCEPT) {
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

      // buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

      if (*comportement == WIND) { // wind
         strncpy (par.gribFileName, fileName, MAX_SIZE_FILE_NAME - 1);
         readGribRet = -1; // Global variable
         /*GThread *thread = */g_thread_new("readGribThread", readGrib, NULL);
         gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readGribCheck, NULL);
         spinner ("Grib File decoding");
      }
      else {                 
         strncpy (par.currentGribFileName, fileName, MAX_SIZE_FILE_NAME - 1);
         readCurrentGribRet = -1; // Global variable
         /*GThread *thread = */g_thread_new("readCurrentGribThread", readCurrentGrib, NULL);
         currentGribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readCurrentGribCheck, NULL);
         spinner ("Current Grib File decoding");
      }
      gtk_widget_queue_draw (drawing_area);
   }
   gtk_widget_destroy(dialog);
}

/*! Select polar file and read polar file, fill polMat */
static void openPolar (GtkWidget *widget, gpointer data) {
   GtkWidget *dialog;
   char *fileName;
   char buffer [MAX_SIZE_BUFFER] = "";
   GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
   gint res;

   dialog = gtk_file_chooser_dialog_new("Open Polar", NULL, action, \
    "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

   gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Polar Files");
   gtk_file_filter_add_pattern(filter, "*.pol");
   gtk_file_filter_add_pattern(filter, "*.csv");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), "pol");

   res = gtk_dialog_run (GTK_DIALOG(dialog));
   if (res == GTK_RESPONSE_ACCEPT) {
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
         drawPolar (window, NULL); 
      }
      else {
         readPolar (fileName, &wavePolMat);
         strcpy (par.wavePolFileName, fileName);
         polToStr (buffer, wavePolMat);
         displayText (buffer, "Wave Polar Matrix");
      }
    
  }
  else {
     gtk_widget_destroy (dialog);
  }
}

/*! Write parameter file and initialize context */
static void saveScenario (GtkWidget *widget, gpointer data) {
   GtkWidget *dialog;
   GtkFileChooser *chooser;
   GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;

   dialog = gtk_file_chooser_dialog_new("Save As",
                                        NULL,
                                        action,
                                        "_Cancel",
                                        GTK_RESPONSE_CANCEL,
                                        "_Save",
                                        GTK_RESPONSE_ACCEPT,
                                        NULL);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), "par");

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name (filter, "Parameter Files");
   gtk_file_filter_add_pattern(filter, "*.par");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
   chooser = GTK_FILE_CHOOSER(dialog);

   // Désactive la confirmation d'écrasement du fichier existant
   gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

   // Définit un nom de fichier par défaut (peut être vide)
   gtk_file_chooser_set_current_name(chooser, "new_file.par");

   gint result = gtk_dialog_run(GTK_DIALOG(dialog));

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
   sprintf (line, "%s %s %s\n", par.editor, PARAMETERS_FILE, par.poiFileName);
   if (system (line) != 0) {
      fprintf (stderr, "Error in editScenario. System call : %s\n", line);
      return;
   }
   if (confirm (PARAMETERS_FILE, "Confirm loading file below")) {
      readParam (PARAMETERS_FILE);
      readGrib (NULL);
      if (readGribRet == 0) {
         infoMessage ("Error in readgrib", GTK_MESSAGE_ERROR);
         return;
      }
         
      initDispZone ();
      readPolar (par.polarFileName, &polMat);
      readPolar (par.wavePolFileName, &wavePolMat);
   }
}

/*! Read parameter file and initialize context */
static void openScenario (GtkWidget *widget, gpointer data) {
   char *fileName;
   GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
   gint res;

   GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Parameters", NULL, action, \
    "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), "par");

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Parameter Files");
   gtk_file_filter_add_pattern(filter, "*.par");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

   res = gtk_dialog_run (GTK_DIALOG(dialog));
   if (res == GTK_RESPONSE_ACCEPT) {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
      fileName = gtk_file_chooser_get_filename(chooser);
      gtk_widget_destroy(dialog);
      readParam (fileName);
      readGrib (NULL);
      if (readGribRet == 0) {
         infoMessage ("Error in readgrib", GTK_MESSAGE_ERROR);
         return;
      }
         
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
   char line [MAX_SIZE_LINE] = "";
   if (*comportement == WIND)
      if (zone.nbLat == 0)
         infoMessage ("No wind data grib available", GTK_MESSAGE_ERROR);
      else {
         gribToStr (buffer, zone);
         sprintf (line, "Size of grib file %s: %ld bytes\n", par.gribFileName, \
            getFileSize (par.gribFileName));
         strcat (buffer, line); 
         displayText (buffer, "Grib Wind Information");
      }
   else
      if (currentZone.nbLat == 0)
         infoMessage ("No current data grib available", GTK_MESSAGE_ERROR);
      else {
         gribToStr (buffer, currentZone);
         sprintf (line, "Size of grib file %s: %ld bytes\n", par.currentGribFileName, \
            getFileSize (par.currentGribFileName));
         strcat (buffer, line); 
         displayText (buffer, "Grib Current Information");
      }
}

/* Check info in grib file */
static void runCheckGrib (GtkWidget *widget, gpointer data) {
   int *comportement = (int *) data; // either WIND or CURRENT
   char buffer [MAX_SIZE_BUFFER] = "";
   if (*comportement == WIND) {
      if (zone.nbLat == 0)
         infoMessage ("No wind data grib available", GTK_MESSAGE_ERROR);
      else {
         checkGribToStr (buffer, zone, gribData);
         displayText (buffer, "Run check bizarre values... !!!");
      }
   }
   else { // current
      if (currentZone.nbLat == 0) 
         infoMessage ("No current data grib available", GTK_MESSAGE_ERROR);
      else {
         checkGribToStr (buffer, currentZone, currentGribData);
         displayText (buffer, "Run check bizarre values... !!!");
      }
   }
}

/*! read mail grib received from provider */
static gboolean mailGribRead (gpointer data) {
   FILE *fp;
   if (! gribRequestRunning) {
      return TRUE;
   }

   char line[MAX_SIZE_LINE] = "";
   char buffer[MAX_SIZE_BUFFER] = "\n";
   int n = 0;
   char *fileName, *end;
   fp = popen (par.imapScript, "r");
   if (fp == NULL) {
      fprintf (stderr, "mailGribRead Error opening: %s\n", par.imapScript);
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
            
            strcpy (par.gribFileName, fileName);
            readGrib (NULL);
            if (readGribRet == 0) {
               infoMessage ("Error in readgrib", GTK_MESSAGE_ERROR);
            } 
            kTime = 0;
            par.constWindTws = 0;
            gribRequestRunning = false;
            initDispZone ();
            gtk_widget_destroy (spinner_window);
            statusBarUpdate ();
            gtk_widget_queue_draw (drawing_area);
         }
      }
   }
   return TRUE;
}

/*! Callback radio buuton selecting providr for mail request */
static void radio_button_toggled1(GtkToggleButton *togglebutton, gpointer user_data) {
   provider = SAILDOCS;
}
static void radio_button_toggled2(GtkToggleButton *togglebutton, gpointer user_data) {
   provider = MAILASAIL;
}
static void radio_button_toggled3(GtkToggleButton *togglebutton, gpointer user_data) {
   provider = GLOBALMARINET;
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
static gint mailRequestBox (const char *buffer) {
   GtkWidget *dialog;
   GtkWidget *content_area;
   GtkWidget *label_buffer;
   GtkWidget *radio_button_provider1 = NULL, *radio_button_provider2 = NULL, *radio_button_provider3 = NULL;
   GtkWidget *hbox0, *hbox1, *hbox2;
   GtkWidget *label_time_step, *label_time_max;
   GtkWidget *spin_button_time_step, *spin_button_time_max;

   // Création de la boîte de dialogue
   dialog = gtk_dialog_new_with_buttons("Launch mail Grib request", \
      GTK_WINDOW(window), GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);

    // Récupération de la zone de contenu de la boîte de dialogue
   content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   
   // Création des boutons radio
   label_buffer = gtk_label_new (buffer);
   if (par.nSmtp > 0) {
      radio_button_provider1 = gtk_radio_button_new_with_label(NULL, par.smtpTo [SAILDOCS] );
      g_signal_connect(radio_button_provider1, "toggled", G_CALLBACK(radio_button_toggled1), NULL);
   }
   if (par.nSmtp > 1) {
      radio_button_provider2 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_button_provider1), \
         par.smtpTo [MAILASAIL]);
      g_signal_connect(radio_button_provider2, "toggled", G_CALLBACK(radio_button_toggled2), NULL);
   }
   if (par.nSmtp > 2) {
      radio_button_provider3 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_button_provider2), \
         par.smtpTo [GLOBALMARINET]);
      g_signal_connect(radio_button_provider3, "toggled", G_CALLBACK(radio_button_toggled3), NULL);
   }
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
   
   if (par.nSmtp > 0)
      gtk_box_pack_start(GTK_BOX(hbox1), radio_button_provider1, FALSE, FALSE, 0);
   if (par.nSmtp > 1)
   gtk_box_pack_start(GTK_BOX(hbox1), radio_button_provider2, FALSE, FALSE, 0);
   if (par.nSmtp > 2)
   gtk_box_pack_start(GTK_BOX(hbox1), radio_button_provider3, FALSE, FALSE, 0);

   gtk_box_pack_start(GTK_BOX(hbox2), label_time_step, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox2), spin_button_time_step, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox2), label_time_max, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox2), spin_button_time_max, FALSE, FALSE, 0);

   gtk_box_pack_start(GTK_BOX(content_area), hbox0, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(content_area), hbox1, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(content_area), hbox2, FALSE, FALSE, 0);

   gtk_widget_show_all(dialog);
   gint result = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
   return result;
}

/*! change parameters and pOr and pDest coordinates */ 
static void change (GtkWidget *widget, gpointer data) {
   GtkWidget *dialog, *content_area, *grid;
   GtkWidget *label_origin0, *label_origin1, *entry_origin_lat, *entry_origin_lon;
   GtkWidget *label_dest0, *label_dest1, *entry_dest_lat, *entry_dest_lon;
   GtkWidget *label_cog, *label_range;
   GtkWidget *label_start_time, *label_time_step, *entry_start_time, *entry_time_step;
   GtkWidget *label_opt, *label_max_iso;
   GtkWidget *label_distTarget, *label_n_sector;
   GtkWidget *label_wind_twd, *label_wind_tws, *entry_wind_twd, *entry_wind_tws;
   GtkWidget *label_current_twd, *label_current_tws, *entry_current_twd, *entry_current_tws;
   GtkWidget *label_penalty0, *label_penalty1, *entry_penalty0, *entry_penalty1;
   GtkWidget *label_sog, *entry_sog;
   GtkWidget *label_style, *label_dms;

   // Création de la boîte de dialogue
   dialog = gtk_dialog_new_with_buttons("Change", NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_CANCEL, NULL);

   content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

   // Création d'une grille pour organiser les éléments
   grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
   gtk_container_add(GTK_CONTAINER(content_area), grid);

   // Création des éléments pour l'origine
   label_origin0 = gtk_label_new("Origine lat:");
   entry_origin_lat = gtk_entry_new();
   label_origin1 = gtk_label_new("lon:");
   entry_origin_lon = gtk_entry_new();
   char originLatStr[50], originLonStr[50];
   snprintf(originLatStr, sizeof(originLatStr), "%.2f", par.pOr.lat);
   snprintf(originLonStr, sizeof(originLonStr), "%.2f", par.pOr.lon);
   gtk_entry_set_text(GTK_ENTRY(entry_origin_lat), originLatStr);
   gtk_entry_set_text(GTK_ENTRY(entry_origin_lon), originLonStr);

   gtk_grid_attach(GTK_GRID(grid), label_origin0,    0, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_origin_lat, 1, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), label_origin1,    2, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_origin_lon, 3, 0, 1, 1);

   // Création des éléments pour la destination
   label_dest0 = gtk_label_new("Destination lat:");
   entry_dest_lat = gtk_entry_new();
   label_dest1 = gtk_label_new("lon:");
   entry_dest_lon = gtk_entry_new();
   gtk_grid_attach(GTK_GRID(grid), label_dest0,      0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_dest_lat,   1, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), label_dest1,      2, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_dest_lon,   3, 1, 1, 1);

   // Pré-remplissage avec les valeurs de départ
   char destLatStr[50], destLonStr[50];
   snprintf(destLatStr, sizeof(destLatStr), "%.2f", par.pDest.lat);
   snprintf(destLonStr, sizeof(destLonStr), "%.2f", par.pDest.lon);
   gtk_entry_set_text(GTK_ENTRY(entry_dest_lat), destLatStr);
   gtk_entry_set_text(GTK_ENTRY(entry_dest_lon), destLonStr);

   // Création des éléments pour cog et cog step
   label_cog = gtk_label_new ("Cog Step:");
   GtkWidget *spin_cog = gtk_spin_button_new_with_range(1, 20, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_cog), par.cogStep);
   label_range = gtk_label_new ("Cog Range:");
   GtkWidget *spin_range = gtk_spin_button_new_with_range(50, 100, 5);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_range), par.rangeCog);
   gtk_grid_attach(GTK_GRID(grid), label_cog,       0, 2, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), spin_cog,        1, 2, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), label_range,     2, 2, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), spin_range,      3, 2, 1, 1);

   // Création des éléments pour start time et time step
   label_start_time = gtk_label_new("Start Time in hours:");
   entry_start_time = gtk_entry_new();
   label_time_step = gtk_label_new("Time Step:");
   entry_time_step = gtk_entry_new();
   gtk_grid_attach(GTK_GRID(grid), label_start_time,      0, 3, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_start_time,      1, 3, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), label_time_step,  2, 3, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_time_step,  3, 3, 1, 1);
   
   // Pré-remplissage avec les valeurs de départ
   char startTimeStr [50], timeStep [50];
   snprintf(startTimeStr, sizeof(startTimeStr), "%.2lf", par.startTimeInHours);
   snprintf(timeStep, sizeof(timeStep), "%.2lf", par.tStep);
   gtk_entry_set_text(GTK_ENTRY(entry_start_time), startTimeStr);
   gtk_entry_set_text(GTK_ENTRY(entry_time_step), timeStep);

   // Création des éléments pour opt et Isoc
   label_opt = gtk_label_new("Opt:");
   gtk_grid_attach(GTK_GRID(grid), label_opt,     0, 4, 1, 1);
   
   GtkListStore *liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
   // Ajouter des éléments à la liste
   GtkTreeIter iter;
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "No opt",         1, 0, -1);
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Sector Algo 1.", 1, 1, -1);
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Sector Algo. 2", 1, 2, -1);
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Convexe Algo.",  1, 3, -1);

   // Créer une combobox et définir son modèle
   GtkWidget *opt_combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
   g_object_unref(liststore);

   // Créer un rendu de texte pour afficher le texte dans la combobox
   GtkCellRenderer *opt_renderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(opt_combo_box), opt_renderer, TRUE);
   gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(opt_combo_box), opt_renderer, "text", 0, NULL);
   gtk_combo_box_set_active(GTK_COMBO_BOX(opt_combo_box), par.opt);
   gtk_grid_attach(GTK_GRID(grid), opt_combo_box,      1, 4, 1, 1);

   label_max_iso = gtk_label_new("Max Isoc:");
   GtkWidget *spin_max_iso = gtk_spin_button_new_with_range(0, MAX_N_ISOC, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_max_iso), par.maxIso);
   gtk_grid_attach(GTK_GRID(grid), label_max_iso, 2, 4, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), spin_max_iso,  3, 4, 1, 1);
   
   // Création des éléments pour sector
   label_distTarget = gtk_label_new("Disp Target:");
   GtkWidget *spin_distTarget = gtk_spin_button_new_with_range(0, 240, 10);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_distTarget), par.distTarget);
   label_n_sector = gtk_label_new("N sectors:");
   GtkWidget *spin_n_sectors = gtk_spin_button_new_with_range(10, 1000, 10);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_n_sectors), par.nSectors);
   gtk_grid_attach(GTK_GRID(grid), label_distTarget,    0, 5, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), spin_distTarget,     1, 5, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), label_n_sector, 2, 5, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), spin_n_sectors, 3, 5, 1, 1);
   
   // Création des éléments pour constWind
   label_wind_twd = gtk_label_new("Const Wind twd");
   entry_wind_twd = gtk_entry_new();
   label_wind_tws = gtk_label_new("Const Wind tws:");
   entry_wind_tws = gtk_entry_new();
   gtk_grid_attach(GTK_GRID(grid), label_wind_twd, 0, 6, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_wind_twd, 1, 6, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), label_wind_tws, 2, 6, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_wind_tws, 3, 6, 1, 1);
   
   // Pré-remplissage avec les valeurs de départ
   char strTwd [50], strTws [50];
   snprintf(strTwd, sizeof(strTwd), "%.2f", par.constWindTwd);
   snprintf(strTws, sizeof(strTws), "%.2f", par.constWindTws);
   gtk_entry_set_text(GTK_ENTRY(entry_wind_twd), strTwd);
   gtk_entry_set_text(GTK_ENTRY(entry_wind_tws), strTws);

   // Création des éléments pour constCurrent
   label_current_twd = gtk_label_new("Const Current twd");
   entry_current_twd = gtk_entry_new();
   label_current_tws = gtk_label_new("Const Current tws:");
   entry_current_tws = gtk_entry_new();
   gtk_grid_attach(GTK_GRID(grid), label_current_twd, 0, 7, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_current_twd, 1, 7, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), label_current_tws, 2, 7, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_current_tws, 3, 7, 1, 1);
   
   // Pré-remplissage avec les valeurs de départ
   snprintf(strTwd, sizeof(strTwd), "%.2f", par.constCurrentD);
   snprintf(strTws, sizeof(strTws), "%.2f", par.constCurrentS);
   gtk_entry_set_text(GTK_ENTRY(entry_current_twd), strTwd);
   gtk_entry_set_text(GTK_ENTRY(entry_current_tws), strTws);

   // Création des éléments pour penalty
   label_penalty0 = gtk_label_new("Virement de bord");
   entry_penalty0 = gtk_entry_new();
   label_penalty1 = gtk_label_new("Empannage:");
   entry_penalty1 = gtk_entry_new();
   gtk_grid_attach(GTK_GRID(grid), label_penalty0, 0, 8, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_penalty0, 1, 8, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), label_penalty1, 2, 8, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_penalty1, 3, 8, 1, 1);
   
   // Pré-remplissage avec les valeurs de départ
   char strP0 [50], strP1 [50];
   snprintf(strP0, sizeof(strP0), "%.2f", par.penalty0);
   snprintf(strP1, sizeof(strP1), "%.2f", par.penalty1);
   gtk_entry_set_text(GTK_ENTRY(entry_penalty0), strP0);
   gtk_entry_set_text(GTK_ENTRY(entry_penalty1), strP1);

   // Création des éléments pour constSog et style
   label_sog = gtk_label_new ("Const Sog:");
   entry_sog = gtk_entry_new ();
   gtk_grid_attach(GTK_GRID(grid), label_sog,      0, 9, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry_sog,      1, 9, 1, 1);
   
   // Pré-remplissage avec les valeurs de départ
   char strSog [50];
   snprintf(strSog, sizeof(strSog), "%.2f", par.constSog);
   gtk_entry_set_text(GTK_ENTRY(entry_sog), strSog);
   
   label_style = gtk_label_new ("Isochrone Style:");
   gtk_grid_attach(GTK_GRID(grid), label_style,      0, 10, 1, 1);
   // Créer un modèle de liste pour la style_combo_box
   liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
   // Ajouter des éléments à la liste
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Nothing", 1, 0, -1);
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Points", 1, 1, -1);
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Segment", 1, 2, -1);
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Bezier", 1, 3, -1);

   // Créer une combobox et définir son modèle
   GtkWidget *style_combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
   g_object_unref(liststore);

   // Créer un rendu de texte pour afficher le texte dans la combobox
   GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(style_combo_box), renderer, TRUE);
   gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(style_combo_box), renderer, "text", 0, NULL);
   gtk_combo_box_set_active(GTK_COMBO_BOX(style_combo_box), par.style);
   gtk_grid_attach(GTK_GRID(grid), style_combo_box,      1, 10, 1, 1);

   label_dms = gtk_label_new ("DMS:");
   gtk_grid_attach(GTK_GRID(grid), label_dms,        2, 10, 1, 1);

   // Créer un modèle de liste pour la style_combo_box
   liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
   // Ajouter des éléments à la liste
   //GtkTreeIter iter;
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Decimal Degrees", 1, 0, -1);
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Degrees, Decimal Minutes", 1, 1, -1);
   gtk_list_store_append(liststore, &iter);
   gtk_list_store_set(liststore, &iter, 0, "Degrees, Minutes, Seconds", 1, 2, -1);

   // Créer une combobox et définir son modèle
   GtkWidget *dms_combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
   g_object_unref(liststore);

   // Créer un rendu de texte pour afficher le texte dans la combobox
   GtkCellRenderer *dms_renderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dms_combo_box), dms_renderer, TRUE);
   gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dms_combo_box), dms_renderer, "text", 0, NULL);
   gtk_combo_box_set_active(GTK_COMBO_BOX(dms_combo_box), par.dispDms);
   gtk_grid_attach(GTK_GRID(grid), dms_combo_box,      3, 10, 1, 1);


   // Affichage de la boîte de dialogue et récupération de la réponse
   gtk_widget_show_all(dialog);
   gint response = gtk_dialog_run(GTK_DIALOG(dialog));
   
   // Récupération des valeurs saisies si la réponse est GTK_RESPONSE_ACCEPT
   if (response == GTK_RESPONSE_ACCEPT) {
       par.pOr.lat = atof (gtk_entry_get_text(GTK_ENTRY(entry_origin_lat)));
       par.pOr.lon= atof (gtk_entry_get_text(GTK_ENTRY(entry_origin_lon)));
       if (par.pOr.lon > 180) par.pOr.lon -= 360;
       par.pDest.lat = atof (gtk_entry_get_text(GTK_ENTRY(entry_dest_lat)));
       par.pDest.lon = atof (gtk_entry_get_text(GTK_ENTRY(entry_dest_lon)));
       if (par.pDest.lon > 180) par.pDest.lon -= 360;
       par.cogStep = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_cog));
       par.rangeCog = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_range));
       par.startTimeInHours = atof (gtk_entry_get_text(GTK_ENTRY(entry_start_time)));
       par.tStep = atof (gtk_entry_get_text(GTK_ENTRY(entry_time_step)));
       par.opt = gtk_combo_box_get_active(GTK_COMBO_BOX(opt_combo_box));
       par.maxIso = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_max_iso));
       if (par.maxIso > MAX_N_ISOC) par.maxIso = MAX_N_ISOC;
       par.distTarget = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_distTarget));
       par.nSectors = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_n_sectors));
       par.constWindTwd = atof (gtk_entry_get_text(GTK_ENTRY(entry_wind_twd)));
       par.constWindTws = atof (gtk_entry_get_text(GTK_ENTRY(entry_wind_tws)));
       if (par.constWindTws != 0)
	        initConst ();
       par.constCurrentD = atof (gtk_entry_get_text(GTK_ENTRY(entry_current_twd)));
       par.constCurrentS = atof (gtk_entry_get_text(GTK_ENTRY(entry_current_tws)));
       par.penalty0 = atof (gtk_entry_get_text(GTK_ENTRY(entry_penalty0)));
       par.penalty1 = atof (gtk_entry_get_text(GTK_ENTRY(entry_penalty1)));
       par.constSog = atof (gtk_entry_get_text(GTK_ENTRY(entry_sog)));
       par.style = gtk_combo_box_get_active(GTK_COMBO_BOX(style_combo_box));
       par.dispDms = gtk_combo_box_get_active(GTK_COMBO_BOX(dms_combo_box));
       par.pOr.id = -1;
       par.pOr.father = -1;
       par.pDest.id = 0;
       par.pDest.father = 0;
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

/*! callback for popup menu */
static void on_popup_menu_selection(GtkWidget *widget, gpointer data) {
   const char *selection = (const char *)data;
   gint width, height;
   char buffer [1024] = "";
 
   // Extraction des coordonnées de la structure
   Coordinates *coords = (Coordinates *)g_object_get_data(G_OBJECT(widget), "coordinates");
   gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel (window)), &width, &height);
   //g_print ("on_popup_manu_s width %d height %d\n", width, height);
   if (strcmp(selection, "Waypoint") == 0) {
      if (wayRoute.n < MAX_N_WAY_POINT) {
         destPressed = false;
         wayRoute.t [ wayRoute.n].lat = yToLat (coords->y, disp.yB, disp.yT);
         wayRoute.t [ wayRoute.n].lon = xToLon (coords->x, disp.xL, disp.xR);
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
      wayRoute.totDist = 0;
      
   } else if (strcmp(selection, "Destination") == 0) {
      destPressed = true;
      route.n = 0;
      par.pDest.lat = yToLat (coords->y, disp.yB, disp.yT);
      par.pDest.lon = xToLon (coords->x, disp.xL, disp.xR);
       wayRoute.t [ wayRoute.n].lat = par.pDest.lat;
       wayRoute.t [ wayRoute.n].lon = par.pDest.lon;

      // g_print ("Clic gauche sur Destination. Coordonnées : x=%.2f, y=%.2f\n", coords->x, coords->y);
      //printf ("In on_popup_menu_selection %s\n", extIsSea (par.pDest.lon ,par.pDest.lat) ? "IsSea\n" : "Is earth");
      calculateOrthoRoute ();
      wayPointToStr (buffer);
      infoMessage (buffer, GTK_MESSAGE_INFO);
   }
   par.pOr.id = -1;
   par.pOr.father = -1;
   par.pDest.id = 0;
   par.pDest.father = 0;
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
   GtkWidget *menu, *item_origine, *item_destination, *item_waypoint;
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

      // Création d'une structure pour stocker les coordonnées
      Coordinates *coords = g_new(Coordinates, 1);
      coords->x = event->x;
      coords->y = event->y;

      // Ajout des coordonnées à la propriété de l'objet widget
      g_object_set_data(G_OBJECT(item_origine), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_waypoint), "coordinates", coords);
      g_object_set_data(G_OBJECT(item_destination), "coordinates", coords);

      g_signal_connect(item_origine, "activate", G_CALLBACK (on_popup_menu_selection), "Origin");
      g_signal_connect(item_waypoint, "activate", G_CALLBACK(on_popup_menu_selection), "Waypoint");
      g_signal_connect(item_destination, "activate", G_CALLBACK(on_popup_menu_selection), "Destination");

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_origine);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_waypoint);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_destination);

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
      provider = SAILDOCS; // default 
      if (mailRequestBox (buffer) == GTK_RESPONSE_OK) {
         smtpGribRequest (provider, lat1, lon1, lat2, lon2);
         spinner ("Waiting for grib Mail response");
         gribRequestRunning = true;
         gribMailTimeout = g_timeout_add (GRIB_TIME_OUT, mailGribRead, NULL);
      }
      statusBarUpdate ();
   }
   selecting = FALSE;
   gtk_widget_queue_draw(widget); // Redessiner pour effacer le rectangle de sélection
   return TRUE;
}

/*! submenu with label and icon */
static GtkWidget *mySubMenu (const gchar *str, const gchar *iconName) {
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
   int forWind = WIND, forCurrent = CURRENT;       // distinction wind current for openGrib call
   //int sailDocs = SAILDOCS, mailASail = MAILASAIL;
   my_gps_data.ret = initGPS ();
   printf ("initGPS : %d\n", my_gps_data.ret);
   
   gtk_init(&argc, &argv);
   if (setlocale(LC_ALL, "C") == NULL) {           // very important for scanf decimal numbers
      fprintf (stderr, "main () Error: setlocale");
      return EXIT_FAILURE;
   }
   
   if (argc <= 1)                                  // no parameter on cmd pLine
      readParam (PARAMETERS_FILE);
   else if ((argc >= 2) && (argv [1][0] != '-'))   // file name is the only arg of cmd pLine
      readParam (argv [1]);
   else if (argv [1][0] == '-') {                  // there is an option
      if (argc >= 3)
         readParam (argv [2]);
      else
         readParam (PARAMETERS_FILE);
      optionManage (argv [1][1]);
      exit (EXIT_SUCCESS);
   }

   for (int i= 0; i < par.nShpFiles; i++)
      initSHP (par.shpFileName [i]);
   readGrib (NULL);
   if (readGribRet == 0) {
      fprintf (stderr, "main: unable to read grib file: %s\n ", par.gribFileName);
   }
      
   nPoi = readPoi (par.poiFileName);
   initDispZone ();
   readPolar (par.polarFileName, &polMat);
   readPolar (par.wavePolFileName, &wavePolMat);
   readIsSea (par.isSeaFileName);
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   wayRoute.n = 0;
   wayRoute.t[0].lat = par.pDest.lat;
   wayRoute.t[0].lon = par.pDest.lon;

   // Création de la fenêtre principale
   window  = gtk_window_new (GTK_WINDOW_TOPLEVEL); // Main window : golbal variable
   gtk_window_maximize (GTK_WINDOW (window)); // full screen
   gtk_window_set_title(GTK_WINDOW(window), "Routing");
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 400);

   // Création du layout vertical
   GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_container_add(GTK_CONTAINER (window), vbox);

   // Création de la barre de menu
   GtkWidget *menubar = gtk_menu_bar_new();
   GtkWidget *file_menu = gtk_menu_new();
   GtkWidget *polar_menu = gtk_menu_new();
   GtkWidget *scenario_menu = gtk_menu_new();
   GtkWidget *dump_menu = gtk_menu_new();
   GtkWidget *help_menu = gtk_menu_new();

   GtkWidget *file_menu_item = gtk_menu_item_new_with_mnemonic("_Grib"); // Alt G
   GtkWidget *polar_menu_item = gtk_menu_item_new_with_mnemonic ("_Polar"); //...
   GtkWidget *scenario_menu_item = gtk_menu_item_new_with_mnemonic ("_Scenarios");
   GtkWidget *dump_menu_item = gtk_menu_item_new_with_mnemonic ("_Display");
   GtkWidget *help_menu_item = gtk_menu_item_new_with_mnemonic ("_Help");

   gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(polar_menu_item), polar_menu);
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(scenario_menu_item), scenario_menu);
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(dump_menu_item), dump_menu);
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_menu_item), help_menu);

   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_menu_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), polar_menu_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), scenario_menu_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), dump_menu_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_menu_item);

   // Ajout d'éléments dans le menu "Grib"
   GtkWidget *file_item_open_wind = mySubMenu ("Wind: Open Grib", "folder");
   GtkWidget *file_item_wind_info = mySubMenu ("Wind: Grib Info", "weather-windy-symbolic");
   GtkWidget *file_item_check = mySubMenu ("Wind: Grib Check", "applications-engineering-symbolic");
   GtkWidget *file_item_download = mySubMenu ("Wind: Download", "network-transmit-receive");
   GtkWidget *file_item_open_current = mySubMenu ("Current: Open Grib", "folder");
   GtkWidget *file_item_current_info = mySubMenu ("Current: Grib Info", "weather-windy-symbolic");
   GtkWidget *file_item_check_current = mySubMenu ("Current: Grib Check", "applications-engineering-symbolic");
   GtkWidget *file_item_exit = mySubMenu ("Quit", "application-exit-symbolic");

   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_open_wind);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_wind_info);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_check);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_download);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_open_current);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_current_info);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_check_current);
   gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), file_item_exit);

   // Ajout d'éléments dans le menu "Polar"
   GtkWidget *polar_item_open = mySubMenu ("Polar or Wave Polar open", "folder-symbolic");
   GtkWidget *polar_item_draw = mySubMenu ("Polar Draw", "utilities-system-monitor-symbolic");
   GtkWidget *polar_item_table = mySubMenu ("Polar Table", "x-office-spreadsheet-symbolic");
   GtkWidget *polar_item_wave = mySubMenu ("Wave Dump", "x-office-spreadsheet-symbolic");

   gtk_menu_shell_append(GTK_MENU_SHELL(polar_menu), polar_item_open);
   gtk_menu_shell_append(GTK_MENU_SHELL(polar_menu), polar_item_draw);
   gtk_menu_shell_append(GTK_MENU_SHELL(polar_menu), polar_item_table);
   gtk_menu_shell_append(GTK_MENU_SHELL(polar_menu), polar_item_wave);
   
   // Ajout d'éléments dans le menu "Scenarios"
   GtkWidget *scenario_item_open = mySubMenu ("Open", "folder-symbolic");
   GtkWidget *scenario_item_change = mySubMenu ("Change", "preferences-desktop");
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
   GtkWidget *dump_item_ortho = gtk_menu_item_new_with_label("ortho Route");
   GtkWidget *dump_item_rte = gtk_menu_item_new_with_label("Route");
   GtkWidget *dump_item_report = gtk_menu_item_new_with_label("Report");
   GtkWidget *dump_item_poi = gtk_menu_item_new_with_label("Points Of Interest");
   GtkWidget *dump_item_gps = gtk_menu_item_new_with_label("GPS");

   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_isoc);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_ortho);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_rte);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_report);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_poi);
   gtk_menu_shell_append(GTK_MENU_SHELL(dump_menu), dump_item_gps);

   // Ajout d'éléments dans le menu "Help"
   GtkWidget *help_item_html = mySubMenu ("Help", "help-browser-symbolic");
   GtkWidget *help_item_info = mySubMenu ("About", "help-about-symbolic");
   //GtkWidget *help_item_html = gtk_menu_item_new_with_label("Help");
   //GtkWidget *help_item_info = gtk_menu_item_new_with_label("About");

   gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_item_html);
   gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), help_item_info);

   // fonctions de CallBack
   g_signal_connect(G_OBJECT(file_item_open_wind), "activate", G_CALLBACK(openGrib), &forWind);
   g_signal_connect(G_OBJECT(file_item_wind_info), "activate", G_CALLBACK(gribInfo), &forWind);
   g_signal_connect(G_OBJECT(file_item_check), "activate", G_CALLBACK(runCheckGrib), &forWind);
   g_signal_connect(G_OBJECT(file_item_download), "activate", G_CALLBACK(downloadGrib), NULL);
   g_signal_connect(G_OBJECT(file_item_open_current), "activate", G_CALLBACK(openGrib), &forCurrent);
   g_signal_connect(G_OBJECT(file_item_current_info), "activate", G_CALLBACK(gribInfo), &forCurrent);
   g_signal_connect(G_OBJECT(file_item_check_current), "activate", G_CALLBACK(runCheckGrib), &forCurrent);
   g_signal_connect(G_OBJECT(file_item_exit), "activate", G_CALLBACK(gtk_main_quit), NULL);
   g_signal_connect(G_OBJECT(polar_item_open), "activate", G_CALLBACK(openPolar), NULL);
   g_signal_connect(G_OBJECT(polar_item_draw), "activate", G_CALLBACK(drawPolar), NULL);
   g_signal_connect(G_OBJECT(polar_item_table), "activate", G_CALLBACK(polarDump), NULL);
   g_signal_connect(G_OBJECT(polar_item_wave), "activate", G_CALLBACK(wavePolarDump), NULL);
   g_signal_connect(G_OBJECT(scenario_item_open), "activate", G_CALLBACK(openScenario), NULL);
   g_signal_connect(G_OBJECT(scenario_item_change), "activate", G_CALLBACK(change), NULL);
   g_signal_connect(G_OBJECT(scenario_item_get), "activate", G_CALLBACK(parDump), NULL);
   g_signal_connect(G_OBJECT(scenario_item_save), "activate", G_CALLBACK(saveScenario), NULL);
   g_signal_connect(G_OBJECT(scenario_item_edit), "activate", G_CALLBACK(editScenario), NULL);
   g_signal_connect(G_OBJECT(dump_item_isoc), "activate", G_CALLBACK(isocDump), NULL);
   g_signal_connect(G_OBJECT(dump_item_ortho), "activate", G_CALLBACK(orthoDump), NULL);
   g_signal_connect(G_OBJECT(dump_item_rte), "activate", G_CALLBACK(rteDump), NULL);
   g_signal_connect(G_OBJECT(dump_item_report), "activate", G_CALLBACK(rteReport), NULL);
   g_signal_connect(G_OBJECT(dump_item_poi), "activate", G_CALLBACK(poiDump), NULL);
   g_signal_connect(G_OBJECT(dump_item_gps), "activate", G_CALLBACK(gpsDump), NULL);
   g_signal_connect(G_OBJECT(help_item_html), "activate", G_CALLBACK(help), NULL);
   g_signal_connect(G_OBJECT(help_item_info), "activate", G_CALLBACK(helpInfo), NULL);

   // Ajout de la barre d'outil
   GtkWidget *toolbar = gtk_toolbar_new();
   gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);

   // Ajouter les événements de clavier à la barre d'outils
   gtk_widget_add_events(toolbar, GDK_KEY_PRESS_MASK);

   // Connecter la fonction on_toolbar_key_press à l'événement key-press-event de la barre d'outils
   g_signal_connect(toolbar, "key-press-event", G_CALLBACK(on_toolbar_key_press), NULL);

   GtkToolItem *run_button = gtk_tool_button_new (NULL, NULL);
   GtkWidget *icon = gtk_image_new_from_icon_name("system-run", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(run_button), icon);
   
   GtkToolItem *change_button = gtk_tool_button_new(NULL, "Change");
   icon = gtk_image_new_from_icon_name("preferences-desktop", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(change_button), icon);
   
   GtkToolItem *polar_button = gtk_tool_button_new(NULL, "Polar");
   icon = gtk_image_new_from_icon_name("utilities-system-monitor-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(polar_button), icon);
   
   GtkToolItem *stop_button = gtk_tool_button_new(NULL, "Stop");
   icon = gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(stop_button), icon);
   
   GtkToolItem *play_button = gtk_tool_button_new(NULL, "Play");
   icon = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(play_button), icon);
   
   GtkToolItem *to_start_button = gtk_tool_button_new(NULL, "<<");
   icon = gtk_image_new_from_icon_name("media-skip-backward", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_start_button), icon);
   
   GtkToolItem *reward_button = gtk_tool_button_new(NULL, "<");
   icon = gtk_image_new_from_icon_name("media-seek-backward", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(reward_button), icon);
   
   GtkToolItem *forward_button = gtk_tool_button_new(NULL, ">");
   icon = gtk_image_new_from_icon_name("media-seek-forward", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(forward_button), icon);
   
   GtkToolItem *to_end_button = gtk_tool_button_new(NULL, ">>");
   icon = gtk_image_new_from_icon_name("media-skip-forward", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_end_button), icon);
   
   GtkToolItem *to_zoom_in_button = gtk_tool_button_new(NULL, "zoomIn");
   icon = gtk_image_new_from_icon_name("zoom-in-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_zoom_in_button), icon);
   
   GtkToolItem *to_zoom_out_button = gtk_tool_button_new(NULL, "zoomOut");
   icon = gtk_image_new_from_icon_name("zoom-out-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_zoom_out_button), icon);
   
   GtkToolItem *to_zoom_original_button = gtk_tool_button_new(NULL, "zoomOriginal");
   icon = gtk_image_new_from_icon_name("zoom-original", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_zoom_original_button), icon);
   
   GtkToolItem *to_left_button = gtk_tool_button_new(NULL, "left");
   icon = gtk_image_new_from_icon_name("pan-start-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_left_button), icon);
   
   GtkToolItem *to_up_button = gtk_tool_button_new(NULL, "up");
   icon = gtk_image_new_from_icon_name("pan-up-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_up_button), icon);
   
   GtkToolItem *to_down_button = gtk_tool_button_new(NULL, "down");
   icon = gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_down_button), icon);
   
   GtkToolItem *to_right_button = gtk_tool_button_new(NULL, "right");
   icon = gtk_image_new_from_icon_name("pan-end-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_right_button), icon);
   
   GtkToolItem *to_gps_pos_button = gtk_tool_button_new(NULL, "gpsPos");
   icon = gtk_image_new_from_icon_name("find-location-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
   gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(to_gps_pos_button), icon);
  
   g_signal_connect(run_button, "clicked", G_CALLBACK (on_run_button_clicked), NULL);
   g_signal_connect(change_button, "clicked", G_CALLBACK (change), NULL);
   g_signal_connect(polar_button, "clicked", G_CALLBACK (drawPolar), NULL);
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

