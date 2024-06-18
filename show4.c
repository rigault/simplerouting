/*! \mainpage Routing for sail software
 * \brief small routing software written in c language
 * \author Rene Rigault
 * \version 0.1
 * \date 2024 
 * \file
 * \section Compilation
 * gcc -c show4.c `pkg-config --cflags gtk4` -Wdeprecated-declarations
 * see "ccall4 file
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

#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/time.h>
#include <time.h>
#include <gtk/gtk.h>
#include <eccodes.h>
#include <curl/curl.h>
//#include <webkit2/webkit2.h>
#include "shapefil.h"
#include "rtypes.h"
#include "rutil.h"
#include "inline.h"
#include "engine.h"
#include "option.h"
#include "gpsutil.h"

#define MAX_N_SURFACE         (24*MAX_N_DAYS_WEATHER)            // 16 days with timeStep 1 hour
#define MAIN_WINDOW_DEFAULT_WIDTH  1200
#define MAIN_WINDOW_DEFAULT_HEIGHT 600
#define APPLICATION_ID        "com.routing"  
#define MIN_ZOOM_POI_VISIBLE  30
#define BOAT_UNICODE          "⛵"
#define DESTINATION_UNICODE   "🏁"
#define CAT_UNICODE           "\U0001F431"
#define ORTHO_ROUTE_PARAM     20             // for drawing orthodromic route
#define MAX_TEXT_LENGTH       5              // in polar
#define POLAR_WIDTH           800
#define POLAR_HEIGHT          500
#define DISP_NB_LAT_STEP      10
#define DISP_NB_LON_STEP      10
#define QUIT_TIME_OUT         1000
#define ROUTING_TIME_OUT      1000
#define GRIB_TIME_OUT         2000
#define READ_GRIB_TIME_OUT    200
#define MIN_MOVE_FOR_SELECT   50             // minimum move to launch smtp grib request after selection
#define MIN_POINT_FOR_BEZIER  10             // minimum number of point to select bezier representation
#define K_LON_LAT            (0.71)          // lon deformation
#define LON_LAT_RATIO         2.8            // not that useful
#define GPS_TIME_INTERVAL     2              // seconds
#define MAX_N_ANIMATION       6              // number of values of speed display => animation.tempo
#define DASHBOARD_MIN_SPEED 1
#define DASHBOARD_MAX_SPEED 20
#define DASHBOARD_RADIUS 100

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

#define CAIRO_SET_SOURCE_RGBA_SHP_MAP(cr)          cairo_set_source_rgba (cr,157.0/255.0,162.0/255.0,12.0/255.0,0.5) // Polygons in yellow
#define CAIRO_SET_SOURCE_RGBA_FORBID_AREA(cr)      cairo_set_source_rgba (cr,0.5,0.5,0.5,0.5)                        // forbid area in gray
#define CAIRO_SET_SOURCE_RGBA_SELECTION(cr)        cairo_set_source_rgba (cr,1.0,0.0,0.0,0.5)                        // selection in semi-transparent red
#define CAIRO_SET_SOURCE_RGBA_POLAR_TWS(cr)        cairo_set_source_rgba (cr,1.0,0.0,0.0,1.0)                        // specific tws polar red

/*! Structure storing data required to update NOAA URL */
typedef struct {
   GtkSpinButton *bottomLatSpin;
   GtkSpinButton *leftLonSpin;
   GtkSpinButton *topLatSpin;
   GtkSpinButton *rightLonSpin;
   GtkSpinButton *timeStepSpin;
   GtkSpinButton *timeMaxSpin;
   GtkTextBuffer *urlBuffer;
   int topLat;
   int bottomLat;
   int leftLon;
   int rightLon;
   int timeStep;  // in hours
   int timeMax;   // in days
   char url [MAX_SIZE_URL];
} DialogData;

cairo_surface_t *surface [MAX_N_SURFACE];
cairo_t *surface_cr [MAX_N_SURFACE];
char existSurface [MAX_N_SURFACE] = {false};

double vOffsetLocalUTC; 

GdkRGBA colors [] = {{1.0,0,0,1}, {0,1.0,0,1},  {0,0,1.0,1},{0.5,0.5,0,1},{0,0.5,0.5,1},{0.5,0,0.5,1},{0.2,0.2,0.2,1},{0.4,0.4,0.4,1},{0.8,0,0.2,1},{0.2,0,0.8,1}}; // Colors for Polar curve
const int nColors = 10;

#define N_WIND_COLORS 6
guint8 colorPalette [N_WIND_COLORS][3] = {{0,0,255}, {0, 255, 0}, {255, 255, 0}, {255, 153, 0}, {255, 0, 0}, {139, 0, 0}};
// blue, green, yellow, orange, red, blacked red
guint8 bwPalette [N_WIND_COLORS][3] = {{250,250,250}, {200, 200, 200}, {170, 170, 170}, {130, 130, 130}, {70, 70, 70}, {10, 10, 10}};
const double tTws [] = {0.0, 15.0, 20.0, 25.0, 30.0, 40.0};

char   parameterFileName [MAX_SIZE_FILE_NAME];
guint  context_id;                        // for statusBar
int    selectedPol = 0;                   // select polar to draw. 0 = all
double selectedTws = 0;                   // selected TWS for polar draw
guint  gribMailTimeout;
guint  routingTimeout;
guint  gribReadTimeout;
guint  currentGribReadTimeout;
bool   destPressed = true;
bool   polygonStarted = false;
bool   gribRequestRunning = false;
bool   selecting = FALSE;
bool   updatedColors = false; 
int    provider = SAILDOCS_GFS;
struct tm startInfo;
long   theTime = 0;
int    polarType = POLAR;
int    segmentOrBezier = SEGMENT;
int    selectedPointInLastIsochrone = 0;
int    iFlow = WIND;
int    typeFlow = WIND;                   // for openGrib.. WIND or CURRENT.
bool   simulationReportExist = false;
bool   gpsTrace = false;                  // used to trace GPS position only one time per session
char   traceName [MAX_SIZE_FILE_NAME];    // global var for newTrace ()

/* struct for animation */
struct {
   int    tempo [MAX_N_ANIMATION];
   guint  timer;
   int    active;
} animation = {{1000, 500, 200, 100, 50, 20}, 0, NO_ANIMATION};

/*! struct for url Request */
struct {
   char outputFileName [MAX_SIZE_LINE];
   char url [MAX_SIZE_BUFFER];
   int urlType;
} urlRequest; 

/*! struct for dashboard */
struct {
   GtkWidget *hourPosZone;
   GtkWidget *speedometer;
   GtkWidget *compass;
   GtkWidget *textZone;
   int timeoutId;
} widgetDashboard = {NULL, NULL, NULL, NULL, 0};

char poiName [MAX_SIZE_POI_NAME];    // for poiName Change

GtkApplication *app;                 // NEW WITH GTK4

GtkWidget *statusbar = NULL;         // the status bar
GtkWidget *window = NULL;            // main Window 
GtkWidget *polar_drawing_area = NULL;// polar
GtkWidget *scaleLabel = NULL;        // for polar scale label
GtkWidget *spinner_window = NULL; 
GtkWidget *drawing_area = NULL;      // main window
GtkWidget *spin_button_time_max = NULL;
GtkWidget *val_size_eval = NULL; 
GtkWidget *menuWindow = NULL;        // Pop up menu Right Click
GtkWidget *menuGrib = NULL;          // Pop up menu Meteoconsult
GtkWidget *menuHist = NULL;          // Pop up menu Hustory Routes
GtkWidget *labelInfoRoute;           // label line for route info

Coordinates whereWasMouse, whereIsMouse, whereIsPopup;

/*! displayed zone */
DispZone dispZone;

/*! struct to memorize zone for mail request */
struct {
   double lat1;
   double lon1;
   double lat2;
   double lon2;
} mailRequestPar;

static void meteogram ();
static void on_run_button_clicked ();
static gboolean routingCheck (gpointer data);
static void destroySurface ();

/*! destroy the child window before exiting application */
static void on_parent_destroy (GtkWidget *widget, gpointer child_window) {
   if (GTK_IS_WIDGET(child_window)) {
      gtk_window_destroy(GTK_WINDOW(child_window));
   }
}

/*! destroy the popWindow window before exiting application */
static void on_parent_window_destroy(GtkWidget *widget, gpointer popWindow) {
   if (GTK_IS_WIDGET(popWindow)) {
      gtk_widget_unparent (popWindow);
   }
}

/*! destroy spinner window */
static void spinnerWindowDestroy () {
   if (spinner_window != NULL) {
      gtk_window_destroy (GTK_WINDOW (spinner_window));
      spinner_window = NULL;
   }
}

/*! hide and terminate popover */
static void popoverFinish (GtkWidget *pop) {
   if (pop != NULL) {
     // gtk_widget_hide (pop);
     gtk_widget_set_visible (pop, false);
     gtk_widget_unparent (pop);
   }
}

/*! dstroy spînner window */
static void stopSpinner () {
   chooseDeparture.ret = STOPPED;
}

/*! intSpin for change */
static void intSpinUpdate (GtkSpinButton *spin_button, gpointer user_data) {
   int *value = (int *) user_data;
   *value = gtk_spin_button_get_value_as_int (spin_button);
}

/*! udpate title of window with right grib file name */
static void titleUpdate () {
   char str [MAX_SIZE_LINE];
   snprintf (str, sizeof (str), "%s %s %s", PROG_NAME, PROG_VERSION, par.gribFileName);
   gtk_window_set_title(GTK_WINDOW(window), str);
}

/*! draw spinner when waiting for something */
static void spinner (const char *title, const char* str) {
   spinner_window = gtk_application_window_new (app);
   gtk_window_set_title(GTK_WINDOW(spinner_window), title);
   gtk_window_set_default_size(GTK_WINDOW (spinner_window), 300, 100);
   g_signal_connect (spinner_window, "destroy", G_CALLBACK (stopSpinner), NULL);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), spinner_window);
   
   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (spinner_window), vBox);

   GtkWidget *label = gtk_label_new (str);
   GtkWidget *spinner = gtk_spinner_new ();
   gtk_box_append (GTK_BOX (vBox), label);
   gtk_box_append (GTK_BOX (vBox), spinner);

   gtk_spinner_start (GTK_SPINNER(spinner));
   gtk_window_present (GTK_WINDOW (spinner_window));
}

/*! draw info message GTK_MESSAGE_INFO, GTK_MESSAGE_WARNING ou GTK_MESSAGE_ERROR
static void OinfoMessage (char *message, GtkMessageType typeMessage) {
   GtkWidget* dialogBox = gtk_dialog_new_with_buttons (message, GTK_WINDOW (window), GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_ACCEPT, NULL);
   g_signal_connect_swapped (dialogBox, "response", G_CALLBACK (gtk_window_destroy), dialogBox);
   gtk_window_present (GTK_WINDOW (dialogBox));
}
*/

/* destroy window when OK is clicked */
static void on_ok_button_clicked(GtkWidget *widget, gpointer window) {
    gtk_window_destroy(GTK_WINDOW(window));
}

/*! draw info message GTK_MESSAGE_INFO, GTK_MESSAGE_WARNING ou GTK_MESSAGE_ERROR */
static void infoMessage (char *message, GtkMessageType typeMessage) {
   GtkWidget *infoWindow = gtk_application_window_new (app);
   gtk_widget_set_size_request(infoWindow, 100, -1);

   switch (typeMessage) {
   case GTK_MESSAGE_INFO:
      gtk_window_set_title (GTK_WINDOW(infoWindow), "Info");
      break;
   case GTK_MESSAGE_WARNING:
      gtk_window_set_title (GTK_WINDOW(infoWindow), "Warning");
      break;
   case GTK_MESSAGE_ERROR:
      gtk_window_set_title (GTK_WINDOW(infoWindow), "Error");
      break;
   default:;
   }

   GtkWidget *vBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (infoWindow), vBox);
   
   GtkWidget *label = gtk_label_new (message);

   GtkWidget *button = gtk_button_new_from_icon_name ("emblem-default");
   g_signal_connect (button, "clicked", G_CALLBACK (on_ok_button_clicked), infoWindow);

   gtk_box_append (GTK_BOX(vBox), label);
   gtk_box_append (GTK_BOX(vBox), button);

   gtk_window_present (GTK_WINDOW (infoWindow));
}


/*! for remote files, no buttons 
static void viewer (const char *url) {
   const int WEB_WIDTH = 800;
   const int WEB_HEIGHT = 450;
   char title [MAX_SIZE_LINE];
   snprintf  (title, sizeof (title), "View: %s", url);
   GtkWidget *viewerWindow = gtk_application_window_new (app);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), viewerWindow);
   gtk_window_set_default_size (GTK_WINDOW(viewerWindow), WEB_WIDTH, WEB_HEIGHT);
   gtk_window_set_title(GTK_WINDOW(viewerWindow), title);
   g_signal_connect(G_OBJECT(viewerWindow), "destroy", G_CALLBACK(gtk_window_destroy), NULL);
   
   GtkWidget *webView = webkit_web_view_new();
   webkit_web_view_load_uri (WEBKIT_WEB_VIEW(webView), url);
   gtk_window_set_child (GTK_WINDOW (viewerWindow), (GtkWidget *) webView);
   gtk_window_present (GTK_WINDOW (viewerWindow));
} */


/*! open windy
   Example: https://www.windytv.com/?36.908,-93.049,8,d:picker */
static void windy (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   FILE *fs;
   const char *URL = "https://www.windy.com";
   const int zoom = 8; // CLAMP (9 - log2 (diffLat), 2, 19);

   char command [MAX_SIZE_LINE * 2];
   char mapUrl [MAX_SIZE_LINE];
   
   snprintf (mapUrl, sizeof (mapUrl), "%s?%.2lf%%2C%.2lf%%2C%d%%2Cd:picker", URL, par.pOr.lat, par.pOr.lon, zoom);
   snprintf (command, MAX_SIZE_LINE * 2, "%s %s", par.webkit, mapUrl);
   printf ("popen %s\n", command);
   if ((fs = popen (command, "r")) == NULL)
      fprintf (stderr, "Error in windy: popen call: %s\n", command);
   pclose (fs);
}

/*! open map using open street map or open sea map */
static void openMap (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   FILE *fs;
   //int *comportement = (int *) data;
   int comportement = GPOINTER_TO_INT (data);
   const char* OSM_URL [] = {"https://www.openstreetmap.org/export/", "https://map.openseamap.org/"};
   const double lat = (dispZone.latMin + dispZone.latMax) / 2.0;
   const double lon = (dispZone.lonLeft + dispZone.lonRight) / 2.0;
   const double diffLat = fabs (dispZone.latMax - dispZone.latMin);
   const double zoom = CLAMP (9 - log2 (diffLat), 2.0, 19.0);
   //printf ("diffLat : %.2lf zoom:%.2lf\n", diffLat, zoom);

   char command [MAX_SIZE_LINE * 2];
   char mapUrl [MAX_SIZE_LINE];
   
   if (comportement == 0)
      snprintf (mapUrl, sizeof (mapUrl), "%sembed.html?bbox=%.4lf%%2C%.4lf%%2C%.4lf%%2C%.4lf\\&layer=mapnik\\&marker=%.4lf%%2C%.4lf", 
         OSM_URL[comportement], dispZone.lonLeft, dispZone.latMin, dispZone.lonRight, dispZone.latMax,
         par.pOr.lat, par.pOr.lon);
   else
      snprintf (mapUrl, sizeof (mapUrl), "%s?lat=%.4lf\\&lon=%.4lf\\&zoom=%.2lf", OSM_URL[comportement], lat, lon, zoom);
   snprintf (command, MAX_SIZE_LINE * 2, "%s %s", par.webkit, mapUrl);
   printf ("popen %s\n", command);
   if ((fs = popen (command, "r")) == NULL)
      fprintf (stderr, "Error in openMap: popen call: %s\n", command);
   pclose (fs);
}

/*! Fonction that applies bold style  */
static void apply_bold_style(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end) {
    GtkTextTag *tag = gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_apply_tag(buffer, tag, start, end);
}

/*! display text with monochrome police */
static void displayText (guint width, guint height, const char *text, const char *title) {
   GtkWidget *textWindow = gtk_application_window_new (app);
   gtk_window_set_title(GTK_WINDOW (textWindow), title);
   gtk_window_set_default_size (GTK_WINDOW(textWindow), width, height);
   g_signal_connect (G_OBJECT(textWindow), "destroy", G_CALLBACK (gtk_window_destroy), NULL);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), textWindow);
   
   GtkWidget *scrolled_window = gtk_scrolled_window_new ();
   gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_window_set_child (GTK_WINDOW (textWindow), (GtkWidget *) scrolled_window);
   
   GtkWidget *text_view = gtk_text_view_new();
   gtk_text_view_set_monospace (GTK_TEXT_VIEW(text_view), TRUE);
   
   // read only text
   gtk_text_view_set_editable (GTK_TEXT_VIEW(text_view), FALSE);
   gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (text_view), FALSE);
   gtk_text_view_set_wrap_mode( GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD_CHAR);
   gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), text_view);

   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

   gtk_text_buffer_insert_at_cursor(buffer, text, -1);
   
   GtkTextIter start_iter, end_iter;
   gtk_text_buffer_get_start_iter (buffer, &start_iter);
   gtk_text_buffer_get_iter_at_line (buffer, &end_iter, 1); // Ligne suivante après la première

   apply_bold_style(buffer, &start_iter, &end_iter);

   gtk_window_present (GTK_WINDOW (textWindow));
}

/*! display file with monochrome police 
   first line is bold according to firstBold parameter */
static void displayFile (char* fileName, char *title, bool firstBold) {
   FILE *f;
   char line [MAX_SIZE_LINE];
   if ((f = fopen (fileName, "r")) == NULL) {
      infoMessage ("Impossible to open file", GTK_MESSAGE_ERROR);
      return;
   }
   GtkWidget *fileWindow = gtk_application_window_new (app);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), fileWindow);
   gtk_window_set_title(GTK_WINDOW(fileWindow), title);
   gtk_window_set_default_size(GTK_WINDOW(fileWindow), 750, 450);
   g_signal_connect(G_OBJECT(fileWindow), "destroy", G_CALLBACK(gtk_window_destroy), NULL);

   GtkWidget *scrolled_window = gtk_scrolled_window_new ();
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_window_set_child (GTK_WINDOW (fileWindow), (GtkWidget *) scrolled_window);

   GtkWidget *text_view = gtk_text_view_new();
   gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
   gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), text_view);

   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
   while (fgets (line, sizeof(line), f) != NULL) {
      gtk_text_buffer_insert_at_cursor(buffer, line, -1);
   }
   fclose (f);
   
   if (firstBold) {
      GtkTextIter start_iter, end_iter;
      gtk_text_buffer_get_start_iter(buffer, &start_iter);
      gtk_text_buffer_get_iter_at_line(buffer, &end_iter, 1);

      // apply bold linr to first linr
      apply_bold_style(buffer, &start_iter, &end_iter);
   }

   gtk_window_present (GTK_WINDOW (fileWindow));
}

/*! init of display zone */
static void initDispZone () {
   double latCenter = (zone.latMin + zone.latMax) / 2;
   double lonCenter = (zone.lonRight + zone.lonLeft) / 2;
   double deltaLat = MAX (0.1, zone.latMax - latCenter);
   double deltaLon = deltaLat * LON_LAT_RATIO;
   dispZone.zoom = 180 / deltaLat;
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
   double deltaLon = deltaLat * LON_LAT_RATIO;
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
   double deltaLon = deltaLat * LON_LAT_RATIO;
   dispZone.zoom = 180 / deltaLat;
   if ((z < 1.0) && (deltaLat < 0.1)) return; // stop zoom
   if ((z > 1.0) && (deltaLat > 60)) return;  // stop zoom

   dispZone.latMin = latCenter - deltaLat * z;
   dispZone.latMax = latCenter + deltaLat * z;
   dispZone.lonLeft = lonCenter - deltaLon * z;
   dispZone.lonRight = lonCenter + deltaLon * z;
   dispZone.latStep = fabs ((dispZone.latMax - dispZone.latMin)) / DISP_NB_LAT_STEP;
   dispZone.lonStep = fabs ((dispZone.lonLeft - dispZone.lonRight)) / DISP_NB_LON_STEP;
   destroySurface ();
}

/*! horizontal and vertical translation of main window */
static void dispTranslate (double h, double v) {
   double saveLatMin = dispZone.latMin, saveLatMax = dispZone.latMax;
   double saveLonLeft = dispZone.lonLeft, saveLonRight = dispZone.lonRight;
   const double K_TRANSLATE = (dispZone.latMax - dispZone.latMin) / 2.0;

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
   destroySurface ();
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
static inline double xToLon (double x) {
   //double kLon = (dispZone.xR - dispZone.xL) / (dispZone.lonRight - dispZone.lonLeft);
   double kLat = (dispZone.yB - dispZone.yT) / (dispZone.latMax - dispZone.latMin);
   double kLon = kLat * K_LON_LAT; // cos (DEG_TO_RAD * (dispZone.latMax + dispZone.latMin)/2.0);
   double lon = dispZone.lonLeft + ((x - dispZone.xL -1) / kLon);
   return lonCanonize (lon);
}

/*! return latitude as a function of y (inverse of getY) */
static inline double yToLat (double y) {
   double kLat = (dispZone.yB - dispZone.yT) / (dispZone.latMax - dispZone.latMin);
   return dispZone.latMax - ((y - dispZone.yT - 1) / kLat); 
}

/*! draw a polygon */
static void drawPolygon (cairo_t *cr, MyPolygon *po) {
   double x = getX (po->points[0].lon);
   double y = getY (po->points[0].lat);
   cairo_move_to (cr, x, y);
   for (int k = 1; k < po->n; k++) {
      x = getX (po->points [k].lon);
      y = getY (po->points [k].lat);
      cairo_line_to (cr, x, y);
   }
   cairo_close_path(cr);
   cairo_fill(cr);
}

/*! exclusion zone is an array of polygons */
static void drawForbidArea (cairo_t *cr) {
   CAIRO_SET_SOURCE_RGBA_FORBID_AREA(cr);
   for (int i = 0; i < par.nForbidZone; i++)
      drawPolygon (cr, &forbidZones [i]);
}

/*! draw trace  */
static void drawTrace (cairo_t *cr) {
   FILE *f;
   char line [MAX_SIZE_LINE];
   double lat, lon, time, x, y;
   bool first = true;
   cairo_set_line_width (cr, 0.5);
   //cairo_set_line_width (cr, 4);
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   if ((f = fopen (par.traceFileName, "r")) == NULL) {
      fprintf (stderr, "In drawTrace: Error Impossible to open: %s\n", par.traceFileName);
      return;
   }
   while (fgets (line, MAX_SIZE_LINE, f) != NULL ) {
      if (sscanf (line, "%lf;%lf;%lf", &lat, &lon, &time) >= 3) {
         x = getX (lon);
         y = getY (lat);
         if (first) {
            cairo_move_to (cr, x, y);
            first = false;
         }
         else
            cairo_line_to (cr, x, y);
      }
   }
   cairo_stroke (cr);
   fclose (f);
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
}

/*! gray if night 
static void paintDayNight (cairo_t *cr, int width, int height) {
   double lat, lon;
   for (int x = 0; x < width; x++) {
      for (int y = 0; y < height; y++) {
         lat = yToLat (y);
         lon = xToLon (x);
         if (! isDayLight (theTime, lat, lon)) {
            cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.5);
            cairo_rectangle (cr, x, y, 1, 1);
            cairo_fill (cr);
         }
      }
   }
}*/
 
/*! update colors for wind */
static void createWindSurface (cairo_t *cr, int index, int width, int height) {
   double u, v, gust, w, twd, tws, lat, lon;
   double val = 0.0;
   guint8 red, green, blue;
   int nCall = 0;
   surface [index] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
   surface_cr [index] = cairo_create(surface [index]);
   existSurface [index] = true;

   for (int x = 0; x < width; x++) {
      for (int y = 0; y < height; y++) {
         lat = yToLat (y);
         lon = xToLon (x);
         if (isInZone (lat, lon, &zone)) {
            nCall += 1;
            switch (par.averageOrGustDisp) {
            case WIND_DISP:
	            findWindGrib (lat, lon, theTime, &u, &v, &gust, &w, &twd, &tws);
               val = tws;
               break;
            case GUST_DISP:
	            findWindGrib (lat, lon, theTime, &u, &v, &gust, &w, &twd, &tws);
               val = MAX (tws, MS_TO_KN * gust);
               break;
            case WAVE_DISP:
	            findWindGrib (lat, lon, theTime, &u, &v, &gust, &w, &twd, &tws);
               val = w;
               break;
            case RAIN_DISP:
	            val = findRainGrib (lat, lon, theTime) * 250000;
               break;
            case PRESSURE_DISP:
               // normalize a value between 0 to 50 associated to 950 to 1050 hP
	            val = findPressureGrib (lat, lon, theTime)/ 100; // hPa
               val -= 990;
               val *= 1;
               if (val > 50) val = 50;
               if (val < 0) val = 0;
               // printf ("val: %.2lf\n", val);
               break;
            default:;
            }
            mapColors (val, &red, &green, &blue);
            cairo_set_source_rgba (surface_cr [index], red/255.0, green/255.0, blue/255.0, 0.5);
            cairo_rectangle (surface_cr [index], x, y, 1, 1);
            cairo_fill (surface_cr [index]);
         }
      }
   }
   // printf ("nCall in PaintWind to findWindGrib: %d\n", nCall);
}

/*! clean surface */
static void destroySurface () {
   for (int i = 0; i < MAX_N_SURFACE; i++) {
      if (existSurface[i]) {
         cairo_destroy(surface_cr [i]);
         cairo_surface_destroy(surface [i]);
      }
   }
   memset (existSurface, false, MAX_N_SURFACE);
}

/*! update colors for wind 
static void OpaintWind (cairo_t *cr, int width, int height) {
   double u, v, gust, w, twd, tws, lat, lon;
   guint8 red, green, blue;
   int nCall = 0;
   for (int x = 0; x < width; x++) {
      for (int y = 0; y < height; y++) {
         lat = yToLat (y);
         lon = xToLon (x);
         if (isInZone (lat, lon, &zone)) {
	         findWindGrib (lat, lon, theTime, &u, &v, &gust, &w, &twd, &tws);
            nCall += 1;
            if (par.averageOrGustDisp != 0)
               tws = MAX (tws, MS_TO_KN * gust);
            mapColors (tws, &red, &green, &blue);
            cairo_set_source_rgba (cr, red/255.0, green/255.0, blue/255.0, 0.5);
            cairo_rectangle (cr, x, y, 1, 1);
            cairo_fill (cr);
         }
      }
   }
   // printf ("nCall in PaintWind to findWindGrib: %d\n", nCall);
}*/

/*! callback for palette */
static void cb_draw_palette (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
   double tws;
   guint8 r, g, b;
   char label [9];

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
}

/*! Palette */
static void paletteDraw () {
   GtkWidget *paletteWindow = gtk_application_window_new (app);
   gtk_window_set_title(GTK_WINDOW (paletteWindow), "TWS (knots)");
   gtk_window_set_default_size(GTK_WINDOW (paletteWindow), 800, 100);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), paletteWindow);
   g_signal_connect (paletteWindow, "destroy", G_CALLBACK(gtk_window_destroy), NULL);

   GtkWidget *drawing_area = gtk_drawing_area_new();
   gtk_widget_set_size_request(drawing_area, 800, 100);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (drawing_area), cb_draw_palette, NULL, NULL);
   gtk_window_set_child (GTK_WINDOW (paletteWindow), (GtkWidget *) drawing_area);
   gtk_window_present (GTK_WINDOW (paletteWindow));
}

/*! make calculation over Ortho route */
static void calculateOrthoRoute () {
   wayPoints.t [0].lCap =  directCap (par.pOr.lat, par.pOr.lon,  wayPoints.t [0].lat,  wayPoints.t [0].lon);
   wayPoints.t [0].oCap = wayPoints.t [0].lCap + givry (par.pOr.lat, par.pOr.lon, wayPoints.t [0].lat, wayPoints.t [0].lon);
   wayPoints.t [0].ld = loxoDist (par.pOr.lat, par.pOr.lon,  wayPoints.t [0].lat,  wayPoints.t [0].lon);
   wayPoints.t [0].od = orthoDist (par.pOr.lat, par.pOr.lon,  wayPoints.t [0].lat,  wayPoints.t [0].lon);
   wayPoints.totLoxoDist = wayPoints.t [0].ld; 
   wayPoints.totOrthoDist = wayPoints.t [0].od; 
   
   for (int i = 0; i <  wayPoints.n; i++) {
      wayPoints.t [i+1].lCap = directCap (wayPoints.t [i].lat, wayPoints.t [i].lon, wayPoints.t [i+1].lat, wayPoints.t [i+1].lon); 
      wayPoints.t [i+1].oCap = wayPoints.t [i+1].lCap + givry (wayPoints.t [i].lat,  wayPoints.t [i].lon, wayPoints.t [i+1].lat, wayPoints.t [i+1].lon);
      wayPoints.t [i+1].ld = loxoDist (wayPoints.t [i].lat, wayPoints.t [i].lon, wayPoints.t [i+1].lat, wayPoints.t [i+1].lon);
      wayPoints.t [i+1].od = orthoDist (wayPoints.t [i].lat, wayPoints.t [i].lon, wayPoints.t [i+1].lat, wayPoints.t [i+1].lon);
      wayPoints.totLoxoDist += wayPoints.t [i+1].ld; 
      wayPoints.totOrthoDist += wayPoints.t [i+1].od; 
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
      angle = orthoCap (lat, lon, lat2, lon2);
      lSeg = orthoDist (lat, lon, lat2, lon2) / (n - i);
      lat += lSeg * cos (angle * DEG_TO_RAD) / 60;
      lon += (lSeg * sin (angle * DEG_TO_RAD) / cos (DEG_TO_RAD * lat)) / 60;
      x = getX (lon);
      y = getY (lat);
      
      angle = orthoCap (lat, lon, lat2, lon2);
      lSeg = orthoDist (lat, lon, lat2, lon2) / (n - i - 1);
      lat += lSeg * cos (angle * DEG_TO_RAD) / 60;
      lon += (lSeg * sin (angle * DEG_TO_RAD) / cos (DEG_TO_RAD * lat)) / 60;
      x1 = getX (lon);
      y1 = getY (lat);

      angle = orthoCap (lat, lon, lat2, lon2);
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
}

/*! return label associated to x */
static GtkWidget *doubleToLabel (double x) {
   char str [MAX_SIZE_LINE];
   snprintf (str, sizeof (str), "%.2lf", x);
   GtkWidget *label = gtk_label_new (str);
   gtk_label_set_yalign (GTK_LABEL(label), 0);
   gtk_label_set_xalign (GTK_LABEL(label), 0);   
   return label;
}

/*! return label associated to str with optionnally i*/
static GtkWidget *strToLabel (char *str0, int i) {
   char str [MAX_SIZE_LINE];
   if (i >= 0) snprintf (str, sizeof (str), "%s %d", str0, i);
   else strncpy (str, str0, sizeof (str) - 1);
   GtkWidget *label = gtk_label_new (str);
   gtk_label_set_yalign(GTK_LABEL(label), 0);
   gtk_label_set_xalign(GTK_LABEL(label), 0);   
   return label;
}

/*! return label in bold associated to str */
static GtkWidget *strToLabelBold (char *str) {
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
   calculateOrthoRoute ();
   GtkWidget *wpWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW(wpWindow), "Orthodomic and Loxdromic Waypoint routes");

   gtk_widget_set_size_request(wpWindow, 400, -1);

   GtkWidget *grid = gtk_grid_new();   
   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

   gtk_window_set_child (GTK_WINDOW (wpWindow), (GtkWidget *) grid);

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
   gtk_grid_attach(GTK_GRID(grid), separator,                      0, 1, 7, 1);
  
   int i;
   for (i = -1; i <  wayPoints.n; i++) {
      if (i == -1) {
         gtk_grid_attach (GTK_GRID (grid), strToLabel("Origin", -1),                                    0, 2, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel(latToStr (par.pOr.lat, par.dispDms, strLat), -1), 1, 2, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel(lonToStr (par.pOr.lon, par.dispDms, strLon), -1), 2, 2, 1, 1);
      }
      else {
         gtk_grid_attach (GTK_GRID (grid), strToLabel("Waypoint", i),                                         0, i+3, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel(latToStr (wayPoints.t[i].lat, par.dispDms, strLat), -1), 1, i+3, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel(lonToStr (wayPoints.t[i].lon, par.dispDms, strLon), -1), 2, i+3, 1, 1);
      }
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayPoints.t [i+1].oCap),                                3, i+3, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayPoints.t [i+1].od),                                  4, i+3, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayPoints.t [i+1].lCap),                                5, i+3, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayPoints.t [i+1].ld),                                  6, i+3, 1, 1);
   }
   gtk_grid_attach (GTK_GRID (grid), strToLabel("Destination", -1),                                 0, i+3, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabel(latToStr (par.pDest.lat, par.dispDms, strLat), -1), 1, i+3, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabel(lonToStr (par.pDest.lon, par.dispDms, strLon), -1), 2, i+3, 1, 1);
   
   i += 1;
   separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID(grid), separator,                                                     0, i+3, 7, 1);
   i += 1;
   gtk_grid_attach (GTK_GRID (grid), strToLabel("Total Orthodomic Distance", -1),                 0, i+3, 3, 1);
   gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayPoints.totOrthoDist),                      3, i+3, 1, 1);
   i += 1;
   separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID(grid), separator,                                                     0, i+3, 7, 1);
   i += 1;
   gtk_grid_attach (GTK_GRID (grid), strToLabel("Total Loxodromic Distance", -1),                 0, i+3, 3, 1);
   gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayPoints.totLoxoDist),                       3, i+3, 1, 1);
   gtk_window_present (GTK_WINDOW (wpWindow));
}

/*! draw loxodromie */
static void drawLoxoRoute (cairo_t *cr) {
   double x = getX (par.pOr.lon);
   double y = getY (par.pOr.lat);
   CAIRO_SET_SOURCE_RGB_LIGHT_GRAY(cr);
   cairo_move_to (cr, x, y);
   for (int i = 0; i <  wayPoints.n; i++) {
      x = getX (wayPoints.t [i].lon);
      y = getY (wayPoints.t [i].lat);
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
   for (int i = 0; i < wayPoints.n; i++) {
      orthoPoints (cr, prevLat, prevLon,  wayPoints.t [i].lat,  wayPoints.t [i].lon, n);
      prevLat =  wayPoints.t [i].lat;
      prevLon =  wayPoints.t [i].lon;
   }
   if (destPressed)
      orthoPoints (cr, prevLat, prevLon, par.pDest.lat, par.pDest.lon, n);
}

/*! draw a circle */
static void circle (cairo_t *cr, double lon, double lat, double red, double green, double blue) {
   cairo_arc (cr, getX (lon), getY (lat), 4.0, 0, 2 * G_PI);
   cairo_set_source_rgb (cr, red, green, blue);
   cairo_fill (cr);
}

/*! draw the boat or cat or ... */
static void showUnicode (cairo_t *cr, const char *unicode, double x, double y) {
   PangoLayout *layout = pango_cairo_create_layout(cr);
   PangoFontDescription *desc = pango_font_description_from_string ("DejaVuSans 16");
   pango_layout_set_font_description(layout, desc);
   pango_font_description_free(desc);

   pango_layout_set_text(layout, unicode, -1);

   cairo_move_to (cr, x, y);
   pango_cairo_show_layout(cr, layout);
   g_object_unref(layout);
}

/*! draw all isochrones stored in isocArray as points */
static gboolean drawAllIsochrones0 (cairo_t *cr) {
   double x, y;
   Pp pt;
   for (int i = 0; i < nIsoc; i++) {
      GdkRGBA color = colors [i % nColors];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = isocArray [i][k]; 
         x = getX (pt.lon); 
         y = getY (pt.lat); 
         cairo_arc (cr, x, y, 1.0, 0, 2 * G_PI);
         cairo_fill (cr);
      }
   }
   return FALSE;
}

/*! draw closest points to pDest in each isochrones */
static gboolean drawClosest (cairo_t *cr) {
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
static gboolean drawFocal (cairo_t *cr) {
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
static gboolean drawAllIsochrones (cairo_t *cr, int style) {
   double x, y, x1, y1, x2, y2;
   //GtkWidget *drawing_area = gtk_drawing_area_new();
   Pp pt;
   Isoc  newIsoc;
   int index;
   if (par.closestDisp) drawClosest (cr);
   if (par.focalDisp) drawFocal (cr);
   if (style == NOTHING) return true;
   if (style == JUST_POINT) return drawAllIsochrones0 (cr);
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
         for (int kk = k; kk < isoDesc [i].size; kk++) { // others with segment
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

/*! find index in route with closest time to t 
   -1 if terminated */
static int findIndexInRoute (double t) {
   int i;
   if (t < route.t[0].time) return 0;
   if (t > route.t[route.n - 1].time) return -1;
   for (i = 0; i < route.n; i++) {
      if (route.t[i].time > t) break;
   }
   if ((t - route.t[i-1].time) < (route.t[i].time - t))
      return i - 1;
   else return i;
}

/*! find index in route just before now  
   -1 if terminated */
static int findIndexInRouteNow () {
   time_t now = time (NULL);
   // tDeltaNow is the number of hours between beginning of grib to now */
   double tDeltaNow = (now - vOffsetLocalUTC)/ 3600.0; // offset between local and UTC in hours
   tDeltaNow -= gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]) / 3600.0;
   int i = (tDeltaNow - par.startTimeInHours) / par.tStep;
   // printf ("tDeltaNow: %.2lf, i = %d\n", tDeltaNow, i);
   return i;
}

/*! give the focus on point in route at theTime  */
static void focusOnPointInRoute (cairo_t *cr)  {
   char strDate [MAX_SIZE_LINE], strLat [MAX_SIZE_NAME] = "", strLon [MAX_SIZE_NAME] = "";
   char sInfoRoute [MAX_SIZE_LINE] = "";
   int i;
   double lat, lon;
   if (route.n == 0) {
      gtk_label_set_text (GTK_LABEL(labelInfoRoute), sInfoRoute);
      return;
   }
   i = findIndexInRoute (theTime);
    
   if (route.destinationReached && (i == -1)) {   
      lat = par.pDest.lat;
      lon = par.pDest.lon;
      snprintf (sInfoRoute, sizeof (sInfoRoute), 
         "Destination Reached on: %s Lat: %-12s Lon: %-12s", \
         newDateWeekDay (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route.duration, strDate), \
         latToStr (lat, par.dispDms, strLat), lonToStr (lon, par.dispDms, strLon));
   }
   else {
      if (i > route.n) {
         i = route.n - 1;
         lat = lastClosest.lat;
         lon = lastClosest.lon;
      }
      else {
         lat = route.t[i].lat;
         lon = route.t[i].lon;
      }
      int twa = fTwa (route.t[i].lCap, route.t[i].twd);
      snprintf (sInfoRoute, sizeof (sInfoRoute), 
         "Route Date: %s Lat: %-12s Lon: %-12s  COG: %4.0f°  SOG:%5.2lfKn  TWD:%4d°  TWA:%4d°  TWS:%5.2lfKn  Gust: %5.2lfKn  Wave: %5.2lfm   %s", \
         newDateWeekDay (zone.dataDate [0], (zone.dataTime [0]/100) + route.t[i].time, strDate), \
         latToStr (route.t[i].lat, par.dispDms, strLat), lonToStr (route.t[i].lon, par.dispDms, strLon), \
         route.t[i].lCap, route.t[i].ld/par.tStep, \
         (int) (route.t[i].twd + 360) % 360,\
         twa, route.t[i].tws, \
         MS_TO_KN * route.t[i].g, route.t[i].w,
         (isDayLight (route.t[i].time, lat, lon)) ? "Day" : "Night");
   }
   showUnicode (cr, BOAT_UNICODE, getX (lon), getY (lat) - 20);
   gtk_label_set_text (GTK_LABEL(labelInfoRoute), sInfoRoute);
   circle (cr, lon, lat, 00, 0.0, 1.0); // blue
}

/* set color of route according to motor and amure */
static inline void routeColor (cairo_t *cr, bool motor, int amure) {
   if (motor) 
      CAIRO_SET_SOURCE_RGB_RED(cr);
   else {
      if (amure == BABORD) 
         CAIRO_SET_SOURCE_RGB_BLACK(cr);
      else CAIRO_SET_SOURCE_RGB_GRAY(cr);
   }
} 

/*! draw the routes based on route table, from pOr to pDest or best point */
static void drawRoute (cairo_t *cr) {
   char line [MAX_SIZE_LINE];
   int motor = route.t[0].motor;
   int amure = route.t[0].amure;
   double x = getX (par.pOr.lon);
   double y = getY (par.pOr.lat);
   cairo_set_line_width (cr, 5);
   routeColor (cr, motor, amure);
   cairo_move_to (cr, x, y);

   for (int i = 1; i < route.n; i++) {
      x = getX (route.t[i].lon);
      y = getY (route.t[i].lat);
      cairo_line_to (cr, x, y);
      if ((route.t[i].motor != motor) || (route.t[i].amure != amure)) {
         cairo_stroke (cr);
         amure = route.t[i].amure;
         motor = route.t[i].motor;
         routeColor (cr, motor, amure);
         cairo_move_to (cr, x, y);
      }
   }
   if (route.destinationReached) {
      x = getX (par.pDest.lon);
      y = getY (par.pDest.lat);
      cairo_line_to (cr, x, y);
   }
   cairo_stroke (cr);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size (cr, 15);
   cairo_move_to (cr, x +5, y + 5);
   snprintf (line, MAX_SIZE_LINE, "Rte: %d", historyRoute.n - 1);
   cairo_show_text (cr, line);
   cairo_stroke(cr);
}

/*! draw the route based on route table index */
static void drawHistoryRoute (cairo_t *cr, int k) {
   char line [MAX_SIZE_LINE];
   double x = getX (historyRoute.r[k].t[0].lon);
   double y = getY (historyRoute.r[k].t[0].lat);
   CAIRO_SET_SOURCE_RGB_PINK(cr);
   cairo_set_line_width (cr, 2);
   cairo_move_to (cr, x, y);

   for (int i = 1; i < historyRoute.r[k].n; i++) {
      x = getX (historyRoute.r[k].t[i].lon);
      y = getY (historyRoute.r[k].t[i].lat);
      cairo_line_to (cr, x, y);
   }
   cairo_stroke (cr);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size (cr, 15);
   cairo_move_to (cr, x +5, y + 5);
   snprintf (line, MAX_SIZE_LINE, "Rte: %d", k);
   cairo_show_text (cr, line);
   cairo_stroke(cr);
   
}

/*! draw the routes based on route table */
static void drawAllRoutes (cairo_t *cr) {
   for (int k = 0; k < historyRoute.n - 1; k++) {
      drawHistoryRoute (cr, k);
   }
   drawRoute (cr);
}

/*! draw Point of Interests */
static void drawPoi (cairo_t *cr) {
   double x, y;
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size (cr, 10);
   for (int i = 0; i < nPoi; i++) {
      x = getX (tPoi [i].lon);
      y = getY (tPoi [i].lat);
      if (tPoi [i].level <= par.maxPoiVisible) {
         switch (tPoi [i].type) {
         case UNVISIBLE:
            break;
         case CAT:
            showUnicode (cr, CAT_UNICODE, x, y - 20);
            break;
         case NORMAL: case NEW:
            circle (cr, tPoi [i].lon, tPoi [i].lat, 0.0, 0.0, 0.0);
            cairo_move_to (cr, x+10, y);
            cairo_show_text (cr, tPoi [i].name);
            break;
         case PORT:
            if (dispZone.zoom > MIN_ZOOM_POI_VISIBLE) {
               CAIRO_SET_SOURCE_RGB_BLACK (cr); // points
               cairo_move_to (cr, x+10, y);
               gchar *utf8String = g_locale_to_utf8(tPoi [i].name, -1, NULL, NULL, NULL);
               cairo_show_text (cr, utf8String);
               g_free (utf8String);
               cairo_rectangle (cr, x, y, 1, 1);
               cairo_fill (cr);
            }
            break;
         default:;
         }
      }
   }
   cairo_stroke (cr);
}

/*! return true if rectangle defined by parameters interects with display zone */
static bool isRectangleIntersecting (double latMin, double latMax, double lonMin, double lonMax) {
    return (latMin <= dispZone.latMax && latMax >= dispZone.latMin && lonMin <= dispZone.lonRight && lonMax >= dispZone.lonLeft);
}

/*! draw shapefile with different color for sea and earth */
static gboolean draw_shp_map (cairo_t *cr) {
   register double x, y;
   register int step = 1, deb, end;

   // Dessiner les entités
   for (int i = 0; i < nTotEntities; i++) {
      if ((entities [i].nSHPType != SHPT_POINT) && (! isRectangleIntersecting (entities[i].latMin, entities[i].latMax, entities[i].lonMin, entities[i].lonMax)))
         continue;
      step = (dispZone.zoom < 5) ? 128 : (dispZone.zoom < 20) ? 64 : (dispZone.zoom < 50) ? 32 : (dispZone.zoom < 100) ? 16 : (dispZone.zoom < 500) ? 8 : 1;
      
      //printf ("draw_sh_map zoom: %.2lf, step: %d\n", dispZone.zoom, step);
      switch (entities [i].nSHPType) {
      case SHPT_POLYGON: // case SHPT_POLYGONZ:// Traitement pour les polygones
         CAIRO_SET_SOURCE_RGBA_SHP_MAP(cr); //lignes
         for (int iPart = 0; iPart < entities [i].maxIndex;  iPart++) {
            // CAIRO_SET_SOURCE_RGB_BLACK(cr); // useful for bordes
            deb = entities [i].index [iPart];
            end = (iPart == entities [i].maxIndex - 1) ? entities [i].numPoints : entities [i].index [iPart + 1];
            // printf ("entity: %d, part: %d, deb %d, end %d\n", i, iPart, deb, end);
            x = getX (entities[i].points[deb].lon);
            y = getY (entities[i].points[deb].lat);
            cairo_move_to (cr, x, y);
            for (int j = deb + 1; j < end; j += step) {
               x = getX (entities[i].points[j].lon);
               y = getY (entities[i].points[j].lat);
               cairo_line_to (cr, x, y);
            }
            cairo_close_path(cr);
            // cairo_stroke_preserve(cr); // usful for borders
            // CAIRO_SET_SOURCE_RGBA_SHP_MAP(cr); //lignes
            cairo_fill(cr);
            // cairo_move_to (cr, x, y);
         }
        break;
      case SHPT_ARC: //case SHPT_ARCZ: // manage lines (arcs). BE CAREFUL:  Multipart not considered
         CAIRO_SET_SOURCE_RGB_RED(cr); //lignes
         x = getX (entities[i].points[0].lon);
         y = getY (entities[i].points[0].lat);
         cairo_move_to (cr, x, y);
         for (int j = 1; j < entities[i].numPoints; j += step) {
            x = getX (entities[i].points[j].lon);
            y = getY (entities[i].points[j].lat);
            cairo_line_to (cr, x, y);
         }
         cairo_stroke(cr);
         break;
      case SHPT_POINT:
         CAIRO_SET_SOURCE_RGB_RED(cr); // points
         x = getX (entities[i].points[0].lon);
         y = getY (entities[i].points[0].lat);
         cairo_rectangle (cr, x, y, 1, 1);
         cairo_fill (cr);
         break;
      case SHPT_NULL: // empty
         break;
      default: fprintf (stderr, "Error in draw_shp_map: SHPtype unknown: %d\n", entities [i].nSHPType); 
      }
   }
   return FALSE;
}

/*! draw something that represent the waves */
static void showWaves (cairo_t *cr, double lat, double lon, double w) {
   double x = getX (lon);
   double y = getY (lat);
   char str [MAX_SIZE_NUMBER];
   if ((w <= 0) || (w > 100)) return;
   CAIRO_SET_SOURCE_RGB_GRAY(cr);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 6);
   cairo_move_to (cr, x, y);
   snprintf (str, MAX_SIZE_NUMBER, "%.2lf", w);
   cairo_show_text (cr, str);
}

/*! draw arrow that represent the direction of the wind */
static void arrow (cairo_t *cr, double head_x, double head_y, double u, double v, double twd, double tws, int typeFlow) {
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
      cairo_show_text (cr, "o");
      return;
   }
    // Dessiner la ligne de la flèche
   cairo_move_to(cr, head_x, head_y);
   cairo_line_to(cr, tail_x, tail_y);
   cairo_stroke(cr);

   // Dessiner le triangle de la flèche à l'extrémité
   cairo_move_to (cr, head_x, head_y);
   cairo_line_to (cr, head_x + arrowSize * sin (DEG_TO_RAD  * twd - G_PI/6), head_y - arrowSize * cos(DEG_TO_RAD * twd - G_PI/6));
   cairo_stroke(cr);
   cairo_move_to (cr, head_x, head_y);
   cairo_line_to (cr, head_x + arrowSize * sin(DEG_TO_RAD * twd + G_PI/6), head_y - arrowSize * cos(DEG_TO_RAD * twd + G_PI/6));
   cairo_stroke(cr);
}

/*! draw barbule that represent the wind */
static void barbule (cairo_t *cr, double lat, double lon, double u, double v, double tws, int typeFlow) {
   int i, j, k;
   double barb0_x, barb0_y, barb1_x, barb1_y, barb2_x, barb2_y;
   /*if (tws == 0 || u <= (MISSING + 1) || v <= (MISSING +1) || fabs (u) > 100 || fabs (v) > 100) {
      //printf ("In barbule, strange value lat: %.2lf, lon: %.2lf, u: %.2lf, v: %.2lf\n", pt.lat, pt.lon, u, v);
      return;
   }*/
   // Calculer les coordonnées de la tête
   double head_x = getX (lon); 
   double head_y = getY (lat); 
   // Calculer les coordonnées de la queue
   double tail_x = head_x - 30 * u/tws;
   double tail_y = head_y + 30 * v/tws;
   if (typeFlow == WIND) CAIRO_SET_SOURCE_RGB_BLACK(cr);
   else CAIRO_SET_SOURCE_RGB_ORANGE(cr);
   cairo_set_line_width (cr, 1.0);
   cairo_set_font_size (cr, 6);

   if (tws < 1) {  // no wind : circle
      cairo_move_to (cr, head_x, head_y);
      cairo_show_text (cr, "o");
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
   if (statusbar == NULL) return;
   char totalDate [MAX_SIZE_DATE]; 
   char sStatus [MAX_SIZE_BUFFER];
   double lat, lon;
   double u = 0, v = 0, g = 0, w = 0, uCurr = 0, vCurr = 0, currTwd = 0, currTws = 0;
   char seaEarth [MAX_SIZE_NAME];
   char strLat [MAX_SIZE_NAME] = "", strLon [MAX_SIZE_NAME] = "";
   double tDeltaCurrent = zoneTimeDiff (&currentZone, &zone);
   double twd, tws;
   lat = yToLat (whereIsMouse.y);
   lon = xToLon (whereIsMouse.x);
   lon = lonCanonize (lon);
	findWindGrib (lat, lon, theTime, &u, &v, &g, &w, &twd, &tws);
   strcpy (seaEarth, (isSea (lat, lon)) ? "Authorized" : "Forbidden");
	findCurrentGrib (lat, lon, theTime - tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);
   
   snprintf (sStatus, sizeof (sStatus), "%s         %3ld/%3ld      %s %s, %s\
      Wind: %03d° %05.2lf Knots  Gust: %05.2lf Knots  Waves: %05.2lf  Current: %03d° %05.2lf Knots         %s      Zoom: %.2lf       %s",
      newDate (zone.dataDate [0], (zone.dataTime [0]/100) + theTime, totalDate),\
      theTime, zone.timeStamp [zone.nTimeStamp-1], " ",\
      latToStr (lat, par.dispDms, strLat),\
      lonToStr (lon, par.dispDms, strLon),\
      (int) (twd + 360) % 360, tws, MS_TO_KN * g, w, 
      (int) (currTwd + 360) % 360, currTws, 
      seaEarth,
      dispZone.zoom,
      (gribRequestRunning || readGribRet == -1) ? "WAITING GRIB" : "");

   gtk_statusbar_push(GTK_STATUSBAR(statusbar), context_id, sStatus);
}

/*! draw scale, scaleLen is for one degree. Modified if not ad hoc */
static void drawScale (cairo_t *cr, DispZone *dispZone) {
   const int DELTA = 4;
   char line [MAX_SIZE_LINE];
   double scaleX, scaleY, scaleLen;;
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   cairo_set_line_width (cr, 2.0);
   int val = 60;
   if ((dispZone->latMax - dispZone ->latMin) <= 2.0) return; // no scale for small zone

   // vertical
   scaleLen = (getY (dispZone->latMax - 1) - getY (dispZone->latMax)); 
   //scaleX = getX (ceil (dispZone->lonLeft));
   scaleX = 30;
   scaleY = getY (ceil (dispZone->latMin));
   cairo_move_to (cr, scaleX, scaleY);
   cairo_line_to (cr, scaleX, scaleY - scaleLen);
   cairo_stroke(cr);
   
   cairo_move_to (cr, scaleX + 15, scaleY);
   snprintf (line, MAX_SIZE_LINE, "Lat: %d miles", val);
   cairo_show_text (cr, line);
   cairo_stroke(cr);

   cairo_move_to (cr, scaleX - DELTA, scaleY);
   cairo_line_to (cr, scaleX + DELTA, scaleY);
   cairo_stroke(cr);
   cairo_move_to (cr, scaleX + DELTA, scaleY - scaleLen);
   cairo_line_to (cr, scaleX - DELTA, scaleY - scaleLen);
   cairo_stroke(cr);

   // horizontal
   scaleLen = (getX (dispZone->lonRight) - getX (dispZone->lonRight - 1)) / MAX (0.1, cos (DEG_TO_RAD * (dispZone->latMax + dispZone->latMin)/2));
   scaleX = getX (round ((dispZone->lonLeft + dispZone->lonRight) / 2.0 + 1));
   scaleY = getY (ceil (dispZone->latMin));
   cairo_move_to (cr, scaleX, scaleY);
   cairo_line_to (cr, scaleX + scaleLen, scaleY);
   cairo_stroke(cr);

   cairo_move_to (cr, scaleX, scaleY - 15);
   snprintf (line, MAX_SIZE_LINE, "Lon: %d miles", val);
   cairo_show_text (cr, line);
   cairo_stroke(cr);
    
   cairo_move_to (cr, scaleX, scaleY - DELTA);
   cairo_line_to (cr, scaleX, scaleY + DELTA);
   cairo_stroke(cr);
   cairo_move_to (cr, scaleX + scaleLen, scaleY - DELTA);
   cairo_line_to (cr, scaleX + scaleLen, scaleY + DELTA);
   cairo_stroke(cr);
}

/* Provide resume of information */
static void drawInfo (cairo_t *cr, int width, int height) {
   double lat, lon, distToDest;
   char str [MAX_SIZE_BUFFER];
   char strDate [MAX_SIZE_LINE];
   const guint INFO_X = 30;
   const guint INFO_Y = 50;
   const int DELTA = 50;
   cairo_select_font_face(cr, "Courier", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   CAIRO_SET_SOURCE_RGB_DARK_GRAY(cr);

   cairo_set_font_size (cr, 30.0);
   char fileName [MAX_SIZE_FILE_NAME];
   buildRootName (par.traceFileName, fileName);
   double od, ld, rd, sog;
   if (infoDigest (fileName, &od, &ld, &rd, &sog)) {
      snprintf (str, MAX_SIZE_LINE, "Miles from Origin...: %8.2lf, Real Dist.......: %.2lf", od, rd);
   }
   else fprintf (stderr, "Error in distance trace calculation");
   cairo_move_to (cr, INFO_X, INFO_Y);
   cairo_show_text (cr, str);
   
   if (my_gps_data.OK) {
      lat = my_gps_data.lat;
      lon = my_gps_data.lon;
   }
   else {
      lat = par.pOr.lat;
      lon = par.pOr.lon; 
   }

   distToDest = orthoDist (lat, lon, par.pDest.lat, par.pDest.lon);
   
   snprintf (str, MAX_SIZE_LINE, "Miles to Destination: %8.2lf", distToDest);
   cairo_move_to (cr, INFO_X, INFO_Y + DELTA);
   cairo_show_text (cr, str);
   
   if (route.n > 0) {
      newDateWeekDay (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route.duration, strDate);
      snprintf (str, MAX_SIZE_BUFFER, "By Sail to Dest.....: %8.2lf, Expected arrival: %s", route.totDist, strDate);
      cairo_move_to (cr, INFO_X, INFO_Y + 2 * DELTA);
      cairo_show_text (cr, str);
   }
}

/*! draw grid with meridians and parallels */
static void drawGrid (cairo_t *cr, DispZone *dispZone, int step) {
   const int MAX_LAT = 85;
   CAIRO_SET_SOURCE_RGB_LIGHT_GRAY (cr);
   cairo_set_line_width(cr, 0.5);
   // meridians
   for (int intLon = MAX (-180, dispZone->lonLeft); intLon < MIN (180, dispZone->lonRight); intLon += step) {
      cairo_move_to (cr, getX (intLon), getY (MAX_LAT));
      cairo_line_to (cr, getX (intLon), getY (-MAX_LAT));
      cairo_stroke(cr);
   }
   // parallels
   for (int intLat = MAX (-MAX_LAT, dispZone->latMin); intLat <= MIN (MAX_LAT, dispZone->latMax); intLat += MIN (step, MAX_LAT)) {
      cairo_move_to (cr, dispZone->xR, getY (intLat));
      cairo_line_to (cr, dispZone->xL, getY (intLat));
      cairo_stroke(cr);
   }
}

/*! exit when timeout expire */
static gboolean exitOnTimeOut (gpointer data) {
   printf ("exit on time out\n");
   exit (EXIT_SUCCESS);
}

/* draw barbules or arrows */
static void drawBarbulesArrows (cairo_t *cr) {
   double u, v, gust, w, twd, tws, uCurr, vCurr, currTwd, currTws, head_x, head_y, lat, lon;
   double tDeltaCurrent = zoneTimeDiff (&currentZone, &zone);
   int nCall = 0;

   lat = dispZone.latMin;
   while (lat <= dispZone.latMax) {
      lon = dispZone.lonLeft;
      while (lon <= dispZone.lonRight) {
	      u = 0; v = 0; w = 0; uCurr = 0; vCurr = 0;
         if (isInZone (lat, lon, &zone) || (par.constWindTws > 0)) {
	         findWindGrib (lat, lon, theTime, &u, &v, &gust, &w, &twd, &tws);
            nCall ++;
	         if (par.windDisp == BARBULE) {
               barbule (cr, lat, lon, u, v, (par.averageOrGustDisp == 0) ? tws : gust *MS_TO_KN, WIND);
            }
            else if (par.windDisp == ARROW) {
               head_x = getX (lon); 
               head_y = getY (lat);
               arrow (cr, head_x, head_y, u, v, twd, tws, WIND);
            }
            if (par.waveDisp) showWaves (cr, lat, lon, w);

            if (par.currentDisp) {
	            findCurrentGrib (lat, lon, theTime - tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);
               barbule (cr, lat, lon, uCurr, vCurr, currTws, CURRENT);
            }
	      }
         lon += (dispZone.lonStep) / 2;
      }
      lat += (dispZone.latStep) / 2;
   }
   // printf ("nCall in drawBarbulesArrows to findWind: %d\n", nCall);
}

/*! draw the main window */
static void drawGribCallback (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
   guint8 r, g, b;
   int iSurface = (int) theTime;
   if (iSurface > MAX_N_SURFACE) {
      fprintf (stderr, "In drawGribCallBack: MAX_N_SURFACE exceeded: %d\n", MAX_N_SURFACE);
      exit (EXIT_FAILURE);
   }

   dispZone.xL = 0;      // right
   dispZone.xR = width;  // left
   dispZone.yT = 0;      // top
   dispZone.yB = height; // bottom

   CAIRO_SET_SOURCE_RGB_WHITE (cr);
   cairo_paint (cr);                         // clear all
   if (par.showColors != 0) {
      if (par.constWindTws != 0) {           // const Wind
         mapColors (par.constWindTws, &r, &g, &b);
         cairo_set_source_rgba (cr, r/255.0, g/255.0, b/255.0, 0.5);
         cairo_rectangle (cr, 1, 1, width, height);
         cairo_fill(cr);
      }
      else {
         if (!existSurface [iSurface])
            createWindSurface (cr, iSurface, width, height);
         cairo_set_source_surface (cr, surface[iSurface], 0, 0);
         cairo_paint(cr);
      }
   }
   // paintDayNight (cr, width, height);

   draw_shp_map (cr);     
   
   drawGrid (cr, &dispZone, (par.gridDisp) ? 1: 90);     // if gridDisp, every degree. Else only every 90°
   drawScale (cr, &dispZone);  
   drawBarbulesArrows (cr);

   calculateOrthoRoute ();
   drawOrthoRoute (cr, ORTHO_ROUTE_PARAM);
   drawLoxoRoute (cr);

   if ((route.n != 0) && (isfinite(route.totDist)) && (route.totDist > 0)) { 
      drawAllIsochrones (cr, par.style);
      drawAllRoutes (cr);
      //Pp selectedPt = isocArray [nIsoc - 1][selectedPointInLastIsochrone]; // selected point in last isochrone
      //circle (cr, selectedPt.lon, selectedPt.lat, 1.0, 0.0, 1.1);
   }
   focusOnPointInRoute (cr);
   if (destPressed) {
      showUnicode (cr, DESTINATION_UNICODE, getX (par.pDest.lon), getY (par.pDest.lat) - 20);
      circle (cr, par.pDest.lon, par.pDest.lat, 0.0, 0.0, 1.0);
   }
   circle (cr, par.pOr.lon, par.pOr.lat, 0.0, 1.0, 0.0);
   if (my_gps_data.OK)
      circle (cr, my_gps_data.lon, my_gps_data.lat , 1.0, 0.0, 0.0);
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
      CAIRO_SET_SOURCE_RGBA_SELECTION(cr);
      cairo_rectangle (cr, whereWasMouse.x, whereWasMouse.y, whereIsMouse.x - whereWasMouse.x, whereIsMouse.y - whereWasMouse.y);
      cairo_fill(cr);
   }
   drawForbidArea (cr);
   if (par.special) {
      on_run_button_clicked ();
      g_timeout_add (QUIT_TIME_OUT, exitOnTimeOut, NULL);
   }
   drawTrace (cr);
   if (par.infoDisp)
      drawInfo (cr, width, height);
   
   //printf ("Time drawGribCallback: %.4f ms\n", (1000 * (double) (clock () - debut)) / CLOCKS_PER_SEC);
}

/*! draw the polar circles and scales */
static void polarTarget (cairo_t *cr, int type, double width, double height, double rStep) {
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double nStep = ceil (maxValInPol (ptMat));
   char str [MAX_SIZE_NUMBER];
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
            snprintf (str, MAX_SIZE_NUMBER, "%2d %%", i * 10);
            cairo_show_text (cr, str);
            cairo_move_to (cr, centerX - 40, centerY + i * rStep);
            cairo_show_text (cr, str);
         }
      }
   
      else {
         if ((rStep > MIN_R_STEP_SHOW) || ((i % 2) == 0)) { // only pair values if too close
            snprintf (str, MAX_SIZE_NUMBER, "%2d kn", i);
            cairo_show_text (cr, str);
            cairo_move_to (cr, centerX - 40, centerY + i * rStep);
            cairo_show_text (cr, str);
         }
      }
   }
   // libelles angles
   for (double angle = -90; angle <= 90; angle += 22.5) {
      cairo_move_to (cr, centerX + rMax * cos (DEG_TO_RAD * angle) * 1.05,
         centerY + rMax * sin (DEG_TO_RAD * angle) * 1.05);
      snprintf (str, MAX_SIZE_NUMBER, "%.2f°", angle + 90);
      cairo_show_text (cr, str);
   }
   cairo_stroke(cr);
}

/*! draw the polar legends */
static void polarLegend (cairo_t *cr, int type) {
   double xLeft = 100;
   double y = 5;
   double hSpace = 18;
   char line [MAX_SIZE_LINE];
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
      snprintf (line, MAX_SIZE_LINE, (type == WAVE_POLAR) ? "Height at %.2lf m" : "Wind at %.2lf kn", ptMat->t [0][c]);
      cairo_show_text (cr, line);
      y += hSpace;
   }
   cairo_stroke(cr);
}

/*! return x and y in polar */
static void getPolarXYbyValue (int type, int l, double w, double width, double height, double radiusFactor, double *x, double *y) {
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double angle = (90 - ptMat->t [l][0]) * DEG_TO_RAD;  // convert to Radius
   double val = findPolar (ptMat->t [l][0], w, *ptMat);
   double radius = val * radiusFactor;
   *x = width / 2 + radius * cos(angle);
   *y = height / 2 - radius * sin(angle);
}

/*! return x and y in polar */
static void getPolarXYbyCol (int type, int l, int c, double width, double height, double radiusFactor, double *x, double *y) {
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double angle = (90 - ptMat->t [l][0]) * DEG_TO_RAD;  // Convert to Radius
   double radius = ptMat->t [l][c] * radiusFactor;
   *x = width / 2 + radius * cos(angle);
   *y = height / 2 - radius * sin(angle);
}

/*! Draw polar from polar matrix */
static void drawPolarBySelectedTws (cairo_t *cr, int polarType, PolMat *ptMat,
   double selectedTws, double width, double height, double radiusFactor) {
   char line [MAX_SIZE_LINE];
   double x, y, x1, x2, y1, y2, vmgAngle, vmgSpeed;
   cairo_set_line_width (cr, 5);
   CAIRO_SET_SOURCE_RGBA_POLAR_TWS (cr);
   getPolarXYbyValue (polarType, 1, selectedTws, width, height, radiusFactor, &x, &y);
   cairo_move_to (cr, x, y);

   // segments
   if (segmentOrBezier == SEGMENT) {
      for (int l = 2; l < ptMat->nLine; l += 1) {
         getPolarXYbyValue (polarType, l, selectedTws, width, height, radiusFactor, &x, &y);
         cairo_line_to (cr, x, y);
      }
   }
   // draw Bezier if number of points exceed threshold 
   else {
      for (int l = 2; l < ptMat->nLine - 2; l += 3) {
         getPolarXYbyValue (polarType, l, selectedTws, width, height, radiusFactor, &x, &y);
         getPolarXYbyValue (polarType, l+1, selectedTws, width, height, radiusFactor, &x1, &y1);
         getPolarXYbyValue (polarType, l+2, selectedTws, width, height, radiusFactor, &x2, &y2);
         cairo_curve_to(cr, x, y, x1, y1, x2, y2);
      }
      getPolarXYbyValue (polarType, ptMat->nLine -1, selectedTws, width, height, radiusFactor, &x, &y);
      cairo_line_to(cr, x, y);
   }
   cairo_stroke(cr);

   if (polarType == POLAR) {
      bestVmg (selectedTws, &polMat, &vmgAngle, &vmgSpeed);
      double ceilSpeed = ceil (maxValInPol (&polMat));
      if (vmgSpeed > 0) {
         cairo_set_line_width (cr, 0.5);
         cairo_move_to (cr, width/2, height/2);
         cairo_line_to (cr, width/2, height/2 - vmgSpeed * radiusFactor);
         cairo_line_to (cr, width/2 + radiusFactor * ceilSpeed, height/2 - vmgSpeed * radiusFactor);
         cairo_stroke(cr);

         cairo_move_to (cr, width/2 + 50 + radiusFactor * ceilSpeed, height/2 - vmgSpeed * radiusFactor);
         snprintf (line,  MAX_SIZE_LINE, "Best VMG at %3.0lf°: %5.2lf Kn", vmgAngle, vmgSpeed);
         cairo_show_text (cr, line);
      }
      bestVmgBack (selectedTws, &polMat, &vmgAngle, &vmgSpeed);
      if (vmgSpeed > 0) {
         cairo_set_line_width (cr, 0.5);
         cairo_move_to (cr, width/2, height/2);
         cairo_line_to (cr, width/2, height/2 + vmgSpeed * radiusFactor);
         cairo_line_to (cr, width/2 + radiusFactor * ceilSpeed, height/2 + vmgSpeed * radiusFactor);
         cairo_stroke(cr);

         cairo_move_to (cr, width/2 + 50 + radiusFactor * ceilSpeed, height/2 + vmgSpeed * radiusFactor);
         snprintf (line,  MAX_SIZE_LINE, "Best Back VMG at %3.0lf°: %5.2lf Kn", vmgAngle, vmgSpeed);
         cairo_show_text (cr, line);
      }
   }
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
      // draw Bezier if number of points exceed threshold 
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
static void on_draw_polar_event (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
   PolMat *ptMat = (polarType == WAVE_POLAR) ? &wavePolMat : &polMat; 
   //double radiusFactor = (polarType == WAVE_POLAR) ? width / 40 : width / (maxValInPol (ptMat) * 5); // Ajustez la taille de la courbe
   double radiusFactor;
   radiusFactor = width / (maxValInPol (ptMat) * 6); // Ajust size of drawing 
   polarTarget (cr, polarType, width, height, radiusFactor);
   polarLegend (cr, polarType);
   drawPolarAll (cr, polarType, *ptMat, width, height, radiusFactor);
   drawPolarBySelectedTws (cr, polarType, ptMat, selectedTws, width, height, radiusFactor);
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/*! find index in polar to draw */
static void on_filter_changed (GtkComboBox *widget, gpointer user_data) {
   GtkWidget *filter_combo = (GtkWidget *) user_data;
   selectedPol = gtk_combo_box_get_active(GTK_COMBO_BOX(filter_combo));
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/*! combo filter zone for polar */
static GtkWidget *create_filter_combo (int type) {
   char str [MAX_SIZE_LINE];
   GtkTreeIter iter;
   // Create model for list
   GtkListStore *filter_store = gtk_list_store_new(1, G_TYPE_STRING);
   // printf ("in create_filter_combo\n");
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   
   // add filters to model
   gtk_list_store_append (filter_store, &iter);
   gtk_list_store_set (filter_store, &iter, 0, "All", -1);  
   for (int c = 1; c < ptMat->nCol; c++) {
      gtk_list_store_append (filter_store, &iter);
      snprintf (str, sizeof (str), "%.2lf %s", ptMat->t [0][c], (type == WAVE_POLAR) ? "m. Wave Height" : "Knots. Wind Speed.");
      gtk_list_store_set (filter_store, &iter, 0, str, -1);
   }

   // Create combo box
   GtkWidget *filter_combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(filter_store));

   // Create and configre rendered to display text
   GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(filter_combo), renderer, TRUE);
   gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(filter_combo), renderer, "text", 0, NULL);
   g_signal_connect(G_OBJECT(filter_combo), "changed", G_CALLBACK(on_filter_changed), filter_combo);
   // Select first line as default
   gtk_combo_box_set_active(GTK_COMBO_BOX(filter_combo), 0);
   return filter_combo;
}

/*! display polar matrix */
static void polarDump () {
   PolMat *ptMat = (polarType == WAVE_POLAR) ? &wavePolMat : &polMat; 
   GtkWidget *label;
   char str [MAX_SIZE_LINE] = "Polar Grid: ";
   GtkWidget *dumpWindow = gtk_application_window_new (app);
   strncat (str, (polarType == WAVE_POLAR) ? par.wavePolFileName : par.polarFileName, MAX_SIZE_LINE - strlen (str));
   gtk_window_set_title(GTK_WINDOW(dumpWindow), str);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), dumpWindow);
   g_signal_connect (G_OBJECT(dumpWindow), "destroy", G_CALLBACK (gtk_window_destroy), NULL);

   // Scroll 
   GtkWidget *scrolled_window = gtk_scrolled_window_new ();
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_window_set_child (GTK_WINDOW (dumpWindow), (GtkWidget *) scrolled_window);
   gtk_widget_set_size_request (scrolled_window, 800, 400);

   // grid creation
   GtkWidget *grid = gtk_grid_new();   
   gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), (GtkWidget *) grid);
   if ((ptMat->nCol == 0) || (ptMat->nLine == 0)) {
      infoMessage ("No polar information", GTK_MESSAGE_ERROR);
      return;
   }
   
   for (int line = 0; line < ptMat->nLine; line++) {
      for (int col = 0; col < ptMat->nCol; col++) {
	      if ((col == 0) && (line == 0)) strcpy (str, (polarType == WAVE_POLAR) ? "Angle/Height" : "TWA/TWS");
         else snprintf (str, sizeof (str), "%6.2f", ptMat->t [line][col]);
         label = gtk_label_new (str);
	      gtk_grid_attach(GTK_GRID (grid), label, col, line, 1, 1);
         gtk_widget_set_size_request (label, MAX_TEXT_LENGTH * 10, -1);
         // Add distinct proportes for first line and first column
         if (line == 0 || col == 0) {
            // set in bold
            PangoAttrList *attrs = pango_attr_list_new();
            PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
            pango_attr_list_insert (attrs, attr);
            gtk_label_set_attributes (GTK_LABEL(label), attrs);
            pango_attr_list_unref (attrs);
        }
     }
   }
   gtk_window_present (GTK_WINDOW (dumpWindow));
}

/*! callback function in polar for edit */
static void on_edit_button_polar_clicked (GtkWidget *widget, gpointer data) {
   char line [MAX_SIZE_LINE] = "";
   char fileName [MAX_SIZE_FILE_NAME] = "";
   strncpy (fileName, (polarType == WAVE_POLAR) ? par.wavePolFileName : par.polarFileName, sizeof (fileName));
   snprintf (line, sizeof (line), "%s %s \n", par.spreadsheet, fileName);
   if (system (line) != 0) {
      fprintf (stderr, "Error in on_edit_button_polar_clicked: System call: %s\n", line);
      return;
   }
   readPolar (fileName, (polarType == WAVE_POLAR) ? &wavePolMat : &polMat);
   gtk_widget_queue_draw (polar_drawing_area);
}

/*! Callback radio button selecting provider for segment or bezier */
static void segmentOrBezierButtonToggled(GtkToggleButton *button, gpointer user_data) {
   segmentOrBezier = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/*! Callback for polar to change scale calue */
static void on_scale_value_changed (GtkScale *scale) {
   char str [MAX_SIZE_LINE];
   snprintf (str, sizeof (str), (polarType == WAVE_POLAR) ? "%.2lf Meters" : "%.2lf Kn", selectedTws); 
   gtk_label_set_text (GTK_LABEL(scaleLabel), str);
   selectedTws = gtk_range_get_value(GTK_RANGE(scale));
   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
}

/*! Draw polar from polar file */
static void polarDraw () {
   char line [MAX_SIZE_LINE] = "Polar: "; 
   char sStatus [MAX_SIZE_LINE], str [MAX_SIZE_LINE];
   PolMat *ptMat = (polarType == WAVE_POLAR) ? &wavePolMat : &polMat; 
   selectedTws = 0;
   if ((ptMat->nCol == 0) || (ptMat->nLine == 0)) {
      infoMessage ("No polar information", GTK_MESSAGE_ERROR);
      return;
   }

   // Créer la fenêtre principale
   GtkWidget *polWindow = gtk_application_window_new (app);
   gtk_window_set_default_size(GTK_WINDOW (polWindow), POLAR_WIDTH, POLAR_HEIGHT);
   strncat (line, (polarType == WAVE_POLAR) ? par.wavePolFileName : par.polarFileName, MAX_SIZE_LINE - strlen (line));
   gtk_window_set_title (GTK_WINDOW (polWindow), line);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), polWindow);
   g_signal_connect (G_OBJECT(polWindow), "destroy", G_CALLBACK(gtk_window_destroy), NULL);

   // Créer un conteneur de type boîte
   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (polWindow), vBox);
   
   // Créer une zone de dessin
   polar_drawing_area = gtk_drawing_area_new ();
   gtk_widget_set_hexpand (polar_drawing_area, TRUE);
   gtk_widget_set_vexpand (polar_drawing_area, TRUE);
   gtk_widget_set_size_request (polar_drawing_area, POLAR_WIDTH, POLAR_HEIGHT - 100);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (polar_drawing_area), on_draw_polar_event, NULL, NULL);

   // create zone for combo box
   GtkWidget *filter_combo = create_filter_combo (polarType);
  
   GtkWidget *dumpButton = gtk_button_new_from_icon_name ("x-office-spreadsheet-symbolic");
   g_signal_connect(G_OBJECT(dumpButton), "clicked", G_CALLBACK (polarDump), NULL);
   
   GtkWidget *editButton = gtk_button_new_from_icon_name ("document-edit-symbolic");
   g_signal_connect(G_OBJECT(editButton), "clicked", G_CALLBACK(on_edit_button_polar_clicked), NULL);
   
   // radio button for bezier or segment choice
   GtkWidget *segmentRadio = gtk_check_button_new_with_label ("Segment");
   GtkWidget *bezierRadio = gtk_check_button_new_with_label ("Bézier");
   gtk_check_button_set_group ((GtkCheckButton *) segmentRadio, (GtkCheckButton *)bezierRadio); 
   g_object_set_data (G_OBJECT(segmentRadio), "index", GINT_TO_POINTER (SEGMENT));
   g_object_set_data (G_OBJECT(bezierRadio), "index", GINT_TO_POINTER (BEZIER));
   g_signal_connect (segmentRadio, "toggled", G_CALLBACK (segmentOrBezierButtonToggled), NULL);
   g_signal_connect (bezierRadio, "toggled", G_CALLBACK (segmentOrBezierButtonToggled), NULL);
   segmentOrBezier = (ptMat->nLine < MIN_POINT_FOR_BEZIER) ? SEGMENT : BEZIER;
   gtk_check_button_set_active ((GtkCheckButton *) ((segmentOrBezier == SEGMENT) ? segmentRadio : bezierRadio), TRUE);

   // Créer le widget GtkScale
  
   int maxScale = ptMat->t[0][ptMat->nCol-1];      // max value of wind or waves
   // printf ("maxScale: %d\n", maxScale);
   GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, maxScale, 1);
   gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_TOP);
   gtk_range_set_value(GTK_RANGE(scale), selectedTws);
   gtk_widget_set_size_request (scale, 300, -1);  // Adjust GtkScale size
   g_signal_connect (scale, "value-changed", G_CALLBACK (on_scale_value_changed), NULL);
   snprintf (str, sizeof (str), (polarType == WAVE_POLAR) ? "%.2lf Meters": "%.2f Kn", selectedTws); 
   scaleLabel = gtk_label_new (str);
  
   GtkWidget *hBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_box_append (GTK_BOX (hBox), filter_combo);
   gtk_box_append (GTK_BOX (hBox), segmentRadio);
   gtk_box_append (GTK_BOX (hBox), bezierRadio);
   gtk_box_append (GTK_BOX (hBox), dumpButton);
   gtk_box_append (GTK_BOX (hBox), editButton);
   gtk_box_append (GTK_BOX (hBox), scale);
   gtk_box_append (GTK_BOX (hBox), scaleLabel);

   // Création de la barre d'état
   GtkWidget *polStatusbar = gtk_statusbar_new ();
   guint context_id = gtk_statusbar_get_context_id (GTK_STATUSBAR(polStatusbar), "Statusbar");
   snprintf (sStatus, sizeof (sStatus), "nCol: %2d   nLig: %2d   max: %2.2lf", \
      ptMat->nCol, ptMat->nLine, maxValInPol (ptMat));
   gtk_statusbar_push (GTK_STATUSBAR (polStatusbar), context_id, sStatus);
   gtk_box_append (GTK_BOX (vBox), hBox);
   gtk_box_append (GTK_BOX (vBox), polar_drawing_area);
   gtk_box_append (GTK_BOX (vBox), polStatusbar);

   if (polar_drawing_area != NULL)
      gtk_widget_queue_draw (polar_drawing_area);
   gtk_window_present (GTK_WINDOW (polWindow));
}

/*! time management init */
static void initStart (struct tm *start) {
   long intDate = zone.dataDate [0];
   long intTime = zone.dataTime [0];
   start->tm_year = (intDate / 10000) - 1900;
   start->tm_mon = ((intDate % 10000) / 100) - 1;
   start->tm_mday = intDate % 100;
   start->tm_hour = intTime / 100;
   start->tm_min = intTime % 100;
   start->tm_isdst = -1;                                       // avoid adjustments with hours summer winter
   start->tm_sec += 3600 * par.startTimeInHours;               // add seconds
   mktime (start);
}

/*! calculate difference in hours between departure time and time 0 */
static double getDepartureTimeInHour (struct tm *start) {
   time_t theTime0 = gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]);
   start->tm_isdst = -1;                                       // avoid adjustments with hours summer winter
   time_t startTime = mktime (start);                          
   return (startTime - theTime0)/3600.0;                       // calculated in hours
} 

/*! Response when calendar choice done */
static void calendarResponse (GtkDialog *dialog, gint response_id, gpointer user_data) {
   time_t currentTime = time (NULL);
   struct tm *gmtTime = gmtime (&currentTime);
   if (response_id == -1) {      // select left button with current date
      startInfo = *gmtTime;
   }
        
   if ((response_id == GTK_RESPONSE_ACCEPT) || (response_id == -1)) {
      // printf ("startInfo before: %s, startTime: %lf\n", asctime (&startInfo), par.startTimeInHours);
      par.startTimeInHours = getDepartureTimeInHour (&startInfo); 
      // printf ("startInfo after: %s, startTime: %lf\n", asctime (&startInfo), par.startTimeInHours);
      if ((par.startTimeInHours < 0) || (par.startTimeInHours > zone.timeStamp [zone.nTimeStamp -1])) {
         gtk_window_destroy (GTK_WINDOW (dialog));
         infoMessage ("start time should be within grib window time !", GTK_MESSAGE_WARNING);
      }
      else {
         route.ret = 0;             // mean routing not terminated
         g_thread_new ("routingLaunch", routingLaunch, NULL); // launch routing
         routingTimeout = g_timeout_add (ROUTING_TIME_OUT, routingCheck, NULL);
         spinner ("Isochrone building", " ");
      }
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! year month date for fCalendar */
static void dateUpdate (GtkCalendar *calendar, gpointer user_data) {
   GDateTime *date = gtk_calendar_get_date (calendar);
   startInfo.tm_sec = 0;
   startInfo.tm_sec = 0;
   startInfo.tm_isdst = -1;                                       // avoid adjustments with hours summer winter
   startInfo.tm_mday = g_date_time_get_day_of_month (date);
   startInfo.tm_mon = g_date_time_get_month (date) - 1;
   startInfo.tm_year = g_date_time_get_year (date) - 1900;
}

/*! get start date and time for routing. Returns true if OK Response */
static void fCalendar (struct tm *start) {
   char str [MAX_SIZE_LINE];
   time_t currentTime = time (NULL);
   struct tm *gmtTime = gmtime(&currentTime);

   snprintf (str, sizeof (str), "%d/%02d/%02d %02d:%02d UTC", gmtTime->tm_year + 1900, gmtTime->tm_mon + 1, 
      gmtTime->tm_mday, gmtTime->tm_hour, gmtTime->tm_min);

   GtkWidget* dialogBox = gtk_dialog_new_with_buttons("Pick a Date", GTK_WINDOW (window), GTK_DIALOG_MODAL, 
                       str, GTK_RESPONSE_NONE, "OK", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_CANCEL, NULL);

   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialogBox));

   g_signal_connect (dialogBox, "destroy", G_CALLBACK (gtk_window_destroy), NULL);
   g_signal_connect_swapped (dialogBox, "response", G_CALLBACK (calendarResponse), dialogBox);

   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   GtkWidget *calendar = gtk_calendar_new();
   GDateTime *date = g_date_time_new_utc (start->tm_year + 1900, start->tm_mon + 1, start->tm_mday, 0, 0, 0);
   gtk_calendar_select_day (GTK_CALENDAR(calendar), date);
   g_signal_connect(calendar, "day-selected", G_CALLBACK (dateUpdate), NULL);
   g_date_time_unref (date);

   // Créer les libellés et les boutons de sélection
   GtkWidget *labelHour = gtk_label_new("Hour");
   GtkWidget *spinHour = gtk_spin_button_new_with_range(0, 23, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinHour), start->tm_hour);
   g_signal_connect (spinHour, "value-changed", G_CALLBACK (intSpinUpdate), &start->tm_hour);

   GtkWidget *labelMinutes = gtk_label_new("Minutes");
   GtkWidget *spinMinutes = gtk_spin_button_new_with_range(0, 59, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinMinutes), start->tm_min);
   g_signal_connect (spinMinutes, "value_changed", G_CALLBACK (intSpinUpdate), &start->tm_min);

   // Créer une boîte de disposition horizontale
   GtkWidget *hBox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_box_append (GTK_BOX (hBox), labelHour);
   gtk_box_append (GTK_BOX (hBox), spinHour);
   gtk_box_append (GTK_BOX (hBox), labelMinutes);
   gtk_box_append (GTK_BOX (hBox), spinMinutes);

   gtk_box_append (GTK_BOX (vBox), calendar);
   gtk_box_append (GTK_BOX (vBox), hBox);

   gtk_box_append (GTK_BOX (content_area), vBox);
   gtk_window_present (GTK_WINDOW (dialogBox));
}

/*! one line for niceReport () */
static void lineReport (GtkWidget *grid, int l, const char* iconName, const char *libelle, const char *value) {
   GtkWidget *icon = gtk_button_new_from_icon_name (iconName);
   GtkWidget *label = gtk_label_new (libelle);
   gtk_grid_attach(GTK_GRID (grid), icon, 0, l, 1, 1);
   gtk_label_set_yalign(GTK_LABEL(label), 0);
   gtk_label_set_xalign(GTK_LABEL(label), 0);   
   gtk_grid_attach(GTK_GRID (grid), label,1, l, 1, 1);
   GtkWidget *labelValue = gtk_label_new (value);
   gtk_label_set_yalign(GTK_LABEL(labelValue), 0);
   gtk_label_set_xalign(GTK_LABEL(labelValue), 1);   
   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   // add separator to grid
   gtk_grid_attach (GTK_GRID (grid), labelValue, 2, l, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), separator,    0, l + 1, 3, 1);
   gtk_widget_set_margin_end(GTK_WIDGET(labelValue), 20);
}

/*! draw legend */
static void drawLegend (cairo_t *cr, guint leftX, guint topY, double colors [][3], char *libelle [], size_t maxColors) {
   const int DELTA = 12;
   const int RECTANGLE_WIDTH = 55;
   CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr);
   cairo_rectangle (cr, leftX, topY, RECTANGLE_WIDTH, (maxColors + 1) * DELTA);
   cairo_stroke (cr);
   
   cairo_set_font_size (cr, 12);
   for (int i = 0; i < maxColors; i++) {
      cairo_set_source_rgb (cr, colors[i][0], colors[i][1], colors[i][2]);
      cairo_move_to (cr, leftX + DELTA /2, topY + (i + 1) * DELTA);
      cairo_show_text (cr, libelle [i]);
   }
}

/*! call back to draw statistics */
static void on_stat_event (GtkDrawingArea *drawing_area, cairo_t *cr, int width, int height, gpointer user_data) {
#define MAX_VAL 3
   width *= 0.9;
   height *= 0.9;
   const int LEGEND_X_LEFT = width - 35;
   const int LEGEND_Y_TOP = 10;
   double statValues [MAX_VAL];
   statValues [0] = route.motorDist;
   statValues [1] = route.tribordDist;
   statValues [2] = route.babordDist;
   const double EPSILON_DASHBOARD = 1.0;
   double total = 0;
   for (int i = 0; i < MAX_VAL; i++) {
      total += statValues[i];
   }
   if (total < 1.0)
      return;

   // Legend
   char *libelle [MAX_VAL] = {"Motor", "Tribord", "Babord"}; 
   double colors[MAX_VAL][3] = {{1, 0, 0}, {0.5, 0.5, 0.5}, {0, 0, 0}}; // Red Gray Black
   drawLegend (cr, LEGEND_X_LEFT, LEGEND_Y_TOP, colors, libelle, MAX_VAL);

   double start_angle = 0;
   for (int i = 0; i < MAX_VAL; i++) {
      double slice_angle = (statValues[i] / total) * 360.0;
      if (slice_angle < EPSILON_DASHBOARD) continue;

      // Define segment color and draw segment
      cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
      cairo_move_to(cr, width / 2, height / 2);
      cairo_arc(cr, width / 2, height / 2, MIN(width, height) / 2, start_angle * DEG_TO_RAD, (start_angle + slice_angle) * DEG_TO_RAD);
      cairo_close_path(cr);
      cairo_fill(cr);

      start_angle += slice_angle;
   }
   // percentage and libelle
   CAIRO_SET_SOURCE_RGB_WHITE(cr);
   start_angle = 0;
   for (int i = 0; i < MAX_VAL; i++) {
      double slice_angle = (statValues[i] / total) * 360.0;
      if (slice_angle < EPSILON_DASHBOARD) continue;
      // update percentage text position
      double mid_angle = start_angle + slice_angle / 2;
      double x = (width / 2) + (MIN(width, height) / 4) * cos(mid_angle * DEG_TO_RAD);
      double y = (height / 2) + (MIN(width, height) / 4) * sin(mid_angle * DEG_TO_RAD);
      
      char percentage [MAX_SIZE_LINE];
      if (statValues [i] > 0) {
         snprintf(percentage, sizeof(percentage), "%.0f%%", (statValues[i] / total) * 100);
         cairo_move_to(cr, x, y);
         cairo_show_text(cr, percentage);
      }
      start_angle += slice_angle;
   }
}

/*! call back to draw allure statistics */
static void on_allure_event (GtkDrawingArea *drawing_area, cairo_t *cr, int width, int height, gpointer user_data) {
#define MAX_ALLURES 3
   width *= 0.9;
   height *= 0.9;
   const int LEGEND_X_LEFT = width - 35;
   const int LEGEND_Y_TOP = 10;
   double twa;
   double statValues [MAX_ALLURES] = {0};
   const int threshold [MAX_ALLURES] = {60, 120, 180};
   const double EPSILON_DASHBOARD = 1.0;
   double total = 0;

   // calculate stat on allures
   for (int i = 0; i < route.n; i++) {
      if (route.t[i].motor) continue;
      twa = fTwa (route.t[i].lCap, route.t[i].twd);
      for (int k = 0; k < MAX_ALLURES; k++) {
         if (fabs (twa) < threshold [k]) {
            statValues [k] += route.t[i].od;
            break;
         }
      }
   }
   for (int i = 0; i < MAX_VAL; i++) {
      total += statValues[i];
   }
   if (total < 1.0)
      return;

   // legend
   double colors[MAX_VAL][3] = {{1, 0, 1}, {1, 165.0/255.0, 0}, {0, 1, 1}}; // Orange in the middle
   char *libelle [MAX_ALLURES] = {"Près", "Travers", "Portant"};
   drawLegend (cr, LEGEND_X_LEFT, LEGEND_Y_TOP, colors, libelle, MAX_ALLURES);

   double start_angle = 0;
   for (int i = 0; i < MAX_VAL; i++) {
      double slice_angle = (statValues[i] / total) * 360.0;
      if (slice_angle < EPSILON_DASHBOARD) continue;

      // Define segment color and draw segment
      cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
      cairo_move_to(cr, width / 2, height / 2);
      cairo_arc(cr, width / 2, height / 2, MIN(width, height) / 2, start_angle * DEG_TO_RAD, (start_angle + slice_angle) * DEG_TO_RAD);
      cairo_close_path(cr);
      cairo_fill(cr);

      start_angle += slice_angle;
   }
   // percentage and libelle
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   start_angle = 0;
   for (int i = 0; i < MAX_VAL; i++) {
      double slice_angle = (statValues[i] / total) * 360.0;
      if (slice_angle < EPSILON_DASHBOARD) continue;
      // update percentage text position
      double mid_angle = start_angle + slice_angle / 2;
      double x = (width / 2) + (MIN(width, height) / 4) * cos(mid_angle * DEG_TO_RAD);
      double y = (height / 2) + (MIN(width, height) / 4) * sin(mid_angle * DEG_TO_RAD);
      
      char percentage [MAX_SIZE_LINE];
      if (statValues [i] > 0) {
         snprintf(percentage, sizeof(percentage), "%.0f%%", (statValues[i] / total) * 100);
         cairo_move_to(cr, x, y);
         cairo_show_text(cr, percentage);
      }
      start_angle += slice_angle;
   }
}

/*! display nice report */
static void niceReport (SailRoute *route, double computeTime) {
   char str [MAX_SIZE_LINE];
   char totalDate [MAX_SIZE_DATE]; 
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   const int WIDTH = 500;
   if (computeTime > 0)
      snprintf (line, sizeof (line), "%s      Compute Time:%.2lf sec.", (route->destinationReached) ? 
       "Destination reached" : "Destination unreached", computeTime);
   else 
      snprintf (line,sizeof (line), "%s", (route->destinationReached) ? "Destination reached" : "Destination unreached");

   GtkWidget *reportWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW(reportWindow), line);
   gtk_widget_set_size_request(reportWindow, WIDTH, -1);

   GtkWidget *vBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (reportWindow), (GtkWidget *) vBox);
   
   GtkWidget *grid = gtk_grid_new();   
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
   
   snprintf (line, sizeof (line), "%4d/%02d/%02d %02d:%02d\n", 
      startInfo.tm_year + 1900, startInfo.tm_mon+1, startInfo.tm_mday, startInfo.tm_hour, startInfo.tm_min);
   lineReport (grid, 0, "document-open-recent", "Departure Date and Time", line);

   snprintf (line, sizeof (line), "%02d:%02d\n", (int) par.startTimeInHours, (int) (60 * fmod (par.startTimeInHours, 1.0)));
   lineReport (grid, 2, "dialog-information-symbolic", "Nb hours:minutes after origin of grib", line);
   
   snprintf (line, sizeof (line), "%d\n", route->nIsoc);
   lineReport (grid, 4, "accessories-text-editor-symbolic", "Nb of isochrones", line);

   snprintf (line, sizeof (line), "%s %s\n", latToStr (lastClosest.lat, par.dispDms, strLat), lonToStr (lastClosest.lon, par.dispDms, strLon));
   lineReport (grid, 6, "emblem-ok-symbolic", "Best Point Reached", line);

   snprintf (line, sizeof (line), "%.2lf\n", (route->destinationReached) ? 0 : orthoDist (lastClosest.lat, lastClosest.lon,\
         par.pDest.lat, par.pDest.lon));
   lineReport (grid, 8, "mail-forward-symbolic", "Distance To Destination", line);

   snprintf (line, sizeof (line), "%s\n",\
      newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route->duration, totalDate));
   lineReport (grid, 10, "alarm-symbolic", "Arrival Date and Time", line);

   snprintf (line, sizeof (line), "%.2lf\n", route->totDist);
   lineReport (grid, 12, "emblem-important-symbolic", "Total Distance in Nautical Miles", line);
   
   snprintf (line, sizeof (line), "%.2lf\n", route->motorDist);
   lineReport (grid, 14, "emblem-important-symbolic", "Motor Distance in Nautical Miles", line);
   
   snprintf (line, sizeof (line), "%s\n", durationToStr (route->duration, str, sizeof (str)));
   lineReport (grid, 16, "user-away", "Duration", line);
   
   snprintf (line, sizeof (line), "%s\n", durationToStr (route->motorDuration, str, sizeof (str)));
   lineReport (grid, 18, "user-away", "Motor Duration", line);
   
   snprintf (line, sizeof (line), "%.2lf\n", route->totDist/route->duration);
   lineReport (grid, 20, "utilities-system-monitor-symbolic", "Mean Speed Over Ground", line);


   // setup drawing area for stat
   GtkWidget *hBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

   GtkWidget *stat_drawing_area = gtk_drawing_area_new ();
   gtk_widget_set_size_request (stat_drawing_area,  WIDTH/2, 150);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (stat_drawing_area), on_stat_event, NULL, NULL);

   GtkWidget *allure_drawing_area = gtk_drawing_area_new ();
   gtk_widget_set_size_request (allure_drawing_area, WIDTH/2, 150);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (allure_drawing_area), on_allure_event, NULL, NULL);

   gtk_box_append (GTK_BOX (hBox), stat_drawing_area);
   gtk_box_append (GTK_BOX (hBox), allure_drawing_area);

   gtk_box_append (GTK_BOX (vBox), grid);
   gtk_box_append (GTK_BOX (vBox), hBox);
   gtk_window_present (GTK_WINDOW (reportWindow));
}

/*! check if routing is terminated */
static gboolean routingCheck (gpointer data) {
   if (route.ret == 0) { // not terminated
      gtk_widget_queue_draw(drawing_area); // affiche le tout
      return TRUE;
   }
   g_source_remove (routingTimeout); // timer stopped
   spinnerWindowDestroy ();
   if (route.ret == -1) {
      infoMessage ("Stopped", GTK_MESSAGE_ERROR);
      return TRUE;
   }
   if (! isfinite (route.totDist) || (route.totDist <= 1)) {
      infoMessage ("No route calculated. Check if wind !", GTK_MESSAGE_WARNING);
   }
   niceReport (&route, route.calculationTime);
   selectedPointInLastIsochrone = (nIsoc <= 1) ? 0 : isoDesc [nIsoc -1].closest; 
   gtk_widget_queue_draw (drawing_area);
   return TRUE;
}

/*! launch routing */
static void on_run_button_clicked () {
   if (! gpsTrace)
      if (addTraceGPS (par.traceFileName))
         gpsTrace = true; // only one GPS trace per session
   if (! isInZone (par.pOr.lat, par.pOr.lon, &zone) && (par.constWindTws == 0)) {
     infoMessage ("Origin point not in wind zone", GTK_MESSAGE_WARNING);
     return;
   }
   if (! isInZone (par.pDest.lat, par.pDest.lon, &zone) && (par.constWindTws == 0)) {
     infoMessage ("Destination point not in wind zone", GTK_MESSAGE_WARNING);
     return;
   }
   initStart (&startInfo);
   if (par.special == 0)
      fCalendar (&startInfo);
}

/*! check if bestDepartureTime is terminated */
static gboolean bestDepartureCheck (gpointer data) {
   char str [MAX_SIZE_LINE];
   snprintf (str, MAX_SIZE_LINE, "Evaluation count: %d", chooseDeparture.count);
   switch (chooseDeparture.ret) {
   case RUNNING: // not terminated
      snprintf (str, MAX_SIZE_LINE, "Evaluation count: %d", chooseDeparture.count);
      gtk_window_set_title(GTK_WINDOW(spinner_window), str);
      break;;
   case NO_SOLUTION: 
      g_source_remove (routingTimeout); // timer stopped
      spinnerWindowDestroy ();
      infoMessage ("No solution", GTK_MESSAGE_WARNING);
      break;
   case STOPPED:
      g_source_remove (routingTimeout); // timer stopped
      spinnerWindowDestroy ();
      infoMessage ("Stopped", GTK_MESSAGE_WARNING);
      break;
   case EXIST_SOLUTION:
      simulationReportExist = true;
      g_source_remove (routingTimeout); // timer stopped
      spinnerWindowDestroy ();
      snprintf (str, MAX_SIZE_LINE, "The quickest trip lasts %.2lf hours, start time: %d hours after beginning of Grib\nSee simulation Report for details", 
         chooseDeparture.minDuration, chooseDeparture.bestTime);
      infoMessage (str, GTK_MESSAGE_WARNING);
      initStart (&startInfo);
      selectedPointInLastIsochrone = (nIsoc <= 1) ? 0 : isoDesc [nIsoc -1].closest; 
      niceReport (&route, route.calculationTime);
      gtk_widget_queue_draw (drawing_area);
      break;
   default: 
      fprintf (stderr, "Error in bestDepartureCheck: Unknown chooseDeparture.ret: %d\n", chooseDeparture.ret);
      break;
   }
   return TRUE;
}

/*! action when choose departure request box */
static void chooseDepartureResponse (GtkDialog *dialog, gint response_id, gpointer user_data) {
   printf ("Response departurep: %d %d\n", response_id, GTK_RESPONSE_ACCEPT);
   if (response_id == GTK_RESPONSE_ACCEPT) {
      chooseDeparture.ret = RUNNING;             // mean not terminated
      simulationReportExist = false;
      g_thread_new("chooseDepartureLaunch", bestTimeDeparture, NULL); // launch routing
      routingTimeout = g_timeout_add (ROUTING_TIME_OUT, bestDepartureCheck, NULL);
      spinner ("Simulation Running", "It can take a while !!! ");
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! launch all routing in ordor to choose best departure time */
static void on_choose_departure_button_clicked (GtkWidget *widget, gpointer data) {
   if (! isInZone (par.pOr.lat, par.pOr.lon, &zone) && (par.constWindTws == 0)) {
     infoMessage ("Origin point not in wind zone", GTK_MESSAGE_WARNING);
     return;
   }
   if (! isInZone (par.pDest.lat, par.pDest.lon, &zone) && (par.constWindTws == 0)) {
     infoMessage ("Destination point not in wind zone", GTK_MESSAGE_WARNING);
     return;
   }
   chooseDeparture.tStep = 1;
   chooseDeparture.tBegin = 0;
   chooseDeparture.tEnd = zone.timeStamp [zone.nTimeStamp - 1];
   
   // Création de la boîte de dialogue ATT Global variable
   GtkWidget *dialogBox = gtk_dialog_new_with_buttons ("Simulation", \
      GTK_WINDOW(window), GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_CANCEL, NULL);

   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialogBox));
   //gtk_widget_set_size_request (dialogiBox, 400, -1);

   GtkWidget *grid = gtk_grid_new ();   
   gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
   
   GtkWidget *label_time_step = gtk_label_new("Time Step");
   GtkWidget *spin_button_time_step = gtk_spin_button_new_with_range (1, 12, 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_button_time_step), chooseDeparture.tStep);
   g_signal_connect (spin_button_time_step, "value-changed", G_CALLBACK (intSpinUpdate), &chooseDeparture.tStep);
   gtk_label_set_xalign(GTK_LABEL (label_time_step), 0);
   gtk_grid_attach(GTK_GRID (grid), label_time_step,       0, 0, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), spin_button_time_step, 1, 0, 1, 1);
   
   GtkWidget *label_mini = gtk_label_new("Min Start After Grib");
   GtkWidget *spin_button_mini = gtk_spin_button_new_with_range (0, zone.timeStamp [zone.nTimeStamp - 1], 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_button_mini), chooseDeparture.tBegin);
   g_signal_connect (spin_button_mini, "value-changed", G_CALLBACK (intSpinUpdate), &chooseDeparture.tBegin);
   gtk_label_set_xalign(GTK_LABEL (label_mini), 0);
   gtk_grid_attach(GTK_GRID (grid), label_mini,       0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), spin_button_mini, 1, 1, 1, 1);
   
   GtkWidget *label_maxi = gtk_label_new("Max Start After Grib");
   GtkWidget *spin_button_maxi = gtk_spin_button_new_with_range (1, zone.timeStamp [zone.nTimeStamp - 1], 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_button_maxi), chooseDeparture.tEnd);
   g_signal_connect (spin_button_maxi, "value-changed", G_CALLBACK (intSpinUpdate), &chooseDeparture.tEnd);
   gtk_label_set_xalign(GTK_LABEL (label_maxi), 0);
   gtk_grid_attach(GTK_GRID (grid), label_maxi,       0, 2, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), spin_button_maxi, 1, 2, 1, 1);
   
   g_signal_connect_swapped (dialogBox, "response", G_CALLBACK (chooseDepartureResponse), dialogBox);
   gtk_box_append(GTK_BOX (content_area), grid);
   gtk_window_present (GTK_WINDOW (dialogBox));
}

/*! Callback for stop button */
static void on_stop_button_clicked(GtkWidget *widget, gpointer data) {
   animation.active = NO_ANIMATION;
   if (animation.timer > 0)
      g_source_remove (animation.timer);
   animation.timer = 0;
}

/*! Callback for animation update */
static gboolean on_play_timeout(gpointer user_data) {
   theTime += par.tStep;
   if (theTime > zone.timeStamp [zone.nTimeStamp - 1]) {
      theTime = zone.timeStamp [zone.nTimeStamp - 1]; 
      if (animation.timer > 0)
         g_source_remove (animation.timer);
      animation.active = NO_ANIMATION;
      animation.timer = 0;
   }
   gtk_widget_queue_draw(drawing_area);
   statusBarUpdate ();
   return animation.active;
}

/*! Callback for animation update */
static gboolean on_loop_timeout(gpointer user_data) {
   theTime += par.tStep;
   if ((theTime > zone.timeStamp [zone.nTimeStamp - 1]) || (theTime > par.startTimeInHours + route.duration))
      theTime = par.startTimeInHours;
   gtk_widget_queue_draw(drawing_area);
   statusBarUpdate ();
   return animation.active;
}

/*! Callback for play button (animation) */
static void on_play_button_clicked(GtkWidget *widget, gpointer user_data) {
   if (animation.active == NO_ANIMATION) {
      animation.active = PLAY;
      animation.timer = g_timeout_add (animation.tempo [par.speedDisp], (GSourceFunc)on_play_timeout, NULL);
   }
}

/*! Callback for loop  button (animation) */
static void on_loop_button_clicked(GtkWidget *widget, gpointer user_data) {
   if (route.n == 0) {
      infoMessage ("No route !", GTK_MESSAGE_WARNING);
      return;
   }
   if (animation.active == NO_ANIMATION) {
      animation.active = LOOP;
      if ((theTime < par.startTimeInHours) || (theTime > par.startTimeInHours + route.duration))
         theTime = par.startTimeInHours; 
      gtk_widget_queue_draw (drawing_area);
      statusBarUpdate ();
      animation.timer = g_timeout_add (animation.tempo [par.speedDisp], (GSourceFunc)on_loop_timeout, NULL);
   }
}

/*! change animation timer */
static void changeAnimation () {
   if (animation.timer > 0 && animation.active != NO_ANIMATION) {
       g_source_remove (animation.timer);
   }
   if (animation.active == PLAY)
      animation.timer = g_timeout_add (animation.tempo [par.speedDisp], (GSourceFunc)on_play_timeout, NULL);
   else
      animation.timer = g_timeout_add (animation.tempo [par.speedDisp], (GSourceFunc)on_loop_timeout, NULL);
}

/*! Callback for to start button */
static void on_to_start_button_clicked(GtkWidget *widget, gpointer data) {
   theTime = 0;
   gtk_widget_queue_draw(drawing_area);
   statusBarUpdate ();
}

static void on_to_end_button_clicked(GtkWidget *widget, gpointer data) {
   theTime = zone.timeStamp [zone.nTimeStamp - 1];
   gtk_widget_queue_draw(drawing_area);
   statusBarUpdate (route.n -1);
}

/*! Callback for reward button */
static void on_reward_button_clicked(GtkWidget *widget, gpointer data) {
   // theTime -= zone.timeStamp [1]-zone.timeStamp [0];
   theTime -= par.tStep;
   if (theTime < 0) theTime = 0;
   gtk_widget_queue_draw(drawing_area);
   statusBarUpdate ();
}

/*! Callback for Forward button */
static void on_forward_button_clicked(GtkWidget *widget, gpointer data) {
   //theTime += zone.timeStamp [1]-zone.timeStamp [0];
   theTime += par.tStep;
   if (theTime > zone.timeStamp [zone.nTimeStamp - 1])
      theTime = zone.timeStamp [zone.nTimeStamp - 1]; 
   gtk_widget_queue_draw(drawing_area);
   statusBarUpdate ();
}

/*! display isochrones */
static void isocDump () {
   char *buffer = NULL;
   const int N_POINT_FACTOR = 1000;
   if (nIsoc == 0)
      infoMessage ("No isochrone", GTK_MESSAGE_INFO); 
   else {
      if ((buffer = (char *) malloc (nIsoc * MAX_SIZE_LINE * N_POINT_FACTOR)) == NULL)
         infoMessage ("Not enough memory", GTK_MESSAGE_ERROR); 
      else {
         allIsocToStr (buffer, nIsoc * MAX_SIZE_LINE * N_POINT_FACTOR); 
         displayText (750, 400, buffer, "Isochrones");
         free (buffer);
      }
   }
}

/*! display isochrone descriptors */
static void isocDescDump () {
   char *buffer = NULL;
   if (route.n <= 0)
      infoMessage ("No route calculated", GTK_MESSAGE_INFO);
   else {
      if ((buffer = (char *) malloc (nIsoc * MAX_SIZE_LINE)) == NULL)
         infoMessage ("Not enough memory", GTK_MESSAGE_ERROR); 
      else {
         if (isoDescToStr (buffer, nIsoc * MAX_SIZE_LINE))
            displayText (750, 400, buffer, "Isochrone Descriptor");
         else infoMessage ("Not enough space", GTK_MESSAGE_ERROR);
      }
   }
}

/*! display Points of Interest information */
static void historyReset  () {
   route.n = 0;
   historyRoute.n = 0;
   gtk_widget_queue_draw(drawing_area);
}

/*! Response for URL change */
static void newTraceResponse (GtkDialog *dialog, gint response_id, gpointer user_data) {
   FILE *f;
   if (response_id == GTK_RESPONSE_ACCEPT) {
      printf ("Trace name OK: %s\n", traceName);
      if ((f = fopen (traceName, "r")) != NULL) {// check if file already exist
         infoMessage ("This name already exist ! Retry...", GTK_MESSAGE_ERROR );
         fclose (f);
      }
      else {
         strncpy (par.traceFileName, traceName, MAX_SIZE_FILE_NAME);
      }
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
   gtk_widget_queue_draw (drawing_area);
}

/*! callback for newTrace change */
static void traceChange (GtkEditable *editable, gpointer user_data) {
   gchar *name = (gchar *) user_data;
   const gchar *text = gtk_editable_get_text (editable);
   g_strlcpy (name, text, MAX_SIZE_BUFFER);
   printf ("Trace name: %s\n", name);
}

/*! new Trace */
static void newTrace () {
   strncpy (traceName, par.traceFileName, MAX_SIZE_FILE_NAME);
   GtkWidget *dialogBox = gtk_dialog_new_with_buttons("New Trace", GTK_WINDOW(window), GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_ACCEPT,
                                           "_Cancel", GTK_RESPONSE_CANCEL, NULL);
   GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialogBox));

   int min_width = MAX (300, strlen (traceName) * 10);
   gtk_widget_set_size_request(dialogBox, min_width, -1);
   GtkEntryBuffer *buffer = gtk_entry_buffer_new (traceName, -1);
   GtkWidget *trace_entry = gtk_entry_new_with_buffer (buffer);

   // g_signal_connect(dialogBox, "destroy", G_CALLBACK(urlWindowDestroy), NULL);
   g_signal_connect_swapped (dialogBox, "response", G_CALLBACK (newTraceResponse), dialogBox);
   g_signal_connect (trace_entry, "changed", G_CALLBACK (traceChange), traceName);

   gtk_box_append(GTK_BOX(content_area), trace_entry);
   gtk_window_present (GTK_WINDOW (dialogBox));
}

/*! Edit trace  */
static void editTrace () {
   FILE *fs;
   char line [MAX_SIZE_LINE] = "";
   snprintf (line, sizeof (line), "%s %s\n", par.editor, par.traceFileName);
   if ((fs = popen (line, "r")) == NULL) {
      fprintf (stderr, "Error in editTrace: popen call: %s\n", line);
      return;
   }
   pclose (fs);
   gtk_widget_queue_draw (drawing_area);
}

/*! add POR to current trace */
static void traceAdd () {
   char fileName [MAX_SIZE_FILE_NAME];
   buildRootName (par.traceFileName, fileName);
   addTracePt (fileName, par.pOr.lat, par.pOr.lon);
   gtk_widget_queue_draw (drawing_area);
}

/*! display trace report */
static void traceReport () {
   char fileName [MAX_SIZE_FILE_NAME];
   double od, ld, rd, sog;
   char str [MAX_SIZE_LINE];
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   double lat, lon, time;

   GtkWidget *traceWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW(traceWindow), "Trace Report");

   GtkWidget *grid = gtk_grid_new();   
   gtk_window_set_child (GTK_WINDOW (traceWindow), (GtkWidget *) grid);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);

   buildRootName (par.traceFileName, fileName);
   if (! distanceTraceDone (fileName, &od, &ld, &rd, &sog)) {
      infoMessage ("Error in distance trace calculation", GTK_MESSAGE_ERROR);
      return;
   }
   snprintf (line, sizeof (line), "%.2lf", od);
   lineReport (grid, 0, "mail-forward-symbolic", "Trace Ortho Dist", line);
   snprintf (line, sizeof (line), "%.2lf", ld);
   lineReport (grid, 2, "mail-forward-symbolic", "Trace Loxo Dist", line);
   snprintf (line, sizeof (line), "%.2lf", rd);
   lineReport (grid, 4, "emblem-important-symbolic", "Trace Real Dist", line);

   snprintf (line, sizeof (line), "%.2lf", sog);
   lineReport (grid, 6, "emblem-important-symbolic", "Average SOG", line);

   if (findLastTracePoint (fileName, &lat, &lon, &time)) {
      snprintf (line, sizeof (line), "%s %s", latToStr (par.pOr.lat, par.dispDms, strLat), lonToStr (par.pOr.lon, par.dispDms, strLon));
      lineReport (grid, 8, "preferences-desktop-locale-symbolic", "Last Position", line);
      snprintf (line, sizeof (line), "%s", epochToStr (time, false, str, sizeof (str)));
      lineReport (grid, 10, "document-open-recent", "Last Time", line);
   }

   gtk_window_present (GTK_WINDOW (traceWindow));
}

/*! display trace information */
static void traceDump () {
   FILE *f = NULL;
   char line [MAX_SIZE_LINE], strLat [MAX_SIZE_LINE], strLon [MAX_SIZE_LINE];
   int count = 0, l = 2;
   char fileName [MAX_SIZE_FILE_NAME];
   buildRootName (par.traceFileName, fileName);
   char *latToken, *lonToken, *timeToken, *cogToken, *sogToken;
   if ((f = fopen (fileName, "r")) == NULL) {
      infoMessage ("Error in tracePoint: cannot read", GTK_MESSAGE_ERROR);
      return;
   }
   
   GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
   GtkWidget *label = NULL;

   GtkWidget *traceWindow = gtk_application_window_new (app);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), traceWindow);
   gtk_window_set_default_size(GTK_WINDOW (traceWindow), 600, 450);
   g_signal_connect(G_OBJECT(traceWindow), "destroy", G_CALLBACK(gtk_window_destroy), NULL);
   GtkWidget *scrolled_window = gtk_scrolled_window_new ();
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_window_set_child (GTK_WINDOW (traceWindow), (GtkWidget *) scrolled_window);

   GtkWidget *grid = gtk_grid_new();   
   gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), grid);
   gtk_grid_set_column_spacing (GTK_GRID(grid), 20);
   //gtk_grid_set_row_spacing (GTK_GRID(grid), 5);

   //gtk_grid_set_row_homogeneous (GTK_GRID(grid), FALSE);
   //gtk_grid_set_column_homogeneous (GTK_GRID(grid), FALSE);
   
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Lat."),      0, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Lon."),      1, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Dat-Time"),  2, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("COG"),       3, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("SOG"),       4, 0, 1, 1);

   gtk_grid_attach(GTK_GRID(grid), separator,                     0, 1, 5, 1);

   while ((fgets (line, MAX_SIZE_LINE, f) != NULL )) {
      if ((latToken = strtok (line, CSV_SEP)) != NULL) {             // Lat
         label = strToLabel (latToStr (strtod (latToken, NULL), par.dispDms, strLat), -1);
         gtk_grid_attach(GTK_GRID (grid), label, 0, l, 1, 1);
      }
      if ((lonToken = strtok (NULL, CSV_SEP)) != NULL) {             // Lon
         label = strToLabel (lonToStr (strtod (lonToken, NULL), par.dispDms, strLon), -1);
         gtk_grid_attach (GTK_GRID (grid), label, 1, l, 1, 1);
      }
      if ((timeToken = strtok (NULL, CSV_SEP))!= NULL );             // epoch not cpnsidered
      if ((timeToken = strtok (NULL, CSV_SEP))!= NULL ) {            // time
         label = strToLabel (timeToken, -1);
         gtk_grid_attach (GTK_GRID (grid), label, 2, l, 1, 1);
      }
      if ((cogToken = strtok (NULL, CSV_SEP))!= NULL ) {             // cog
         label = strToLabel (cogToken, -1);
         gtk_grid_attach (GTK_GRID (grid), label, 3, l, 1, 1);
      }
      if ((sogToken = strtok (NULL, CSV_SEP))!= NULL ) {             // sog
         label = strToLabel (sogToken, -1);
         gtk_grid_attach (GTK_GRID (grid), label, 4, l, 1, 1);
      }
      gtk_widget_set_margin_end (GTK_WIDGET(label), 20);
      l += 1;
      count += 1;
   }
   fclose (f);
   snprintf (line, sizeof (line), "%s (Number: %d)",  strrchr (fileName, '/' + 1), count);
   gtk_window_set_title (GTK_WINDOW (traceWindow), line);
   gtk_window_present (GTK_WINDOW (traceWindow));
}

/*! callback for openTrace */
static void on_open_trace_response (GtkDialog *dialog, int response) {
   if (response == GTK_RESPONSE_ACCEPT) {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
      g_autoptr(GFile) file = gtk_file_chooser_get_file (chooser);
      if (file != NULL) {
         gchar *fileName = g_file_get_path(file);
         if (fileName != NULL) {
            strncpy (par.traceFileName, fileName, MAX_SIZE_FILE_NAME); 
            g_free(fileName);
            traceReport ();
            gtk_widget_queue_draw (drawing_area);
         }
     }
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! Select trace file and read trace file */
static void openTrace () {
   char directory [MAX_SIZE_DIR_NAME];
   snprintf (directory, sizeof (directory), "%strace", par.workingDir);
   GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open Trace", GTK_WINDOW (window), GTK_FILE_CHOOSER_ACTION_OPEN,
                       "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

   GFile *gDirectory = g_file_new_for_path(directory);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), gDirectory, NULL);
   g_object_unref(gDirectory);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Trace Files");
   gtk_file_filter_add_pattern(filter, "*.csv");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
   g_object_unref (filter);

   gtk_window_present (GTK_WINDOW (dialog));
   g_signal_connect (dialog, "response", G_CALLBACK (on_open_trace_response), NULL);
}

/*! display Points of Interest information */
static void poiDump () {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE];
   char strLon [MAX_SIZE_LINE];
   int count = 0;
   int l = 2;
   GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
   GtkWidget *label;

   GtkWidget *poiWindow = gtk_application_window_new (app);
   GtkWidget *grid = gtk_grid_new();   
   gtk_window_set_child (GTK_WINDOW (poiWindow), (GtkWidget *) grid);
   gtk_grid_set_column_spacing (GTK_GRID(grid), 20);
   gtk_grid_set_row_spacing (GTK_GRID(grid), 5);

   gtk_grid_set_row_homogeneous (GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous (GTK_GRID(grid), FALSE);
   
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Lat."),  0, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Lon."),  1, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Type"),  2, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Level"), 3, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold("Name"),  4, 0, 1, 1);

   gtk_grid_attach(GTK_GRID(grid), separator,                 0, 1, 5, 1);

   for (int i = 0; i < nPoi; i++) {
      if ((tPoi [i].type != UNVISIBLE) && (tPoi [i].type != PORT)) {
         label = gtk_label_new (latToStr (tPoi[i].lat, par.dispDms, strLat));
         gtk_grid_attach(GTK_GRID (grid), label, 0, l, 1, 1);
         
         label = gtk_label_new (lonToStr (tPoi[i].lon, par.dispDms, strLon));
         gtk_grid_attach (GTK_GRID (grid), label, 1, l, 1, 1);
         
         snprintf (line, sizeof (line), "%d", tPoi [i].type);
         label = gtk_label_new (line);
         gtk_grid_attach (GTK_GRID (grid), label, 2, l, 1, 1);
         
         snprintf (line, sizeof (line), "%d", tPoi [i].level);
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
   snprintf (line, sizeof (line), "Points of Interest (Number: %d)", count);
   gtk_window_set_title (GTK_WINDOW (poiWindow), line);
   gtk_window_present (GTK_WINDOW (poiWindow));
}

/*! print the route from origin to destination or best point */
static void historyRteDump (gpointer user_data) {
   int k = GPOINTER_TO_INT (user_data);
   char buffer [MAX_SIZE_BUFFER];
   char title [MAX_SIZE_LINE];
   if (historyRoute.r[k].n <= 0)
      infoMessage ("No route calteculated", GTK_MESSAGE_INFO);
   else {
      routeToStr (&historyRoute.r[k], buffer, MAX_SIZE_BUFFER); 
      snprintf (title, MAX_SIZE_LINE, "History: %2d", k);
      displayText (1400, 400, buffer, title);
   }
}

/*! display history of routes */
static void rteHistory () {
   char str [MAX_SIZE_LINE];
   GtkWidget *itemHist; 
   GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   if (historyRoute.n <= 1) {
      infoMessage ("No History", GTK_MESSAGE_INFO);
      return;
   }
   else if (historyRoute.n == 2) {
      historyRteDump (GINT_TO_POINTER (0));
      return;
   }
   for (int i = 0; i < historyRoute.n - 1; i++) {
      snprintf (str, sizeof (str), "History: %d", i); 
      itemHist = gtk_button_new_with_label (str);
      g_signal_connect_swapped (itemHist, "clicked", G_CALLBACK (historyRteDump), GINT_TO_POINTER (i));
      gtk_box_append (GTK_BOX (box), itemHist);
   }
   menuHist = gtk_popover_new ();
   gtk_widget_set_parent (menuHist, window);
   g_signal_connect(window, "destroy", G_CALLBACK (on_parent_window_destroy), menuHist);
   gtk_popover_set_child ((GtkPopover*) menuHist, (GtkWidget *) box);
   gtk_popover_set_has_arrow ((GtkPopover*) menuHist, false);
   GdkRectangle rect = {200, 100, 1, 1};
   gtk_popover_set_pointing_to ((GtkPopover*) menuHist, &rect);
   gtk_widget_set_visible (menuHist, true);
}

/*! display the report of route calculation */
static void rteReport () {
   if (route.n <= 0)
      infoMessage ("No route calculated", GTK_MESSAGE_INFO);
   else {
      niceReport (&route, 0);
   }
}

/*! Draw simulation */
static void cb_simulation_report (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
   char str [MAX_SIZE_NUMBER];
   int x, y;
   cairo_set_line_width (cr, 1);
   
   const int X_LEFT = 30;
   const int X_RIGHT = width - 20;
   const int Y_TOP = 10;
   const int Y_BOTTOM = height - 25;
   const int XK = (X_RIGHT - X_LEFT) / (chooseDeparture.tStop -chooseDeparture.tBegin);  
   const int YK = (Y_BOTTOM - Y_TOP) / (int) chooseDeparture.maxDuration;
   const int DELTA = 5;
   const int RECTANGLE_WIDTH = CLAMP (XK, 1, 20); // between 1 and 20

   // Draw horizontal line wirh arrow
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_move_to (cr, X_LEFT,  Y_BOTTOM );
   cairo_line_to (cr, X_RIGHT, Y_BOTTOM);
   cairo_line_to (cr, X_RIGHT - DELTA, Y_BOTTOM + DELTA); // for arrow
   cairo_stroke (cr);
   cairo_move_to (cr, X_RIGHT, Y_BOTTOM );
   cairo_line_to (cr, X_RIGHT - DELTA, Y_BOTTOM - DELTA);
   cairo_stroke (cr);
   
   // Draw vertical line wirh arrow
   cairo_move_to (cr, X_LEFT, Y_BOTTOM);
   cairo_line_to (cr, X_LEFT, Y_TOP);
   cairo_line_to (cr, X_LEFT - DELTA, Y_TOP + DELTA);
   cairo_stroke (cr);
   cairo_move_to (cr, X_LEFT, Y_TOP);
   cairo_line_to (cr, X_LEFT + DELTA, Y_TOP + DELTA);
   cairo_stroke (cr);
   int h; 
   CAIRO_SET_SOURCE_RGB_GRAY(cr);
   for (int t = chooseDeparture.tBegin; t < chooseDeparture.tStop; t += chooseDeparture.tStep) {
      if (t != chooseDeparture.bestTime) {
         h = (int) chooseDeparture.t [t] * YK;
         cairo_rectangle (cr, X_LEFT + t * XK, Y_BOTTOM - h, RECTANGLE_WIDTH, h);
      }
   }
   cairo_fill (cr);
   CAIRO_SET_SOURCE_RGB_GREEN(cr);
   h = (int) chooseDeparture.t [chooseDeparture.bestTime] * YK;
   cairo_rectangle (cr, X_LEFT + chooseDeparture.bestTime * XK, Y_BOTTOM - h, RECTANGLE_WIDTH, h);
   cairo_fill (cr);

   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   // Draw time labels
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 10);

   const int displayStep = MAX (1, (chooseDeparture.tStop - chooseDeparture.tBegin) / 20);
   
   for (int t = chooseDeparture.tBegin; t < chooseDeparture.tStop; t += displayStep) {
      x = X_LEFT + (RECTANGLE_WIDTH/2) + XK * t;
      cairo_move_to (cr, x, Y_BOTTOM + 10);
      snprintf (str, MAX_SIZE_NUMBER,"%d", t); 
      cairo_show_text (cr, str);
   }
   cairo_stroke(cr);

   // Draw duration labels;
   const int durationStep = (int) (chooseDeparture.maxDuration) / 10;
   for (int duration = 0; duration < chooseDeparture.maxDuration ; duration += durationStep) {
      y = Y_BOTTOM - duration * YK;
      cairo_move_to (cr, X_LEFT - 20, y);
      snprintf (str, MAX_SIZE_NUMBER,"%2d", duration); 
      cairo_show_text (cr, str);
      CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr); // Horizontal lines
      cairo_move_to (cr, X_LEFT, y);
      cairo_line_to (cr, X_RIGHT, y);
      cairo_stroke (cr);
      CAIRO_SET_SOURCE_RGB_BLACK(cr);
   }
   cairo_stroke(cr);
}

/*! display the report of simulation for best time departure */
static void simulationReport () { 
   char line [MAX_SIZE_LINE];
   if (! simulationReportExist) {
      printf ("no simulation available\n");
      infoMessage ("No simulation available", GTK_MESSAGE_INFO);
      return;
   }
   snprintf (line, sizeof (line), "Simulation Report. Min Duration: %.2lf hours, Best Departure Time:  %d hours after beginning of Grib", 
      chooseDeparture.minDuration, chooseDeparture.bestTime);

   GtkWidget *simulationWindow = gtk_application_window_new (GTK_APPLICATION (app));
   gtk_window_set_title (GTK_WINDOW(simulationWindow), line);
   gtk_window_set_default_size (GTK_WINDOW(simulationWindow), 1200, 400);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), simulationWindow);
   GtkWidget *drawing_area = gtk_drawing_area_new ();

   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (drawing_area), cb_simulation_report, NULL, NULL);
   gtk_window_set_child (GTK_WINDOW (simulationWindow), drawing_area);
   gtk_window_present (GTK_WINDOW (simulationWindow));   
}

/*! Draw routegram */
static void on_routegram_event (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
#define MAX_VAL_ROUTE 3
   int x, y;
   char str [MAX_SIZE_LINE], strDate [MAX_SIZE_LINE];
   const int LEGEND_X_LEFT = width - 80;
   const int LEGEND_Y_TOP = 10;
   const int X_LEFT = 30;
   const int X_RIGHT = width - 100;
   const int Y_TOP = 20;
   const int Y_BOTTOM = height - 25;
   const int HEAD_Y = 10;
   const int DELTA = 5;
   const int DAY_LG = 10;
   const int XK = (X_RIGHT - X_LEFT) / (route.duration + par.tStep);  
   double u, v, head_x;

   cairo_set_line_width (cr, 1);
   cairo_set_font_size (cr, 12);

   char *libelle [MAX_VAL_ROUTE] = {"Wind", "Gust", "Waves"}; 
   double colors[MAX_VAL_ROUTE][3] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}; // blue red blue
   drawLegend (cr, LEGEND_X_LEFT, LEGEND_Y_TOP, colors, libelle, MAX_VAL_ROUTE);

   // Draw horizontal line wirh arrow
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_move_to (cr, X_LEFT,  Y_BOTTOM);
   cairo_line_to (cr, X_RIGHT, Y_BOTTOM);
   cairo_line_to (cr, X_RIGHT - DELTA, Y_BOTTOM + DELTA); // for arrow
   cairo_stroke (cr);
   cairo_move_to (cr, X_RIGHT, Y_BOTTOM );
   cairo_line_to (cr, X_RIGHT - DELTA, Y_BOTTOM - DELTA);
   cairo_stroke (cr);
   
   // Draw verical line wirh arrow
   cairo_move_to (cr, X_LEFT, Y_BOTTOM);
   cairo_line_to (cr, X_LEFT, Y_TOP);
   cairo_line_to (cr, X_LEFT - DELTA, Y_TOP + DELTA);
   cairo_stroke (cr);
   cairo_move_to (cr, X_LEFT, Y_TOP);
   cairo_line_to (cr, X_LEFT + DELTA, Y_TOP + DELTA);
   cairo_stroke (cr);
   
   // Draw time labels and vertical lines
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 10);
   CAIRO_SET_SOURCE_RGB_BLACK(cr);

   struct tm tmTime;
   memcpy (&tmTime, &startInfo, sizeof (tmTime)); 
   double timeStep = (route.duration <= 120) ? 6 : 12;
   int lastDay = -1;

   for (double i = 0; i <= route.duration; i+= timeStep) {
      x = X_LEFT + XK * i;
      cairo_move_to (cr, x, Y_BOTTOM + 10);
      snprintf (strDate, sizeof (strDate), "%4d/%02d/%02d %02d:%02d", tmTime.tm_year + 1900, tmTime.tm_mon+1, tmTime.tm_mday, tmTime.tm_hour, tmTime.tm_min);
      cairo_show_text (cr, strrchr (strDate, ' ') + 1);  // hour is after space

      if (tmTime.tm_mday != lastDay) {                   // change day
         cairo_move_to (cr, x, Y_BOTTOM + 20);
         strDate [DAY_LG] = '\0';                        // only date
         cairo_show_text (cr, strDate);
         CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr);
         cairo_move_to (cr, x, Y_BOTTOM);
         cairo_line_to (cr, x, Y_TOP);
         cairo_stroke (cr);
         CAIRO_SET_SOURCE_RGB_BLACK(cr);
      }
      lastDay = tmTime.tm_mday;
      tmTime.tm_min += timeStep * 60;
      mktime (&tmTime);
   }
   cairo_stroke(cr);
   
   // vertical arrival line
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   double lastX = X_LEFT + XK * route.duration;
   cairo_move_to (cr, lastX, Y_BOTTOM);
   cairo_line_to (cr, lastX, Y_TOP);
   cairo_stroke(cr);
   showUnicode (cr, DESTINATION_UNICODE, lastX, Y_TOP + 50);
   
   // arrival date
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size (cr, 16);
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route.duration, strDate);
   cairo_move_to (cr, lastX, Y_TOP + 100);
   char *ptHour = strchr (strDate, ' ');
   *ptHour = '\0'; // cut strdate with date and hour
   cairo_show_text (cr, strDate); // the date
   cairo_move_to (cr, lastX , Y_TOP + 120);
   cairo_show_text (cr, ptHour + 1); // the time
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 10);

   // Draw speeds labels and horizontal lines
   cairo_set_font_size (cr, 10);
   const double step = 5;
   double maxMax = MAX (MS_TO_KN * route.maxGust, route.maxTws);
   const int YK = (Y_BOTTOM - Y_TOP) / maxMax;
   for (double speed = step; speed <= maxMax + step; speed += step) {
      y = Y_BOTTOM - YK * speed;
      cairo_move_to (cr, X_LEFT - 20, y);
      snprintf (str, MAX_SIZE_LINE, "%02.0lf", speed);
      cairo_show_text (cr, str);
      CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr); // Horizontal lines
      cairo_move_to (cr, X_LEFT, y);
      cairo_line_to (cr, lastX, y);
      cairo_stroke (cr);
      CAIRO_SET_SOURCE_RGB_BLACK(cr);
   }
   cairo_stroke(cr);

   double lastU, lastV, lastG, lastW, lastTwd, lastTws;
   // find last values at destination
   if (route.destinationReached) {
      findWindGrib (par.pDest.lat, par.pDest.lon, route.t[route.n - 1].time + route.lastStepDuration,\
          &lastU, &lastV, &lastG, &lastW, &lastTwd, &lastTws);
   }
   // draw tws arrows
   int arrowStep = (route.n / 24) + 1;
   for (int i = 0; i < route.n; i += arrowStep) {
      head_x = X_LEFT + XK * i * par.tStep;
      u = -sin (DEG_TO_RAD * route.t[i].twd) * route.t[i].tws / MS_TO_KN;
      v = -cos (DEG_TO_RAD * route.t[i].twd) * route.t[i].tws / MS_TO_KN; 
      arrow (cr, head_x, HEAD_Y, u, v, route.t [i].twd, route.t[i].tws, WIND);
   }

   // draw gust
   if (route.maxGust > 0) { 
      CAIRO_SET_SOURCE_RGB_RED(cr);
      for (int i = 0; i < route.n; i ++) {
         x = X_LEFT + XK * i * par.tStep;
         y = Y_BOTTOM - YK * MAX (MS_TO_KN * route.t[i].g, route.t[i].tws);
         if (i == 0) cairo_move_to (cr, x, y);
         else cairo_line_to (cr, x, y);
      }
   if (route.destinationReached) 
      cairo_line_to (cr, lastX, Y_BOTTOM - YK * MAX (MS_TO_KN * lastG, lastTws));
   cairo_stroke(cr);
   }

   // draw tws 
   CAIRO_SET_SOURCE_RGB_BLUE(cr);
   for (int i = 0; i < route.n; i++) {
      x = X_LEFT + XK * i * par.tStep;
      y = Y_BOTTOM - YK * route.t[i].tws;
      if (i == 0) cairo_move_to (cr, x, y);
      else cairo_line_to (cr, x, y);
   }
   if (route.destinationReached) 
      cairo_line_to (cr, lastX, Y_BOTTOM - YK * lastTws);
   cairo_stroke(cr);
   
   //draw Waves
   if (route.maxWave > 0) { 
      CAIRO_SET_SOURCE_RGB_GREEN(cr);
      for (int i = 0; i < route.n; i ++) {
         x = X_LEFT + XK * i * par.tStep;
         y = Y_BOTTOM - YK * route.t[i].w;
         if (i == 0) cairo_move_to (cr, x, y);
         else cairo_line_to (cr, x, y);
      }
      if (route.destinationReached) 
         cairo_line_to (cr, lastX, Y_BOTTOM - YK * lastW);
      cairo_stroke(cr);
   }

   //draw SOG
   cairo_set_line_width (cr, 5);
   x = X_LEFT;
   y = Y_BOTTOM - YK * route.t[0].sog;
   int motor = route.t[0].motor;
   int amure = route.t[0].amure;
   routeColor (cr, motor, amure);
   cairo_move_to (cr, x, y);
   for (int i = 1; i < route.n; i++) {
      x = X_LEFT + XK * i * par.tStep;
      y = Y_BOTTOM - YK * route.t[i].sog;
      cairo_line_to (cr, x, y);
      if ((route.t[i].motor != motor) || (route.t[i].amure != amure)) {
         cairo_stroke (cr);
         amure = route.t[i].amure;
         motor = route.t[i].motor;
         routeColor (cr, motor, amure);
         cairo_move_to (cr, x, y);
      }
   }
   // last SOG segment to destination
   cairo_line_to (cr, lastX, y);
   cairo_stroke(cr);
}

/*! represent routegram */
static void routeGram () {
   char line [MAX_SIZE_LINE];
   if (route.n <= 0) {
      infoMessage ("No route calculated", GTK_MESSAGE_INFO);
      return;
   }
   snprintf (line, MAX_SIZE_LINE, "Routegram max TWS: %.2lf, max Gust: %.2lf, max Waves: %.2lf, miles: %.2lf",\
      route.maxTws, MAX (route.maxTws, MS_TO_KN * route.maxGust), route.maxWave, route.totDist);

   GtkWidget *routeWindow = gtk_application_window_new (GTK_APPLICATION (app));
   gtk_window_set_title (GTK_WINDOW(routeWindow), line);
   gtk_window_set_default_size (GTK_WINDOW(routeWindow), 1400, 400);
   g_signal_connect(window, "destroy", G_CALLBACK(on_parent_destroy), routeWindow);

   GtkWidget *route_drawing_area = gtk_drawing_area_new ();
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (route_drawing_area), on_routegram_event, NULL, NULL);
   gtk_window_set_child (GTK_WINDOW (routeWindow), route_drawing_area);
   gtk_window_present (GTK_WINDOW (routeWindow));   
}

/*! print the route from origin to destination or best point */
static void rteDump () {
   char buffer [MAX_SIZE_BUFFER];
   if (route.n <= 0)
      infoMessage ("No route calculated", GTK_MESSAGE_INFO);
   else {
      routeToStr (&route, buffer, MAX_SIZE_BUFFER); 
      displayText (1400, 400, buffer, (route.destinationReached) ? "Destination reached" :\
         "Destination unreached. Route to best point");
   }
}

/*! show parameters information */
static void parDump () {
   char str [MAX_SIZE_FILE_NAME];
   writeParam (buildRootName (TEMP_FILE_NAME, str), true, false);
   displayFile (buildRootName (TEMP_FILE_NAME, str), "Parameter Dump", true);
}

/*! show parameters information */
static void parInfo () {
   char str [MAX_SIZE_FILE_NAME];
   displayFile (buildRootName (par.parInfoFileName, str), "Parameter Info", true);
}

/*! Call back point of interest */
static void cbEditPoi (GtkDialog *dialog, gint response_id, gpointer user_data) {
   if (response_id == GTK_RESPONSE_ACCEPT) {
      nPoi = 0;
      nPoi = readPoi (par.poiFileName);
      nPoi += readPoi (par.portFileName);
      gtk_widget_queue_draw (drawing_area);
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! edit poi or port */
static void poiEdit (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   FILE *fs;
   int comportement = GPOINTER_TO_INT (data);
   char line [MAX_SIZE_LINE] = "";
   snprintf (line, sizeof (line), "%s %s\n", par.spreadsheet, (comportement == POI_SEL) ? par.poiFileName: par.portFileName);
   if ((fs = popen (line, "r")) == NULL) {
      fprintf (stderr, "Error in poiEdit: system call: %s\n", line);
      return;
   }
   pclose (fs);
   snprintf (line, sizeof (line), "Confirm loading: %s", (comportement == POI_SEL) ? par.poiFileName: par.portFileName);
   GtkWidget *dialogBox = gtk_dialog_new_with_buttons (line, \
         GTK_WINDOW(window), GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_CANCEL, NULL);
   g_signal_connect_swapped (dialogBox, "response", G_CALLBACK (cbEditPoi), dialogBox);
   gtk_window_present (GTK_WINDOW (dialogBox));
}

/*!  Function to draw the speedometer */
static void draw_speedometer(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
   MyGpsData  *dashboard = (MyGpsData *)data;
   double sog = (dashboard->OK) ? dashboard->sog : 0;
   if (isnan (sog)) sog = 0;

   double xc = width / 2.0;
   double yc = height / 2.0;
   
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_arc(cr, xc, yc, DASHBOARD_RADIUS, 0, 2 * G_PI);
   cairo_stroke(cr);

   // Draw the speed inside a rectangle at the center
   char speed_str[10];
   cairo_set_font_size (cr, 10);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   snprintf(speed_str, sizeof(speed_str), "%.2lfKn", sog);
   cairo_rectangle(cr, xc - 25, yc + 15, 60, 20);
   cairo_fill(cr);
   CAIRO_SET_SOURCE_RGB_WHITE(cr);
   cairo_move_to(cr, xc - 15, yc + 30);
   cairo_show_text(cr, speed_str);

   // Draw speed marks
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_set_line_width(cr, 2);
   for (int i = DASHBOARD_MIN_SPEED; i <= DASHBOARD_MAX_SPEED; ++i) {
      double angle = (i - DASHBOARD_MIN_SPEED) * (G_PI / (DASHBOARD_MAX_SPEED - DASHBOARD_MIN_SPEED)) - G_PI;
      double x1 = xc + (DASHBOARD_RADIUS - 10) * cos(angle);
      double y1 = yc + (DASHBOARD_RADIUS - 10) * sin(angle);
      double x2 = xc + DASHBOARD_RADIUS * cos(angle);
      double y2 = yc + DASHBOARD_RADIUS * sin(angle);
      cairo_move_to(cr, x1, y1);
      cairo_line_to(cr, x2, y2);
      cairo_stroke(cr);

      // Draw the speed numbers
      char speed_text[4];
      snprintf(speed_text, sizeof(speed_text), "%d", i);
      cairo_text_extents_t extents;
      cairo_text_extents(cr, speed_text, &extents);
      double tx = xc + (DASHBOARD_RADIUS - 20) * cos(angle) - extents.width / 2;
      double ty = yc + (DASHBOARD_RADIUS - 20) * sin(angle) + extents.height / 2;
      cairo_move_to(cr, tx, ty);
      cairo_show_text(cr, speed_text);
   }

   // Draw the needle
   CAIRO_SET_SOURCE_RGB_RED(cr);
   cairo_set_line_width(cr, 3);
   double needle_angle = (sog - DASHBOARD_MIN_SPEED) * (G_PI / (DASHBOARD_MAX_SPEED - DASHBOARD_MIN_SPEED)) - G_PI;
   double needle_x = xc + (DASHBOARD_RADIUS - 20) * cos(needle_angle);
   double needle_y = yc + (DASHBOARD_RADIUS - 20) * sin(needle_angle);
   cairo_move_to(cr, xc, yc);
   cairo_line_to(cr, needle_x, needle_y);
   cairo_stroke(cr);
}


/*!  Function to draw the compass */
static void draw_compass(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
   MyGpsData  *dashboard = (MyGpsData *)data;
   double cog = (dashboard->OK) ? dashboard->cog : 0;
   if (isnan (cog)) cog = 0;
   double angle_rad = cog * DEG_TO_RAD;

   cairo_set_line_width(cr, 2.0);

   double xc = width / 2.0;
   double yc = height / 2.0;
   
   // Draw exterior circle for compass
   cairo_arc(cr, xc, yc, DASHBOARD_RADIUS, 0, 2 * G_PI);
   cairo_stroke(cr);

   // Draw main  directions (N, E, S, W) and secondary (NE, SE, SW, NW)
   cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size(cr, 20);

   struct {
      const char *label;
      double angle;
   } directions[] = {
      {"N", 0}, {"NE", 45}, {"E", 90}, {"SE", 135},
      {"S", 180}, {"SW", 225}, {"W", 270}, {"NW", 315}
   };

   for (int i = 0; i < 8; i++) {
      double direction_angle = directions[i].angle * DEG_TO_RAD;
      double label_x = xc + (DASHBOARD_RADIUS - 30) * sin(direction_angle) - 10;
      double label_y = yc - (DASHBOARD_RADIUS - 30) * cos(direction_angle) + 10;
      cairo_move_to(cr, label_x, label_y);
      cairo_show_text(cr, directions[i].label);
   }

   // Draw graduations
   cairo_set_line_width(cr, 1.0);
   for (int i = 0; i < 360; i += 10) {
      double tick_angle = i * DEG_TO_RAD;
      double inner_radius = DASHBOARD_RADIUS - (i % 30 == 0 ? 20 : 10);
      double tick_x1 = xc + DASHBOARD_RADIUS * sin(tick_angle);
      double tick_y1 = yc - DASHBOARD_RADIUS * cos(tick_angle);
      double tick_x2 = xc + inner_radius * sin(tick_angle);
      double tick_y2 = yc - inner_radius * cos(tick_angle);
      cairo_move_to(cr, tick_x1, tick_y1);
      cairo_line_to(cr, tick_x2, tick_y2);
      cairo_stroke(cr);
   }

   // Draw the speed inside a rectangle at the center
   char cog_str[10];
   cairo_set_font_size (cr, 10);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   snprintf(cog_str, sizeof(cog_str), "%.0lf°", cog);
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_rectangle(cr, xc - 25, yc + 15, 60, 20);
   cairo_fill(cr);
   CAIRO_SET_SOURCE_RGB_WHITE(cr);
   cairo_move_to(cr, xc - 15, yc + 30);
   cairo_show_text(cr, cog_str);

   double needle_length = DASHBOARD_RADIUS * 0.8;
   double needle_x = xc + needle_length * sin(angle_rad);
   double needle_y = yc - needle_length * cos(angle_rad);

   // Draw basis for arrow
   cairo_set_source_rgb(cr, 0, 0, 0);
   cairo_arc(cr, xc, yc, 10, 0, 2 * G_PI);
   cairo_fill(cr);

   // Draw line for arrow
   CAIRO_SET_SOURCE_RGB_RED(cr);
   cairo_set_line_width(cr, 4.0);
   cairo_move_to(cr, xc, yc);
   cairo_line_to(cr, needle_x, needle_y);
   cairo_stroke(cr);

   // Draw opposite side of arrow
   double opposite_x = xc - needle_length * sin(angle_rad);
   double opposite_y = yc + needle_length * cos(angle_rad);
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_move_to(cr, xc, yc);
   cairo_line_to(cr, opposite_x, opposite_y);
   cairo_stroke(cr);
}

/*! draw hour */
static void drawHourPos (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
   char strLat [MAX_SIZE_LINE], strLon [MAX_SIZE_LINE];
   char strDate [MAX_SIZE_DATE], str [MAX_SIZE_LINE];
   PangoLayout *layout;
   PangoFontDescription *font_desc;
   double xc = width / 2.0;
   double yc = height / 2.0;
   //time_t now = time(NULL);
   //struct tm *utc_time = gmtime(&now);
   //strftime(strDate, sizeof(strDate), "%Y-%m-%d %H:%M:%S UTC", utc_time);*/

   epochToStr (my_gps_data.time, true, strDate, sizeof (strDate));

   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   cairo_arc(cr, xc, yc, DASHBOARD_RADIUS * 0.8, 0, 2 * G_PI);
   cairo_fill(cr);
   CAIRO_SET_SOURCE_RGB_WHITE (cr);
   cairo_arc(cr, xc, yc, DASHBOARD_RADIUS * 0.8 - 2, 0, 2 * G_PI);
   cairo_stroke (cr);

   layout = pango_cairo_create_layout(cr);
   pango_layout_set_text(layout, strchr (strDate, ' ') + 1, -1);

   // Set font description
   font_desc = pango_font_description_from_string("DSEG7 Classic 12");
   pango_layout_set_font_description(layout, font_desc);
   pango_font_description_free(font_desc);

   CAIRO_SET_SOURCE_RGB_GREEN (cr);
   cairo_move_to (cr, xc-40, yc - 20);
   pango_cairo_update_layout(cr, layout);
   pango_cairo_show_layout(cr, layout);

   g_object_unref(layout);

   // position
   CAIRO_SET_SOURCE_RGB_WHITE (cr);
   cairo_set_font_size (cr, 12);
   
   cairo_move_to (cr, xc-38, yc + 25);
   snprintf (str, sizeof (str), "%s", latToStr (my_gps_data.lat, par.dispDms, strLat));
   cairo_show_text (cr, str);
   
   cairo_move_to (cr, xc-38, yc + 40);
   snprintf (str, sizeof (str), "%s", lonToStr (my_gps_data.lon, par.dispDms, strLon));
   cairo_show_text (cr, str);
}

/*! Function to draw the text zone */
static void draw_text_zone (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
   char str [MAX_SIZE_LINE];
   double nextLat, nextLon;
   int nextWp = (wayPoints.n == 0) ? -1 : 0; // to be revaluated
  
   double xc = width / 2.0;
   double yc = height / 2.0;
   
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   cairo_arc(cr, xc, yc, DASHBOARD_RADIUS * 0.8, 0, 2 * G_PI);
   cairo_fill(cr);
   CAIRO_SET_SOURCE_RGB_WHITE (cr);
   cairo_arc(cr, xc, yc, DASHBOARD_RADIUS * 0.8 - 2, 0, 2 * G_PI);
   cairo_stroke (cr);

   cairo_move_to (cr, xc, yc);
   cairo_set_font_size (cr, 12);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   CAIRO_SET_SOURCE_RGB_WHITE(cr);

   if (! my_gps_data.OK) return;

   // calculate route to Way Point
   cairo_move_to (cr, xc-50, yc - 15);
   nextLat = (nextWp == -1) ? par.pDest.lat : wayPoints.t [nextWp].lat;
   nextLon = (nextWp == -1) ? par.pDest.lon : wayPoints.t [nextWp].lon;
   snprintf (str, sizeof (str), "Ortho Route: %.0lf°",
      // directCap (my_gps_data.lat, my_gps_data.lon, nextLat, nextLon),
      orthoCap (my_gps_data.lat, my_gps_data.lon, nextLat, nextLon)); 
   cairo_show_text (cr, str);

   // calculate route to SailRoute
   if (route.n > 0) {
      int i = findIndexInRouteNow ();
      cairo_move_to (cr, xc-50, yc + 15);
      if (i < 0)
         snprintf (str, sizeof (str), "Sail Route: NA");
      else if (i >= route.n)
         snprintf (str, sizeof (str), "Sail Route: N/A");
      else
         snprintf (str, sizeof (str), "Sail Route: %.0lf°", route.t[i].oCap);
      cairo_show_text (cr, str);
   }
}

/*! update on timeout */
static gboolean updateDashboard(gpointer label) {
   if (window == NULL) return FALSE;
   gtk_widget_queue_draw (widgetDashboard.hourPosZone);
   gtk_widget_queue_draw (widgetDashboard.speedometer);
   gtk_widget_queue_draw (widgetDashboard.compass);
   gtk_widget_queue_draw (widgetDashboard.textZone);
   return TRUE;
}

/*! callback destroy dashboard */
static void on_dashboard_window_destroy (GtkWidget *widget, gpointer data) {
   if (widgetDashboard.timeoutId != 0) {
      g_source_remove (widgetDashboard.timeoutId);
      widgetDashboard.timeoutId = 0;
   }
}

/*! show Dashboard */
static void dashboard () {
   const int D_WIDTH = 250;
   const int D_HEIGHT = 250;
   const int D_TIMER = 1;
       
   GtkWidget *dashboardWindow = gtk_application_window_new (GTK_APPLICATION (app));
   gtk_window_set_title(GTK_WINDOW(dashboardWindow), "");
   gtk_window_set_default_size (GTK_WINDOW(dashboardWindow), 4 * D_WIDTH, D_HEIGHT);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), dashboardWindow);
   g_signal_connect (dashboardWindow, "destroy", G_CALLBACK(on_dashboard_window_destroy), NULL);

   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_window_set_child(GTK_WINDOW(dashboardWindow), box);

   // Hour Zone
   widgetDashboard.hourPosZone = gtk_drawing_area_new();
   gtk_widget_set_size_request (widgetDashboard.hourPosZone, D_WIDTH, D_HEIGHT);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA(widgetDashboard.hourPosZone), drawHourPos, &my_gps_data, NULL);
   gtk_box_append (GTK_BOX(box), widgetDashboard.hourPosZone);

   // Speedometer
   widgetDashboard.speedometer = gtk_drawing_area_new();
   gtk_widget_set_size_request(widgetDashboard.speedometer, D_WIDTH, D_HEIGHT);
   gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA (widgetDashboard.speedometer), draw_speedometer, &my_gps_data, NULL);
   gtk_box_append(GTK_BOX(box), widgetDashboard.speedometer);

   // Compass
   widgetDashboard.compass = gtk_drawing_area_new();
   gtk_widget_set_size_request (widgetDashboard.compass, D_WIDTH, D_HEIGHT);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA(widgetDashboard.compass), draw_compass, &my_gps_data, NULL);
   gtk_box_append (GTK_BOX(box), widgetDashboard.compass);

   // text Zone
   widgetDashboard.textZone = gtk_drawing_area_new();
   gtk_widget_set_size_request (widgetDashboard.textZone, D_WIDTH, D_HEIGHT);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA(widgetDashboard.textZone), draw_text_zone, &my_gps_data, NULL);
   gtk_box_append (GTK_BOX(box), widgetDashboard.textZone);

   // UTC Time
   widgetDashboard.timeoutId = g_timeout_add_seconds (D_TIMER, updateDashboard, NULL); // Update every second

   gtk_window_present (GTK_WINDOW (dashboardWindow));   
}

/*! show GPS information */
static void gpsDump () {
   char line [MAX_SIZE_LINE], strLat [MAX_SIZE_LINE], strLon [MAX_SIZE_LINE], str [MAX_SIZE_LINE];

   GtkWidget *gpsWindow = gtk_application_window_new (GTK_APPLICATION (app));
   gtk_window_set_title (GTK_WINDOW(gpsWindow), "GPS Information");

   GtkWidget *grid = gtk_grid_new();   
   /*if (!my_gps_data.OK) {
      infoMessage ("No GPS position available", GTK_MESSAGE_WARNING);
      return;
   }*/
   gtk_window_set_child (GTK_WINDOW (gpsWindow), (GtkWidget *) grid);

   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);

   snprintf (line, sizeof (line), "%s", (my_gps_data.OK) ? "YES" : "NO");
   lineReport (grid, 0, "audio-input-microphone-symbolic", "GPS Available", line);
   
   snprintf (line, sizeof (line), "%s %s\n", \
      latToStr (my_gps_data.lat, par.dispDms, strLat), lonToStr (my_gps_data.lon, par.dispDms, strLon));
   lineReport (grid, 2, "network-workgroup-symbolic", "Position", line);

   snprintf (line, sizeof (line), "%.2f°\n", my_gps_data.cog);
   lineReport (grid, 4, "view-refresh-symbolic", "COG", line);

   snprintf (line, sizeof (line), "%.2f Knots\n", my_gps_data.sog);
   lineReport (grid, 6, "media-playlist-consecutive-symbolic", "SOG", line);

   snprintf (line, sizeof (line), "%.2f meters\n", my_gps_data.alt);
   lineReport (grid, 8, "airplane-mode-symbolic", "Altitude", line);

   snprintf (line, sizeof (line), "%d\n", my_gps_data.status);
   lineReport (grid, 10, "dialog-information-symbolic", "Status", line);

   snprintf (line, sizeof (line), "%d\n", my_gps_data.nSat);
   lineReport (grid, 12, "preferences-system-network-symbolic", "Number of satellites", line);

   snprintf (line, sizeof (line), "%s\n", epochToStr (my_gps_data.time, true, str, sizeof (str)));
   lineReport (grid, 14, "alarm-symbolic", "UTC", line);

   gtk_window_present (GTK_WINDOW (gpsWindow));
}

/*! launch help HTML file */
static void help () {
   FILE *fs;
   char command [MAX_SIZE_LINE ];
   snprintf (command, MAX_SIZE_LINE, "%s %s", par.webkit, par.helpFileName);
   printf ("%s \n", command);
   if ((fs = popen (command, "r")) == NULL) {
      fprintf (stderr, "Error in Help: popen call: %s\n", command);
      return;
   }
   pclose (fs);
}

/*! display info on the program */
static void helpInfo () {
   const char *authors[2] = {PROG_AUTHOR, NULL};
   char str [MAX_SIZE_LINE];
   char strVersion [MAX_SIZE_LINE];
   char strGps [MAX_SIZE_LINE];
  
   snprintf (strVersion, sizeof (strVersion), "%s\nGTK version: %d.%d.%d\nGlib version: %d.%d.%d\nCairo Version:%s\n \
      GPS %s\nECCODES version from ECMWF: %s\n Curl version: %s\n Shapefil version: %s\n Compilation date: %s\n", 
      PROG_VERSION,
      gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
      // webkit_get_major_version (), webkit_get_minor_version (),webkit_get_micro_version (), // ATT GTK4 CHANGE
      GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION,
      CAIRO_VERSION_STRING, 
      gpsInfo (par.gpsType, strGps, MAX_SIZE_LINE),
      ECCODES_VERSION_STR, LIBCURL_VERSION, "1.56", 
      __DATE__);
   GtkWidget *p_about_dialog = gtk_about_dialog_new ();
   gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (p_about_dialog), strVersion);
   gtk_about_dialog_set_program_name  (GTK_ABOUT_DIALOG (p_about_dialog), PROG_NAME);
   gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (p_about_dialog), authors);
   gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (p_about_dialog), PROG_WEB_SITE);
   GdkPixbuf *p_logo = gdk_pixbuf_new_from_file (buildRootName (PROG_LOGO, str), NULL);
   GdkTexture *texture_logo = gdk_texture_new_for_pixbuf (p_logo);
   gtk_about_dialog_set_logo (GTK_ABOUT_DIALOG (p_about_dialog), (GdkPaintable *) texture_logo); // ATT GTK4 CHANGE
   gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG (p_about_dialog), DESCRIPTION);
   gtk_window_set_modal (GTK_WINDOW(p_about_dialog), true); // ATT GTK4 CHANGE
   gtk_window_present (GTK_WINDOW (p_about_dialog)); // ATT GTK4 CHANGE
}

/*! callback for URL change */
static void url_entry_changed (GtkEditable *editable, gpointer user_data) {
   gchar *url = (gchar *) user_data;
   const gchar *text = gtk_editable_get_text (editable);
   g_strlcpy (url, text, MAX_SIZE_BUFFER);
   printf ("url: %s\n", url);
}

/*! check if readGrib is terminated (wind) */
static gboolean readGribCheck (gpointer data) {
   // printf ("readGribCheck\n");
   if (readGribRet == -1) { // not terminated
      return TRUE;
   }
   g_source_remove (gribReadTimeout); // timer stopped
   spinnerWindowDestroy ();
   if (readGribRet == 0) {
      infoMessage ("Error in readGribCheck (wind)", GTK_MESSAGE_ERROR);
   }
   else {
      theTime = 0;
      par.constWindTws = 0;
      initDispZone ();
      updatedColors = false;
      titleUpdate ();
   }
   gtk_widget_queue_draw (drawing_area);
   destroySurface ();
   return TRUE;
}

/*! check if readGrib is terminated  (current) */
static gboolean readCurrentGribCheck (gpointer data) {
   gtk_widget_queue_draw (drawing_area);
   if (readCurrentGribRet == -1) { // not terminated
      return TRUE;
   }
   g_source_remove (currentGribReadTimeout); // timer stopped
   spinnerWindowDestroy ();
   if (readCurrentGribRet == 0) {
      infoMessage ("Error in readCurrentGribCheck (current)", GTK_MESSAGE_ERROR);
   }
   return TRUE;
}
   
/*! open selected file */
static void loadGribFile (int type, char *fileName) {
   iFlow = type;
   if (type == WIND) { // wind
      printf ("loadGribFile Wind: %s\n", fileName);
      strncpy (par.gribFileName, fileName, MAX_SIZE_FILE_NAME - 1);
      readGribRet = -1; // Global variable
      g_thread_new("readGribThread", readGrib, &iFlow);
      spinner ("Grib File decoding", " ");
      gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readGribCheck, NULL);
   }
   else {                 
      strncpy (par.currentGribFileName, fileName, MAX_SIZE_FILE_NAME - 1);
      readCurrentGribRet = -1; // Global variable
      g_thread_new("readCurrentGribThread", readGrib, &iFlow);
      spinner ("Current Grib File decoding", " ");
      currentGribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readCurrentGribCheck, NULL);
   }
}

/*! getMeteoConsult */
void * getMeteoConsult (void *data) {
   if (! curlGet (urlRequest.url, urlRequest.outputFileName)) {
      readGribRet = 0;
      return NULL;
   }
   strncpy (par.gribFileName, urlRequest.outputFileName, MAX_SIZE_FILE_NAME - 1);
   if (readGribAll (par.gribFileName, &zone, WIND))
      readGribRet = 1;
   else readGribRet = 0;
   return NULL;
}

/*! Response for URL change */
static void urlWindowResponse (GtkDialog *dialog, gint response_id, gpointer user_data) {
   printf ("Response URL win: %d %d\n", response_id, GTK_RESPONSE_ACCEPT);
   printf ("urlRequest.url: %s\n", urlRequest.url);
   printf ("urlRequest.outputFileName: %s\n", urlRequest.outputFileName);
   if (response_id == GTK_RESPONSE_ACCEPT) {
      strncat (urlRequest.outputFileName, strrchr (urlRequest.url, '/'), MAX_SIZE_LINE - strlen (urlRequest.outputFileName));
      printf ("urlRequest.outputFileName: %s\n", urlRequest.outputFileName);
      readGribRet = -1; // Global variable
      g_thread_new("readGribThread", getMeteoConsult, NULL);
      spinner ("Grib meteoConsultDownload and decoding", " ");
      gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readGribCheck, NULL);
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}
   
/*! build meteoconsult url then download it and load the grib file */ 
static void urlDownloadGrib (gpointer user_data) {
   int index = GPOINTER_TO_INT(user_data);
   if (menuGrib != NULL) popoverFinish (menuGrib);
   urlRequest.urlType = typeFlow;
   snprintf (urlRequest.outputFileName, sizeof (urlRequest.outputFileName), (typeFlow == WIND) ? "%sgrib": "%scurrentgrib", par.workingDir);
   buildMeteoUrl (urlRequest.urlType, index, urlRequest.url);
   printf ("url: %s\n", urlRequest.url);

   GtkWidget *dialogBox = gtk_dialog_new_with_buttons ("Meteo consult URL", GTK_WINDOW(window), GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_ACCEPT,
                                           "_Cancel", GTK_RESPONSE_CANCEL, NULL);
   GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialogBox));

   int min_width = MAX (300, strlen(urlRequest.url) * 10);
   gtk_widget_set_size_request(dialogBox, min_width, -1);

   GtkEntryBuffer *buffer = gtk_entry_buffer_new(urlRequest.url, -1);
   GtkWidget *url_entry = gtk_entry_new_with_buffer(buffer);

   g_signal_connect_swapped(dialogBox, "response", G_CALLBACK (urlWindowResponse), dialogBox);
   g_signal_connect(url_entry, "changed", G_CALLBACK (url_entry_changed), urlRequest.url);

   gtk_box_append(GTK_BOX(content_area), url_entry);
   gtk_window_present (GTK_WINDOW (dialogBox));
}

/*! update NOAA URL */
static void updateNoaaUrl(GtkSpinButton *spin_button, gpointer user_data) {
   DialogData *data = (DialogData *)user_data;
   data->bottomLat = gtk_spin_button_get_value_as_int(data->bottomLatSpin);
   data->leftLon = gtk_spin_button_get_value_as_int(data->leftLonSpin);
   data->topLat = gtk_spin_button_get_value_as_int(data->topLatSpin);
   data->rightLon = gtk_spin_button_get_value_as_int(data->rightLonSpin);
   data->timeStep = gtk_spin_button_get_value_as_int(data->timeStepSpin);
   data->timeMax = gtk_spin_button_get_value_as_int(data->timeMaxSpin);

   buildNoaaUrl (data->url, data->topLat, data->leftLon, data->bottomLat, data->rightLon, 0);
   
   gtk_text_buffer_set_text (data->urlBuffer, data->url, -1);
}
      
/*! Download all NOOA files,concat them in a bigFile and load it */
static void *getGribNoaaAll (void *user_data) {
   DialogData *data = (DialogData *)user_data;
   time_t now = time (NULL);
   struct tm *pTime = gmtime (&now);
   char fileName [MAX_SIZE_URL], bigFile [MAX_SIZE_LINE], prefix [MAX_SIZE_LINE];
   char strDate [MAX_SIZE_DATE];
   strftime (strDate, MAX_SIZE_DATE, "%F", pTime); 
   snprintf (prefix, sizeof (prefix), "%sgrib/interNoaa-", par.workingDir);
   printf ("Step: %d\n", data->timeStep);

   for (int i = 0; i <= (data->timeMax) * 24; i += (data->timeStep)) {
      buildNoaaUrl (data->url, data->topLat, data->leftLon, data->bottomLat, data->rightLon, i);
      printf ("URL is: %s\n", data->url);
      snprintf (fileName, sizeof (fileName), "%s%03d", prefix, i);
      printf ("outputFileName: %s\n\n", fileName);
      if (! curlGet (data->url, fileName)) {
         readGribRet = 0;
         return NULL;
      }
   }
   snprintf (bigFile, sizeof (bigFile), "%sgrib/NOAA-%s-%02d-%03d.grb", par.workingDir, strDate, data->timeStep, data->timeMax * 24);
   if (concat (prefix, data->timeStep, data->timeMax * 24, bigFile)) {
      printf ("bigFile: %s\n", bigFile);
      strncpy (par.gribFileName, bigFile, MAX_SIZE_FILE_NAME - 1);
      if (readGribAll (par.gribFileName, &zone, WIND))
         readGribRet = 1;
      else readGribRet = 0;
   }
   else
      readGribRet = 0;
   return NULL;
}

/*! Manage response to NOAA dialog box */
static void on_noaa_dialog_response (GtkDialog *dialog, gint response_id, gpointer user_data) {
   if (response_id == GTK_RESPONSE_ACCEPT) {
      DialogData *data = (DialogData *)user_data;
      readGribRet = -1; // Global variable
      g_thread_new("readGribThread", getGribNoaaAll, data);
      spinner ("Grib NOAA Download and decoding", " ");
      gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readGribCheck, NULL);
   }
   gtk_window_destroy(GTK_WINDOW(dialog));
}

/*! Dialog box to Manage NOAA URL and lauch downloadinf*/
static void gribNoaa (GSimpleAction *action, GVariant *parameter, gpointer *userData) {
   GtkWidget *label;
   DialogData *data = g_malloc(sizeof(DialogData));
   // const int TOP_LAT= 55, BOTTOM_LAT = 40, LEFT_LON =-20, RIGHT_LON = 0;
   const int TIME_STEP = 3, TIME_MAX = 2;
   //data->bottomLat = BOTTOM_LAT, data->topLat = TOP_LAT, data->leftLon = LEFT_LON, data->rightLon = RIGHT_LON;
   data->bottomLat = zone.latMin, data->topLat = zone.latMax, data->leftLon = zone.lonLeft, data->rightLon = zone.lonRight;
   data->timeStep = TIME_STEP, data->timeMax = TIME_MAX;

   GtkWidget *dialogBox = gtk_dialog_new_with_buttons("NOAA URL", GTK_WINDOW(window), GTK_DIALOG_MODAL,
           "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_CANCEL, NULL);

   GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialogBox));
   GtkWidget *grid = gtk_grid_new();
   gtk_box_append(GTK_BOX(content_area), grid);

   buildNoaaUrl (data->url, data->topLat, data->leftLon, data->bottomLat, data->rightLon, 0);

   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);

   // Two firts lines : bottomLat, leftLon, topLat, rightLon
   label = gtk_label_new ("Bottom Lat");
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
   data->bottomLatSpin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(-90, 90, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->bottomLatSpin), data->bottomLat);
   gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(data->bottomLatSpin), 1, 0, 1, 1);

   label = gtk_label_new("Top Lat");
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
   data->topLatSpin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(-90, 90, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->topLatSpin), data->topLat);
   gtk_grid_attach(GTK_GRID(grid), label, 2, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(data->topLatSpin), 3, 0, 1, 1);

   label = gtk_label_new("Left Lon");
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
   data->leftLonSpin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(-180, 180, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->leftLonSpin), data->leftLon);
   gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(data->leftLonSpin), 1, 1, 1, 1);

   label = gtk_label_new("Right Lon");
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
   data->rightLonSpin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(-180, 180, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->rightLonSpin), data->rightLon);
   gtk_grid_attach(GTK_GRID(grid), label, 2, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(data->rightLonSpin), 3, 1, 1, 1);

   // Third line: timeStep, timeMax
   label = gtk_label_new("Time Step");
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
   data->timeStepSpin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 24, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->timeStepSpin), TIME_STEP);
   gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(data->timeStepSpin), 1, 2, 1, 1);

   label = gtk_label_new("Forecat Time in Days");
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
   data->timeMaxSpin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, MAX_N_DAYS_WEATHER, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->timeMaxSpin), TIME_MAX);
   gtk_grid_attach(GTK_GRID(grid), label, 2, 2, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(data->timeMaxSpin), 3, 2, 1, 1);

   // Multi-line zone to display URL
   label = gtk_label_new("NOAA URL");
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
   gtk_grid_attach(GTK_GRID(grid), label, 0, 3, 1, 1);

   GtkWidget *text_view = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
   data->urlBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
   gtk_text_buffer_set_text(data->urlBuffer, data->url, -1);
   gtk_grid_attach(GTK_GRID(grid), text_view, 1, 3, 3, 2);

   // spin button connection
   g_signal_connect(data->bottomLatSpin, "value-changed", G_CALLBACK(updateNoaaUrl), data);
   g_signal_connect(data->leftLonSpin, "value-changed", G_CALLBACK(updateNoaaUrl), data);
   g_signal_connect(data->topLatSpin, "value-changed", G_CALLBACK(updateNoaaUrl), data);
   g_signal_connect(data->rightLonSpin, "value-changed", G_CALLBACK(updateNoaaUrl), data);
   g_signal_connect(data->timeStepSpin, "value-changed", G_CALLBACK(updateNoaaUrl), data);
   g_signal_connect(data->timeMaxSpin, "value-changed", G_CALLBACK(updateNoaaUrl), data);

   g_signal_connect(dialogBox, "response", G_CALLBACK(on_noaa_dialog_response), data);
   gtk_window_present (GTK_WINDOW (dialogBox));
}

/*! callback for openGrib */
static void on_open_grib_response (GtkDialog *dialog, int response) {
   if (response == GTK_RESPONSE_ACCEPT) {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
      g_autoptr(GFile) file = gtk_file_chooser_get_file (chooser);
      if (file != NULL) {
         gchar *fileName = g_file_get_path(file);
         if (fileName != NULL) {
            printf ("Fichier sélectionné : %s\n", fileName);
            loadGribFile (typeFlow, fileName);
            g_free(fileName);
         }
     }
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! Select grib file and read grib file, fill gribData */
static void openGrib (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   typeFlow = GPOINTER_TO_INT (data);
   char directory [MAX_SIZE_DIR_NAME];
   snprintf (directory, sizeof (directory), (typeFlow == WIND) ? "%sgrib": "%scurrentgrib", par.workingDir);
   GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open Grib", GTK_WINDOW (window), GTK_FILE_CHOOSER_ACTION_OPEN,
                       "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

   GFile *gDirectory = g_file_new_for_path(directory);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), gDirectory, NULL);
   g_object_unref(gDirectory);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Grib Files");
   gtk_file_filter_add_pattern(filter, "*.gr*");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
   g_object_unref (filter);

   gtk_window_present (GTK_WINDOW (dialog));
   g_signal_connect (dialog, "response", G_CALLBACK (on_open_grib_response), NULL);
}

/*! callback for openPolar */
static void on_open_polar_response (GtkDialog *dialog, int response) {
   if (response == GTK_RESPONSE_ACCEPT) {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
      g_autoptr(GFile) file = gtk_file_chooser_get_file (chooser);
      if (file != NULL) {
         gchar *fileName = g_file_get_path(file);
         if (fileName != NULL) {
            if (strstr (fileName, "polwave.csv") == NULL) {
               readPolar (fileName, &polMat);
               strncpy (par.polarFileName, fileName, sizeof (par.polarFileName) - 1);
               polarType = POLAR;
               polarDraw (); 
            }
            else {
               readPolar (fileName, &wavePolMat);
               strncpy (par.wavePolFileName, fileName, sizeof (par.wavePolFileName) - 1);
               polarType = WAVE_POLAR;
               polarDraw (); 
            }
            g_free(fileName);
         }
     }
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! Select polar file and read polar file, fill polMat */
static void openPolar () {
   char directory [MAX_SIZE_DIR_NAME];
   snprintf (directory, sizeof (directory), "%spol", par.workingDir);
   GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Polar",  GTK_WINDOW (window), GTK_FILE_CHOOSER_ACTION_OPEN, \
                       "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

   gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);

   GFile *gDirectory = g_file_new_for_path(directory);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), gDirectory, NULL);
   g_object_unref(gDirectory);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Polar Files");
   gtk_file_filter_add_pattern(filter, "*.pol");
   gtk_file_filter_add_pattern(filter, "*.csv");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
   g_object_unref (filter);
   
   gtk_window_present (GTK_WINDOW (dialog));
   g_signal_connect (dialog, "response", G_CALLBACK (on_open_polar_response), NULL);
}

/*! call back for save Scenario */
static void on_save_scenario_response (GtkDialog *dialog, int response) {
   if (response == GTK_RESPONSE_ACCEPT) {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
      g_autoptr(GFile) file = gtk_file_chooser_get_file (chooser);
      if (file != NULL) {
         gchar *fileName = g_file_get_path(file);
         if (fileName != NULL) {
            printf ("Fichier sélectionné : %s\n", fileName);
            printf ("Mail Pw Exist: %d\n", par.storeMailPw);
            writeParam (fileName, false, par.storeMailPw);
            g_free(fileName);
         }
     }
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! Write parameter file and initialize context */
static void saveScenario () {
   char directory [MAX_SIZE_DIR_NAME];
   snprintf (directory, sizeof (directory), "%spar", par.workingDir);
   GtkWidget *dialog = gtk_file_chooser_dialog_new("Save As", GTK_WINDOW (window), GTK_FILE_CHOOSER_ACTION_SAVE,
                       "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
   GFile *gDirectory = g_file_new_for_path(directory);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), gDirectory, NULL);
   g_object_unref(gDirectory);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name (filter, "Parameter Files");
   gtk_file_filter_add_pattern(filter, "*.par");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
   GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);

   // Définit un nom de fichier par défaut
   gtk_file_chooser_set_current_name (chooser, "new_file.par");
   gtk_window_present (GTK_WINDOW (dialog));
   g_signal_connect (dialog, "response", G_CALLBACK (on_save_scenario_response), NULL);
}

/*! Make initialization following parameter file load */
static void initScenario () {
   char str [MAX_SIZE_LINE];
   int poiIndex = -1;
   double unusedTime;

   if (par.gribFileName [0] != '\0') {
      iFlow = WIND;
      readGrib (&iFlow);
      if (readGribRet == 0) {
         fprintf (stderr, "Error in initScenario: Unable to read grib file: %s\n ", par.gribFileName);
         return;
      }
      printf ("Grib loaded    : %s\n", par.gribFileName);
      printf ("Grib DateTime0 : %s\n", gribDateTimeToStr (zone.dataDate [0], zone.dataTime [0], str, sizeof (str)));
      theTime = zone.timeStamp [0];
      updatedColors = false; 
      initDispZone ();
   }

   if (par.currentGribFileName [0] != '\0') {
      iFlow = CURRENT;
      readGrib (&iFlow);
      printf ("Cur grib loaded: %s\n", par.currentGribFileName);
      printf ("Grib DateTime0 : %s\n", gribDateTimeToStr (currentZone.dataDate [0], currentZone.dataTime [0], str, sizeof (str)));
   }
   if (readPolar (par.polarFileName, &polMat))
      printf ("Polar loaded   : %s\n", par.polarFileName);
      
   if (readPolar (par.wavePolFileName, &wavePolMat))
      printf ("Polar loaded   : %s\n", par.wavePolFileName);
   
   nPoi = 0;
   if (par.poiFileName [0] != '\0')
      nPoi += readPoi (par.poiFileName);
   if (par.portFileName [0] != '\0')
   nPoi += readPoi (par.portFileName);
   
   if ((poiIndex = findPoiByName (par.pOrName, &par.pOr.lat, &par.pOr.lon)) != -1)
      strncpy (par.pOrName, tPoi [poiIndex].name, sizeof (par.pOrName) - 1);
   else par.pOrName [0] = '\0';
   if ((poiIndex = findPoiByName (par.pDestName, &par.pDest.lat, &par.pDest.lon)) != -1)
      strncpy (par.pDestName, tPoi [poiIndex].name, sizeof (par.pDestName) - 1);
   else par.pDestName [0] = '\0';

   if (par.pOr.id != -1) // not initialized
      findLastTracePoint (par.traceFileName, &par.pOr.lat, &par.pOr.lon, &unusedTime);
   
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   initWayPoints ();
}

/*! Call back editScenario */
static void cbEditScenario (GtkDialog *dialog, gint response_id, gpointer user_data) {
   if (response_id == GTK_RESPONSE_ACCEPT) {
      readParam (parameterFileName);
      initScenario ();
      if (readGribRet == 0) {
         infoMessage ("Error in readgrib", GTK_MESSAGE_ERROR);
      }
      gtk_widget_queue_draw (drawing_area);
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! Edit parameter file  */
static void editScenario () {
   FILE *fs;
   char line [MAX_SIZE_LINE] = "";
   snprintf (line, sizeof (line), "%s %s\n", par.editor, parameterFileName);
   if ((fs = popen (line, "r")) == NULL) {
      fprintf (stderr, "Error in editScenario: popen call: %s\n", line);
      return;
   }
   pclose (fs);
   snprintf (line, sizeof (line), "Confirm loading: %s", parameterFileName);
   GtkWidget *dialogBox = gtk_dialog_new_with_buttons (line, \
         GTK_WINDOW(window), GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_CANCEL, NULL);
   g_signal_connect_swapped (dialogBox, "response", G_CALLBACK (cbEditScenario), dialogBox);
   gtk_window_present (GTK_WINDOW (dialogBox));
}

/* call back for openSecnario */
static void on_open_scenario_response (GtkDialog *dialog, int response) {
   if (response == GTK_RESPONSE_ACCEPT) {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
      g_autoptr(GFile) file = gtk_file_chooser_get_file (chooser);
      if (file != NULL) {
         gchar *fileName = g_file_get_path(file);
         if (fileName != NULL) {
            strncpy (parameterFileName, fileName, MAX_SIZE_FILE_NAME);
            readParam (fileName);
            initScenario ();
            if (readGribRet == 0)
               infoMessage ("Error in readgrib", GTK_MESSAGE_ERROR);
            g_free(fileName);
            gtk_widget_queue_draw (drawing_area);
         }
     }
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! Read parameter file and initialize context */
static void openScenario () {
   char directory [MAX_SIZE_DIR_NAME];
   snprintf (directory, sizeof (directory), "%spar", par.workingDir);
   GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open Parameters",  GTK_WINDOW (window), GTK_FILE_CHOOSER_ACTION_OPEN, \
                       "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
   GFile *gDirectory = g_file_new_for_path(directory);
   gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), gDirectory, NULL);
   g_object_unref(gDirectory);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "Parameter Files");
   gtk_file_filter_add_pattern(filter, "*.par");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
   g_object_unref (filter);

   gtk_window_present (GTK_WINDOW (dialog));
   g_signal_connect (dialog, "response", G_CALLBACK (on_open_scenario_response), NULL);
}

/*! provides meta information about grib file, called by gribInfo */
static void gribInfoDisplay (const char *fileName, Zone *zone) {
   const size_t MAX_VISIBLE_SHORTNAME = 10;
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
     if (dicTab [i].id == zone->centreId) 
        strncpy (centreName, dicTab [i].name, sizeof (centreName) - 1);
         
   snprintf (line, sizeof (line), "Centre ID: %ld %s   Ed. number: %ld", zone->centreId, centreName, zone->editionNumber);

   GtkWidget *gribWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (gribWindow), line);
   //gtk_widget_set_size_request(dialog, 400, -1);

   GtkWidget *grid = gtk_grid_new();   
   gtk_window_set_child (GTK_WINDOW (gribWindow), (GtkWidget *) grid);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);

   newDate (zone->dataDate [0], zone->dataTime [0]/100, strTmp);
   lineReport (grid, 0, "document-open-recent", "Date From", strTmp);
   
   newDate (zone->dataDate [0], zone->dataTime [0]/100 + zone->timeStamp [zone->nTimeStamp -1], strTmp);
   lineReport (grid, l+=2, "document-open-recent", "Date To", strTmp);

   snprintf (line, sizeof (line), "%d", zone->nMessage);
   lineReport (grid, l+=2, "zoom-original-symbolic", "Nb. Messages", line);

   snprintf (line, sizeof (line), "%ld", zone->stepUnits);
   lineReport (grid, l+=2, "document-page-setup", "Step Unit", line);

   snprintf (line, sizeof (line), "%ld", zone->numberOfValues);
   lineReport (grid, l+=2, "document-page-setup-symbolic", "Nb. of Values", line);

   snprintf (line, sizeof (line), "From: %s, %s To: %s %s", latToStr (zone->latMax, par.dispDms, strLat0), lonToStr (zone->lonLeft, par.dispDms, strLon0),
      latToStr (zone->latMin, par.dispDms, strLat1), lonToStr (zone->lonRight, par.dispDms, strLon1));
   lineReport (grid, l+=2, "network-workgroup-symbolic", "Zone ", line);

   snprintf (line, sizeof (line), "%.3f° - %.3f°\n", zone->latStep, zone->lonStep);
   lineReport (grid, l+=2, "dialog-information-symbolic", "Lat Step - Lon Step", line);

   snprintf (line, sizeof (line), "%ld - %ld\n", zone->nbLat, zone->nbLon);
   lineReport (grid, l+=2 , "preferences-desktop-locale-symbolic", "Nb. Lat - Nb. Lon", line);

   timeStep = zone->timeStamp [1] - zone->timeStamp [0];
   // check if regular
   for (size_t i = 1; i < zone->nTimeStamp - 1; i++)
      if ((zone->timeStamp [i] - zone->timeStamp [i-1]) != timeStep) {
         isTimeStepOK = false;
         // printf ("timeStep: %ld other timeStep: %ld\n", timeStep, zone->timeStamp [i] - zone->timeStamp [i-1]);
      }

   snprintf (strTmp, sizeof (strTmp), "TimeStamp List of %s %zu", (isTimeStepOK)? "regular" : "UNREGULAR", zone->nTimeStamp);
   if (zone->nTimeStamp < 8 || ! isTimeStepOK) {
      strcpy (buffer, "[ ");
      for (size_t k = 0; k < zone->nTimeStamp; k++) {
         if ((k > 0) && ((k % 20) == 0)) 
            strncat (buffer, "\n", MAX_SIZE_BUFFER - strlen (buffer));
         snprintf (str, sizeof (str), "%ld ", zone->timeStamp [k]);
         if (strlen (buffer) > MAX_SIZE_BUFFER - 10)
            break;
         strncat (buffer, str, MAX_SIZE_BUFFER - strlen (buffer));
      }
      strncat (buffer, "]\n", MAX_SIZE_BUFFER - strlen (buffer));
   }
   else {
      snprintf (buffer, sizeof (buffer), "[%ld, %ld, ..%ld]\n", zone->timeStamp [0], zone->timeStamp [1], zone->timeStamp [zone->nTimeStamp - 1]);
   }
   lineReport (grid, l+=2, "view-list-symbolic", strTmp, buffer);
   
   strcpy (line, "[ ");
   for (size_t k = 0; k < MIN (zone->nShortName -1, MAX_VISIBLE_SHORTNAME); k++) {
      snprintf (str, sizeof (str), "%s ", zone->shortName [k]);
      strncat (line, str, MAX_SIZE_LINE - strlen (line));
   }
   if (zone->nShortName > 0) {
      if (zone->nShortName -1 <  MAX_VISIBLE_SHORTNAME) {
         snprintf (str, sizeof (str), "%s ]\n", zone->shortName [zone->nShortName -1]);
         strncat (line, str, MAX_SIZE_LINE - strlen (line));
      }
      else
         strcat (line, ", ...]");
   }
   snprintf (strTmp, sizeof (strTmp), "List of %zu shortnames", zone->nShortName);
   lineReport (grid, l+=2, "non-starred-symbolic", strTmp, line);

   snprintf (line, sizeof (line), "%s\n", (zone->wellDefined) ? (zone->allTimeStepOK) ? "Well defined" : "All TimeSteps are not defined" : "Undefined");
   lineReport (grid, l+=2 , (zone->wellDefined) ? "weather-clear" : "weather-showers", "Zone is", line);
   
   lineReport (grid, l+=2 , "mail-attachment-symbolic", "Grib File Name", fileName);
   
   formatThousandSep (str1, getFileSize (fileName));
    
   lineReport (grid, l+=2, "document-properties-symbolic", "Grib File size", str1);

   if ((zone->nDataDate > 1) || (zone->nDataTime > 1)) {
      snprintf (line, sizeof (line), "Date: %zu, Time: %zu\n", zone->nDataDate, zone->nDataTime);
      lineReport (grid, l+=2, "software-update-urgent-symbolic", "Warning number of", line);
   }

   gtk_window_present (GTK_WINDOW (gribWindow));
}

/*! provides meta information about grib file */
static void gribInfo (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   typeFlow = GPOINTER_TO_INT (data);
   if (typeFlow == WIND) {
      if (zone.nbLat == 0)
         infoMessage ("No wind data grib available", GTK_MESSAGE_ERROR);
      else 
         gribInfoDisplay (par.gribFileName, &zone);
      
   }
   else { // current
      if (currentZone.nbLat == 0)
         infoMessage ("No current data grib available", GTK_MESSAGE_ERROR);
      else 
         gribInfoDisplay (par.currentGribFileName, &currentZone);
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
   char newPw [MAX_SIZE_NAME];
   dollarSubstitute (par.mailPw, newPw, MAX_SIZE_NAME);
   
   snprintf (command, sizeof (command), "%s %s%s %s", par.imapScript, par.workingDir, (provider == SAILDOCS_CURR) ? "currentgrib" : "grib", newPw); 
   if ((fp = popen (command, "r")) == NULL) {
      fprintf (stderr, "Error in mailGribRead. popen command: %s\n", command);
      gribRequestRunning = false;
      spinnerWindowDestroy ();
      return TRUE;
   }
   while (fgets (line, sizeof(line)-1, fp) != NULL) {
      n += 1;
      printf ("line: %s\n", line);
      strncat (buffer, line, MAX_SIZE_BUFFER - strlen (buffer));
   }
   pclose(fp);
   if (n > 0) {
      g_source_remove (gribMailTimeout); // timer stopped
      if (((fileName = strstr (buffer, "File: /") + 6) != NULL) && (n > 2)) {
         printf ("Mail Response: %s\n", buffer);
         while (isspace (*fileName)) fileName += 1;
         if ((end = strstr (fileName, " ")) != NULL)
            *(end) = '\0';
         spinnerWindowDestroy ();
         loadGribFile ((provider == SAILDOCS_CURR) ? CURRENT: WIND, fileName);
      }
      else infoMessage ("Grib File not found", GTK_MESSAGE_ERROR); 
      spinnerWindowDestroy ();
      gribRequestRunning = false;
   }
   return TRUE;
}

/*! return maxTimeRange of GFS model based on timeStep and resolution */
static int maxTimeRange () {
   switch (provider) {
   case SAILDOCS_GFS: case MAILASAIL:
      if (par.gribResolution <= 0.25)
         return 120;
      else if (par.gribResolution < 1.0)
         return (par.gribTimeStep < 6) ? 192 : 240;
      else return (par.gribTimeStep < 6) ? 192 : (par.gribTimeStep < 12) ? 240 : 384;
   case SAILDOCS_ECMWF:
      return (par.gribTimeStep < 6) ? 144 : 240;
   case SAILDOCS_ICON:
      return 120;
   case SAILDOCS_ARPEGE:
      return 96;
   case SAILDOCS_AROME:
      return 48;
   case SAILDOCS_CURR:
      return 192;
   default:
      fprintf (stderr, "Error in maxTimeRange: provider not supported: %d\n", provider);
      return 120;
   }
}

/*! return number of values x n message wich give an evaluation
   of grib file size in bytes */
static int evalSize (int nShortName) {
   int nValue = ((abs (mailRequestPar.lat2-mailRequestPar.lat1) / par.gribResolution) + 1) * \
      ((abs (mailRequestPar.lon2-mailRequestPar.lon1) / par.gribResolution) + 1);
   int nMessage = nShortName * (1 + (par.gribTimeMax / par.gribTimeStep));
   return nMessage * nValue;
}

/* update timemax and size value of the mail request box */
static void updateMailRequestBox () {
   char str [MAX_SIZE_LINE] = "";
   if (spin_button_time_max != NULL) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin_button_time_max), par.gribTimeMax / 24);
      formatThousandSep (str, evalSize (6));
      gtk_label_set_text(GTK_LABEL (val_size_eval), str);
   }
}

/*! Callback Time Step value */
static void time_step_changed(GtkSpinButton *spin_button, gpointer user_data) {
   par.gribTimeStep = gtk_spin_button_get_value_as_int(spin_button);
   par.gribTimeMax = maxTimeRange ();
   updateMailRequestBox ();
}

/*! Callback Time Max value */
static void time_max_changed (GtkSpinButton *spin_button, gpointer user_data) {
   par.gribTimeMax = gtk_spin_button_get_value_as_int(spin_button) * 24;
   par.gribTimeMax = MIN (par.gribTimeMax, maxTimeRange ());
   updateMailRequestBox ();
}

/*! Callback resolution */
static void resolution_changed (GtkSpinButton *spin_button, gpointer user_data) {
   par.gribResolution = gtk_spin_button_get_value (spin_button);
   par.gribTimeMax = maxTimeRange ();
   updateMailRequestBox ();
}

/*! Callback model combo box indice */
static void model_combo_changed (GtkComboBox *widget, gpointer user_data) {
   provider = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
   par.gribTimeMax = maxTimeRange ();
   updateMailRequestBox ();
}

/*! Callback to set up mail password  */
static void password_entry_changed (GtkEditable *editable, gpointer user_data) {
   const gchar *text = gtk_editable_get_text (editable);
   g_strlcpy (par.mailPw, text, MAX_SIZE_NAME);
}

/*! action when closing mail request box */
static void mailRequestBoxResponse (GtkDialog *dialog, gint response_id, gpointer user_data) {
   FILE *fs;
   char buffer [MAX_SIZE_BUFFER] = "";
   char *ptBuffer, *ptBuffer1;
   char command [MAX_SIZE_LINE * 2] = "";
   char newPw [MAX_SIZE_NAME];
   dollarSubstitute (par.mailPw, newPw, MAX_SIZE_NAME);
   if (response_id == GTK_RESPONSE_ACCEPT) {
      if (smtpGribRequestPython (provider, mailRequestPar.lat1, mailRequestPar.lon1, mailRequestPar.lat2, mailRequestPar.lon2, buffer, MAX_SIZE_BUFFER)) { 
         ptBuffer = strchr (buffer, ' ');          // delete first words 
         ptBuffer1 = strchr (ptBuffer + 1, ' ');   // delete second word 
         spinner ("Waiting for grib Mail response", ptBuffer1);
         snprintf (command, sizeof (command), "%s %s", par.imapToSeen, newPw);
         if ((fs = popen (command, "r")) == NULL) {
            infoMessage ("Error running imapToSeen script", GTK_MESSAGE_ERROR);;
            return;
         }
         pclose (fs);
         // printf ("ImapToSeen: command:%s\n", command);
         gribRequestRunning = true;
         gribMailTimeout = g_timeout_add (GRIB_TIME_OUT, mailGribRead, NULL);
      }
      else {
         infoMessage ("Error SMTP request Python, check connectivity and mail Password", GTK_MESSAGE_ERROR);
      }
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! mail request dialog box */
static void mailRequestBox (double lat1, double lon1, double lat2, double lon2) {
   char strLat1 [MAX_SIZE_NAME] = "", strLon1 [MAX_SIZE_NAME] = "";
   char strLat2 [MAX_SIZE_NAME] = "", strLon2 [MAX_SIZE_NAME] = "";
   char str [MAX_SIZE_LINE] = "",  str1 [MAX_SIZE_LINE] = "";
   mailRequestPar.lat1 = lat1;
   mailRequestPar.lon1 = lon1;
   mailRequestPar.lat2 = lat2;
   mailRequestPar.lon2 = lon2;

   snprintf (str, sizeof (str), "%s, %s to  %s, %s", latToStr (lat1, par.dispDms, strLat1), lonToStr (lon1, par.dispDms, strLon1),
         latToStr (lat2, par.dispDms, strLat2), lonToStr (lon2, par.dispDms, strLon2));

   GtkWidget *separator = gtk_separator_new  (GTK_ORIENTATION_HORIZONTAL);

   GtkWidget* dialogBox = gtk_dialog_new_with_buttons ("Launch mail Grib request", \
      GTK_WINDOW (window), GTK_DIALOG_MODAL, "OK", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_CANCEL, NULL);
   
   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialogBox));
   gtk_widget_set_size_request (dialogBox, 400, -1);
   GtkWidget *grid = gtk_grid_new();   
   gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
   
   GtkWidget *label_zone = gtk_label_new ("Zone");
   gtk_label_set_xalign(GTK_LABEL(label_zone), 0);
   gtk_grid_attach(GTK_GRID (grid), label_zone,                 0, 0, 1, 1);
   GtkWidget *label_buffer = gtk_label_new (str);
   gtk_grid_attach(GTK_GRID (grid), label_buffer,               1, 0, 3, 1);
   
   GtkWidget *label_model = gtk_label_new ("Model"); 
   GtkListStore *liststore = gtk_list_store_new (1, G_TYPE_STRING);
   // Add lements to list
   
   GtkTreeIter iter;
   for (int i = 0;  i < N_PROVIDERS; i++) {
      gtk_list_store_append (liststore, &iter);
      gtk_list_store_set (liststore, &iter, 0, providerTab [i].libelle, -1);
   }

   // Create combobox and define its model
   GtkWidget *model_combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
   g_object_unref(liststore);

   // Create rendred for text in combobox
   GtkCellRenderer *myRenderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(model_combo_box), myRenderer, TRUE);
   gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(model_combo_box), myRenderer, "text", 0, NULL);
   gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo_box), provider);

   g_signal_connect(model_combo_box, "changed", G_CALLBACK (model_combo_changed), NULL);

   gtk_label_set_xalign(GTK_LABEL(label_model), 0);
   gtk_grid_attach(GTK_GRID(grid), label_model,          0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), model_combo_box,      1, 1, 3, 1);

   // Create Time Step and Time Max
   GtkWidget *label_resolution = gtk_label_new("Resolution");
   // gtk_adjustment_new(gdouble value, gdouble lower, gdouble upper, gdouble step_increment, gdouble page_increment, gdouble page_size);
   GtkAdjustment *adjustment = gtk_adjustment_new (0.5, 0.25, 1.0, 0.25, 1.0, 0.0);

   // Crete GtkSpinButton with GtkAdjustment
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

   strcpy (str, "Forecast time in days");
   GtkWidget *label_time_max = gtk_label_new (str);
   spin_button_time_max = gtk_spin_button_new_with_range (1, MAX_N_DAYS_WEATHER, 1); // ATT : Global variable
   gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin_button_time_max), par.gribTimeMax / 24);
   g_signal_connect (spin_button_time_max, "value-changed", G_CALLBACK(time_max_changed), NULL);
   gtk_label_set_xalign(GTK_LABEL (label_time_max), 0);
   gtk_grid_attach(GTK_GRID (grid), label_time_max,             0, 4, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), spin_button_time_max,       1, 4, 1, 1);

   GtkWidget *label_size_eval = gtk_label_new ("Number of values ");
   gtk_label_set_xalign(GTK_LABEL (label_size_eval), 0);

   formatThousandSep (str1, evalSize (6));
   val_size_eval = gtk_label_new (str1);
   gtk_widget_set_halign (val_size_eval, GTK_ALIGN_START);

   gtk_grid_attach(GTK_GRID (grid), label_size_eval, 0, 5, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), val_size_eval,   1, 5, 1, 1);
   
   gtk_grid_attach(GTK_GRID (grid), separator, 0, 6, 3, 1);
   
   if (par.mailPw [0] == '\0') { // password needed if does no exist 
      GtkWidget *password_label = gtk_label_new ("Mail Password");
      gtk_label_set_xalign(GTK_LABEL (password_label), 0);
      GtkWidget *password_entry = gtk_entry_new ();
      gtk_entry_set_visibility (GTK_ENTRY (password_entry), FALSE);  
      gtk_entry_set_invisible_char (GTK_ENTRY (password_entry), '*');
      g_signal_connect (password_entry, "changed", G_CALLBACK (password_entry_changed), par.mailPw);
      gtk_grid_attach (GTK_GRID (grid), password_label, 0, 7, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), password_entry, 1, 7, 1, 1);
   }

   g_signal_connect_swapped (dialogBox, "response", G_CALLBACK (mailRequestBoxResponse), dialogBox);
   gtk_box_append(GTK_BOX (content_area), grid);
   gtk_window_present (GTK_WINDOW (dialogBox));
}

/*! For testing */
static void testFunction () {
   char buffer [MAX_SIZE_BUFFER];
   printf ("test\n");
   checkGribToStr (buffer, MAX_SIZE_BUFFER);
   displayText (750, 400, buffer, "Grib Wind Check");
   // printGrib (&zone, tGribData [WIND]);
}

/*! Display label in col c and ligne l of tab */
static void labelCreate (GtkWidget *tab, char *name, int c, int l) {
   GtkWidget *label = gtk_label_new (name);
   gtk_grid_attach(GTK_GRID(tab), label, c, l, 1, 1);
   gtk_widget_set_margin_start(label, 10);
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
}

/*! Lat update for change */
static void onLatChange (GtkWidget *widget, gpointer data) {
   double *x_ptr = (double *) data;
   *x_ptr = getCoord (gtk_editable_get_text (GTK_EDITABLE (widget)));
   gtk_widget_queue_draw (drawing_area);
}

/*! Origin Lon update for change */
static void onLonChange (GtkWidget *widget, gpointer data) {
   double *x_ptr = (double *) data;
   *x_ptr = getCoord (gtk_editable_get_text (GTK_EDITABLE (widget)));
   *x_ptr = lonCanonize (*x_ptr);
   gtk_widget_queue_draw (drawing_area);
}

/*! Callback Radio button for Wind colors */ 
static void radio_button_Colors (GtkToggleButton *button, gpointer user_data) {
   par.showColors = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw (drawing_area); // affiche le tout
   destroySurface ();
}

/*! Radio button for Wind Colar */
static GtkWidget *createRadioButtonColors (char *name, GtkWidget *tab_display, GtkWidget *from, int i) {
   GtkWidget *choice = gtk_check_button_new_with_label (name);
   g_object_set_data (G_OBJECT(choice), "index", GINT_TO_POINTER(i));
   g_signal_connect (choice, "toggled", G_CALLBACK(radio_button_Colors), NULL);
   gtk_grid_attach (GTK_GRID(tab_display), choice,           i+1, 1, 1, 1);
   if (from != NULL)
      gtk_check_button_set_group ((GtkCheckButton *) choice, (GtkCheckButton *) from); 
   if (i == par.showColors)
      gtk_check_button_set_active ((GtkCheckButton *) choice, TRUE);
   return choice;
}

/*! Callback wind representation */
static void radio_button_Wind_Disp (GtkToggleButton *button, gpointer user_data) {
   par.windDisp = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw (drawing_area);
}

/*! Radio button for Wind disp */
static GtkWidget *createRadioButtonWindDisp (char *name, GtkWidget *tab_display, GtkWidget *from, int i) {
   GtkWidget *choice = gtk_check_button_new_with_label (name);
   g_object_set_data (G_OBJECT(choice), "index", GINT_TO_POINTER(i));
   g_signal_connect (choice, "toggled", G_CALLBACK (radio_button_Wind_Disp), NULL);
   gtk_grid_attach (GTK_GRID(tab_display), choice,           i+1, 2, 1, 1);
   if (from != NULL)
      gtk_check_button_set_group ((GtkCheckButton *) choice, (GtkCheckButton *) from); 
   if (i == par.windDisp)
      gtk_check_button_set_active ((GtkCheckButton *) choice, TRUE);
   return choice;
}

/*! CallBack for disp choice Avr or Gust or Rain or Pressure */
static void on_combo_box_disp_changed (GtkComboBoxText *combo_box, gpointer user_data) {
   par.averageOrGustDisp = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_box));
   gtk_widget_queue_draw (drawing_area); // Draw all
   destroySurface ();
}

/*! CallBack for Isochrone choice */
static void on_combo_box_Isoc_changed (GtkComboBoxText *combo_box, gpointer user_data) {
   par.style = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_box));
   gtk_widget_queue_draw (drawing_area); // Draw all
}

/*! CallBack for DMS choice */
static void on_combo_box_DMS_changed (GtkComboBoxText *combo_box, gpointer user_data) {
   par.dispDms = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_box));
   statusBarUpdate ();
}

/*! Callback activated or not  */
static void on_checkbox_toggled (GtkWidget *checkbox, gpointer user_data) {
   gboolean *flag = (gboolean *)user_data;
   *flag = gtk_check_button_get_active ((GtkCheckButton *) checkbox);
   gtk_widget_queue_draw (drawing_area);
}

/*! Callback for poi visibility change level */
static void on_level_poi_visible_changed (GtkScale *scale, gpointer label) {
   par.maxPoiVisible = gtk_range_get_value(GTK_RANGE(scale));
   gchar *value_str = g_strdup_printf("POI: %d", par.maxPoiVisible);
   gtk_label_set_text(GTK_LABEL(label), value_str);
   g_free(value_str);
   gtk_widget_queue_draw (drawing_area);
}

/*! Callback for speed display change  */
static void on_tempo_display_changed (GtkScale *scale, gpointer label) {
   par.speedDisp = gtk_range_get_value(GTK_RANGE(scale)) ;
   gchar *value_str = g_strdup_printf("Display Speed: %d", par.speedDisp);
   gtk_label_set_text(GTK_LABEL(label), value_str);
   g_free(value_str);
   changeAnimation ();
}

/*! Translate double to entry */
static GtkWidget *doubleToEntry (double x) {
   char str [MAX_SIZE_NAME];
   snprintf (str, sizeof (str), "%.2lf", x);
   return gtk_entry_new_with_buffer(gtk_entry_buffer_new(str, -1)); // Create and return the entry with buffer
}

/*! Translate lat to entry */
static GtkWidget *latToEntry (double lat) {
   char str [MAX_SIZE_NAME];
   latToStr (lat, par.dispDms, str);
   return gtk_entry_new_with_buffer(gtk_entry_buffer_new(str, -1)); // Create and return the entry with buffer
}

/*! Translate lon to entry */
static GtkWidget *lonToEntry (double lon) {
   char str [MAX_SIZE_NAME];
   lonToStr (lon, par.dispDms, str);
   return gtk_entry_new_with_buffer(gtk_entry_buffer_new(str, -1)); // Create and return the entry with buffer
}

/*! double update for change */
static void doubleUpdate (GtkWidget *widget, gpointer data) {
   double *x_ptr = (double *)data;
   *x_ptr = strtod (gtk_editable_get_text(GTK_EDITABLE(widget)), NULL);
}

/*! wind Twd update for change */
static void windTwdUpdate (GtkWidget *widget, gpointer data) {
   par.constWindTwd = strtod (gtk_editable_get_text(GTK_EDITABLE(widget)), NULL);
   gtk_widget_queue_draw  (drawing_area); 
}

/*! wind Tws update for change */
static void windTwsUpdate (GtkWidget *widget, gpointer data) {
   par.constWindTws = strtod (gtk_editable_get_text(GTK_EDITABLE(widget)), NULL);
   if (par.constWindTws != 0)
	   initZone (&zone);
   gtk_widget_queue_draw  (drawing_area); 
}

/*! Change parameters and pOr and pDest coordinates */ 
static void change () {
   GtkWidget *entry_origin_lat, *entry_origin_lon;
   GtkWidget *entry_dest_lat, *entry_dest_lon;
   GtkWidget *entry_start_time;
   GtkWidget *entry_x_wind;
   GtkWidget *entry_wind_twd, *entry_wind_tws;
   GtkWidget *entry_current_twd, *entry_current_tws;
   GtkWidget *entry_wave;
   GtkWidget *entry_sog, *entry_threshold;
   GtkWidget *entry_max_wind;
   GtkWidget *entry_day_efficiency, *entry_night_efficiency;

   // Create dialog box
   GtkWidget *changeWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW(changeWindow), "change");

   // Create notebook
   GtkWidget *notebook = gtk_notebook_new();
   gtk_window_set_child (GTK_WINDOW (changeWindow), notebook);

   // "Display" notebook
   GtkWidget *tab_display = gtk_grid_new();
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_display, gtk_label_new("Display"));
   gtk_widget_set_halign(GTK_WIDGET(tab_display), GTK_ALIGN_START);
   gtk_widget_set_valign(GTK_WIDGET(tab_display), GTK_ALIGN_START);
   gtk_grid_set_row_spacing(GTK_GRID(tab_display), 20);
   gtk_grid_set_column_spacing(GTK_GRID(tab_display), 5);

   // "Parameter" notebook
   GtkWidget *tab_param = gtk_grid_new();
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_param, gtk_label_new("Parameter"));
   
   gtk_grid_set_row_spacing(GTK_GRID(tab_param), 5);
   gtk_grid_set_column_spacing(GTK_GRID(tab_param), 5);
   
   // "Technical" notebook
   GtkWidget *tab_tec = gtk_grid_new();
   if (par.techno)
      gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab_tec, gtk_label_new("Technical"));
   
   gtk_grid_set_row_spacing(GTK_GRID(tab_tec), 5);
   gtk_grid_set_column_spacing(GTK_GRID(tab_tec), 5);

   // Create origin
   entry_origin_lat = latToEntry (par.pOr.lat);
   entry_origin_lon = lonToEntry (par.pOr.lon);
   labelCreate (tab_param, "Origin. Lat",                  0, 0);
   gtk_grid_attach(GTK_GRID(tab_param), entry_origin_lat,  1, 0, 1, 1);
   labelCreate (tab_param, "Lon",                          0, 1);
   gtk_grid_attach (GTK_GRID(tab_param), entry_origin_lon, 1, 1, 1, 1);
   g_signal_connect (entry_origin_lat, "changed", G_CALLBACK (onLatChange), &par.pOr.lat);
   g_signal_connect (entry_origin_lon, "changed", G_CALLBACK (onLonChange), &par.pOr.lon);

   // Crate destination
   entry_dest_lat = latToEntry (par.pDest.lat);
   entry_dest_lon = lonToEntry (par.pDest.lon);
   labelCreate (tab_param, "Dest. Lat",                    0, 2);
   gtk_grid_attach (GTK_GRID(tab_param), entry_dest_lat,   1, 2, 1, 1);
   labelCreate (tab_param, "Lon",                          0, 3);
   gtk_grid_attach( GTK_GRID(tab_param), entry_dest_lon,   1, 3, 1, 1);
   g_signal_connect (entry_dest_lat, "changed", G_CALLBACK (onLatChange), &par.pDest.lat);
   g_signal_connect (entry_dest_lon, "changed", G_CALLBACK (onLonChange), &par.pDest.lon);

   // Create xWind
   labelCreate (tab_param, "xWind",                        0, 4);
   entry_x_wind = doubleToEntry (par.xWind);
   gtk_grid_attach (GTK_GRID (tab_param), entry_x_wind,    1, 4, 1, 1);
   g_signal_connect (entry_x_wind, "changed", G_CALLBACK (doubleUpdate), &par.xWind);

   // Create max Wind
   labelCreate (tab_param, "Max Wind",                     0, 5);
   entry_max_wind = doubleToEntry (par.maxWind);
   gtk_grid_attach(GTK_GRID(tab_param), entry_max_wind,    1, 5, 1, 1);
   g_signal_connect (entry_max_wind, "changed", G_CALLBACK (doubleUpdate), &par.maxWind);

   // Create penalty
   labelCreate (tab_param, "Virement de bord",             0, 6);
   GtkWidget *spin_penalty0 = gtk_spin_button_new_with_range (0, 60, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_penalty0), par.penalty0);
   gtk_grid_attach(GTK_GRID(tab_param), spin_penalty0,     1, 6, 1, 1);
   g_signal_connect (spin_penalty0, "value-changed", G_CALLBACK (intSpinUpdate), &par.penalty0);

   labelCreate (tab_param, "Empannage",                    0, 7);
   GtkWidget *spin_penalty1 = gtk_spin_button_new_with_range (0, 60, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_penalty1), par.penalty1);
   gtk_grid_attach(GTK_GRID(tab_param), spin_penalty1,     1, 7, 1, 1);
   g_signal_connect (spin_penalty1, "value-changed", G_CALLBACK (intSpinUpdate), &par.penalty1);

   // Create motorSpeed, threshold
   labelCreate (tab_param, "Motor Speed          ",        0, 8);
   entry_sog = doubleToEntry (par.motorSpeed);
   gtk_grid_attach(GTK_GRID(tab_param), entry_sog,         1, 8, 1, 1);
   g_signal_connect (entry_sog, "changed", G_CALLBACK (doubleUpdate), &par.motorSpeed);

   labelCreate (tab_param, "Threshold for Motor",          0, 9);
   entry_threshold = doubleToEntry (par.threshold);
   gtk_grid_attach(GTK_GRID(tab_param), entry_threshold,   1, 9, 1, 1);
   g_signal_connect (entry_threshold, "changed", G_CALLBACK (doubleUpdate), &par.threshold);

   // Create day_efficiency night_efficiency
   labelCreate (tab_param, "Day Efficiency",                    0, 10);
   entry_day_efficiency = doubleToEntry (par.dayEfficiency);
   gtk_grid_attach(GTK_GRID(tab_param), entry_day_efficiency,   1, 10, 1, 1);
   g_signal_connect (entry_day_efficiency, "changed", G_CALLBACK (doubleUpdate), &par.dayEfficiency);

   labelCreate (tab_param, "Night Efficiency",                  0, 11);
   entry_night_efficiency = doubleToEntry (par.nightEfficiency);
   gtk_grid_attach(GTK_GRID(tab_param), entry_night_efficiency, 1, 11, 1, 1);
   g_signal_connect (entry_night_efficiency, "changed", G_CALLBACK (doubleUpdate), &par.nightEfficiency);

   // create time Step
   labelCreate (tab_param, "Time Step",                       0, 12);
   GtkWidget *spin_tStep = gtk_spin_button_new_with_range (1, 24, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_tStep), (int) par.tStep);
   gtk_grid_attach(GTK_GRID(tab_param), spin_tStep,           1, 12, 1, 1);
   g_signal_connect (spin_tStep, "value-changed", G_CALLBACK (intSpinUpdate), &par.tStep);

   // Crate cog step and cog range
   labelCreate (tab_param, "Cog Step",                   0, 13);
   GtkWidget *spin_cog = gtk_spin_button_new_with_range (1, 20, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_cog), par.cogStep);
   gtk_grid_attach(GTK_GRID(tab_param), spin_cog,        1, 13, 1, 1);
   g_signal_connect (spin_cog, "value-changed", G_CALLBACK (intSpinUpdate), &par.cogStep);

   labelCreate (tab_param, "Cog Range",                  0, 14);
   GtkWidget *spin_range = gtk_spin_button_new_with_range (50, 150, 5);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_range), par.rangeCog);
   gtk_grid_attach(GTK_GRID(tab_param), spin_range,      1, 14, 1, 1);
   g_signal_connect (spin_range, "value-changed", G_CALLBACK (intSpinUpdate), &par.rangeCog);

   // Create startTime 
   labelCreate (tab_tec, "Start Time in hours",          0, 0);
   entry_start_time = doubleToEntry (par.startTimeInHours);
   gtk_grid_attach(GTK_GRID (tab_tec), entry_start_time, 1, 0, 1, 1);
   g_signal_connect (entry_start_time, "changed", G_CALLBACK (doubleUpdate),&par.startTimeInHours);
    
   // Create Opt
   labelCreate (tab_tec, "Opt",                          0, 1);
   GtkWidget *spin_opt = gtk_spin_button_new_with_range (0, 4, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_opt), par.opt);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_opt,          1, 1, 1, 1);
   g_signal_connect (spin_opt, "value-changed", G_CALLBACK (intSpinUpdate), &par.opt);
   
   // Create Max Isoc
   labelCreate (tab_tec, "Max Isoc",                     0, 2);
   GtkWidget *spin_max_iso = gtk_spin_button_new_with_range(0, MAX_N_ISOC, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_max_iso), par.maxIso);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_max_iso,      1, 2, 1, 1);
   g_signal_connect (spin_max_iso, "value-changed", G_CALLBACK (intSpinUpdate), &par.maxIso);

   // Crate JFactor
   labelCreate (tab_tec, "jFactor",                       0, 3);
   GtkWidget *spin_jFactor = gtk_spin_button_new_with_range (0, 1000, 10);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_jFactor), par.jFactor);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_jFactor,       1, 3, 1, 1);
   g_signal_connect (spin_jFactor, "value-changed", G_CALLBACK (intSpinUpdate), &par.jFactor);
   
   // Create k Factor and N sector
   labelCreate (tab_tec, "k Factor",                    0, 4);
   GtkWidget *spin_kFactor = gtk_spin_button_new_with_range(0, 4, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_kFactor), par.kFactor);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_kFactor,     1, 4, 1, 1);
   g_signal_connect (spin_kFactor, "value-changed", G_CALLBACK (intSpinUpdate), &par.kFactor);

   labelCreate (tab_tec, "N sectors",                   0, 5);
   GtkWidget *spin_n_sectors = gtk_spin_button_new_with_range(0, 1000, 10);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_n_sectors), par.nSectors);
   gtk_grid_attach(GTK_GRID(tab_tec), spin_n_sectors,   1, 5, 1, 1);
   g_signal_connect (spin_n_sectors, "value-changed", G_CALLBACK (intSpinUpdate), &par.nSectors);
   
   // Created constWind Twd and Tws
   labelCreate (tab_tec, "Const Wind Twd",              0, 6);
   entry_wind_twd = doubleToEntry (par.constWindTwd);
   gtk_grid_attach(GTK_GRID (tab_tec), entry_wind_twd,  1, 6, 1, 1);
   g_signal_connect (entry_wind_twd, "changed", G_CALLBACK (windTwdUpdate), NULL);

   labelCreate (tab_tec, "Const Wind Tws",              0, 7);
   entry_wind_tws = doubleToEntry (par.constWindTws);
   gtk_grid_attach (GTK_GRID(tab_tec), entry_wind_tws,  1, 7, 1, 1);
   g_signal_connect (entry_wind_tws, "changed", G_CALLBACK (windTwsUpdate), NULL);
   
   // Creare constCurrent Twd and Tws
   labelCreate (tab_tec, "Const Current Twd",            0, 8);
   entry_current_twd = doubleToEntry (par.constCurrentD);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_current_twd, 1, 8, 1, 1);
   g_signal_connect (entry_current_twd, "changed", G_CALLBACK (doubleUpdate), &par.constCurrentD);

   labelCreate (tab_tec, "Const Current Tws",            0, 9);
   entry_current_tws = doubleToEntry (par.constCurrentS);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_current_tws, 1, 9, 1, 1);
   g_signal_connect (entry_current_tws, "changed", G_CALLBACK (doubleUpdate), &par.constCurrentS);
   
   // Create constWave
   labelCreate (tab_tec, "Const Wave Height",            0, 10);
   entry_wave = doubleToEntry (par.constWave);
   gtk_grid_attach(GTK_GRID(tab_tec), entry_wave,        1, 10, 1, 1);
   g_signal_connect (entry_wave, "changed", G_CALLBACK (doubleUpdate), &par.constWave);

   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID(tab_tec), separator,         0, 11, 11, 1);

   // checkbox: "closest"
   GtkWidget *checkboxClosest = gtk_check_button_new_with_label("Closest");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxClosest, par.closestDisp);
   g_signal_connect(G_OBJECT(checkboxClosest), "toggled", G_CALLBACK (on_checkbox_toggled), &par.closestDisp);
   gtk_grid_attach(GTK_GRID (tab_tec), checkboxClosest,  0, 12, 1, 1);

   // checkbox: "focal point"
   GtkWidget *checkboxFocal = gtk_check_button_new_with_label("Focal Point");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxFocal, par.focalDisp);
   g_signal_connect(G_OBJECT(checkboxFocal), "toggled", G_CALLBACK (on_checkbox_toggled), &par.focalDisp);
   gtk_grid_attach(GTK_GRID (tab_tec), checkboxFocal, 1, 12, 1, 1);

   labelCreate (tab_display, "",              0, 0); // just for space on top
   
   // Radio Button Colors
   labelCreate (tab_display, "Colors",                     0, 1);
   GtkWidget *choice0 = createRadioButtonColors ("None", tab_display, NULL, 0);
   GtkWidget *choice1 = createRadioButtonColors ("B.& W.", tab_display, choice0, 1);
   createRadioButtonColors ("Colored", tab_display, choice1, 2);
   
   // Radio Button Wind
   labelCreate (tab_display, "Wind",                       0, 2);
   choice0 = createRadioButtonWindDisp ("None", tab_display, NULL, 0);
   choice1 = createRadioButtonWindDisp ("Arrow", tab_display, choice0, 1);
   createRadioButtonWindDisp ("Barbule", tab_display, choice1, 2);
   
   // Create combo box for Avr/gust/rain/pressure
   labelCreate (tab_display, "Wind/Gust/Rain/Pressure",     0, 3);
   GtkWidget *combo_box_disp = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_disp), NULL, "Wind");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_disp), NULL, "Gust");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_disp), NULL, "Waves");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_disp), NULL, "Rain");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_disp), NULL, "Pressure");
   gtk_grid_attach (GTK_GRID(tab_display), combo_box_disp, 1, 3, 1, 1);
   gtk_combo_box_set_active(GTK_COMBO_BOX (combo_box_disp), par.averageOrGustDisp);

   g_signal_connect(combo_box_disp, "changed", G_CALLBACK (on_combo_box_disp_changed), NULL);
   
   // Create combo box for Isochrones
   labelCreate (tab_display, "Isoc.",                       0, 4);
   GtkWidget *combo_box_Isoc = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_Isoc), NULL, "None");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_Isoc), NULL, "Points");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_Isoc), NULL, "Segment");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_Isoc), NULL, "Bezier");
   gtk_grid_attach (GTK_GRID(tab_display), combo_box_Isoc, 1, 4, 1, 1);
   gtk_combo_box_set_active(GTK_COMBO_BOX (combo_box_Isoc), par.style);
   g_signal_connect(combo_box_Isoc, "changed", G_CALLBACK (on_combo_box_Isoc_changed), NULL);

   // Create combo box for DMS (Degree Minutes Second) representaton
   labelCreate (tab_display, "DMS",                       2, 4);
   GtkWidget *combo_box_DMS = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_DMS), NULL, "Basic");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_DMS), NULL, "DD");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_DMS), NULL, "DM");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box_DMS), NULL, "DMS");
   gtk_grid_attach (GTK_GRID(tab_display), combo_box_DMS, 3, 4, 1, 1);
   gtk_combo_box_set_active(GTK_COMBO_BOX (combo_box_DMS), par.dispDms);
   g_signal_connect(combo_box_DMS, "changed", G_CALLBACK (on_combo_box_DMS_changed), NULL);

   // Checkbox: "waves"
   GtkWidget *checkboxWave = gtk_check_button_new_with_label("Waves");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxWave, par.waveDisp);
   g_signal_connect(G_OBJECT (checkboxWave), "toggled", G_CALLBACK (on_checkbox_toggled), &par.waveDisp);
   gtk_grid_attach(GTK_GRID(tab_display), checkboxWave,          0, 5, 1, 1);

   // Checkbox: "current"
   GtkWidget *checkboxCurrent = gtk_check_button_new_with_label("Current");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxCurrent, par.currentDisp);
   g_signal_connect (G_OBJECT(checkboxCurrent), "toggled", G_CALLBACK (on_checkbox_toggled), &par.currentDisp);
   gtk_grid_attach(GTK_GRID (tab_display), checkboxCurrent,      1, 5, 1, 1);

   // Checkbox: "Grid" for parallels and meridians
   GtkWidget *checkboxGrid = gtk_check_button_new_with_label("Grid");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxGrid, par.gridDisp);
   g_signal_connect (G_OBJECT(checkboxGrid), "toggled", G_CALLBACK (on_checkbox_toggled), &par.gridDisp);
   gtk_grid_attach(GTK_GRID (tab_display), checkboxGrid,         2, 5, 1, 1);

   // Checkbox: "info display"
   GtkWidget *checkboxInfoDisp = gtk_check_button_new_with_label("Info Display");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxInfoDisp, par.infoDisp);
   g_signal_connect(G_OBJECT(checkboxInfoDisp), "toggled", G_CALLBACK (on_checkbox_toggled), &par.infoDisp);
   gtk_grid_attach(GTK_GRID (tab_display), checkboxInfoDisp,     3, 5, 1, 1);

   GtkWidget *separator1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID(tab_display), separator1, 0, 6, 10, 1);

   // Crate GtkScale POI widget
   const int MAX_LEVEL_POI_VISIBLE = 5; 
   GtkWidget *labelNumber = gtk_label_new (g_strdup_printf("POI: %d", par.maxPoiVisible));
   gtk_grid_attach(GTK_GRID (tab_display), labelNumber, 0, 7, 1, 1);
   gtk_widget_set_margin_start (labelNumber, 4);
   gtk_label_set_xalign(GTK_LABEL(labelNumber), 0.0);
   
   GtkWidget *levelVisible = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 1, MAX_LEVEL_POI_VISIBLE, 1);
   gtk_range_set_value (GTK_RANGE(levelVisible), par.maxPoiVisible);
   gtk_scale_set_value_pos (GTK_SCALE(levelVisible), GTK_POS_TOP);
   gtk_widget_set_size_request (levelVisible, 200, -1);  // Ajuster la largeur du GtkScale
   g_signal_connect (levelVisible, "value-changed", G_CALLBACK (on_level_poi_visible_changed), labelNumber);
   gtk_grid_attach(GTK_GRID(tab_display), levelVisible, 1, 7, 2, 1);
  
   // Crate GtkScale Display Speed widget
   GtkWidget *labelSpeedDisplay = gtk_label_new (g_strdup_printf("Display Speed: %d", par.speedDisp));
   gtk_grid_attach(GTK_GRID (tab_display), labelSpeedDisplay, 0, 8, 1, 1);
   gtk_widget_set_margin_start (labelSpeedDisplay, 4);
   gtk_label_set_xalign(GTK_LABEL(labelSpeedDisplay), 0.0);
   
   GtkWidget *levelSpeedDisplay = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, MAX_N_ANIMATION - 1, 1);
   gtk_range_set_value (GTK_RANGE(levelSpeedDisplay), par.speedDisp);
   gtk_scale_set_value_pos (GTK_SCALE(levelSpeedDisplay), GTK_POS_TOP);
   gtk_widget_set_size_request (levelSpeedDisplay, 200, -1);  // Ajuster la largeur du GtkScale
   g_signal_connect (levelSpeedDisplay, "value-changed", G_CALLBACK (on_tempo_display_changed), labelSpeedDisplay);
   gtk_grid_attach(GTK_GRID(tab_display), levelSpeedDisplay,   1, 8, 2, 1);
  
   gtk_window_present (GTK_WINDOW (changeWindow));
}
    
/*! zoom in */ 
static void on_zoom_in_button_clicked (GtkWidget *widget, gpointer data) {
   dispZoom (0.6);
   gtk_widget_queue_draw (drawing_area);
}

/*! zoom out */ 
static void on_zoom_out_button_clicked (GtkWidget *widget, gpointer data) {
   dispZoom (1.4);
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
static void on_center_map (GtkWidget *widget, gpointer data) {
   centerDispZone (par.pOr.lon, par.pOr.lat);
   gtk_widget_queue_draw (drawing_area);
}

/*! poi Name change */
static void poi_entry_changed (GtkEditable *editable, gpointer user_data) {
   char *ptPoiName = (gchar *) user_data;
   const gchar *text = gtk_editable_get_text (editable);
   g_strlcpy (poiName, text, MAX_SIZE_NAME);
   printf ("poi: %s\n", ptPoiName);
}

/*! Response for poi change */
static void poi_name_response (GtkDialog *dialog, gint response_id, gpointer user_data) {
   printf ("Response POI : %d %d\n", response_id, GTK_RESPONSE_ACCEPT);
   if (menuWindow != NULL) popoverFinish (menuWindow);
   if (response_id == GTK_RESPONSE_ACCEPT) {
      strncpy (tPoi [nPoi].name, poiName, MAX_SIZE_POI_NAME);
      tPoi [nPoi].lon = xToLon (whereIsPopup.x);
      tPoi [nPoi].lat = yToLat (whereIsPopup.y);
      tPoi [nPoi].level = 1;
      tPoi [nPoi].type = NEW;
      nPoi += 1;
      gtk_widget_queue_draw (drawing_area);
   }
   gtk_window_destroy (GTK_WINDOW (dialog));
}

/*! poi name */ 
static void poiNameChoose () {
   if (nPoi >= MAX_N_POI) {
      infoMessage ("Number of poi exceeded", GTK_MESSAGE_ERROR);
      return;
   }
   
   GtkWidget *dialogBox = gtk_dialog_new_with_buttons("Poi Name", GTK_WINDOW (window), GTK_DIALOG_MODAL,
                       "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_CANCEL, NULL);

   GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG(dialogBox));
   GtkWidget *poiName_entry = gtk_entry_new ();
   g_signal_connect_swapped (dialogBox, "response", G_CALLBACK (poi_name_response), dialogBox);
   g_signal_connect (poiName_entry, "changed", G_CALLBACK (poi_entry_changed), poiName);
   gtk_box_append(GTK_BOX (content_area), poiName_entry);
   gtk_window_present (GTK_WINDOW (dialogBox));
}

/*! create wayPoint */
static void wayPointSelected () {
   if (menuWindow != NULL) popoverFinish (menuWindow);
   if (wayPoints.n < MAX_N_WAY_POINT) {
      destPressed = false;
      wayPoints.t [wayPoints.n].lat = yToLat (whereIsPopup.y);
      wayPoints.t [wayPoints.n].lon = xToLon (whereIsPopup.x);
      wayPoints.n += 1;
      gtk_widget_queue_draw (drawing_area);
   }
   else (infoMessage ("Number of waypoints exceeded", GTK_MESSAGE_ERROR));
}

/*! update point of origin */
static void originSelected () {
   if (menuWindow != NULL) popoverFinish (menuWindow);
   destPressed = false;
   par.pOr.lat = yToLat (whereIsPopup.y);
   par.pOr.lon = xToLon (whereIsPopup.x);
   route.n = 0;
   wayPoints.n = 0;
   wayPoints.totOrthoDist = 0;
   wayPoints.totLoxoDist = 0;
   par.pOrName [0] = '\0';
   gtk_widget_queue_draw (drawing_area);
} 

/*! update point of destination */
static void destinationSelected () {
   if (menuWindow != NULL) popoverFinish (menuWindow);
   destPressed = true;
   route.n = 0;
   par.pDest.lat = yToLat (whereIsPopup.y);
   par.pDest.lon = xToLon (whereIsPopup.x);
   wayPoints.t [wayPoints.n].lat = par.pDest.lat;
   wayPoints.t [wayPoints.n].lon = par.pDest.lon;
   calculateOrthoRoute ();
   niceWayPointReport (); 
   par.pOr.id = -1;
   par.pOr.father = -1;
   par.pDest.id = 0;
   par.pDest.father = 0;
   par.pDestName [0] = '\0';
   gtk_widget_queue_draw (drawing_area);
} 

/*! update start polygon for forbidden zone */
static void startPolygonSelected () {
   if (menuWindow != NULL) popoverFinish (menuWindow);
   if ((forbidZones [par.nForbidZone].points = (Point *) malloc (MAX_SIZE_FORBID_ZONE * sizeof(Point))) == NULL) {
      fprintf (stderr, "Error in on_popup_menu_selection: malloc forbidZones with k = %d\n", par.nForbidZone);
      infoMessage ("in on_popup_menu_selection Error malloc forbidZones", GTK_MESSAGE_ERROR);
      return;
   } 
   polygonStarted = true;
   if (par.nForbidZone < MAX_N_FORBID_ZONE) {
      forbidZones [par.nForbidZone].points [0].lat = yToLat (whereIsPopup.y);
      forbidZones [par.nForbidZone].points [0].lon = xToLon (whereIsPopup.x);
      forbidZones [par.nForbidZone].n = 1;
      gtk_widget_queue_draw (drawing_area); // affiche tout
   }
}

/*! intermediate vertex  polygon for forbidden zone */
static void vertexPolygonSelected () {
   if (menuWindow != NULL) popoverFinish (menuWindow);
   if (polygonStarted && (forbidZones [par.nForbidZone].n < MAX_SIZE_FORBID_ZONE-1)) {
      printf ("vertex polygon %d %d\n", par.nForbidZone, forbidZones [par.nForbidZone].n);
      forbidZones [par.nForbidZone].points [forbidZones [par.nForbidZone].n].lat = yToLat (whereIsPopup.y);
      forbidZones [par.nForbidZone].points [forbidZones [par.nForbidZone].n].lon = xToLon (whereIsPopup.x);
      forbidZones [par.nForbidZone].n += 1;
      gtk_widget_queue_draw (drawing_area); // affiche tout
   }
}

/*! close polygon for forbidden zone */
static void closePolygonSelected () {
   if (menuWindow != NULL) popoverFinish (menuWindow);
   if (polygonStarted && (forbidZones [par.nForbidZone].n < MAX_SIZE_FORBID_ZONE) && (forbidZones [par.nForbidZone].n > 2)) {
      forbidZones [par.nForbidZone].points [forbidZones [par.nForbidZone].n].lat = forbidZones [par.nForbidZone].points [0].lat;
      forbidZones [par.nForbidZone].points [forbidZones [par.nForbidZone].n].lon = forbidZones [par.nForbidZone].points [0].lon;
      forbidZones [par.nForbidZone].n += 1;
      par.nForbidZone += 1;
      updateIsSeaWithForbiddenAreas ();
      gtk_widget_queue_draw (drawing_area); // affiche tout
   }
   polygonStarted = false;
}

/*! callback when mouse move */
static gboolean on_motion_event (GtkEventControllerMotion *controller, double x, double y, gpointer user_data) {
   whereIsMouse.x = x;
   whereIsMouse.y = y;
   if (selecting) {
      // g_print("Déplacement de la souris : x = %f, y = %f\n", x, y);
      gtk_widget_queue_draw (drawing_area); // Redessiner pour mettre à jour le rectangle de sélection
   }
   else statusBarUpdate ();
   return TRUE;
}

/*! change the route selecting another point in last isochrone*/
static void changeLastPoint () {
   double dXy, xLon, yLat, minDxy = DBL_MAX;
   double x = whereIsPopup.x;
   double y = whereIsPopup.y;

   if (menuWindow != NULL) popoverFinish (menuWindow);
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
   storeRoute (&par.pOr, &lastClosest, 0);
   niceReport (&route, 0);
}

/*! select action associated to right click */ 
static void on_right_click_event (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
   char str [MAX_SIZE_LINE];
   whereIsPopup.x = x;
   whereIsPopup.y = y;
   
   GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);

   if (route.n != 0 && !route.destinationReached) {
      GtkWidget *recalculateButton = gtk_button_new_with_label ("Last Point In Isochrone");
      g_signal_connect (recalculateButton, "clicked", G_CALLBACK (changeLastPoint), NULL);
      gtk_box_append (GTK_BOX (box), recalculateButton);
   }

   GtkWidget *meteoGramButton = gtk_button_new_with_label ("Meteogram");
   g_signal_connect (meteoGramButton, "clicked", G_CALLBACK (meteogram), NULL);
   GtkWidget *originButton = gtk_button_new_with_label ("Point Of Origin");
   g_signal_connect (originButton, "clicked", G_CALLBACK (originSelected), NULL);
   snprintf (str, sizeof (str), "Waypoint no: %d",  wayPoints.n + 1);
   GtkWidget *waypointButton = gtk_button_new_with_label (str);
   g_signal_connect (waypointButton, "clicked", G_CALLBACK (wayPointSelected), NULL);
   GtkWidget *destinationButton = gtk_button_new_with_label ("Point of Destination");
   g_signal_connect (destinationButton, "clicked", G_CALLBACK (destinationSelected), NULL);
   GtkWidget *poiButton = gtk_button_new_with_label ("Point of Interest");
   g_signal_connect (poiButton, "clicked", G_CALLBACK (poiNameChoose), NULL);
   GtkWidget *startPolygonButton = gtk_button_new_with_label ("Start Polygon");
   g_signal_connect (startPolygonButton, "clicked", G_CALLBACK (startPolygonSelected), NULL);
   GtkWidget *vertexPolygonButton = gtk_button_new_with_label ("Vertex Polygon");
   g_signal_connect (vertexPolygonButton, "clicked", G_CALLBACK (vertexPolygonSelected), NULL);
   GtkWidget *closePolygonButton = gtk_button_new_with_label ("Close Polygon");
   g_signal_connect (closePolygonButton, "clicked", G_CALLBACK (closePolygonSelected), NULL);
   
   gtk_box_append (GTK_BOX (box), meteoGramButton);
   gtk_box_append (GTK_BOX (box), originButton);
   gtk_box_append (GTK_BOX (box), waypointButton);
   gtk_box_append (GTK_BOX (box), destinationButton);
   gtk_box_append (GTK_BOX (box), poiButton);
   gtk_box_append (GTK_BOX (box), startPolygonButton);
   gtk_box_append (GTK_BOX (box), vertexPolygonButton);
   gtk_box_append (GTK_BOX (box), closePolygonButton);

   menuWindow = gtk_popover_new ();
   gtk_widget_set_parent (menuWindow, drawing_area);
   gtk_popover_set_child ((GtkPopover*) menuWindow, (GtkWidget *) box);

   g_signal_connect(drawing_area, "destroy", G_CALLBACK (on_parent_window_destroy), menuWindow);
   GdkRectangle rect = {x, y, 1, 1};
   //gtk_popover_set_has_arrow ((GtkPopover*) menuWindow, false);
   gtk_popover_set_pointing_to ((GtkPopover*) menuWindow, &rect);
   //gtk_popover_popup ((GtkPopover *) menuWindow);
   //gtk_popover_set_cascade_popdown ((GtkPopover*) menuWindow, TRUE);
   //gtk_popover_set_autohide ((GtkPopover*) menuWindow, TRUE);
   gtk_widget_set_visible (menuWindow, true);
}

/*! Callback  associated to mouse left clic release event
    Action after selection. Launch smtp request for grib file */
static void on_left_release_event (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
   double lat1, lat2, lon1, lon2;
   if (n_press == 1) {
      //g_print("Clic gauche relache\n");
      if (selecting && \
         ((whereIsMouse.x - whereWasMouse.x) > MIN_MOVE_FOR_SELECT) && \
         ((whereIsMouse.x - whereWasMouse.x) > MIN_MOVE_FOR_SELECT)) {

         lat1 = yToLat (whereIsMouse.y);
         lon1 = xToLon (whereWasMouse.x);
         lat2 = yToLat (whereWasMouse.y);
         lon2 = xToLon (whereIsMouse.x);
         lon1 = lonCanonize (lon1);
         lon2 = lonCanonize (lon2);

         provider = SAILDOCS_GFS; // default 
         mailRequestBox (lat1, lon1, lat2, lon2);
      }
   }
   selecting = false;
   gtk_widget_queue_draw (drawing_area); // Redessiner pour effacer le rectangle de sélection
}

/*! Callback  associated to mouse left clic event */
static void on_left_click_event(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
   selecting = !selecting;
   whereWasMouse.x = whereIsMouse.x = x;
   whereWasMouse.y = whereIsMouse.y = y;
   gtk_widget_queue_draw (drawing_area); // Redessiner pour mettre à jour le rectangle de sélection
}

/*! Zoom associated to mouse scroll event */
static gboolean on_scroll_event (GtkEventController *controller, double delta_x, double delta_y, gpointer user_data) {
   if (delta_y > 0) {
      dispZoom (1.4);
   } else if (delta_y < 0) {
	   dispZoom (0.6);
   }
   gtk_widget_queue_draw (drawing_area);
   return TRUE;
}

/*! Callback when fullscreen or back */ 
static void on_fullscreen_notify (void) {
   destroySurface ();
   gtk_widget_queue_draw (drawing_area);
}

/*! Callback when keyboard key pressed */
static gboolean on_key_event (GtkEventController *controller, guint keyval, guint keycode, GdkModifierType modifiers, gpointer user_data) {
   switch (keyval) {
   case GDK_KEY_F1:
      if (!my_gps_data.OK)
         infoMessage ("No GPS position available", GTK_MESSAGE_WARNING);
      else {
         par.pOr.lat = my_gps_data.lat;
         par.pOr.lon = my_gps_data.lon;
         infoMessage ("Point of Origin = GPS position", GTK_MESSAGE_WARNING);
         gtk_widget_queue_draw (drawing_area);
      }
      break;
   case GDK_KEY_Escape:
      printf ("Exit success\n");
      exit (EXIT_SUCCESS); // exit if ESC key pressed
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
   return TRUE; // event has been managed with success
}

/*! Draw meteogram */
static void on_meteogram_event (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
#define MAX_VAL_MET 4
   const int LEGEND_X_LEFT = width - 80;
   const int LEGEND_Y_TOP = 10;
   char str [MAX_SIZE_LINE];
   int x, y;
   double u, v, g = 0, w, twd, tws, head_x, maxTws = 0, maxG = 0, maxMax = 0, maxWave = 0;
   double uCurr, vCurr, currTwd, currTws, maxCurrTws = 0;// current
   long tMax = zone.timeStamp [zone.nTimeStamp -1];
   double tDeltaCurrent = zoneTimeDiff (&currentZone, &zone);
   double tDeltaNow = 0; // time between beginning of grib and now in hours
   double lat, lon;
   char totalDate [MAX_SIZE_DATE]; 
   int timeMeteo, initTimeMeteo;
   lat = yToLat (whereIsPopup.y);
   lon = xToLon (whereIsPopup.x);
   lon = lonCanonize (lon);

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

   char *libelle [MAX_VAL_MET] = {"Wind", "Gust", "Waves", "Current"}; 
   double colors[MAX_VAL_MET][3] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {1, 165.0/255.0, 0}}; // blue red blue orange
   drawLegend (cr, LEGEND_X_LEFT, LEGEND_Y_TOP, colors, libelle, MAX_VAL_MET);

   // graphical difference between up to now and after now 
   // tDeltaNow is the number of hours between beginning of grib to now */
   time_t now = time (NULL);
   tDeltaNow = (now - vOffsetLocalUTC)/ 3600.0; // offset between local and UTC in hours
   tDeltaNow -= gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]) / 3600.0;
   //printf ("tDeltaNow: %.2lf\n", tDeltaNow);
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
   
   // Draw horizontal line with arrow
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_move_to (cr, X_LEFT,  Y_BOTTOM );
   cairo_line_to (cr, X_RIGHT, Y_BOTTOM);
   cairo_line_to (cr, X_RIGHT - DELTA, Y_BOTTOM + DELTA); // for arrow
   cairo_stroke (cr);
   cairo_move_to (cr, X_RIGHT, Y_BOTTOM );
   cairo_line_to (cr, X_RIGHT - DELTA, Y_BOTTOM - DELTA);
   cairo_stroke (cr);
   
   // Draw vertical line with arrow
   cairo_move_to (cr, X_LEFT, Y_BOTTOM);
   cairo_line_to (cr, X_LEFT, Y_TOP);
   cairo_line_to (cr, X_LEFT - DELTA, Y_TOP + DELTA);
   cairo_stroke (cr);
   cairo_move_to (cr, X_LEFT, Y_TOP);
   cairo_line_to (cr, X_LEFT + DELTA, Y_TOP + DELTA);
   cairo_stroke (cr);

   // find max Tws max gust max waves max current and draw twd arrows
   for (int i = 0; i < tMax; i += 1) {
      findWindGrib (lat, lon, i, &u, &v, &g, &w, &twd, &tws);
	   findCurrentGrib (lat, lon, i- tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);

      head_x = X_LEFT + XK * i;
      if ((i % (tMax / 24)) == 0) {
         arrow (cr, head_x, HEAD_Y, u, v, twd, tws, WIND);
         arrow (cr, head_x, HEAD_Y + 20, uCurr, vCurr, currTwd, currTws, CURRENT);
      }
      if (tws > maxTws) maxTws = tws;
      if (w > maxWave) maxWave = w;
      if (g > maxG) maxG = g;
      if (currTws > maxCurrTws) maxCurrTws = currTws;
   }
   maxG *= MS_TO_KN; 
   maxMax = ceil (MAX (maxG, MAX (maxTws, MAX (maxWave, (MAX (maxCurrTws, 10)))))); 
   
   // Draw time label and vertical line
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 10);
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   initTimeMeteo = zone.dataTime [0]/100; 
   for (int i = 0; i <= tMax; i+= (par.gribTimeMax <= 120) ? 6 : 12) {
      timeMeteo = (zone.dataTime [0]/100) + i;
      x = X_LEFT + XK * i;
      cairo_move_to (cr, x, Y_BOTTOM + 10);
      newDate (zone.dataDate [0], timeMeteo, totalDate);
      cairo_show_text (cr, strrchr (totalDate, ' ') + 1); // hour is after space
      if ((timeMeteo % ((par.gribTimeMax <= 240) ? 24 : 48)) == initTimeMeteo) { // only year month day considered
         cairo_move_to (cr, x, Y_BOTTOM + 20);
         totalDate [DAY_LG] = '\0'; // date
         cairo_show_text (cr, totalDate);
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

   // Draw speed labels and horizontal lines
   cairo_set_font_size (cr, 10);
   double step = (maxMax > 50) ? 10 : 5;
   for (double speed = step; speed <= maxMax; speed += step) {
      y = Y_BOTTOM - YK * speed;
      cairo_move_to (cr, X_LEFT - 20, y);
      snprintf (str, MAX_SIZE_LINE, "%02.0lf", speed);
      cairo_show_text (cr, str);
      CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr); // horizontal line
      cairo_move_to (cr, X_LEFT, y);
      cairo_line_to (cr, X_RIGHT, y);
      cairo_stroke (cr);
      CAIRO_SET_SOURCE_RGB_BLACK(cr);
   }
   cairo_stroke(cr);
  
   CAIRO_SET_SOURCE_RGB_BLUE(cr);
   // draw tws 
   for (int i = 0; i < tMax; i += 1) {
      findWindGrib (lat, lon, i, &u, &v, &g, &w, &twd, &tws);
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
         findWindGrib (lat, lon, i, &u, &v, &g, &w, &twd, &tws);
         x = X_LEFT + XK * i;
         y = Y_BOTTOM - YK * MAX (g * MS_TO_KN, tws);
         if (i == 0) cairo_move_to (cr, x, y);
         else cairo_line_to (cr, x, y);
      }
      cairo_stroke(cr);
   }

   // draw waves
   if (maxWave > 0) { 
      CAIRO_SET_SOURCE_RGB_GREEN(cr);
      for (int i = 0; i < tMax; i += 1) {
         findWindGrib (lat, lon, i, &u, &v, &g, &w, &twd, &tws);
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
	      findCurrentGrib (lat, lon, i - tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);
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
   char strLat [MAX_SIZE_NAME], strLon [MAX_SIZE_NAME];
   char line [MAX_SIZE_LINE]; 
   if (par.constWindTws > 0) {
      infoMessage ("Wind is constant !", GTK_MESSAGE_INFO);
      return;
   }
   popoverFinish (menuWindow);
   Pp pt;
   pt.lat = yToLat (whereIsPopup.y);
   pt.lon = xToLon (whereIsPopup.x);
   snprintf (line, sizeof (line), "Meteogram for %s %s beginning %s during %ld hours", latToStr (pt.lat, par.dispDms, strLat), 
      lonToStr (pt.lon, par.dispDms, strLon),
      newDate (zone.dataDate [0], (zone.dataTime [0]/100), totalDate),
      zone.timeStamp [zone.nTimeStamp -1]);

   GtkWidget *meteoWindow = gtk_application_window_new (GTK_APPLICATION (app));
   gtk_window_set_title (GTK_WINDOW(meteoWindow), line);
   gtk_window_set_default_size (GTK_WINDOW(meteoWindow), 1400, 400);
   g_signal_connect (window, "destroy", G_CALLBACK(on_parent_destroy), meteoWindow);
   
   GtkWidget *meteogram_drawing_area = gtk_drawing_area_new ();

   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (meteogram_drawing_area), on_meteogram_event, NULL, NULL);
   gtk_window_set_child (GTK_WINDOW (meteoWindow), meteogram_drawing_area);
   gtk_window_present (GTK_WINDOW (meteoWindow));   
}

/*! submenu with label and icon
static void try_subMenu(GMenu *vMenu, const char *str, const char *app, const gchar *iconName) {
   GMenuItem *menu_item = g_menu_item_new (str, app);
   g_menu_item_set_label(menu_item, str);
   if (iconName != NULL) {
      GIcon *icon = g_themed_icon_new_with_default_fallbacks(iconName);
      g_menu_item_set_icon (menu_item, G_ICON(icon));
      g_object_unref(icon);
   }    
   g_menu_append_item(vMenu, menu_item);
   g_object_unref(menu_item);
}
*/

/*! submenu with label but no icon */
static void subMenu (GMenu *vMenu, const char *str, const char *app, const gchar *iconName) {
   GMenuItem *menu_item = g_menu_item_new (str, app);
   g_menu_append_item (vMenu, menu_item);
   g_object_unref (menu_item);
}

/*! Grib URL selection for Meteo Consult */
static void gribUrl (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   typeFlow = GPOINTER_TO_INT (data);
   GtkWidget *itemUrl; 
   GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);

   for (int i = 0; i < N_WIND_URL; i++) {
      itemUrl = gtk_button_new_with_label (WIND_URL [i * 2]);
      g_signal_connect_swapped (itemUrl, "clicked", G_CALLBACK (urlDownloadGrib), GINT_TO_POINTER (i));
      gtk_box_append (GTK_BOX (box), itemUrl);
   }
   menuGrib = gtk_popover_new ();
   gtk_widget_set_parent (menuGrib, window);
   g_signal_connect (window, "destroy", G_CALLBACK (on_parent_window_destroy), menuGrib);
   gtk_popover_set_child ((GtkPopover*) menuGrib, (GtkWidget *) box);
   gtk_popover_set_has_arrow ((GtkPopover*) menuGrib, false);
   GdkRectangle rect = {200, 100, 1, 1};
   gtk_popover_set_pointing_to ((GtkPopover*) menuGrib, &rect);
   gtk_widget_set_visible (menuGrib, true);
}

/*! Callback exit */
static void quit_activated (GSimpleAction *action, GVariant *parameter, GApplication *application) {
   exit (EXIT_SUCCESS);
}
   
/*! Callback polar_draw */
static void polar_draw_activated (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   polarType = GPOINTER_TO_INT (data);
   polarDraw (); 
}

/*! Callback poi (point of interest) save */
static void poi_save_activated (GSimpleAction *action, GVariant *parameter, GApplication *application) {
   writePoi (par.poiFileName);
}

/*! Callback for text read file */
static void cli_activated (GSimpleAction *action, GVariant *parameter, GApplication *application) {
   char str [MAX_SIZE_FILE_NAME];
   displayFile (buildRootName (par.cliHelpFileName, str), "CLI Info", true);
}

/*! create a button within toolBow, with an icon and a callback function */
static void createButton (GtkWidget *toolBox, char *iconName, void *callBack) { 
   GtkWidget *button = gtk_button_new_from_icon_name (iconName);
   g_signal_connect (button, "clicked", G_CALLBACK (callBack), NULL);
   gtk_box_append (GTK_BOX (toolBox), button);
}

/*! Update info GPS */ 
static gboolean update_GPS_callback (gpointer data) {
   char str [MAX_SIZE_LINE], strDate [MAX_SIZE_LINE], strLat [MAX_SIZE_LINE], strLon [MAX_SIZE_LINE];
   GtkWidget *label = GTK_WIDGET(data);
   if (my_gps_data.OK)
      snprintf (str, sizeof (str), "%s UTC   Lat: %s Lon: %s  COG: %.0lf°  SOG: %.2lf Kn",
         epochToStr (my_gps_data.time, false, strDate, sizeof (strDate)),
         latToStr (my_gps_data.lat, par.dispDms, strLat), lonToStr (my_gps_data.lon, par.dispDms, strLon),
         my_gps_data.cog, my_gps_data.sog);
   else 
      snprintf (str, sizeof (str), "   No GPS Info");
   gtk_label_set_text (GTK_LABEL(label), str);
   return G_SOURCE_CONTINUE;
}

/*! Main window set up */
static void app_activate (GApplication *application) {
   app = GTK_APPLICATION (application);
   window = gtk_application_window_new (app);
   titleUpdate ();
   gtk_window_set_default_size (GTK_WINDOW (window), MAIN_WINDOW_DEFAULT_WIDTH, MAIN_WINDOW_DEFAULT_HEIGHT);
   gtk_window_maximize (GTK_WINDOW (window)); // full screen
   gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (window), TRUE);
   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (window), vBox);

   GtkWidget *toolBox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);

   createButton (toolBox, "system-run", on_run_button_clicked);
   createButton (toolBox, "starred", on_choose_departure_button_clicked);
   createButton (toolBox, "preferences-desktop", change);
   createButton (toolBox, "media-playback-pause", on_stop_button_clicked);
   createButton (toolBox, "media-playback-start", on_play_button_clicked);
   createButton (toolBox, "media-playlist-repeat", on_loop_button_clicked);
   createButton (toolBox, "media-skip-backward", on_to_start_button_clicked);
   createButton (toolBox, "media-seek-backward", on_reward_button_clicked);
   createButton (toolBox, "media-seek-forward", on_forward_button_clicked);
   createButton (toolBox, "media-skip-forward", on_to_end_button_clicked);
   createButton (toolBox, "zoom-in", on_zoom_in_button_clicked);
   createButton (toolBox, "zoom-out", on_zoom_out_button_clicked);
   createButton (toolBox, "zoom-original", on_zoom_original_button_clicked);
   createButton (toolBox, "pan-start-symbolic", on_left_button_clicked);
   createButton (toolBox, "pan-up-symbolic", on_up_button_clicked);
   createButton (toolBox, "pan-down-symbolic", on_down_button_clicked);
   createButton (toolBox, "pan-end-symbolic", on_right_button_clicked);
   createButton (toolBox, "find-location-symbolic", on_center_map);
   createButton (toolBox, "edit-select-all", paletteDraw);
   createButton (toolBox, "applications-engineering-symbolic", testFunction);
   
   GtkWidget *gpsInfo = gtk_label_new (" GPS Info coming...");
   gtk_box_append (GTK_BOX (toolBox), gpsInfo);

   // label line for route info in bold red left jutified (Global Variable)
   labelInfoRoute = gtk_label_new ("");
   //gtk_label_set_justify (GTK_LABEL(labelInfoRoute), GTK_JUSTIFY_LEFT);
   PangoAttrList *attrs = pango_attr_list_new();
   PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
   pango_attr_list_insert(attrs, attr);
   attr = pango_attr_foreground_new (65535, 0, 0);       // red
   pango_attr_list_insert(attrs, attr);
   gtk_label_set_attributes (GTK_LABEL(labelInfoRoute), attrs);
   gtk_label_set_xalign(GTK_LABEL (labelInfoRoute), 0); // left justified

   // Create statusbar (Global Variable)
   statusbar = gtk_statusbar_new();
   gtk_widget_set_size_request (statusbar, -1, 30);
   gtk_widget_set_hexpand (statusbar, TRUE); 
   context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "Statusbar");

   // Create drawing area (Global Variable)
   drawing_area = gtk_drawing_area_new();
   gtk_widget_set_hexpand(drawing_area, TRUE);
   gtk_widget_set_vexpand(drawing_area, TRUE);

   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (drawing_area), drawGribCallback, NULL, NULL);

   // Create event controller for mouse motion
   GtkEventController *motion_controller = gtk_event_controller_motion_new();
   gtk_widget_add_controller (drawing_area, motion_controller);
   g_signal_connect(motion_controller, "motion", G_CALLBACK (on_motion_event), NULL);

   // Create left clic manager
   GtkGesture *click_gesture_left = gtk_gesture_click_new();
   gtk_gesture_single_set_button (GTK_GESTURE_SINGLE(click_gesture_left), GDK_BUTTON_PRIMARY);
   gtk_widget_add_controller (drawing_area, GTK_EVENT_CONTROLLER (click_gesture_left));
   g_signal_connect(click_gesture_left, "pressed", G_CALLBACK (on_left_click_event), NULL);
   // Connexion du signal pour le relâchement du clic gauche
   g_signal_connect(click_gesture_left, "released", G_CALLBACK (on_left_release_event), NULL);   

   // Create rigtt clic manager
   GtkGesture *click_gesture_right = gtk_gesture_click_new();
   gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture_right), GDK_BUTTON_SECONDARY);
   GtkEventController *click_controller_right = GTK_EVENT_CONTROLLER (click_gesture_right);
   gtk_widget_add_controller (drawing_area, GTK_EVENT_CONTROLLER (click_controller_right));
   g_signal_connect(click_gesture_right, "pressed", G_CALLBACK (on_right_click_event), NULL);

   // Create event controller for mouse scroll
   GtkEventController *scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
   gtk_widget_add_controller (drawing_area, scroll_controller);
   g_signal_connect(scroll_controller, "scroll", G_CALLBACK (on_scroll_event), NULL);

   // Create event controller for keyboard keys
   GtkEventController *key_controller = gtk_event_controller_key_new();
   gtk_widget_add_controller (window, key_controller); // window and not drawing_area required !!!
   g_signal_connect(key_controller, "key-pressed", G_CALLBACK (on_key_event), NULL);

   // Connext signal notiy to detect change in window (e.g. fullsceren)
   g_signal_connect(window, "notify", G_CALLBACK(on_fullscreen_notify), NULL);

   g_timeout_add_seconds (GPS_TIME_INTERVAL, update_GPS_callback, gpsInfo);
   
   gtk_box_append (GTK_BOX (vBox), toolBox);
   gtk_box_append (GTK_BOX (vBox), labelInfoRoute);
   gtk_box_append (GTK_BOX (vBox), drawing_area);
   gtk_box_append (GTK_BOX (vBox), statusbar);
   statusBarUpdate ();
   gtk_window_present (GTK_WINDOW (window));   
}

/*! for app_startup */
static void createAction (char *actionName, void *callBack) {
   GSimpleAction *act_item = g_simple_action_new (actionName, NULL);
   g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (act_item));
   g_signal_connect (act_item, "activate", G_CALLBACK (callBack), NULL);
}

/*! for app_startup */
static void createActionWithParam (char *actionName, void *callBack, int param) {
   //static int zero = 0;
   //static int one = 1;
   GSimpleAction *act_item = g_simple_action_new (actionName, NULL);
   g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (act_item));
   //g_signal_connect_data (act_item, "activate", G_CALLBACK(callBack), (param == 0) ? &zero : &one, NULL, 0);
   g_signal_connect_data (act_item, "activate", G_CALLBACK(callBack), GINT_TO_POINTER (param), NULL, 0);
}

/*! Menu set up */
static void app_startup (GApplication *application) {
   if (setlocale (LC_ALL, "C") == NULL) {
      fprintf (stderr, "Error in ap_startup: setlocale failed");
      return;
   }
   GtkApplication *app = GTK_APPLICATION (application);

   createActionWithParam ("gribOpen", openGrib, WIND);
   createActionWithParam ("gribInfo", gribInfo, WIND);
   createActionWithParam ("windNOAAselect", gribNoaa, WIND);
   createActionWithParam ("windURLselect", gribUrl, WIND);
   createActionWithParam ("currentGribOpen", openGrib, CURRENT);
   createActionWithParam ("currentGribInfo", gribInfo, CURRENT);
   createActionWithParam ("currentURLselect", gribUrl, CURRENT);
   createAction ("quit", quit_activated);

   createAction ("scenarioOpen", openScenario);
   createAction ("scenarioSettings", change);
   createAction ("scenarioShow", parDump);
   createAction ("scenarioSave", saveScenario);
   createAction ("scenarioEdit", editScenario);
   createAction ("scenarioInfo", parInfo);

   createAction ("polarOpen", openPolar);
   createActionWithParam ("polarDraw", polar_draw_activated, POLAR);
   createActionWithParam ("wavePolarDraw", polar_draw_activated, WAVE_POLAR);

   createAction ("isochrones", isocDump);
   createAction ("isochronesDesc", isocDescDump);
   createAction ("orthoReport", niceWayPointReport);
   createAction ("routeGram", routeGram);
   createAction ("sailRoute", rteDump);
   createAction ("sailReport", rteReport);
   createAction ("simulationReport", simulationReport);
   createAction ("dashboard", dashboard);
   createAction ("gps", gpsDump);

   createAction ("historyReset", historyReset);
   createAction ("sailHistory", rteHistory);

   createAction ("poiDump", poiDump);
   createAction ("poiSave", poi_save_activated);
   createActionWithParam ("poiEdit", poiEdit, POI_SEL);
   createActionWithParam ("portsEdit", poiEdit, PORT_SEL);
   
   createAction ("traceAdd", traceAdd);
   createAction ("traceReport", traceReport);
   createAction ("traceDump", traceDump);
   createAction ("openTrace", openTrace);
   createAction ("newTrace", newTrace);
   createAction ("editTrace", editTrace);

   createAction ("help", help);
   createActionWithParam ("OSM0", openMap, 0); // open Street Map
   createActionWithParam ("OSM1", openMap, 1); // open Sea Map
   createAction ("windy", windy);
   createAction ("CLI", cli_activated);
   createAction ("info", helpInfo);

   GMenu *menubar = g_menu_new ();
   GMenuItem *file_menu = g_menu_item_new ("_Grib", NULL);
   GMenuItem *polar_menu = g_menu_item_new ("_Polar", NULL);
   GMenuItem *scenario_menu = g_menu_item_new ("_Scenario", NULL);
   GMenuItem *display_menu = g_menu_item_new ("_Display", NULL);
   GMenuItem *history_menu = g_menu_item_new ("_History", NULL);
   GMenuItem *poi_menu = g_menu_item_new("PO_I", NULL);
   GMenuItem *trace_menu = g_menu_item_new("_Trace", NULL);
   GMenuItem *help_menu = g_menu_item_new("_Help", NULL);

   // Add elements for "Grib" menu
   GMenu *file_menu_v = g_menu_new ();
   subMenu (file_menu_v, "Wind: Open Grib", "app.gribOpen", "folder");
   subMenu (file_menu_v, "Wind: Grib Info", "app.gribInfo", "applications-engineering-symbolic");
   subMenu (file_menu_v, "Wind: Meteoconsult", "app.windURLselect", "applications-engineering-symbolic");
   subMenu (file_menu_v, "Wind: NOAA", "app.windNOAAselect", "applications-engineering-symbolic");
   subMenu (file_menu_v, "Current: Open Grib", "app.currentGribOpen", "folder");
   subMenu (file_menu_v, "Current: Grib Info", "app.currentGribInfo", "applications-engineering-symbolic");
   subMenu (file_menu_v, "Current: Meteoconsult", "app.currentURLselect", "applications-engineering-symbolic");
   subMenu (file_menu_v, "Quit", "app.quit", "application-exit-symbolic");

   // Add elements for "Polar" menu
   GMenu *polar_menu_v = g_menu_new ();
   subMenu (polar_menu_v, "Polar or Wave Polar open", "app.polarOpen", "folder-symbolic");
   subMenu (polar_menu_v, "Polar Draw", "app.polarDraw", "utilities-system-monitor-symbolic");
   subMenu (polar_menu_v, "Wave Polar Draw", "app.wavePolarDraw", "x-office-spreadsheet-symbolic");

   // Add elements for "Scenario" menu
   GMenu *scenario_menu_v = g_menu_new ();
   subMenu (scenario_menu_v, "Open", "app.scenarioOpen", "folder-symbolic");
   subMenu (scenario_menu_v, "Settings", "app.scenarioSettings", "preferences-desktop");
   subMenu (scenario_menu_v, "Show", "app.scenarioShow", "document-open-symbolic");
   subMenu (scenario_menu_v, "Save", "app.scenarioSave", "media-floppy-symbolic");
   subMenu (scenario_menu_v, "Edit", "app.scenarioEdit", "document-edit-symbolic");
   subMenu (scenario_menu_v, "Info Parameters", "app.scenarioInfo", "document-properties-symbolic");
   
   // Add elements for "Display" menu
   GMenu *display_menu_v = g_menu_new ();
   if (par.techno) {
      subMenu (display_menu_v, "Isochrones", "app.isochrones", "");
      subMenu (display_menu_v, "Isochrones Descriptors", "app.isochronesDesc", "");
   }
   subMenu (display_menu_v, "Way Points Report", "app.orthoReport", "");
   subMenu (display_menu_v, "Routegram", "app.routeGram", "");
   subMenu (display_menu_v, "Sail Route", "app.sailRoute", "");
   subMenu (display_menu_v, "Sail Report", "app.sailReport", "");
   subMenu (display_menu_v, "Simulation Report", "app.simulationReport", "");
   subMenu (display_menu_v, "Dashboard", "app.dashboard", "");
   subMenu (display_menu_v, "GPS", "app.gps", "");

   // Add elements for "History" menu
   GMenu *history_menu_v = g_menu_new ();
   subMenu (history_menu_v, "Reset", "app.historyReset", "document-open-symbolic");
   subMenu (history_menu_v, "Sail History Routes", "app.sailHistory", "");

   // Add elements for "Poi" (Points of interest) menu
   GMenu *poi_menu_v = g_menu_new ();
   subMenu (poi_menu_v, "Report", "app.poiDump", "document-open-symbolic");
   subMenu (poi_menu_v, "Save", "app.poiSave", "media-floppy-symbolic-symbolic");
   subMenu (poi_menu_v, "Edit PoI", "app.poiEdit", "document-edit-symbolic");
   subMenu (poi_menu_v, "Edit Ports", "app.portsEdit", "document-edit-symbolic");

   // Add elements for "Trace" menu
   GMenu *trace_menu_v = g_menu_new ();
   subMenu (trace_menu_v, "Add POR", "app.traceAdd", "");
   subMenu (trace_menu_v, "Report", "app.traceReport", "");
   subMenu (trace_menu_v, "Dump", "app.traceDump", "");
   subMenu (trace_menu_v, "Open", "app.openTrace", "");
   subMenu (trace_menu_v, "New", "app.newTrace", "");
   subMenu (trace_menu_v, "Edit", "app.editTrace", "");

   // Add elements for "Help" menu
   GMenu *help_menu_v = g_menu_new ();
   subMenu (help_menu_v, "Help", "app.help", "");
   subMenu (help_menu_v, "Windy", "app.windy", "");
   subMenu (help_menu_v, "Open Street Map", "app.OSM0", "");
   subMenu (help_menu_v, "Open Sea Map", "app.OSM1", "");
   subMenu (help_menu_v, "CLI mode", "app.CLI", "text-x-generic");
   subMenu (help_menu_v, "Info", "app.info", "");

   //g_menu_item_set_submenu (menu_item_menu, G_MENU_MODEL (menu));
   g_menu_item_set_submenu (file_menu, G_MENU_MODEL (file_menu_v));
   g_object_unref (file_menu_v);
   g_menu_item_set_submenu (polar_menu, G_MENU_MODEL (polar_menu_v));
   g_object_unref (polar_menu_v);
   g_menu_item_set_submenu (scenario_menu, G_MENU_MODEL (scenario_menu_v));
   g_object_unref (scenario_menu_v);
   g_menu_item_set_submenu (display_menu, G_MENU_MODEL (display_menu_v));
   g_object_unref (display_menu_v);
   g_menu_item_set_submenu (history_menu, G_MENU_MODEL (history_menu_v));
   g_object_unref (history_menu_v);
   g_menu_item_set_submenu (poi_menu, G_MENU_MODEL (poi_menu_v));
   g_object_unref (poi_menu_v);
   g_menu_item_set_submenu (trace_menu, G_MENU_MODEL (trace_menu_v));
   g_object_unref (trace_menu_v);
   g_menu_item_set_submenu (help_menu, G_MENU_MODEL (help_menu_v));
   g_object_unref (help_menu_v);

   g_menu_append_item (menubar, file_menu);
   g_menu_append_item (menubar, polar_menu);
   g_menu_append_item (menubar, scenario_menu);
   g_menu_append_item (menubar, display_menu);
   g_menu_append_item (menubar, history_menu);
   g_menu_append_item (menubar, poi_menu);
   g_menu_append_item (menubar, trace_menu);
   g_menu_append_item (menubar, help_menu);

   g_object_unref (file_menu);
   g_object_unref (polar_menu);
   g_object_unref (scenario_menu);
   g_object_unref (display_menu);
   g_object_unref (history_menu);
   g_object_unref (poi_menu);
   g_object_unref (trace_menu);
   g_object_unref (help_menu);

   gtk_application_set_menubar (GTK_APPLICATION (app), G_MENU_MODEL (menubar));
}

/*! Display main menu and make initializations */
int main (int argc, char *argv[]) {
   bool ret = true;

   vOffsetLocalUTC = offsetLocalUTC ();
   printf ("LocalTime - UTC: %.0lf hours\n", vOffsetLocalUTC / 3600.0);

   if (setlocale (LC_ALL, "C") == NULL) {              // very important for scanf decimal numbers
      fprintf (stderr, "Error in main: setlocale failed");
      return EXIT_FAILURE;
   }

   strncpy (parameterFileName, PARAMETERS_FILE, sizeof (parameterFileName));
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

   g_thread_new ("GPS", getGPS, &par.gpsType);  // launch GPS with gpsType info
   initZone (&zone);
   initDispZone ();
   
   if (par.mostRecentGrib)                      // most recent grib will replace existing grib
      initWithMostRecentGrib ();
   initScenario ();      

   if (par.isSeaFileName [0] != '\0')
      readIsSea (par.isSeaFileName);
   printf ("readIsSea done : %s\n", par.isSeaFileName);
   updateIsSeaWithForbiddenAreas ();
   printf ("update isSea   : with Forbid Areas done\n");
   
   for (int i = 0; i < par.nShpFiles; i++) {
      initSHP (par.shpFileName [i]);
      printf ("SHP file loaded: %s\n", par.shpFileName [i]);
   }

   printf ("Working dir    : %s\n", par.workingDir); 
   printf ("Editor         : %s\n", par.editor); 
   printf ("Spreadsheet    : %s\n", par.spreadsheet); 
   printf ("poi File Name  : %s\n", par.poiFileName);
   printf ("poi File Name  : %s\n", par.portFileName);
   printf ("nPoi           : %d\n", nPoi);
  
   app = gtk_application_new (APPLICATION_ID, 0);
   g_signal_connect (app, "startup", G_CALLBACK (app_startup), NULL); 
   g_signal_connect (app, "activate", G_CALLBACK (app_activate), NULL);
   //printf ("launching...\n");
   ret = g_application_run (G_APPLICATION (app), 0, NULL);
  
   g_object_unref (app);
   freeSHP ();
   free (tIsSea);
   return ret;
}

