/*! nd\mainpage Routing for sail software
 * \brief Small routing software written in C language with GTK4
 * \author Rene Rigault
 * \version 0.1
 * \date 2025
 * \section Compilation
 * \li see "ccall4" file
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
// #include <webkit2/webkit2.h>
#include "shapefil.h"
#include "rtypes.h"
#include "rutil.h"
#include "grib.h"
#include "polar.h"
#include "inline.h"
#include "engine.h"
#include "option.h"
#include "aisgps.h"
#include "mailutil.h"
#include "editor.h"
#include "dashboardVR.h"
#include "displaytext.h"
#include "rshputil.h"

#ifdef _WIN32
static const bool windowsOS = true;
#else
static const bool windowsOS = false;
#endif

#define WARNING_NMEA          "If you want to reinit port:\n\
1. Ununplug and replug ports,\n\
2. Enter sysadmin password,\n\
3. Click OK, \n\
4. Exit and Relaunch application."

#define EPSILON               0.0000001
#define MAX_N_SURFACE         (24*MAX_N_DAYS_WEATHER + 1)            // 16 days with timeStep 1 hour
#define MAIN_WINDOW_DEFAULT_WIDTH  1800
#define MAIN_WINDOW_DEFAULT_HEIGHT 800
#define APPLICATION_ID        "com.routing"  
#define MIN_ZOOM_POI_VISIBLE  30
#define BOAT_UNICODE          "⛵"
#define DESTINATION_UNICODE   "🏁"
#define CAT_UNICODE           "\U0001F431"
#define ORTHO_ROUTE_PARAM     20             // for drawing orthodromic route
#define MAX_TEXT_LENGTH       20             // in polar
#define POLAR_WIDTH           900
#define POLAR_HEIGHT          800
#define REPORT_WIDTH          500
#define DISP_NB_LAT_STEP      10
#define DISP_NB_LON_STEP      10
#define EXEC_TIME_OUT         120000
#define ROUTING_TIME_OUT      1000
#define MAIL_GRIB_TIME_OUT    5000
#define READ_GRIB_TIME_OUT    500
#define MIN_MOVE_FOR_SELECT   50             // minimum move to launch smtp grib request after selection
#define MIN_POINT_FOR_BEZIER  10             // minimum number of point to select bezier representation
#define K_LON_LAT            (0.71)          // lon deformation
#define LON_LAT_RATIO         2.8            // ratio betwwen lon and lat for display
#define GPS_TIME_INTERVAL     2              // seconds
#define MAX_N_ANIMATION       6              // number of values of speed display => animation.tempo
#define DASHBOARD_MIN_SPEED   1
#define DASHBOARD_MAX_SPEED   20
#define DASHBOARD_RADIUS      100.0
#define WAVE_MULTIPLICATOR    5              // for wave display in meters
#define RAIN_MULTIPLICATOR    100000         // for rain display kg m-2 s-1
#define MAX_TIME_SCALE        5760           // for timeScale display. A multiplier of 2,3,4,5,6,8,10,12,16,18,24,48i...
#define MAIL_TIME_OUT         5              // second
#define MAX_N_TRY             4              // max try number for meteoconsult download
#define MAX_LEVEL_POI_VISIBLE 5              // for point of interest visibility
#define SEP_WIDTH             40             // separator in menus
#define MAX_VISIBLE_SHORTNAME 10             // for gribInfo. Maximum number of shortname displayed
#define YELLOW                5              // consistency with colorStr
#define DEFAULT_GRIB_TIME_STEP         3     // for gribRequestBox
#define DEFAULT_GRIB_TIME_STEP_INDEX   1     // idem. The index.        

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

/*! Structure storing data required to update Grib request. Used for: 
   - web download (NOAA, ECMWF, ARPEGE, AROME...
   - meteoconsilt download (Wind and Current)
   - mail request (saildocs, mailasail)
*/
typedef struct {
   GtkSpinButton *latMinSpin;   // for web and mail
   GtkSpinButton *lonLeftSpin;  // for web and mail
   GtkSpinButton *latMaxSpin;   // for web and mail
   GtkSpinButton *lonRightSpin; // for web and mail
   GtkSpinButton *timeMaxSpin;  // for web and mail
   GtkWidget     *hhZBuffer;    // for web download and meteoconsult
   GtkTextBuffer *urlBuffer;    // for web
   GtkWidget     *dropDownTimeStep;  // for web
   GtkWidget     *dropDownServ; // for web and mail
   GtkWidget     *dropDownMail; // for mail
   GtkWidget     *warning;      // for web and mail
   GtkWidget     *sizeEval;     // for web
   bool onlyDownload;           // true if only download in file, without decoding and loading
   int typeWeb;                 // for web and mail: NOAA_WIND or ECMWF_WIND or ARPEGE_WIND or AROME_WIND or MAIL 
   int hhZ;                     // for Web download ans meteoconsult. Grib time run; 007, 06Z, ...
   int mailService;             // for mail
   int latMax;                  // for web and mail
   int latMin;                  // for web and mail
   int lonLeft;                 // for web and mail
   int lonRight;                // for web and mail
   int timeStep;                // for web and mail. in hours
   int timeMax;                 // // for web and mail. in days
   char url      [MAX_SIZE_MESSAGE * 4]; // for web download and meteoconsult
   char fileName [MAX_SIZE_LINE];        // for all
   char object   [MAX_SIZE_MESSAGE];     //  for mail
   char body     [MAX_SIZE_MESSAGE];     // for mail
   GtkWidget  *urlEntry;            // for meteoconsult
   int        index;                // for meteoconsult: geo zone (ex: ANTILLES, EUROPE, ...)
   int        urlType;              // for meteoconsult: WIND or CURRENT
} GribRequestData;

static GribRequestData gribRequestData;

static cairo_surface_t *surface [MAX_N_SURFACE];
static cairo_t *surface_cr [MAX_N_SURFACE];
static char existSurface [MAX_N_SURFACE] = {false};

static double vOffsetLocalUTC; // store difference in seconds between local time and UTC

static const char *arrayTStep [] = {"15 mn", "30 mn", "1 h", "2 h", "3 h",  NULL}; // for time step combo box

#define N_COLORS 10        // Colors for Polar curve
static const GdkRGBA colors [N_COLORS] = {{1.0,0,0,1}, {0,1.0,0,1},  {0,0,1.0,1},{0.5,0.5,0,1},{0,0.5,0.5,1},{0.5,0,0.5,1},{0.2,0.2,0.2,1},{0.4,0.4,0.4,1},{0.8,0,0.2,1},{0.2,0,0.8,1}};

#define MAX_N_COLOR_SHIP 6 // ship colors
static const GdkRGBA colShip [MAX_N_COLOR_SHIP] = {{0.0, 0.0, 1.0, 1.0}, {1.0, 0.0, 0.0, 1.0}, {1.0, 165.0/255.0, 0.0, 1.0}, {0.5, 0.5, 0.5, 1.0}, {0.0, 1.0, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}};


#define N_WIND_COLORS 6    // wind color palette
static guint8 colorPalette [N_WIND_COLORS][3] = {{0,0,255}, {0, 255, 0}, {255, 255, 0}, {255, 153, 0}, {255, 0, 0}, {139, 0, 0}};
// blue, green, yellow, orange, red, blacked red
static guint8 bwPalette [N_WIND_COLORS][3] = {{250,250,250}, {200, 200, 200}, {170, 170, 170}, {130, 130, 130}, {70, 70, 70}, {10, 10, 10}};
static const double tTws [] = {0.0, 15.0, 20.0, 25.0, 30.0, 40.0};

// sail color for routeGram
const GdkRGBA sailColor [MAX_N_SAIL] = {{0.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}, {0.502, 0.0, 0.502, 1.0}, \
      {0.0, 0.0, 0.0, 1.0}, {0.0, 0.0, 1.0, 1.0}, {1.0, 1.0, 0.8, 1.0}, {0.0, 0.0, 0.0, 1.0},{1.0, 0.0, 0.0, 1.0}};

static int    readGribRet;                         // to check if readGrib is terminated
static int    gloStatusMailRequest = 0;            // global status of grib mail request
static char   sysAdminPw [MAX_SIZE_NAME];          // sysadmin password for reseting NMEA ports (Unix)
static char   parameterFileName [MAX_SIZE_FILE_NAME];
static int    selectedPol = 0;                     // select polar to draw. 0 = all
static double selectedTws = 0.0;                   // selected TWS for polar draw
static double polarCenterX;                        // center of polar x
static double polarCenterY;                        // center of polar y
static guint  gribMailTimeout;
static guint  routingTimeout;
static guint  gribReadTimeout;
static bool   destPressed = true;
static bool   polygonStarted = false;
static bool   selecting = FALSE;
static bool   updatedColors = false; 
static double theTime = 0.0;                       // number of decimal hours after befinning of Grib
static int    polarType = WIND_POLAR;
static int    segmentOrBezier = SEGMENT;
static int    selectedPointInLastIsochrone = 0;
static int    typeFlow = WIND;                     // for openGrib.. WIND or CURRENT.
static bool   gpsTrace = false;                    // used to trace GPS position only one time per session
static char   traceName [MAX_SIZE_FILE_NAME];      // global var for newTrace ()
static char   selectedPort [MAX_SIZE_NAME];
static GThread *gribThread;                        // global var for grib download and decoding threads
static GThread *runThread;                         // global var for isochrone run threads
static GMutex warningMutex;                        // mutex for warnin message
static char   statusbarWarningStr [MAX_SIZE_TEXT]; // global var to store warning string
static struct tm startInfo =  {0};                 // initilized with fCalendar

/*! struct for animation */
static struct {
   int    tempo [MAX_N_ANIMATION];
   guint  timer;
   int    active;
} animation = {{1000, 500, 200, 100, 50, 20}, 0, NO_ANIMATION};

/*! struct for dashboard */
static struct {
   GtkWidget *hourPosZone;
   GtkWidget *speedometer;
   GtkWidget *compass;
   GtkWidget *textZone;
   int timeoutId;
} widgetDashboard = {NULL, NULL, NULL, NULL, 0};

static GtkApplication *app;

static GtkWidget *waitWindow = NULL;        // window for wait message
static GtkWidget *statusbar = NULL;         // the status bar
static GtkWidget *polStatusbar = NULL;      // polar status bar
static GtkWidget *window = NULL;            // main Window 
static GtkWidget *polarDrawingArea = NULL;  // polar
static GtkWidget *drawing_area = NULL;      // main window
static GtkWidget *menuWindow = NULL;        // Pop up menu Right Click
static GtkWidget *menuHist = NULL;          // Pop up menu History Routes
static GtkWidget *labelInfoRoute;           // label line for route info
static GtkWidget* timeScale = NULL;         // time scale

static Coordinates whereWasMouse, whereIsMouse, whereIsPopup;

/*! displayed zone */
static DispZone dispZone;

static void initScenario ();
static void circle (cairo_t *cr, double lon, double lat, double red, double green, double blue);
static void meteogram ();
static void onRunButtonClicked ();
static void destroySurface ();
static void onCheckBoxToggled (GtkWidget *checkbox, gpointer userData);
static void drawAis (cairo_t *cr);
static void drawShip (cairo_t *cr, const char *name, double x, double y, int type, int cog);
static void gribRequestBox (GribRequestData *data);
static void *mailGribRequest ();
static gboolean routingCheck (gpointer data);
static gboolean mailGribCheck (gpointer data);
static void competitorsDump();
static void *readGribLaunch ();
static void cbDropDownTStep (GObject *dropDown, GParamSpec *pspec, gpointer userData);
static void cleanAll ();
static void simulationReport ();
static void routeGram ();
static void calculateOrthoRoute ();
static int  findIndexInRoute (SailRoute *route, double t);

/*! destroy the child window before exiting application */
static void onParentDestroy (GtkWidget *widget, gpointer childWindow) {
   if ((widget != NULL) && GTK_IS_WIDGET (childWindow))
      gtk_window_destroy (GTK_WINDOW (childWindow));
}

/*! destroy the popWindow window before exiting application */
static void onParentWindowDestroy(GtkWidget *widget, gpointer popWindow) {
   if (GTK_IS_WIDGET (popWindow)) {
      gtk_widget_unparent (popWindow);
   }
}

/*! stop child thread */
static void stopChildThread () {
   // grib threads, timers and global status
   if (gribThread != NULL)
      g_thread_unref (gribThread);
   gribThread = NULL;
   g_atomic_int_set (&gloStatusMailRequest, GRIB_STOPPED); // for mailGribCheck force stop
   g_atomic_int_set (&readGribRet, GRIB_STOPPED);

   if (runThread != NULL)
      g_thread_unref (runThread);
   runThread = NULL;
   g_atomic_int_set (&route.ret, ROUTING_STOPPED);         // for route calculation stop BUG
   g_atomic_int_set (&chooseDeparture.ret, STOPPED);       // to force stop of choose departure
   g_atomic_int_set (&competitors.ret, STOPPED);           // to force stop when all competitors run
   printf ("Thread killed\n");
}

/*! destroy waitMessage */
static void waitMessageDestroy () {
   if ((waitWindow != NULL) && GTK_IS_WINDOW (waitWindow))
      gtk_window_destroy (GTK_WINDOW (waitWindow));
   waitWindow = NULL;
}

/*! hide and terminate popover */
static void popoverFinish (GtkWidget *pop) {
   if (pop != NULL) {
     // gtk_widget_hide (pop);
     gtk_widget_set_visible (pop, false);
     gtk_widget_unparent (pop);
   }
}

/*! intSpin */
static void intSpinUpdate (GtkSpinButton *spinButton, gpointer userData) {
   int *value = (int *) userData;
   *value = gtk_spin_button_get_value_as_int (spinButton);
}

/*! intSpin  */
static void intSpinUpdateWithQueueDraw (GtkSpinButton *spinButton, gpointer userData) {
   int *value = (int *) userData;
   *value = gtk_spin_button_get_value_as_int (spinButton);
   gtk_widget_queue_draw (drawing_area); // Draw all
}

/*! hour Spin for onChooseDepartureButtonClicked */
static void hourSpinUpdate (GtkSpinButton *spinButton, gpointer userData) {
   double *value = (double *) userData;
   int hours = gtk_spin_button_get_value_as_int (spinButton);
   *value = fmod (*value, 1.0) + hours;
}

/*! hour min for onChooseDepartureButtonClicked */
static void minSpinUpdate (GtkSpinButton *spinButton, gpointer userData) {
   double *value = (double *) userData;
   int min = gtk_spin_button_get_value_as_int (spinButton);
   *value = floor (*value) + (double) min / 60.0;
}

/*! udpate title of window with right grib file name */
static void titleUpdate () {
   char str [MAX_SIZE_LINE];
   snprintf (str, sizeof (str), "%s %s %s", PROG_NAME, PROG_VERSION, par.gribFileName);
   gtk_window_set_title (GTK_WINDOW(window), str);
}

/*! draw waiting for something message */
static void waitMessage (const char *title, const char* message) {
   waitWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (waitWindow), title);
   gtk_widget_set_size_request (waitWindow, 300, -1);

   gtk_window_set_default_size (GTK_WINDOW (waitWindow), 300, 100);
   gtk_window_set_transient_for (GTK_WINDOW (waitWindow), GTK_WINDOW (window)); // Ensure it's above parent window
   //gtk_window_set_modal(GTK_WINDOW (waitWindow), TRUE); // Modal is a good idea to prevent interaction with the parent window
   g_signal_connect (waitWindow, "destroy", G_CALLBACK (stopChildThread), NULL);
   //g_signal_connect (window, "destroy", G_CALLBACK (onParentDestroy), waitWindow);

   GtkWidget *label = gtk_label_new (message);
   gtk_window_set_child (GTK_WINDOW (waitWindow), label);

   gtk_window_present (GTK_WINDOW (waitWindow));
}

/*! draw info message GTK_MESSAGE_INFO, GTK_MESSAGE_WARNING ou GTK_MESSAGE_ERROR */
static void infoMessage (const char *message, GtkMessageType typeMessage) {
   if (typeMessage < 0 || typeMessage > 3) return;
   const char *strType [4] = {"Info", "Warning", "", "Error"};
   GtkAlertDialog *alertBox = gtk_alert_dialog_new ("%s: %s", strType [typeMessage], message);
   gtk_alert_dialog_show (alertBox, GTK_WINDOW (window));
}

/*! Confirm Box OK Cancel. Launch of callBack if OK
static void confirmBox (const char *line, void (*callBack)()) {
   GtkAlertDialog *confirmBox = gtk_alert_dialog_new ("%s", line);
   const char *labels[] = { "_OK", "_Cancel", NULL};
   gtk_alert_dialog_set_buttons (GTK_ALERT_DIALOG(confirmBox), labels);
   gtk_alert_dialog_choose (confirmBox, GTK_WINDOW (window), NULL, callBack, NULL);
}
*/

/*! callback for entryBox change */
static void entryBoxChange (GtkEditable *editable, gpointer userData) {
   char *name = (char *) userData;
   const char *text = gtk_editable_get_text (editable);
   g_strlcpy (name, text, MAX_SIZE_TEXT);
   // printf ("%s\n", name);
}

/* action when Cancel is clicked */
static void onCancelButtonClicked (GtkWidget *widget, gpointer theWindow) {
   if ((theWindow != NULL) && GTK_IS_WINDOW (theWindow))
      gtk_window_destroy (GTK_WINDOW (theWindow));
}

/*! OK Cancel Line. Launch func if OK */
static GtkWidget *OKCancelLine (void (*func)(), gpointer theWindow) {
   GtkWidget *hBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *OKbutton = gtk_button_new_with_label ("OK");
   GtkWidget *cancelButton = gtk_button_new_with_label ("Cancel");
   gtk_box_append (GTK_BOX(hBox), OKbutton);
   gtk_box_append (GTK_BOX(hBox), cancelButton);
   g_signal_connect (OKbutton, "clicked", G_CALLBACK (func), theWindow);
   g_signal_connect (cancelButton, "clicked", G_CALLBACK (onCancelButtonClicked), theWindow);
   return hBox;
}

/*! draw entry Box and launch callback if OK */
static void entryBox (const char *title, const char *message, char *strValue, void  (*callBack) ()) {
   GtkWidget *entryWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (entryWindow), title);
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), entryWindow);
   
   GtkWidget *vBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (entryWindow), vBox);
   
   GtkWidget *hBox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *label = gtk_label_new (message);

   GtkEntryBuffer *buffer = gtk_entry_buffer_new (strValue, -1);
   GtkWidget *entryValue = gtk_entry_new_with_buffer (buffer);
   int min_width = MAX (300, strlen (strValue) * 10);
   gtk_widget_set_size_request (entryValue, min_width, -1);

   gtk_box_append (GTK_BOX(hBox0), label);
   gtk_box_append (GTK_BOX(hBox0), entryValue);

   GtkWidget *hBox1 = OKCancelLine (callBack, entryWindow);

   g_signal_connect (entryValue, "changed", G_CALLBACK (entryBoxChange), strValue);
   gtk_box_append (GTK_BOX(vBox), hBox0);
   gtk_box_append (GTK_BOX(vBox), hBox1);

   gtk_window_present (GTK_WINDOW (entryWindow));
}

/* Dialog box for file selection */
static GtkFileDialog* selectFile (const char *title, const char *dirName, const char *nameFilter, const char *strFilter0, const char *strFilter1, const char *initialFile) {
   char directory [MAX_SIZE_DIR_NAME];

   GtkFileDialog* fileDialog = gtk_file_dialog_new ();
   gtk_file_dialog_set_title (fileDialog, title);

   // initial folder   
   snprintf (directory, sizeof (directory), "%s%s", par.workingDir, dirName);
   GFile *gDirectory = g_file_new_for_path (directory);
   gtk_file_dialog_set_initial_folder (fileDialog, gDirectory);
   g_object_unref (gDirectory);

   // initial file
   if (initialFile != NULL && initialFile[0] != '\0') {
      GFile *gInitialFile = g_file_new_for_path(initialFile);
      gtk_file_dialog_set_initial_file(fileDialog, gInitialFile);
      g_object_unref(gInitialFile);
   }
   
   // filter
   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name (filter, nameFilter);
   gtk_file_filter_add_pattern (filter, strFilter0);
   gtk_file_filter_add_pattern (filter, strFilter1);

   GListStore *filter_list = g_list_store_new (GTK_TYPE_FILE_FILTER);
   g_list_store_append (filter_list, filter);

   gtk_file_dialog_set_filters (fileDialog, (GListModel *) filter_list);
   g_object_unref (filter);
   
   return fileDialog;
}

/*! for remote files, no buttons 
static void viewer (const char *url) {
   const int webWidth = 800;
   const int webHeight = 450;
   char title [MAX_SIZE_LINE];
   snprintf  (title, sizeof (title), "View: %s", url);
   GtkWidget *viewerWindow = gtk_application_window_new (app);
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), viewerWindow);
   gtk_window_set_default_size (GTK_WINDOW(viewerWindow), webWidth, webHeight);
   gtk_window_set_title(GTK_WINDOW(viewerWindow), title);
   // g_signal_connect(G_OBJECT(viewerWindow), "destroy", G_CALLBACK(gtk_window_destroy), NULL);
   
   GtkWidget *webView = webkit_web_view_new();
   webkit_web_view_load_uri (WEBKIT_WEB_VIEW(webView), url);
   gtk_window_set_child (GTK_WINDOW (viewerWindow), (GtkWidget *) webView);
   gtk_window_present (GTK_WINDOW (viewerWindow));
}*/

/*! launch system command in a thread */
static void *commandWindy (void *data) {
   FILE *fs;
   GString *string = (GString *) data;
   // printf ("commandRun Line: %s\n", line);
   if ((fs = popen (string->str, "r")) == NULL)
      fprintf (stderr, "In commandRun, Error popen call: %s\n", string->str);
   else
      pclose (fs);
   g_free (string); 
   return NULL;
   g_string_free (string, TRUE);
}

/*! find UTC TIME at theTime 
   theTime is the number of decimal hours after the beginning of Grib */
static struct tm *tmTime (double theTime) {  
   time_t time0 = gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]); // epoch Grib Time
   time0 += offsetLocalUTC ();
   time0 += theTime * 3600.0;             // Add offset time
   struct tm *utcTime = gmtime (&time0);  // UTC conversion
   return utcTime;
}

/*! open windy using URL
   "https://www.windy.com/route-planner/45,1;45,2;46,3?2025-02-14-18,47.0,-2.0,8,d:picker"
   https://www.windy.com/route-planner/lat0,lon0;lat1,lon1,...,latn,lonN?time,lat,lon,zoom,d:picker */
static void windy (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   const char *windyUrl = "\"https://www.windy.com/route-planner/boat/";
   const int zoom = 8; // CLAMP (9 - log2 (diffLat), 2, 19);
   GString *string = g_string_new ("");
   char strTime [MAX_SIZE_DATE];
   struct tm *utcTime = tmTime ((route.n == 0) ? theTime : route.t[0].time);
   strftime (strTime, sizeof (strTime), "%Y-%m-%d-%H", utcTime);  // format Windy
   
   g_string_append_printf (string, "%s %s", par.webkit, windyUrl);

   if (route.n > 0) {
      for (int i = 0; i < route.n; i++) {
         g_string_append_printf (string, "%.6f,%.6f;", route.t[i].lat, route.t[i].lon);
      }
      if (route.destinationReached)
         g_string_append_printf (string, "%.6f,%.6f", par.pDest.lat, par.pDest.lon);
      else
         g_string_append_printf (string, "%.6f,%.6f", route.t[route.n - 1].lat, route.t[route.n - 1].lon); // no comma

   }
   else {
      calculateOrthoRoute ();
      g_string_append_printf (string, "%.6f,%.6f;", par.pOr.lat, par.pOr.lon);
      for (int i = 0; i < wayPoints.n; i++) {
         g_string_append_printf (string, "%.6f,%.6f;", wayPoints.t[i].lat, wayPoints.t[i].lon);
      }
      g_string_append_printf (string, "%.6f,%.6f", par.pDest.lat, par.pDest.lon);
   }
   g_string_append_printf (string, "?%s,%.6lf,%.6lf,%d\",d:picker", strTime, par.pOr.lat, par.pOr.lon, zoom);
   printf ("%s\n", string->str);
   g_thread_new ("windy", commandWindy, string);
}

/*! generate json description of isochrones */
static GString *generateIsochronesJson () {
   Pp pt;
   int index;
   GString *jsonString = g_string_new ("[\n");
   Pp *newIsoc = NULL; // array of points
   if ((newIsoc = malloc (MAX_SIZE_ISOC * sizeof(Pp))) == NULL) {
      fprintf (stderr, "In generateIsochronesJson: error in memory newIsoc allocation\n");
      return NULL;
   }
   
   for (int i = 0; i < nIsoc; i += MAX (1, par.stepIsocDisp)) {
      g_string_append_printf (jsonString, "   [\n"); 
      index = isoDesc [i].first;
      for (int j = 0; j < isoDesc [i].size; j++) {
         newIsoc [j] = isocArray [i * MAX_SIZE_ISOC + index];
         index += 1;
         if (index == isoDesc [i].size) index = 0;
      }
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = newIsoc [k]; 
         g_string_append_printf (jsonString, "      [%.6lf, %.6lf],\n", pt.lat, pt.lon); 
      }
      g_string_append_printf (jsonString, "   ],\n"); 
   }
   g_string_append_printf (jsonString, "]\n"); 
   free (newIsoc);
   return jsonString;
}

/*! generate json description of track boats */
static GString *generateRoutesJson (int index) {
   GString *jsonString = g_string_new ("{\n");

   // current route
   if (route.n > 0) {
      g_string_append_printf (jsonString, "\"%s\": {\n\"heading\": %.0lf, \"rank\": %d, \"duration\":%.2lf, \"totDist\":%.2lf, \"polar\":\"%s\", \"track\": [\n", 
         competitors.t[0].name, route.t [index].lCap, 0, route.duration, route.totDist, route.polarFileName);

      for (int i = 0; i < route.n; i++) {
         g_string_append_printf (jsonString, "   [%.6f, %.6f],\n", route.t[i].lat, route.t[i].lon);
      }
      if (route.destinationReached)
         g_string_append_printf (jsonString, "   [%.6f, %.6f]\n", par.pDest.lat, par.pDest.lon);
      else
         g_string_append_printf (jsonString, "   [%.6f, %.6f]\n", route.t[route.n - 1].lat, route.t[route.n - 1].lon); // no comma

      g_string_append_printf (jsonString, "]\n}\n");
   }

   // history routes
   for (int k = 0; k < historyRoute.n - 1; k += 1) {
      g_string_append_printf (jsonString, ",\n\"Rte%d\": {\n\"heading\": %.0lf, \"rank\": %d, \"track\": [\n", 
         k, historyRoute.r[k].t[index].lCap, k + 1); 

      for (int i = 0; i < historyRoute.r[k].n-1; i += 1) {
         g_string_append_printf (jsonString, "   [%.6f, %.6f],\n", historyRoute.r[k].t[i].lat, historyRoute.r[k].t[i].lon);
      }

      g_string_append_printf (jsonString, "   [%.6f, %.6f]\n", 
         historyRoute.r[k].t[historyRoute.r[k].n - 1].lat, historyRoute.r[k].t[historyRoute.r[k].n - 1].lon);

      g_string_append_printf (jsonString, "]\n}\n");
   }

   g_string_append_printf (jsonString, "}\n");
   
   return jsonString;
}

/*! open windy using API 
   creation of paramFile wich contain parameters (javascipt values)
   creation of json file for the routes
   launch navigator on local html file thar referenves javascript files */
static void windyAPI (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   /*if (route.n == 0) {
      windy (action, parameter, data);
      return;
   }*/
   bool OK = true;
   const char *windyFile = "windyindex.html";
   const char *paramFile = "windyparam.js";
   char *command = NULL;
   int index = (route.n == 0) ? 0 : findIndexInRoute (&route, theTime);
   time_t epochStartTime = gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]);
   epochStartTime += (route.n == 0) ? 0 : 3600 * route.t [index].time;
   char *fullWindyFile = g_build_filename (par.web, windyFile, NULL);
   char *fullParamFile = g_build_filename (par.web, paramFile, NULL);
   GString *string = g_string_new ("");

   g_string_append_printf (string, "let rCubeIndex = %d;\nlet rCubeStep = %0lf;\nlet rCubeEpoch = %ld;\nlet rCubeKey = \"%s\"\n", 
                           index, route.isocTimeStep * 3600.0, epochStartTime, par.windyApiKey);

   g_string_append_printf (string, "let wayPoints = [[%.6f, %.6f], ", par.pOr.lat, par.pOr.lon);
   for (int i = 0; i < wayPoints.n; i++) {
      g_string_append_printf (string, "[%.6f, %.6f],", wayPoints.t[i].lat, wayPoints.t[i].lon);
   }
   g_string_append_printf (string, "[%.6f, %.6f]];\n", par.pDest.lat, par.pDest.lon);

   if (route.n > 0) {
      GString *routes = generateRoutesJson (index);
      g_string_append_printf (string, "let routes = %s\n", routes->str);
      g_string_free (routes, TRUE);

      GString *isochrones = generateIsochronesJson ();
      g_string_append_printf (string, "let isochrones = %s\n", isochrones->str);
      g_string_free (isochrones, TRUE);
   }

   printf ("File: %s\n%s\n", fullParamFile, string->str);

   if (! g_file_set_contents (fullParamFile, string->str, -1, NULL)) {
      infoMessage ("In Windy: Impossible to  create paramFile file", GTK_MESSAGE_ERROR);
      OK = false;
   }
   if (OK && (command = malloc (MAX_SIZE_LINE)) == NULL) {
      fprintf (stderr, "In Windy Malloc: %d\n", MAX_SIZE_LINE);
      OK = false;
   } 
   if (OK) {
      snprintf (command, MAX_SIZE_LINE, "%s %s", par.webkit, fullWindyFile);
      printf ("windy command  : %s\n", command);
      g_thread_new ("windy", commandRun, command);
   }
   g_string_free (string, TRUE);
   g_free (fullWindyFile);
   g_free (fullParamFile);
}

/*! open virtual Regatta */
static void virtualRegatta (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   const char *VR_URL = "https://www.virtualregatta.com/offshore-game/";
   char *command = NULL;
   if ((command = malloc (MAX_SIZE_LINE)) == NULL) {
      fprintf (stderr, "In virtual Regatta Malloc: %d\n", MAX_SIZE_LINE);
      return;
   }   
   snprintf (command, MAX_SIZE_LINE, "%s %s", par.webkit, VR_URL);
   g_thread_new ("virtualRegatta", commandRun, command);
}

/*! Response for shom OK */
static void shomResponse (GtkWidget *widget, gpointer entryWindow) {
   const char *SHOM_URL = "https://maree.shom.fr/harbor/";
   char *command = NULL;
   if ((command = malloc (MAX_SIZE_LINE)) == NULL) {
      fprintf (stderr, "In Shom Malloc: %d\n", MAX_SIZE_LINE);
      return;
   }

   snprintf (command, MAX_SIZE_LINE, "%s %s%s/", par.webkit, SHOM_URL, selectedPort);
   printf ("shom command   : %s\n", command);
   g_thread_new ("shom", commandRun, command);
   if ((entryWindow != NULL) && GTK_IS_WINDOW (entryWindow))
      gtk_window_destroy (GTK_WINDOW (entryWindow));
   gtk_widget_queue_draw (drawing_area);
}

/*! open shom with nearest port */
static void shom (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   if (my_gps_data.OK) {
      nearestPort (my_gps_data.lat, my_gps_data.lon, "FR", selectedPort, sizeof (selectedPort));
      entryBox ("SHOM", "Nearest Port from GPS position: ", selectedPort, shomResponse);
   }
   else {
      if ((nearestPort (par.pOr.lat, par.pOr.lon, par.tidesFileName, selectedPort, sizeof (selectedPort)) != NULL) 
         && (selectedPort [0] != '\0'))
         entryBox ("SHOM", "Nearest Port from center map: ", selectedPort, shomResponse);
      else infoMessage ("No nearest port found", GTK_MESSAGE_ERROR);
   }
}

/*! open map using open street map or open sea map */
static void openMap (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   int comportement = GPOINTER_TO_INT (data);
   const char* OSM_URL [] = {"https://www.openstreetmap.org/export/", "https://map.openseamap.org/"};
   const double lat = (dispZone.latMin + dispZone.latMax) / 2.0;
   const double lon = (dispZone.lonLeft + dispZone.lonRight) / 2.0;
   const double diffLat = fabs (dispZone.latMax - dispZone.latMin);
   const double zoom = CLAMP (9 - log2 (diffLat), 2.0, 19.0);
   //printf ("diffLat : %.2lf zoom:%.2lf\n", diffLat, zoom);
   char *command = NULL;
   if ((command = malloc (MAX_SIZE_LINE)) == NULL) {
      fprintf (stderr, "In openMap Malloc: %d\n", MAX_SIZE_LINE);
      return;
   }   
   if (comportement == 0)
      snprintf (command, MAX_SIZE_LINE, "%s %sembed.html?bbox=%.4lf%%2C%.4lf%%2C%.4lf%%2C%.4lf\\&layer=mapnik\\&marker=%.4lf%%2C%.4lf", 
         par.webkit, OSM_URL[comportement], dispZone.lonLeft, dispZone.latMin, dispZone.lonRight, dispZone.latMax,
         par.pOr.lat, par.pOr.lon);
   else
      snprintf (command, MAX_SIZE_LINE, "%s %s?lat=%.4lf\\&lon=%.4lf\\&zoom=%.2lf", par.webkit, OSM_URL[comportement], lat, lon, zoom);
   g_thread_new ("openMap", commandRun, command);
}

/*! replace strFrom by strTo in file. Create a new file */
static bool replaceStrInFile (const char *fromFile, const char *toFile, const char *strFrom, const char *strTo) {
   char *content = NULL;
   gsize length = 0;

   // get template.html
   if (!g_file_get_contents (fromFile, &content, &length, NULL)) {
      fprintf (stderr, "In replaceHtmlKey: Error reading file: %s\n", fromFile);
      return false;
   }
   GString *string = g_string_new (content);

   g_string_replace (string, strFrom, strTo, 1); 

   // Write new file after replacement
   if (! g_file_set_contents (toFile, string->str, -1, NULL)) {
      fprintf (stderr, "In replaceHtmlKey: Error creating file: %s\n", toFile);
      return false;
   }
   g_free (content);
   g_string_free (string, TRUE);
   return true;
}

/*! create js file for google map*/
static bool generateGoogleMapParam (const char *fileName) {
   // build list of points
   GString *coords = g_string_new("let routeCoords =\n[\n   [\n");
   calculateOrthoRoute ();

   // Waypoints
   g_string_append_printf (coords, "      {lat: %.6f, lng: %.6f},\n", par.pOr.lat, par.pOr.lon);
   for (int i = 0; i < wayPoints.n; i++) {
      g_string_append_printf (coords, "      {lat: %.6f, lng: %.6f},\n", wayPoints.t[i].lat, wayPoints.t[i].lon);
   }
   g_string_append_printf (coords, "      {lat: %.6f, lng: %.6f}\n", par.pDest.lat, par.pDest.lon);
   g_string_append_printf (coords, "   ],\n");

   // Main route
   if (route.n > 0) {
      g_string_append_printf (coords, "[\n");
      for (int i = 0; i < route.n; i++) {
         g_string_append_printf (coords, "      {lat: %.6f, lng: %.6f},\n", route.t[i].lat, route.t[i].lon);
      }
      if (route.destinationReached)
         g_string_append_printf (coords, "      {lat: %.6f, lng: %.6f}\n", par.pDest.lat, par.pDest.lon);
      g_string_append_printf (coords, "   ],\n");
   }

   // history routes
   for (int k = 0; k < historyRoute.n - 1; k += 1) {
      g_string_append_printf (coords, "[\n");
      for (int i = 0; i < historyRoute.r[k].n; i += 1) {
         g_string_append_printf (coords, "      {lat: %.6f, lng: %.6f},\n", historyRoute.r[k].t[i].lat, historyRoute.r[k].t[i].lon);
      }
      g_string_append_printf (coords, "   ],\n");
   }
   g_string_append_printf (coords, "];");

   if (! g_file_set_contents (fileName, coords->str, -1, NULL)) {
      fprintf (stderr, "In generateHtml: Error creating file: %s\n", fileName);
      return false;
   }
   g_string_free (coords, TRUE);
   return true;
}

/*! open google map */
static void googleMap (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   const char *paramFileName = "googlemapparam.js";
   const char *templateFileName = "googlemaptemplate.html";
   const char *fileName = "googlemap.html";
   bool OK = true;

   char *fullParamFileName = g_build_filename (par.web, paramFileName, NULL);
   char *fullTemplateFileName = g_build_filename (par.web, templateFileName, NULL);
   char *fullFileName = g_build_filename (par.web, fileName, NULL);
   char *command = NULL;
   if (! generateGoogleMapParam (fullParamFileName)) {
      infoMessage ("In googleMap: Impossible to generate param file", GTK_MESSAGE_ERROR);
      OK = false;
      return;
      
   }
   if (OK && ! replaceStrInFile (fullTemplateFileName, fullFileName, "REPLACE_API_KEY", par.googleApiKey)) {
      infoMessage ("In googleMap: Impossible to get html file", GTK_MESSAGE_ERROR);
      OK = false;
      return;
   }
   if (OK && (command = malloc (MAX_SIZE_LINE)) == NULL) {
      fprintf (stderr, "In googleMap Malloc: %d\n", MAX_SIZE_LINE);
      OK = false;
      return;
   }   
   if (OK) {
      snprintf (command, MAX_SIZE_LINE, "%s %s", par.webkit, fullFileName);
      printf ("google command  : %s\n", command);
      g_thread_new ("googleMap", commandRun, command);
   }
   g_free (fullParamFileName);
   g_free (fullTemplateFileName);
   g_free (fullFileName);
}

/*! set display lonLeft, lonRight according to displayed antemeridian */
static void dispMeridianUpdate () {
   //dispZone.anteMeridian = ((lonCanonize (dispZone.lonLeft) > 0.0) && (lonCanonize (dispZone.lonRight) < 0.0));
   dispZone.lonLeft = lonNormalize (dispZone.lonLeft, dispZone.anteMeridian);
   dispZone.lonRight = lonNormalize (dispZone.lonRight, dispZone.anteMeridian);
}

/*! init of display zone */
static void initDispZone () {
   double latCenter = (zone.latMin + zone.latMax) / 2;
   double lonCenter = (zone.lonRight + zone.lonLeft) / 2;
   double deltaLat = MAX (0.1, zone.latMax - latCenter);
   double deltaLon = MAX (0.1, zone.lonLeft - lonCenter);
   dispZone.anteMeridian = zone.anteMeridian;

   if ((deltaLat * LON_LAT_RATIO) > deltaLon) {
      deltaLon = deltaLat * LON_LAT_RATIO;
      dispZone.latMin = zone.latMin;
      dispZone.latMax = zone.latMax;
      dispZone.lonLeft = lonCenter - deltaLon;
      dispZone.lonRight = lonCenter + deltaLon;
   }
   else {
      deltaLat = deltaLon / LON_LAT_RATIO;
      dispZone.lonLeft = zone.lonLeft;
      dispZone.lonRight = zone.lonRight;
      dispZone.latMin = latCenter - deltaLat;
      dispZone.latMax = latCenter + deltaLat;
   }

   dispMeridianUpdate ();
   dispZone.zoom = 180 / deltaLat;
   dispZone.latStep = fabs ((dispZone.latMax - dispZone.latMin)) / DISP_NB_LAT_STEP;
   dispZone.lonStep = dispZone.latStep * LON_LAT_RATIO * DISP_NB_LAT_STEP / DISP_NB_LON_STEP;
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
   dispMeridianUpdate ();
   dispZone.latStep = fabs ((dispZone.latMax - dispZone.latMin)) / DISP_NB_LAT_STEP;
   dispZone.lonStep = dispZone.latStep * LON_LAT_RATIO * DISP_NB_LAT_STEP / DISP_NB_LON_STEP;
   destroySurface ();
   gtk_widget_queue_draw (drawing_area);
}

/*! zoom the main window */
static void dispZoom (double z) {
   double latCenter = (dispZone.latMin + dispZone.latMax) / 2.0;
   double lonCenter = (dispZone.lonLeft + dispZone.lonRight) / 2.0;
   double deltaLat = MAX (0.1, dispZone.latMax - latCenter);
   double deltaLon = deltaLat * LON_LAT_RATIO;
   dispZone.zoom = 180 / deltaLat;
   // printf ("dispZoom: %.2lf\n", dispZone.zoom);
   if ((z < 1.0) && (deltaLat < 0.1)) return; // stop zoom
   if ((z > 1.0) && (deltaLat > 60)) return;  // stop zoom

   dispZone.latMin = latCenter - deltaLat * z;
   dispZone.latMax = latCenter + deltaLat * z;
   dispZone.lonLeft = lonCenter - deltaLon * z;;
   dispZone.lonRight = lonCenter + deltaLon * z;
   dispZone.latStep = fabs ((dispZone.latMax - dispZone.latMin)) / DISP_NB_LAT_STEP;
   dispZone.lonStep = dispZone.latStep * LON_LAT_RATIO * DISP_NB_LAT_STEP / DISP_NB_LON_STEP;
   dispMeridianUpdate ();
   destroySurface ();
   gtk_widget_queue_draw (drawing_area);
}

/*! horizontal and vertical translation of main window */
static void dispTranslate (double h, double v) {
   const double kTranslate = 0.5 * (dispZone.latMax - dispZone.latMin) / 2.0;

   dispZone.latMin += h * kTranslate;
   dispZone.latMax += h * kTranslate;
   dispZone.lonLeft += v * kTranslate;
   dispZone.lonRight += v * kTranslate;
   
   dispMeridianUpdate ();
   destroySurface ();
   gtk_widget_queue_draw (drawing_area);
}   

/*! return x as a function of longitude */
static double getX (double lon) {
   double kLat = (dispZone.yB - dispZone.yT) / (dispZone.latMax - dispZone.latMin);
   double kLon = kLat * K_LON_LAT; //cos (DEG_TO_RAD * (dispZone.latMax + dispZone.latMin)/2.0);
   lon = lonNormalize (lon, dispZone.anteMeridian);
   return kLon * (lon - dispZone.lonLeft) + dispZone.xL;
}

/*! return y as a function of latitude */
static double getY (double lat) {
   double kLat = (dispZone.yB - dispZone.yT) / (dispZone.latMax - dispZone.latMin);
   return kLat * (dispZone.latMax - lat) + dispZone.yT;
}

/*! return longitude as a function of x (inverse of getX) */
static inline double xToLon (double x) {
   double kLat = (dispZone.yB - dispZone.yT) / (dispZone.latMax - dispZone.latMin);
   double kLon = kLat * K_LON_LAT; // cos (DEG_TO_RAD * (dispZone.latMax + dispZone.latMin)/2.0);
   double lon = dispZone.lonLeft + ((x - dispZone.xL -1) / kLon);
   return lonNormalize (lon, dispZone.anteMeridian);
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

/*!x ensure x, y is in display zone 
static bool inDispZone (double x, double y) {
   return (x > dispZone.xL) && (x < dispZone.xR) && (y > dispZone.yT) && (y < dispZone.yB); 
}*/

/*! draw trace  */
static void drawTrace (cairo_t *cr) {
   FILE *f;
   char line [MAX_SIZE_LINE];
   double lat, lon, time, x, y;
   bool first = true;

   cairo_set_line_width (cr, 0.5);
   CAIRO_SET_SOURCE_RGB_BLACK (cr);

   if (par.traceFileName [0] == '\0') 
      return;
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
         else {
            cairo_line_to (cr, x, y);
         }
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
   const double epsilon = 10.0;
   surface [index] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
   surface_cr [index] = cairo_create(surface [index]);
   existSurface [index] = true;

   for (int x = 0; x < width; x++) {
      for (int y = 0; y < height; y++) {
         lat = yToLat (y);
         lon = xToLon (x);
         //if (fabs (getX (lon) - x) > epsilon)  {
         if (G_APPROX_VALUE (getX (lon), x, epsilon) && (isInZone (lat, lon, &zone))) {
            nCall += 1;
            switch (par.indicatorDisp) {
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
               val = w * WAVE_MULTIPLICATOR;
               break;
            case RAIN_DISP:
	            val = findRainGrib (lat, lon, theTime) * RAIN_MULTIPLICATOR;
               break;
            case PRESSURE_DISP:
               // normalize a value between 0 to 50 associated to 950 to 1050 hP
	            val = findPressureGrib (lat, lon, theTime) / 100; // hPa
               val -= 990;
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

/*! callback for palette */
static void cbDrawPalette (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer userData) {
   double tws;
   guint8 r, g, b;
   char label [9];

   for (int x = 0; x < width; x++) {
      tws = (double) x * 50.0 / (double) width;
      mapColors (tws, &r, &g, &b);
      cairo_set_source_rgba (cr, r/255.0, g/255.0, b/255.0, 0.5);
      cairo_rectangle (cr, x, 0, 1, height / 2);
      cairo_fill (cr);
   }
   
   for (tws = 0; tws < 50; tws += 5.0) {
      int x = (int) (tws * (double) width / 50.0);
      CAIRO_SET_SOURCE_RGB_BLACK (cr);
      cairo_move_to (cr, x, height / 2);
      cairo_line_to (cr, x, height);
      cairo_stroke (cr);

      snprintf (label, sizeof(label), "%0.2lf", 
         (par.indicatorDisp == WAVE_DISP) ? tws / WAVE_MULTIPLICATOR : 
         (par.indicatorDisp == RAIN_DISP) ? 1000 * tws / RAIN_MULTIPLICATOR : 
         (par.indicatorDisp == PRESSURE_DISP) ? 990 + tws :
         tws);
      cairo_move_to(cr, x + 5, height - 5);
      cairo_show_text(cr, label);
   }
}

/*! Palette */
static void paletteDraw () {
   GtkWidget *paletteWindow = gtk_application_window_new (app);
   switch (par.indicatorDisp) {
   case WIND_DISP: case GUST_DISP:
      gtk_window_set_title (GTK_WINDOW (paletteWindow), "TWS (knots)");
      break;
   case WAVE_DISP:
      gtk_window_set_title (GTK_WINDOW (paletteWindow), "Waves (meters)");
      break;
   case RAIN_DISP:
      gtk_window_set_title (GTK_WINDOW (paletteWindow), "Rain (1000 x kg m-2 s-1)");
      break;
   case PRESSURE_DISP:
      gtk_window_set_title (GTK_WINDOW (paletteWindow), "Pressure (hPa)");
      break;
   default:;
   }
   gtk_window_set_default_size(GTK_WINDOW (paletteWindow), 800, 100);
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), paletteWindow);
   // g_signal_connect (paletteWindow, "destroy", G_CALLBACK(gtk_window_destroy), NULL);

   GtkWidget *paletteDrawingArea = gtk_drawing_area_new();
   gtk_widget_set_size_request (paletteDrawingArea, 800, 100);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (paletteDrawingArea), cbDrawPalette, NULL, NULL);
   gtk_window_set_child (GTK_WINDOW (paletteWindow), (GtkWidget *) paletteDrawingArea);
   gtk_window_present (GTK_WINDOW (paletteWindow));
}

/*! make calculation over Ortho route */
static void calculateOrthoRoute () {
   wayPoints.t [wayPoints.n].lat = par.pDest.lat;
   wayPoints.t [wayPoints.n].lon = par.pDest.lon;

   wayPoints.t [0].lCap =  directCap (par.pOr.lat, par.pOr.lon,  wayPoints.t [0].lat,  wayPoints.t [0].lon);
   wayPoints.t [0].oCap = wayPoints.t [0].lCap + givry (par.pOr.lat, par.pOr.lon, wayPoints.t [0].lat, wayPoints.t [0].lon);
   wayPoints.t [0].ld = loxoDist (par.pOr.lat, par.pOr.lon,  wayPoints.t [0].lat,  wayPoints.t [0].lon);
   wayPoints.t [0].od = orthoDist (par.pOr.lat, par.pOr.lon,  wayPoints.t [0].lat,  wayPoints.t [0].lon);
   wayPoints.totLoxoDist = wayPoints.t [0].ld; 
   wayPoints.totOrthoDist = wayPoints.t [0].od; 
   
   for (int i = 0; i <  wayPoints.n; i++) {
      wayPoints.t [i+1].lCap = directCap (wayPoints.t [i].lat, wayPoints.t [i].lon, wayPoints.t [i+1].lat, wayPoints.t [i+1].lon); 
      wayPoints.t [i+1].oCap = wayPoints.t [i+1].lCap +\
          givry (wayPoints.t [i].lat,  wayPoints.t [i].lon, wayPoints.t [i+1].lat, wayPoints.t [i+1].lon);
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
static GtkWidget *strToLabel (const char *str0, int i) {
   char str [MAX_SIZE_LINE];
   if (i >= 0) snprintf (str, sizeof (str), "%s %d", str0, i);
   else g_strlcpy (str, str0, sizeof (str));
   GtkWidget *label = gtk_label_new (str);
   gtk_label_set_yalign(GTK_LABEL(label), 0);
   gtk_label_set_xalign(GTK_LABEL(label), 0);   
   return label;
}

/*! return label in bold associated to str */
static GtkWidget *strToLabelBold (const char *str) {
   GtkWidget *label = gtk_label_new (str);
   char *pango_markup = g_strdup_printf ("<span foreground='red' weight='bold'>%s</span>", str);
   gtk_label_set_markup (GTK_LABEL(label), pango_markup);
   g_free (pango_markup);
   
   gtk_label_set_yalign(GTK_LABEL(label), 0);
   gtk_label_set_xalign(GTK_LABEL(label), 0);   
   return label;
}

/*! dump orthodomic route into str */
static void niceWayPointReport () {
   char strLat [MAX_SIZE_NAME];
   char strLon [MAX_SIZE_NAME];
   calculateOrthoRoute ();
   GtkWidget *wpWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (wpWindow), "Orthodomic and Loxdromic Waypoint routes");
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), wpWindow);

   GtkWidget *grid = gtk_grid_new();   
   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

   gtk_window_set_child (GTK_WINDOW (wpWindow), (GtkWidget *) grid);

   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
   
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold ("Point"),      0, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold ("Lat."),       1, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold ("Lon."),       2, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold ("Ortho Cap."), 3, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold ("Ortho Dist."),4, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold ("Loxo Cap."),  5, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabelBold ("Loxo Dist."), 6, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), separator,                     0, 1, 7, 1);
  
   int i;
   for (i = -1; i <  wayPoints.n; i++) {
      if (i == -1) {
         gtk_grid_attach (GTK_GRID (grid), strToLabel ("Origin", -1),                                     0, 2, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel (latToStr (par.pOr.lat, par.dispDms, strLat, sizeof (strLat)), -1),
                                                                                                          1, 2, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel (lonToStr (par.pOr.lon, par.dispDms, strLon, sizeof (strLon)), -1),
                                                                                                          2, 2, 1, 1);
      }
      else {
         gtk_grid_attach (GTK_GRID (grid), strToLabel ("Waypoint", i),                                         0, i+3, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel (latToStr (wayPoints.t[i].lat, par.dispDms, strLat, sizeof (strLat)), -1), 
                                                                                                               1, i+3, 1, 1);
         gtk_grid_attach (GTK_GRID (grid), strToLabel (lonToStr (wayPoints.t[i].lon, par.dispDms, strLon, sizeof (strLon)), -1), 
                                                                                                               2, i+3, 1, 1);
      }
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (fmod (wayPoints.t [i+1].oCap + 360.0, 360.0)),          3, i+3, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayPoints.t [i+1].od),                                  4, i+3, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (fmod (wayPoints.t [i+1].lCap + 360.0, 360.0)),          5, i+3, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayPoints.t [i+1].ld),                                  6, i+3, 1, 1);
   }
   gtk_grid_attach (GTK_GRID (grid), strToLabel ("Destination", -1),                                 0, i+3, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabel (latToStr (par.pDest.lat, par.dispDms, strLat, sizeof (strLat)), -1), 
                                                                                                     1, i+3, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), strToLabel (lonToStr (par.pDest.lon, par.dispDms, strLon, sizeof (strLon)), -1), 
                                                                                                     2, i+3, 1, 1);
   
   i += 1;
   separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID(grid), separator,                                                     0, i+3, 7, 1);
   i += 1;
   gtk_grid_attach (GTK_GRID (grid), strToLabel ("Total Orthodomic Distance", -1),                0, i+3, 3, 1);
   gtk_grid_attach (GTK_GRID (grid), doubleToLabel (wayPoints.totOrthoDist),                      3, i+3, 1, 1);
   i += 1;
   separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID(grid), separator,                                                     0, i+3, 7, 1);
   i += 1;
   gtk_grid_attach (GTK_GRID (grid), strToLabel ("Total Loxodromic Distance", -1),                0, i+3, 3, 1);
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
   PangoLayout *layout = pango_cairo_create_layout (cr);
   PangoFontDescription *desc = pango_font_description_from_string ("DejaVuSans 16");
   pango_layout_set_font_description (layout, desc);
   pango_font_description_free (desc);

   pango_layout_set_text (layout, unicode, -1);

   cairo_move_to (cr, x, y);
   pango_cairo_show_layout (cr, layout);
   g_object_unref (layout);
}

/*! draw all isochrones stored in isocArray as points */
static gboolean drawAllIsochrones0 (cairo_t *cr) {
   double x, y;
   Pp pt;
   for (int i = 

   0; i < nIsoc; i += MAX (1, par.stepIsocDisp)) {
      GdkRGBA color = colors [i % N_COLORS];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = isocArray [i * MAX_SIZE_ISOC + k]; 
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
      pt = isocArray [i* MAX_SIZE_ISOC + isoDesc [i].closest];
      // printf ("lat: %.2lf, lon: %.2lf\n", pt.lat, pt.lon); 
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
      // printf ("lat: %.2lf, lon: %.2lf\n", lat, lon); 
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
   Pp pt;
   Pp *newIsoc = NULL; // array of points
   int index;
   if (style == NOTHING) return true;
   if (style == JUST_POINT) return drawAllIsochrones0 (cr);

   if ((newIsoc = malloc (MAX_SIZE_ISOC * sizeof(Pp))) == NULL) {
      fprintf (stderr, "In drawAllIsochrones: error in memory newIsoc allocation\n");
      return -1;
   }
   
   CAIRO_SET_SOURCE_RGB_BLUE(cr);
   cairo_set_line_width(cr, 1.0);
   for (int i = 0; i < nIsoc; i += MAX (1, par.stepIsocDisp)) {
      // GdkRGBA color = colors [i % N_COLORS];
      // cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      
      index = isoDesc [i].first;
      for (int j = 0; j < isoDesc [i].size; j++) {
         newIsoc [j] = isocArray [i * MAX_SIZE_ISOC + index];
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
   free (newIsoc);
   return FALSE;
}

/*! find index in route with closest time to t 
   -1 if terminated */
static int findIndexInRoute (SailRoute *route, double t) {
   int i;
   if (t < route->t[0].time) return 0;
   if (t > route->t[route->n - 1].time) return -1;
   for (i = 0; i < route->n; i++) {
      if (route->t[i].time > t) break;
   }
   return ((t - route->t[i-1].time) < (route->t[i].time - t)) ? i - 1 : i;
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

/*! give the focus on points in history routes at theTime */
static void focusOnPointInHistory (cairo_t *cr)  {
   if (route.n == 0) return;

   for (int k = 0; k < historyRoute.n - 1; k += 1) {
      int i = findIndexInRoute (&historyRoute.r [k], theTime);
      if ((i < 0) || (i > historyRoute.r [k].n)) return;
      if (historyRoute.r[k].n == 0) break;
      int iComp = historyRoute.r[k].competitorIndex;
      if ((i >= 0) && (i < historyRoute.r[k].n)) {
         drawShip (cr, "", getX (historyRoute.r[k].t[i].lon), getY (historyRoute.r[k].t[i].lat),
            competitors.t[iComp].colorIndex, historyRoute.r[k].t[i].lCap);
      }
   }
}

/*! give the focus on point in route at theTime  */
static void focusOnPointInRoute (cairo_t *cr, SailRoute *route)  {
   char strDate [MAX_SIZE_DATE], strLat [MAX_SIZE_NAME] = "", strLon [MAX_SIZE_NAME] = "";
   char sInfoRoute [MAX_SIZE_LINE] = "";
   int i;
   double lat, lon;
   if (route->n == 0) {
      gtk_label_set_text (GTK_LABEL(labelInfoRoute), sInfoRoute);
      return;
   }
   i = findIndexInRoute (route, theTime);
    
   if (route->destinationReached && (i == -1)) {   
      lat = par.pDest.lat;
      lon = par.pDest.lon;
      snprintf (sInfoRoute, sizeof (sInfoRoute), 
         "Destination Reached on: %s Lat: %-12s Lon: %-12s", \
         newDateWeekDay (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route->duration, strDate, sizeof (strDate)), \
         latToStr (lat, par.dispDms, strLat, sizeof (strLat)),\
         lonToStr (lon, par.dispDms, strLon, sizeof (strLon)));
   }
   else {
      if (i > route->n) {
         i = route->n - 1;
         lat = lastClosest.lat;
         lon = lastClosest.lon;
      }
      else {
         lat = route->t[i].lat;
         lon = route->t[i].lon;
      }
      int twa = fTwa (route->t[i].lCap, route->t[i].twd);
      snprintf (sInfoRoute, sizeof (sInfoRoute), 
         "Route Date: %s Lat: %-12s Lon: %-12s  COG: %4d°  SOG:%5.2lfKn  TWD:%4d°  TWA:%4d°  TWS:%5.2lfKn  Gust: %5.2lfKn  Wave: %5.2lfm   %s", \
         newDateWeekDay (zone.dataDate [0], (zone.dataTime [0]/100) + route->t[i].time, strDate, sizeof (strDate)),\
            latToStr (route->t[i].lat, par.dispDms, strLat, sizeof (strLat)),\
            lonToStr (route->t[i].lon, par.dispDms, strLon, sizeof (strLon)),\
            (int) (route->t[i].lCap + 360) % 360, route->t[i].ld/par.tStep,\
            (int) (route->t[i].twd + 360) % 360,\
            twa, route->t[i].tws,\
            MS_TO_KN * route->t[i].g, route->t[i].w,\
            (isDayLight (route->t[i].time, lat, lon)) ? "Day" : "Night");
   }
   // showUnicode (cr, BOAT_UNICODE, getX (lon), getY (lat) - 20);
   if (i >= 0) {
      int iComp = route->competitorIndex;
      drawShip (cr, "", getX (lon), getY (lat), competitors.t [iComp].colorIndex, route->t [i].lCap);
   }
   gtk_label_set_text (GTK_LABEL(labelInfoRoute), sInfoRoute);
   // circle (cr, lon, lat, 00, 0.0, 1.0); // blue
}

/* set color of route according to motor and amure */
static inline void routeColor (cairo_t *cr, bool motor, int amure) {
   if (motor) 
      CAIRO_SET_SOURCE_RGB_RED (cr);
   else {
      if (amure == BABORD) 
         CAIRO_SET_SOURCE_RGB_BLACK (cr);
      else CAIRO_SET_SOURCE_RGB_GRAY (cr);
   }
} 

/*! draw the routes based on route table, from pOr to pDest or best point */
static void drawRoute (cairo_t *cr) {
   char line [MAX_SIZE_LINE];
   int motor = route.t[0].motor;
   int amure = route.t[0].amure;
   //double x = getX (par.pOr.lon);
   //double y = getY (par.pOr.lat);
   double x = getX (route.t[0].lon);
   double y = getY (route.t[0].lat);
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

/*! draw the route based on route table index if competitor index < limit */
static void drawHistoryRoute (cairo_t *cr, int k) {
   char line [MAX_SIZE_LINE];
   double x = getX (historyRoute.r[k].t[0].lon);
   double y = getY (historyRoute.r[k].t[0].lat);
   CAIRO_SET_SOURCE_RGB_PINK (cr);
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

/*! draw competitors points */
static void drawCompetitors (cairo_t *cr) {
   double x, y;
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size (cr, 10);
   for (int i = 0; i < competitors.n; i++) {
      int iComp = competitors.t[i].colorIndex;
      GdkRGBA color = colShip [MIN (MAX_N_COLOR_SHIP - 1, iComp)];
      circle (cr, competitors.t[i].lon, competitors.t[i].lat, color.red, color.green, color.blue);
      //circle (cr, competitors.t[i].lon, competitors.t[i].lat, 0.0, 0.0, 0.0);
      x = getX (competitors.t[i].lon);
      y = getY (competitors.t[i].lat);
      cairo_move_to (cr, x+10, y);
      cairo_show_text (cr, competitors.t[i].name);
      // printf ("i: %d, lat: %.2lf, lon: %.2lf, %s\n", i, competitors.t[i].lat, competitors.t[i].lon, competitors.t[i].name);
   }
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
               char *utf8String = g_locale_to_utf8 (tPoi [i].name, -1, NULL, NULL, NULL);
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

/*! return true if rectangle defined by parameters intersects with display zone */
static bool isRectangleIntersecting (double latMin, double latMax, double lonMin, double lonMax) {
    return (latMin <= dispZone.latMax && latMax >= dispZone.latMin && lonMin <= dispZone.lonRight && lonMax >= dispZone.lonLeft);
}

/*! draw shapefile with different color for sea and earth */
static gboolean drawShpMap (cairo_t *cr) {
   register double x, y, lastX;
   register int step = 1, deb, end;
   const double xThreshold = (dispZone.xR - dispZone.xL) / 2.0; 

   // Dessiner les entités
   for (int i = 0; i < nTotEntities; i++) {
      if ((entities [i].nSHPType != SHPT_POINT) &&          // NOT that useful
         (! isRectangleIntersecting (entities[i].latMin, entities[i].latMax, entities[i].lonMin, entities[i].lonMax)))
         continue;

      step = (dispZone.zoom < 5) ? 128 
            : (dispZone.zoom < 20) ? 64 
            : (dispZone.zoom < 50) ? 32 
            : (dispZone.zoom < 100) ? 16 
            : (dispZone.zoom < 500) ? 8 
            : 1;
      
      switch (entities [i].nSHPType) {
      case SHPT_POLYGON: // case SHPT_POLYGONZ:// Traitement pour les polygones
         CAIRO_SET_SOURCE_RGBA_SHP_MAP (cr); 
         for (int iPart = 0; iPart < entities [i].maxIndex;  iPart++) {
            deb = entities [i].index [iPart];
            end = (iPart == entities [i].maxIndex - 1) ? entities [i].numPoints : entities [i].index [iPart + 1];
            x = getX (entities[i].points[deb].lon);
            y = getY (entities[i].points[deb].lat);
            lastX = x;

            cairo_move_to (cr, x, y);
            for (int j = deb + 1; j < end; j += step) {
               x = getX (entities[i].points[j].lon);
               y = getY (entities[i].points[j].lat);
               if (par.shpPointsDisp) {
                  cairo_rectangle (cr, x, y, 1, 1);
                  cairo_fill (cr);
               }
               else if (fabs (lastX - x) > xThreshold) {
                  cairo_close_path (cr);
                  cairo_fill (cr);
                  cairo_move_to (cr, x, y);
               }
               else cairo_line_to (cr, x, y);

               lastX = x;
            }
            if (! par.shpPointsDisp) {
               cairo_close_path (cr);
               // cairo_stroke_preserve(cr); // useful for borders
               cairo_fill (cr);
            }
         }
        break;
      case SHPT_ARC: //case SHPT_ARCZ: // manage lines (arcs). BE CAREFUL:  Multipart not considered
         CAIRO_SET_SOURCE_RGB_RED(cr); //lines
         x = getX (entities[i].points[0].lon);
         y = getY (entities[i].points[0].lat);
         lastX = x;
         cairo_move_to (cr, x, y);
         for (int j = 1; j < entities[i].numPoints; j += step) {
            x = getX (entities[i].points[j].lon);
            y = getY (entities[i].points[j].lat);
            if (par.shpPointsDisp) {
               cairo_rectangle (cr, x, y, 1, 1);
               cairo_fill (cr);
            }
            else if (fabs (lastX - x) > xThreshold) { // close line and open another
               cairo_stroke (cr);
               cairo_move_to (cr, x, y);
               //printf ("X_Threshold detected\n");
               //circle (cr, entities[i].points[j].lon, entities[i].points[j].lat, 0.0, 0.0, 1.0);
            }
            else cairo_line_to (cr, x, y);
            lastX = x;
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
      default: fprintf (stderr, "In drawShpMap: SHPtype unknown: %d\n", entities [i].nSHPType); 
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
   snprintf (str, sizeof (str), "%.2lf", w);
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
static void barbule (cairo_t *cr, double lat, double lon, double u, double v,\
   double tws, int typeFlow) {

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
   int barb50 = (int) tws/50;  
   int barb10 = ((int) tws % 50)/10;  
   int barb5 =  ((int) (tws) % 10) / 5; 
 
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
static void statusBarUpdate (GtkWidget *statusbar) {
   if (statusbar == NULL) return;
   if ((g_atomic_int_get (&readGribRet) == GRIB_RUNNING) || 
       (g_atomic_int_get (&competitors.ret) == RUNNING) || 
       (g_atomic_int_get (&chooseDeparture.ret) == RUNNING) ||
       (g_atomic_int_get (&route.ret) == ROUTING_RUNNING)
      )
      return;
   char sStatus [MAX_SIZE_TEXT];
   double lat, lon;
   double u = 0, v = 0, g = 0, w = 0, uCurr = 0, vCurr = 0, currTwd = 0, currTws = 0, pressure = 0;
   char seaEarth [MAX_SIZE_NAME];
   char strLat [MAX_SIZE_NAME] = "", strLon [MAX_SIZE_NAME] = "";
   double tDeltaCurrent = zoneTimeDiff (&currentZone, &zone);
   double twd, tws;
   lat = yToLat (whereIsMouse.y);
   lon = xToLon (whereIsMouse.x);
	findWindGrib (lat, lon, theTime, &u, &v, &g, &w, &twd, &tws);
	pressure = findPressureGrib (lat, lon, theTime) / 100; // hPa
   g_strlcpy (seaEarth, (isSea (tIsSea, lat, lon)) ? "Authorized" : "Forbidden", sizeof (seaEarth));
	findCurrentGrib (lat, lon, theTime - tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);
   
   snprintf (sStatus, sizeof (sStatus), "%s %s\
      Wind: %03d° %05.2lf Knots  Gust: %05.2lf Knots  Waves: %05.2lf m Current: %03d° %05.2lf Knots  Pressure: %05.0lf hPa %s   Zoom: %.2lf",\
      latToStr (lat, par.dispDms, strLat, sizeof (strLat)),\
      lonToStr (lon, par.dispDms, strLon, sizeof (strLon)),\
      (int) (twd + 360) % 360, tws, MS_TO_KN * g, w,\
      (int) (currTwd + 360) % 360, currTws,\
      pressure,\
      seaEarth,\
      dispZone.zoom);

   gtk_label_set_text (GTK_LABEL (statusbar), sStatus);
}

/*! write a red bold  error message on status bar */
static void statusErrorMessage (GtkWidget *statusbar, const char *message) {
   char *pango_markup = g_strdup_printf ("<span foreground='red' weight='bold'>%s</span>", message);
   gtk_label_set_markup (GTK_LABEL (statusbar), pango_markup);
   g_free (pango_markup);
   sleep (2);
}

/*! write a green bold warning essage on status bar */
static void statusWarningMessage (GtkWidget *statusbar, const char *message) {
   const char *animStr[] = {"↖︎","↑","↗︎","→","↘︎","↓","↙︎","←"};
   static int count = 0;
   char *pango_markup = g_strdup_printf ("<span foreground='green' weight='bold' font_family='monospace'> %s %s</span>", 
      animStr [count], message);
   count = (count + 1) % 8;
   gtk_label_set_markup (GTK_LABEL(statusbar), pango_markup);
   g_free (pango_markup);
}

/*! draw scale, scaleLen is for one degree. Modified if not ad hoc */
static void drawScale (cairo_t *cr, DispZone *dispZone) {
   const int constDelta = 4;
   char line [MAX_SIZE_LINE];
   double scaleX, scaleY, scaleLen;;
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   cairo_set_line_width (cr, 2.0);
   int val = 60;
   if ((dispZone->latMax - dispZone ->latMin) <= 2.0) return; // no scale for small zone

   // vertical
   scaleLen = (getY (dispZone->latMax - 1) - getY (dispZone->latMax)); 
   //scaleX = getX (ceil (dispZone.lonLeft));
   scaleX = 30;
   scaleY = getY (ceil (dispZone->latMin));
   cairo_move_to (cr, scaleX, scaleY);
   cairo_line_to (cr, scaleX, scaleY - scaleLen);
   cairo_stroke(cr);
   
   cairo_move_to (cr, scaleX + 15, scaleY);
   snprintf (line, MAX_SIZE_LINE, "Lat: %d miles", val);
   cairo_show_text (cr, line);
   cairo_stroke(cr);

   cairo_move_to (cr, scaleX - constDelta, scaleY);
   cairo_line_to (cr, scaleX + constDelta, scaleY);
   cairo_stroke(cr);
   cairo_move_to (cr, scaleX + constDelta, scaleY - scaleLen);
   cairo_line_to (cr, scaleX - constDelta, scaleY - scaleLen);
   cairo_stroke(cr);

   // horizontal
   scaleLen = (getX (dispZone->lonRight) - getX (dispZone->lonRight - 1))\
              / MAX (0.1, cos (DEG_TO_RAD * (dispZone->latMax + dispZone->latMin)/2));
   scaleX = getX (round ((dispZone->lonLeft + dispZone->lonRight) / 2.0 + 1));
   scaleY = getY (ceil (dispZone->latMin));
   cairo_move_to (cr, scaleX, scaleY);
   cairo_line_to (cr, scaleX + scaleLen, scaleY);
   cairo_stroke(cr);

   cairo_move_to (cr, scaleX, scaleY - 15);
   snprintf (line, MAX_SIZE_LINE, "Lon: %d miles", val);
   cairo_show_text (cr, line);
   cairo_stroke(cr);
    
   cairo_move_to (cr, scaleX, scaleY - constDelta);
   cairo_line_to (cr, scaleX, scaleY + constDelta);
   cairo_stroke(cr);
   cairo_move_to (cr, scaleX + scaleLen, scaleY - constDelta);
   cairo_line_to (cr, scaleX + scaleLen, scaleY + constDelta);
   cairo_stroke(cr);
}

/* Provide resume of information */
static void drawInfo (cairo_t *cr) {
   double lat, lon, distToDest;
   char str [MAX_SIZE_TEXT];
   char strDate [MAX_SIZE_DATE];
   const guint infoX = 30;
   const guint infoY = 50;
   const int constDelta = 50;
   cairo_select_font_face(cr, "Courier", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   CAIRO_SET_SOURCE_RGB_DARK_GRAY(cr);

   cairo_set_font_size (cr, 30.0);
   char fileName [MAX_SIZE_FILE_NAME];
   buildRootName (par.traceFileName, fileName, sizeof (fileName));
   double od, ld, rd, sog;
   if (infoDigest (fileName, &od, &ld, &rd, &sog)) {
      snprintf (str, MAX_SIZE_LINE, "Miles from Origin...: %8.2lf, Real Dist.......: %.2lf", od, rd);
   }
   else fprintf (stderr, "In drawInfo, In distance trace calculation");
   cairo_move_to (cr, infoX, infoY);
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
   cairo_move_to (cr, infoX, infoY + constDelta);
   cairo_show_text (cr, str);
   
   if (route.n > 0) {
      newDateWeekDay (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route.duration, strDate, sizeof (strDate));
      snprintf (str, sizeof (str), "By Sail to Dest.....: %8.2lf, Expected arrival: %s", route.totDist, strDate);
      cairo_move_to (cr, infoX, infoY + 2 * constDelta);
      cairo_show_text (cr, str);
   }
}

/*! draw grid with meridians and parallels */
static void drawGrid (cairo_t *cr, DispZone *dispZone, int step) {
   CAIRO_SET_SOURCE_RGB_LIGHT_GRAY (cr);
   cairo_set_line_width (cr, 0.5);
   // meridians
   for (int intLon = 0; intLon > -180; intLon -= step) {
      cairo_move_to (cr, getX (intLon), getY (MAX_LAT));
      cairo_line_to (cr, getX (intLon), getY (MIN_LAT));
      cairo_stroke(cr);
   }
   for (int intLon = 0; intLon <= 180; intLon += step) {
      cairo_move_to (cr, getX (intLon), getY (MAX_LAT));
      cairo_line_to (cr, getX (intLon), getY (MIN_LAT));
      cairo_stroke(cr);
   }
   // parallels
   for (int intLat = 0; intLat > MAX (-MAX_LAT, dispZone->latMin); intLat -= step) {
      cairo_move_to (cr, dispZone->xR, getY (intLat));
      cairo_line_to (cr, dispZone->xL, getY (intLat));
      cairo_stroke(cr);
   }
   for (int intLat = 0; intLat < MIN (MAX_LAT, dispZone->latMax); intLat += step) {
      cairo_move_to (cr, dispZone->xR, getY (intLat));
      cairo_line_to (cr, dispZone->xL, getY (intLat));
      cairo_stroke(cr);
   }
}

/* draw barbules or arrows */
static void drawBarbulesArrows (cairo_t *cr) {
   double u, v, gust, w, twd, tws, uCurr, vCurr, currTwd, currTws, head_x, head_y, lat, lon;
   double tDeltaCurrent = zoneTimeDiff (&currentZone, &zone);
   int nCall = 0;

   lat = dispZone.latMin;
   while (lat <= dispZone.latMax) {
      lon = dispZone.lonLeft;
      while (lon <= dispZone.lonRight || lon <= zone.lonRight) {
	      u = 0; v = 0; w = 0; uCurr = 0; vCurr = 0;
         if (isInZone (lat, lon, &zone) || (par.constWindTws > 0)) {
	         findWindGrib (lat, lon, theTime, &u, &v, &gust, &w, &twd, &tws);
            nCall += 1;
	         if (par.windDisp == BARBULE) {
               double val = (par.indicatorDisp == 0) ? tws : MAX (tws, MS_TO_KN * gust);
               barbule (cr, lat, lon, u, v, val, WIND);
            }
            else if (par.windDisp == ARROW) {
               head_x = getX (lon); 
               head_y = getY (lat);
               arrow (cr, head_x, head_y, u, v, twd, tws, WIND);
            }
            if (par.waveDisp) showWaves (cr, lat, lon, w);

            if (par.currentDisp && isInZone (lat, lon, &currentZone)) {
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
static void drawGribCallback (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer userData) {
   guint8 r, g, b;
   int iSurface = (int) theTime;
   if (iSurface > MAX_N_SURFACE) {
      fprintf (stderr, "In drawGribCallBack: MAX_N_SURFACE exceeded: %d\n", MAX_N_SURFACE);
      exit (EXIT_FAILURE);
   }

   dispZone.xL = 0;      // left
   dispZone.xR = width;  // right
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

   drawShpMap (cr);     
   
   drawGrid (cr, &dispZone, (par.gridDisp) ? 1 : 45);     // if gridDisp, every degree. Else only every 45°
   drawScale (cr, &dispZone);  
   drawBarbulesArrows (cr);

   calculateOrthoRoute ();
   drawOrthoRoute (cr, ORTHO_ROUTE_PARAM);
   drawLoxoRoute (cr);

   if ((route.n != 0) && (isfinite (route.totDist)) && (route.totDist > 0)) {
      drawAllIsochrones (cr, par.style);
      drawAllRoutes (cr);
      if (par.closestDisp) drawClosest (cr);
      if (par.focalDisp) drawFocal (cr);
   }

   // circle (cr, par.pOr.lon, par.pOr.lat, 0.0, 0.0, 0.0);
   focusOnPointInHistory (cr);
   focusOnPointInRoute (cr, &route);

   if (destPressed) {
      showUnicode (cr, DESTINATION_UNICODE, getX (par.pDest.lon), getY (par.pDest.lat) - 20);
      circle (cr, par.pDest.lon, par.pDest.lat, 0.0, 0.0, 0.0);
   }
   drawCompetitors (cr);

   if (my_gps_data.OK)
      circle (cr, my_gps_data.lon, my_gps_data.lat , 1.0, 0.0, 0.0);

   if (polygonStarted) {
      for (int i = 0; i < forbidZones [par.nForbidZone].n ; i++) {
         circle (cr, forbidZones [par.nForbidZone].points [i].lon, forbidZones [par.nForbidZone].points [i].lat, 1.0, 0, 0);
      }
   }
   drawPoi (cr);
   if (par.aisDisp) 
      drawAis (cr);

   // Draw selecting rectangle of selecting is under creation
   if (selecting) {
      CAIRO_SET_SOURCE_RGBA_SELECTION(cr);
      cairo_rectangle (cr, whereWasMouse.x, whereWasMouse.y, whereIsMouse.x - whereWasMouse.x, whereIsMouse.y - whereWasMouse.y);
      cairo_fill(cr);
   }
   drawForbidArea (cr);
   drawTrace (cr);
   if (par.infoDisp)
      drawInfo (cr);
   
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
   polarCenterX = width / 2;
   polarCenterY = height / 2;
   const int minRStepShow = 12;
   CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr);
  
   for (int i = 1; i <= nStep; i++)
      cairo_arc (cr, polarCenterX, polarCenterY, i * rStep, -G_PI/2, G_PI/2);  // circle
   
   for (double angle = -90; angle <= 90; angle += 22.5) {            // segments
      cairo_move_to (cr, polarCenterX, polarCenterY);
      cairo_rel_line_to (cr, rMax * cos (DEG_TO_RAD * angle),
         rMax * sin (DEG_TO_RAD * angle));
   }
   cairo_stroke(cr);
   CAIRO_SET_SOURCE_RGB_DARK_GRAY(cr);

   // libelles vitesses
   for (int i = 1; i <= nStep; i++) {
      cairo_move_to (cr, polarCenterX - 40, polarCenterY - i * rStep);
      if (type == WAVE_POLAR) {
         if ((i % 2) == 0) {// only pair values
            snprintf (str, sizeof (str), "%2d %%", i * 10);
            cairo_show_text (cr, str);
            cairo_move_to (cr, polarCenterX - 40, polarCenterY + i * rStep);
            cairo_show_text (cr, str);
         }
      }
   
      else {
         if ((rStep > minRStepShow) || ((i % 2) == 0)) { // only pair values if too close
            snprintf (str, sizeof (str), "%2d kn", i);
            cairo_show_text (cr, str);
            cairo_move_to (cr, polarCenterX - 40, polarCenterY + i * rStep);
            cairo_show_text (cr, str);
         }
      }
   }
   // libelles angles
   for (double angle = -90; angle <= 90; angle += 22.5) {
      cairo_move_to (cr, polarCenterX + rMax * cos (DEG_TO_RAD * angle) * 1.05,
         polarCenterY + rMax * sin (DEG_TO_RAD * angle) * 1.05);
      snprintf (str, sizeof (str), "%.2f°", angle + 90);
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
   for (int c = 1; c < ptMat->nCol; c += 1) {
      color = colors [c % N_COLORS];
      cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
      cairo_move_to (cr, xLeft, y);
      snprintf (line, MAX_SIZE_LINE, (type == WAVE_POLAR) ? "Height at %.2lf m" : "Wind at %.2lf kn", ptMat->t [0][c]);
      cairo_show_text (cr, line);
      y += hSpace;
   }
   cairo_stroke(cr);
}

/*! return x and y in polar */
static void getPolarXYbyValue (int type, int l, double w, double width, \
   double height, double radiusFactor, double *x, double *y) {

   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double angle = (90.0 - ptMat->t [l][0]) * DEG_TO_RAD;  // convert to Radius
   double val = findPolar (ptMat->t [l][0], w, *ptMat);
   double radius = val * radiusFactor;
   *x = width / 2.0 + radius * cos(angle);
   *y = height / 2.0 - radius * sin(angle);
}

/*! return x and y in polar */
static void getPolarXYbyCol (int type, int l, int c, double width, \
   double height, double radiusFactor, double *x, double *y) {

   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double angle = (90.0 - ptMat->t [l][0]) * DEG_TO_RAD;  // Convert to Radius
   double radius = ptMat->t [l][c] * radiusFactor;
   *x = width / 2.0 + radius * cos(angle);
   *y = height / 2.0 - radius * sin(angle);
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

   if (polarType == WIND_POLAR) {
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
      GdkRGBA color = colors [c % N_COLORS];
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
            cairo_curve_to (cr, x, y, x1, y1, x2, y2);
         }
         getPolarXYbyCol (polarType, ptMat.nLine -1, c, width, height, radiusFactor, &x, &y);
         cairo_line_to(cr, x, y);
      }
      cairo_stroke(cr);
   }
}

/*! Draw polar from polar matrix */
static void onDrawPolarEvent (GtkDrawingArea *area, cairo_t *cr, \
   int width, int height, gpointer userData) {

   PolMat *ptMat = (polarType == WAVE_POLAR) ? &wavePolMat : &polMat; 
   //double radiusFactor = (polarType == WAVE_POLAR) ? width / 40 : width / (maxValInPol (ptMat) * 5); // Ajustez la taille de la courbe
   double radiusFactor;
   radiusFactor = width / (maxValInPol (ptMat) * 6); // Ajust size of drawing 
   polarTarget (cr, polarType, width, height, radiusFactor);
   polarLegend (cr, polarType);
   drawPolarAll (cr, polarType, *ptMat, width, height, radiusFactor);
   drawPolarBySelectedTws (cr, polarType, ptMat, selectedTws, width, height, radiusFactor);
   if (polarDrawingArea != NULL)
      gtk_widget_queue_draw (polarDrawingArea);
}

/*! callback DropDown to find index in polar to draw */
static void cbPolarFilterDropDown (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   selectedPol = gtk_drop_down_get_selected (GTK_DROP_DOWN(dropDown));
   if (polarDrawingArea != NULL)
      gtk_widget_queue_draw (polarDrawingArea);
}

/*! create label with background color based index */
static GtkWidget *createLabelWithBackGroundColor (int index, const char *str) {
   GtkWidget *label = gtk_label_new (str);
   if (index < 0 || index >= MAX_N_SAIL)
      index = 7; // red
   // black foreground if yellow background
   char *foreGroundColor = g_strdup_printf ("%s",  (index == YELLOW) ? "black" : "white");
   char *pango_markup = g_strdup_printf ("<span background='%s' foreground='%s' weight='bold' font_family='monospace'> %s </span>", \
                        colorStr [index], foreGroundColor, str);
   gtk_label_set_markup (GTK_LABEL (label), pango_markup);
   g_free (foreGroundColor);
   g_free (pango_markup);
   return label;
} 

/*! create label with color based index */
static GtkWidget *createLabelWithColor (int index, const char *str) {
   GtkWidget *label = gtk_label_new (str);
   if (index < 0 || index >= MAX_N_SAIL)
      index = 7; // red
   char *pango_markup = g_strdup_printf ("<span background='white' foreground='%s' weight='bold' font_family='monospace'> %s </span>", \
                        colorStr [index], str);
   gtk_label_set_markup (GTK_LABEL (label), pango_markup);
   g_free (pango_markup);
   return label;
} 

/*! combo filter zone for polar */
static GtkWidget *create_filter_combo (int type) {
   PolMat *ptMat = (type == WAVE_POLAR) ? &wavePolMat : &polMat; 
   char str [MAX_SIZE_LINE];

   // Create GlistStore to store strings
   GtkStringList *string_list = gtk_string_list_new (NULL);
   gtk_string_list_append (string_list, "All");
   for (int c = 1; c < ptMat->nCol; c++) {
      snprintf (str, sizeof (str), "%.2lf %s", ptMat->t [0][c], (type == WAVE_POLAR) ? "m. Wave Height" : "Knots. Wind Speed.");
      gtk_string_list_append (string_list, str);
   }
   // Create dropdown
   GtkWidget *dropDown = gtk_drop_down_new (G_LIST_MODEL (string_list), NULL);
   gtk_drop_down_set_selected (GTK_DROP_DOWN(dropDown), 0);
   g_signal_connect (dropDown, "notify::selected", G_CALLBACK (cbPolarFilterDropDown), NULL);

   return dropDown;
}

/*! display polar matrix */
static void polarDumpWithParam (int polarType, const PolMat *ptMat, const char *title) {
   GtkWidget *label;
   char str [MAX_SIZE_LINE] = "";
   bool legend = false;
   int line;
   GtkWidget *dumpWindow = gtk_application_window_new (app);

   gtk_window_set_title (GTK_WINDOW (dumpWindow), title);
   g_signal_connect (window, "destroy", G_CALLBACK (onParentDestroy), dumpWindow);
   // g_signal_connect (G_OBJECT(dumpWindow), "destroy", G_CALLBACK (gtk_window_destroy), NULL);

   // Scroll 
   GtkWidget *scrolled_window = gtk_scrolled_window_new ();
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_window_set_child (GTK_WINDOW (dumpWindow), (GtkWidget *) scrolled_window);

   // grid creation
   GtkWidget *grid = gtk_grid_new();   
   gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), (GtkWidget *) grid);

   for (line = 0; line < ptMat->nLine; line++) {
      for (int col = 0; col < ptMat->nCol; col++) {
	      if ((col == 0) && (line == 0)) {
            g_strlcpy (str, (polarType == WAVE_POLAR) ? "Angle/Height" : "TWA/TWS", sizeof (str));
            label = createLabelWithColor (-1, str); // red
         }
         else if ((col == 0) || (line == 0)) {
            snprintf (str, sizeof (str), "%.0f", ptMat->t [line][col]);
            label = createLabelWithColor (-1, str); // red
         }
         else { 
               //either WAVE_POLAR or POLAR (WIND)
               snprintf (str, sizeof (str), "%4.1f", ptMat->t [line][col]);
               if ((polarType == WIND) && (sailPolMat.nLine != 0) && (sailPolMat.nCol != 0)) {
                  legend = true;
                  int val = (int) sailPolMat.t [line][col];
                  label = createLabelWithBackGroundColor (val, str);
               } 
               else {
                  label = gtk_label_new (str);
               }
         }
	      gtk_grid_attach (GTK_GRID (grid), label, col, line, 1, 1);
         //gtk_widget_set_size_request (label, MAX_TEXT_LENGTH, -1);
      }
   }
   if (legend) {
      label = gtk_label_new (""); // blank line
	   gtk_grid_attach (GTK_GRID (grid), label, 0, line + 1, 1, 1);
      for (int i = 0; i < MAX_N_SAIL; i += 1) {
         fSailName (i, str, sizeof (str));
         label = createLabelWithBackGroundColor (i, str);
	      gtk_grid_attach (GTK_GRID (grid), label, i + 1, line + 2, 1, 1);
      }
   }
   // Get grid preferred size
   GtkRequisition preferredSize;
   gtk_widget_get_preferred_size (grid, NULL, &preferredSize);
   gtk_widget_set_size_request (scrolled_window, MIN (preferredSize.width + 20, 1400), preferredSize.height + 20);

   gtk_window_present (GTK_WINDOW (dumpWindow));
}

/*! display polar matrix */
static void polarDump () {
   if (polarType == WAVE_POLAR)
      polarDumpWithParam (WAVE_POLAR, &wavePolMat, par.wavePolFileName);
   else 
      polarDumpWithParam (WIND_POLAR, &polMat, par.polarFileName);
}

/*! callback function after polar edit */
static void cbPolarEdit () {
   char sailPolFileName [MAX_SIZE_NAME] = "";
   char errMessage [MAX_SIZE_TEXT] = "";

   if ((windowEditor != NULL) && GTK_IS_WINDOW (windowEditor))
      gtk_window_destroy (GTK_WINDOW (windowEditor));

   if (polarType != WAVE_POLAR) {
      if (readPolar (true, par.polarFileName, &polMat, errMessage, sizeof (errMessage))) {
         newFileNameSuffix (par.polarFileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
         readPolar (false, sailPolFileName, &sailPolMat, errMessage, sizeof (errMessage));
         gtk_widget_queue_draw (polarDrawingArea);
      }
      else 
         fprintf (stderr, "In cbPolarEdit Error loading polarfile: %s\n", par.polarFileName);
   }
   else { 
      if (readPolar (true, par.wavePolFileName, &wavePolMat, errMessage, sizeof (errMessage)))
         gtk_widget_queue_draw (polarDrawingArea);
      else 
         fprintf (stderr, "In cbPolarEdit Error loading wavePolarfile: %s\n", par.wavePolFileName);
   }
}

/*! callback function in polar for edit */
static void onEditButtonPolarClicked (GtkWidget *widget, gpointer data) {
   char sailPolFileName [MAX_SIZE_NAME] = "";
   newFileNameSuffix (par.polarFileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
   const char *polFileContents [] = {par.polarFileName, sailPolFileName};
   const char *waveFileContents [] = {par.wavePolFileName};
   if (polarType == WAVE_POLAR) {
      if (! myEditor (app, waveFileContents, 1, "Wave Polar Edition", cbPolarEdit))
         infoMessage ("Impossible to open wave polar", GTK_MESSAGE_ERROR);
   }
   else {
      if (! myEditor (app, polFileContents, 2, "Polar Edition", cbPolarEdit))
      infoMessage ("Impossible to open polar", GTK_MESSAGE_ERROR);
   }
}

/*! Callback radio button selecting provider for segment or bezier */
static void segmentOrBezierButtonToggled (GtkToggleButton *button, gpointer userData) {
   segmentOrBezier = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   if (polarDrawingArea != NULL)
      gtk_widget_queue_draw (polarDrawingArea);
}

/*! Callback for polar to change scale value */
static void onScaleValueChanged (GtkScale *scale, gpointer userData){
   GtkWidget *scaleLabel = (GtkWidget *) userData;
   char str [MAX_SIZE_LINE];
   selectedTws = gtk_range_get_value(GTK_RANGE(scale));
   if (polarType == WAVE_POLAR) {
      snprintf (str, sizeof (str), "%.2lf Meters", selectedTws); 
   }
   else {
      double maxSpeed = maxSpeedInPolarAt (selectedTws, &polMat);
      snprintf (str, sizeof (str), "%.2lf Kn, Max Boat Speed: %.2lf Kn", selectedTws, maxSpeed); 
   }
   gtk_label_set_text (GTK_LABEL(scaleLabel), str);
   if (polarDrawingArea != NULL)
      gtk_widget_queue_draw (polarDrawingArea);
}

/*! Callback associated to mouse motion in polar drawing area, creating update of polar status bar  */
static gboolean onPolarMotionEvent (GtkEventControllerMotion *controller, double x, double y, gpointer userData) {
   PolMat *ptMat = (polarType == WAVE_POLAR) ? &wavePolMat : &polMat; 
   double twa = round (RAD_TO_DEG * atan2 (x - polarCenterX, polarCenterY - y));
   char *str;
   char strSail [MAX_SIZE_NAME] = "";            
   double val = findPolar (twa, selectedTws, *ptMat);

   if (polarType == WAVE_POLAR) {
      str = g_strdup_printf ("TWS: %7.2lf Kn, TWA: %7.2lf°, Adjust: %7.2lf%%", selectedTws, twa, val);
   }
   else { 
      if ((sailPolMat.nCol != 0) && (sailPolMat.nLine != 0)) { // sail information exist
         int sail = closestInPolar (twa, selectedTws, sailPolMat);   
         fSailName (sail, strSail, sizeof (strSail));
         str = g_strdup_printf ("TWS: %7.2lf Kn, TWA: %7.2lf°, Speed: %7.2lf Kn,    %s", selectedTws, twa, val, strSail);
      }
      else
         str = g_strdup_printf ("TWS: %7.2lf Kn, TWA: %7.2lf°, Speed: %7.2lf Kn", selectedTws, twa, val);
   }

   gtk_label_set_text (GTK_LABEL (polStatusbar), str);
   g_free (str);
   return TRUE;
}

/*! Draw polar from polar file */
static void polarDraw () {
   char line [MAX_SIZE_LINE]; 
   char sStatus [MAX_SIZE_LINE];
   PolMat *ptMat = (polarType == WAVE_POLAR) ? &wavePolMat : &polMat; 
   if ((ptMat->nCol == 0) || (ptMat->nLine == 0)) {
      infoMessage ("No polar information", GTK_MESSAGE_ERROR);
      return;
   }

   // Main window creation
   GtkWidget *polWindow = gtk_application_window_new (app);
   gtk_window_set_default_size (GTK_WINDOW (polWindow), -1, -1);
   snprintf (line, sizeof (line), "%s, Max: %.2lf", \
      (polarType == WAVE_POLAR) ? par.wavePolFileName : par.polarFileName, maxValInPol (ptMat));
   gtk_window_set_title (GTK_WINDOW (polWindow), line);
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), polWindow);
   // g_signal_connect (polWindow, "destroy", G_CALLBACK(gtk_window_destroy), NULL);

   // vertical box container creation
   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (polWindow), vBox);
   
   // drawing area creation
   polarDrawingArea = gtk_drawing_area_new ();
   gtk_widget_set_hexpand (polarDrawingArea, TRUE);
   gtk_widget_set_vexpand (polarDrawingArea, TRUE);
   gtk_widget_set_size_request (polarDrawingArea, POLAR_WIDTH, POLAR_HEIGHT - 100);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (polarDrawingArea), onDrawPolarEvent, NULL, NULL);

   // create zone for combo box
   GtkWidget *filter_combo = create_filter_combo (polarType);
  
   GtkWidget *dumpButton = gtk_button_new_from_icon_name ("x-office-spreadsheet-symbolic");
   gtk_widget_set_tooltip_text (dumpButton, "Dump");
   g_signal_connect (G_OBJECT (dumpButton), "clicked", G_CALLBACK (polarDump), NULL);
   
   GtkWidget *editButton = gtk_button_new_from_icon_name ("document-edit-symbolic");
   gtk_widget_set_tooltip_text (editButton, "Edit");
   g_signal_connect (G_OBJECT (editButton), "clicked", G_CALLBACK (onEditButtonPolarClicked), NULL);
   
   // radio button for bezier or segment choice
   GtkWidget *segmentRadio = gtk_check_button_new_with_label ("Segment");
   GtkWidget *bezierRadio = gtk_check_button_new_with_label ("Bézier");
   gtk_check_button_set_group ((GtkCheckButton *) segmentRadio, (GtkCheckButton *) bezierRadio); 
   g_object_set_data (G_OBJECT (segmentRadio), "index", GINT_TO_POINTER (SEGMENT));
   g_object_set_data (G_OBJECT (bezierRadio), "index", GINT_TO_POINTER (BEZIER));
   g_signal_connect (segmentRadio, "toggled", G_CALLBACK (segmentOrBezierButtonToggled), NULL);
   g_signal_connect (bezierRadio, "toggled", G_CALLBACK (segmentOrBezierButtonToggled), NULL);
   segmentOrBezier = (ptMat->nLine < MIN_POINT_FOR_BEZIER) ? SEGMENT : BEZIER;
   gtk_check_button_set_active ((GtkCheckButton *) ((segmentOrBezier == SEGMENT) ? segmentRadio : bezierRadio), TRUE);

   // GtkScale widget creation
   int maxScale = ptMat->t[0][ptMat->nCol-1];      // max value of wind or waves
   if (maxScale < 1) {
      fprintf (stderr, "In polarDrow, Strange maxScale: %d\n", maxScale);
   }
   GtkWidget *scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, MAX (1, maxScale), 1);
   gtk_widget_set_tooltip_text (scale, "Select TWS value");
   gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
   gtk_range_set_value (GTK_RANGE(scale), selectedTws);
   gtk_widget_set_size_request (scale, 300, -1);  // Adjust GtkScale size
   GtkWidget *scaleLabel = gtk_label_new ("");
   g_signal_connect (scale, "value-changed", G_CALLBACK (onScaleValueChanged), scaleLabel);
   onScaleValueChanged (GTK_SCALE (scale), scaleLabel);

   // Create event controller for mouse motion
   GtkEventController *motionController = gtk_event_controller_motion_new ();
   g_signal_connect (motionController, "motion", G_CALLBACK (onPolarMotionEvent), NULL);
   gtk_widget_add_controller (polarDrawingArea, motionController);

   GtkWidget *hBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_box_append (GTK_BOX (hBox), filter_combo);
   gtk_box_append (GTK_BOX (hBox), segmentRadio);
   gtk_box_append (GTK_BOX (hBox), bezierRadio);
   gtk_box_append (GTK_BOX (hBox), dumpButton);
   gtk_box_append (GTK_BOX (hBox), editButton);
   gtk_box_append (GTK_BOX (hBox), scale);
   gtk_box_append (GTK_BOX (hBox), scaleLabel);

   GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);

   // status bar creation 
   snprintf (sStatus, sizeof (sStatus), "nCol: %2d   nLig: %2d   max: %2.2lf", \
      ptMat->nCol, ptMat->nLine, maxValInPol (ptMat));
   polStatusbar = gtk_label_new (sStatus);

   gtk_box_append (GTK_BOX (vBox), hBox);
   gtk_box_append (GTK_BOX (vBox), polarDrawingArea);
   gtk_box_append (GTK_BOX (vBox), separator);
   gtk_box_append (GTK_BOX (vBox), polStatusbar);

   if (polarDrawingArea != NULL)
      gtk_widget_queue_draw (polarDrawingArea);

   gtk_window_present (GTK_WINDOW (polWindow));
}

/*! check if routing is terminated */
static gboolean allCompetitorsCheck (gpointer data) {
   char str [MAX_SIZE_LINE] = "";
   int localRet = g_atomic_int_get (&competitors.ret); // atomic read
   switch (localRet) {
   case RUNNING: // not terminated
      g_mutex_lock (&warningMutex);
      int i = MAX (0, competitors.runIndex);
      snprintf (str, sizeof (str),"%.0lf%% Competitors count: %2d  %s", 
         100.0 * (competitors.n - i - 1) / competitors.n, competitors.n - i, competitors.t [i].name);
      statusWarningMessage (statusbar, str);
      g_mutex_unlock (&warningMutex);
      return TRUE;
   case NO_SOLUTION: 
      snprintf (str, MAX_SIZE_LINE, "No solution: No competitor can reach target");
      break;
   case STOPPED:
      route.n = 0;
      nIsoc = 0;
      break;
   case EXIST_SOLUTION:
      selectedPointInLastIsochrone = (nIsoc <= 1) ? 0 : isoDesc [nIsoc -1].closest; 
      logReport (competitors.n);
      exportRouteToGpx (&route, par.dumpRFileName);
      dumpIsocToFile (par.dumpIFileName);
      routeGram ();
      competitorsDump ();
      gtk_widget_queue_draw (drawing_area);
      break;
   default: 
      snprintf (str, MAX_SIZE_LINE, "In allCompetitorsCheck: Unknown compretitors.ret: %d\n", localRet);
      break;
   }
   waitMessageDestroy ();
   if (runThread != NULL)
      g_thread_unref (runThread);
   runThread = NULL;
   if (str [0] != '\0')
      infoMessage (str, GTK_MESSAGE_WARNING);
   return FALSE;
}

/*! when OK in calendar is clicked */
static void onOkButtonCalClicked (GtkWidget *widget, gpointer window) {
   if ((window != NULL) && GTK_IS_WINDOW (window))
      gtk_window_destroy (GTK_WINDOW (window));

   par.startTimeInHours = getDepartureTimeInHour (&startInfo); 
   // printf ("startInfo after: %s, startTime: %lf\n", asctime (&startInfo), par.startTimeInHours);
   if ((par.startTimeInHours < 0) || (par.startTimeInHours > zone.timeStamp [zone.nTimeStamp -1])) {
      infoMessage ("start time should be within grib window time !", GTK_MESSAGE_WARNING);
      return;
   }
   
   if (! isInZone (par.pOr.lat, par.pOr.lon, &zone) && (par.constWindTws == 0)) { 
      infoMessage ("Origin point not in wind zone", GTK_MESSAGE_WARNING);
      return;
   }
   if (! isInZone (par.pDest.lat, par.pDest.lon, &zone) && (par.constWindTws == 0)) {
      infoMessage ("Destination point not in wind zone", GTK_MESSAGE_WARNING);
      return;
   }
   if (competitors.runIndex == -1) {         // all competitors run
      g_atomic_int_set (&competitors.ret , RUNNING); // mean not terminated
      freeHistoryRoute ();                   // liberate history
      
      waitMessage ("All competitors Running", "It can take a while !!!\nWatch status bar ");
      runThread = g_thread_new ("All Competitors", allCompetitors, NULL); // launch routing
      routingTimeout = g_timeout_add (ROUTING_TIME_OUT, allCompetitorsCheck, NULL);
   }
   else {
      waitMessage ("Isochrone building", "Be patient\nWatch status bar");
      runThread = g_thread_new ("routingLaunch", routingLaunch, NULL); // launch routing runThread is global
      routingTimeout = g_timeout_add (ROUTING_TIME_OUT, routingCheck, NULL);
   }
}

/*! when now in calendar is clicked */
static void onNowButtonCalClicked (GtkWidget *widget, gpointer window) {
   time_t currentTime = time (NULL);
   struct tm *gmtTime = gmtime (&currentTime);
   startInfo = *gmtTime;
   onOkButtonCalClicked (NULL, window);
}

/*! when competitor is selected */
static void cbCalCompetitor (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   int index = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   if (index == 0) {
      competitors.runIndex = -1;
      return;
   }
   par.pOr.lat = competitors.t [index - 1].lat;
   par.pOr.lon = competitors.t [index - 1].lon;
   competitors.runIndex = index - 1;
}

/*! year month date for fCalendar */
static void dateUpdate (GtkCalendar *calendar, gpointer userData) {
   struct tm *startTime = (struct tm *) userData;
   GDateTime *date = gtk_calendar_get_date (calendar);
   startTime -> tm_sec = 0;
   startTime->tm_isdst = -1;                                       // avoid adjustments with hours summer winter
   startTime->tm_mday = g_date_time_get_day_of_month (date);
   startTime->tm_mon = g_date_time_get_month (date) - 1;
   startTime->tm_year = g_date_time_get_year (date) - 1900;
   mktime (startTime);
}

/*! get start date and time for routing. */
static void fCalendar (struct tm *start) {
   char str [MAX_SIZE_LINE];
   time_t currentTime = time (NULL);
   struct tm *gmtTime = gmtime(&currentTime);

   snprintf (str, sizeof (str), "%d/%02d/%02d %02d:%02d UTC", gmtTime->tm_year + 1900, gmtTime->tm_mon + 1, 
      gmtTime->tm_mday, gmtTime->tm_hour, gmtTime->tm_min);

   GtkWidget *calWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (calWindow), "Pick a Date");
   gtk_window_set_transient_for (GTK_WINDOW (calWindow), GTK_WINDOW (window));
   g_signal_connect (window, "destroy", G_CALLBACK (onParentDestroy), calWindow);
   
   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (calWindow), (GtkWidget *) vBox);

   GtkWidget *calendar = gtk_calendar_new ();
   GDateTime *date = g_date_time_new_utc (start->tm_year + 1900, start->tm_mon + 1, start->tm_mday, 0, 0, 0);
   gtk_calendar_select_day (GTK_CALENDAR(calendar), date);
   g_signal_connect (calendar, "day-selected", G_CALLBACK (dateUpdate), &startInfo);
   g_date_time_unref (date);

   // Create labels and selection buttons
   GtkWidget *labelHour = gtk_label_new ("Hour");
   GtkWidget *spinHour = gtk_spin_button_new_with_range (0, 23, 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinHour), start->tm_hour);
   g_signal_connect (spinHour, "value-changed", G_CALLBACK (intSpinUpdate), &start->tm_hour);

   GtkWidget *labelMinutes = gtk_label_new ("Minutes");
   GtkWidget *spinMinutes = gtk_spin_button_new_with_range (0, 59, 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinMinutes), start->tm_min);
   g_signal_connect (spinMinutes, "value_changed", G_CALLBACK (intSpinUpdate), &start->tm_min);

   // Create horizontal box
   GtkWidget *hBox0 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_box_append (GTK_BOX (hBox0), labelHour);
   gtk_box_append (GTK_BOX (hBox0), spinHour);
   gtk_box_append (GTK_BOX (hBox0), labelMinutes);
   gtk_box_append (GTK_BOX (hBox0), spinMinutes);

   // Create line to select isochrone step
   GtkWidget *labelTStep = gtk_label_new ("Isoc. Time Step: ");
   GtkWidget *dropDownTStep = gtk_drop_down_new_from_strings (arrayTStep); // arrayStep is a global const table
   int indice = (par.tStep == 0.25) ? 0 : (par.tStep == 0.5) ? 1 : (par.tStep == 1) ? 2 : (par.tStep == 2) ? 3 : 4; 
   gtk_drop_down_set_selected ((GtkDropDown *) dropDownTStep, indice);
   g_signal_connect (dropDownTStep, "notify::selected", G_CALLBACK (cbDropDownTStep), NULL);

   // Create second horizontal box
   GtkWidget *hBox1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_box_append (GTK_BOX (hBox1), labelTStep);
   gtk_box_append (GTK_BOX (hBox1), dropDownTStep);
   
   // Create last horizontal box
   GtkWidget *hBox2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *nowButton = gtk_button_new_with_label (str);
   GtkWidget *OKbutton = gtk_button_new_with_label ("OK");
   GtkWidget *cancelButton = gtk_button_new_with_label ("Cancel");
   gtk_box_append (GTK_BOX(hBox2), nowButton);
   gtk_box_append (GTK_BOX(hBox2), OKbutton);
   gtk_box_append (GTK_BOX(hBox2), cancelButton);

   // Create GlistStore to store strings 
   GtkStringList *competitorsList = gtk_string_list_new (NULL);
   gtk_string_list_append (competitorsList, "All competitors");
   for (int i = 0;  i < competitors.n; i++) {
      snprintf (str, sizeof (str), "%s", competitors.t[i].name);
      gtk_string_list_append (competitorsList, str);
   }
   GtkWidget *dropDownCompetitors = gtk_drop_down_new (G_LIST_MODEL (competitorsList), NULL); 
   par.pOr.lat = competitors.t [0].lat;
   par.pOr.lon = competitors.t [0].lon;
   competitors.runIndex = 0;
   gtk_drop_down_set_selected ((GtkDropDown *) dropDownCompetitors, 1);
   g_signal_connect (dropDownCompetitors, "notify::selected", G_CALLBACK (cbCalCompetitor), NULL);

   gtk_box_append (GTK_BOX (vBox), calendar);
   gtk_box_append (GTK_BOX (vBox), hBox0);
   gtk_box_append (GTK_BOX (vBox), hBox1);
   gtk_box_append (GTK_BOX (vBox), dropDownCompetitors);
   gtk_box_append (GTK_BOX (vBox), hBox2);
   g_signal_connect (nowButton, "clicked", G_CALLBACK (onNowButtonCalClicked), calWindow);
   g_signal_connect (OKbutton, "clicked", G_CALLBACK (onOkButtonCalClicked), calWindow);
   g_signal_connect (cancelButton, "clicked", G_CALLBACK (onCancelButtonClicked), calWindow);

   gtk_window_present (GTK_WINDOW (calWindow));
}

/*! one line for reports */
static void lineReport (GtkWidget *grid, int l, const char* iconName, const char *libelle, const char *value) {
   GtkWidget *icon = gtk_button_new_from_icon_name (iconName);
   //GtkWidget *icon = gtk_label_new ("");
   GtkWidget *label = gtk_label_new (libelle);
   gtk_grid_attach(GTK_GRID (grid), icon, 0, l, 1, 1);
   gtk_label_set_yalign (GTK_LABEL (label), 0);
   gtk_label_set_xalign (GTK_LABEL (label), 0);   
   gtk_grid_attach (GTK_GRID (grid), label,1, l, 1, 1);
   GtkWidget *labelValue = gtk_label_new (value);
   gtk_label_set_yalign (GTK_LABEL (labelValue), 0);
   gtk_label_set_xalign (GTK_LABEL (labelValue), 1);   
   GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
   // add separator to grid
   gtk_grid_attach (GTK_GRID (grid), labelValue, 2, l, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), separator,    0, l + 1, 3, 1);
   gtk_widget_set_margin_end (GTK_WIDGET(labelValue), 20);
}

/*! draw legend */
static void drawLegend (cairo_t *cr, guint leftX, guint topY, double colors [][3], const char *libelle [], size_t maxColors) {
   const int constDelta = 12;
   const int rectangleWidth = 55;
   CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr);
   cairo_rectangle (cr, leftX, topY, rectangleWidth, (maxColors + 1) * constDelta);
   cairo_stroke (cr);
   
   cairo_set_font_size (cr, 12);
   for (size_t i = 0; i < maxColors; i++) {
      cairo_set_source_rgb (cr, colors[i][0], colors[i][1], colors[i][2]);
      cairo_move_to (cr, leftX + constDelta /2, topY + (i + 1) * constDelta);
      cairo_show_text (cr, libelle [i]);
   }
}

/*! call back to draw statistics */
static void onStatEvent (GtkDrawingArea *drawing_area, cairo_t *cr, int width, int height, gpointer userData) {
#define MAX_VAL 3
   width *= 0.9;
   height *= 0.9;
   const int legendXLeft = width - 35;
   const int legendYTop = 10;
   double statValues [MAX_VAL];
   statValues [0] = route.motorDist;
   statValues [1] = route.tribordDist;
   statValues [2] = route.babordDist;
   const double epsilonDashboard = 1.0;
   double total = 0.0;
   for (int i = 0; i < MAX_VAL; i++) {
      total += statValues[i];
   }
   if (total < 1.0)
      return;

   // Legend
   const char *libelle [MAX_VAL] = {"Motor", "Tribord", "Babord"}; 
   double colors[MAX_VAL][3] = {{1, 0, 0}, {0.5, 0.5, 0.5}, {0, 0, 0}}; // Red Gray Black
   drawLegend (cr, legendXLeft, legendYTop, colors, libelle, MAX_VAL);

   double start_angle = 0.0;
   for (int i = 0; i < MAX_VAL; i++) {
      double slice_angle = (statValues[i] / total) * 360.0;
      if (slice_angle < epsilonDashboard) continue;

      // Define segment color and draw segment
      cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
      cairo_move_to(cr, width / 2, height / 2);
      cairo_arc (cr, width / 2, height / 2, MIN(width, height) / 2, \
         start_angle * DEG_TO_RAD, (start_angle + slice_angle) * DEG_TO_RAD);
      cairo_close_path(cr);
      cairo_fill(cr);

      start_angle += slice_angle;
   }
   // percentage and libelle
   CAIRO_SET_SOURCE_RGB_WHITE(cr);
   start_angle = 0.0;
   for (int i = 0; i < MAX_VAL; i++) {
      double slice_angle = (statValues[i] / total) * 360.0;
      if (slice_angle < epsilonDashboard) continue;
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
static void onAllureEvent (GtkDrawingArea *drawing_area, cairo_t *cr, \
   int width, int height, gpointer userData) {

#define MAX_ALLURES 3
   width *= 0.9;
   height *= 0.9;
   const int legendXLeft = width - 35;
   const int legendYTop = 10;
   double twa;
   double statValues [MAX_ALLURES] = {0};
   const int threshold [MAX_ALLURES] = {60, 120, 180}; // Près, Travers, Portant
   const double epsilonDashboard = 1.0;
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
   const char *libelle [MAX_ALLURES] = {"Près", "Travers", "Portant"};
   drawLegend (cr, legendXLeft, legendYTop, colors, libelle, MAX_ALLURES);

   double start_angle = 0.0;
   for (int i = 0; i < MAX_VAL; i++) {
      double slice_angle = (statValues [i] / total) * 360.0;
      if (slice_angle < epsilonDashboard) continue;

      // Define segment color and draw segment
      cairo_set_source_rgb (cr, colors[i][0], colors[i][1], colors[i][2]);
      cairo_move_to(cr, width / 2, height / 2);
      cairo_arc (cr, width / 2, height / 2, MIN(width, height) / 2, \
         start_angle * DEG_TO_RAD, (start_angle + slice_angle) * DEG_TO_RAD);
      cairo_close_path(cr);
      cairo_fill(cr);

      start_angle += slice_angle;
   }
   // percentage and libelle
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   start_angle = 0.0;
   for (int i = 0; i < MAX_VAL; i++) {
      double slice_angle = (statValues[i] / total) * 360.0;
      if (slice_angle < epsilonDashboard) continue;
      // update percentage text position
      double mid_angle = start_angle + slice_angle / 2;
      double x = (width / 2) + (MIN(width, height) / 4) * cos (mid_angle * DEG_TO_RAD);
      double y = (height / 2) + (MIN(width, height) / 4) * sin (mid_angle * DEG_TO_RAD);
      
      char percentage [MAX_SIZE_LINE];
      if (statValues [i] > 0) {
         snprintf (percentage, sizeof(percentage), "%.0f%%", (statValues[i] / total) * 100);
         cairo_move_to(cr, x, y);
         cairo_show_text(cr, percentage);
      }
      start_angle += slice_angle;
   }
}

/*! check if routing is terminated */
static gboolean routingCheck (gpointer data) {
   char str [MAX_SIZE_LINE] = "";
   int localRet = g_atomic_int_get (&route.ret); // atomic read
   switch (localRet) {
   case ROUTING_RUNNING: // not terminated
      g_mutex_lock (&warningMutex); 
      snprintf (str, sizeof (str),"%3.0lf%% Isoc count: %5d",  100.0 * nIsoc/maxNIsoc, nIsoc);
      // printf ("Running: %s\n", str);
      statusWarningMessage (statusbar, str);
      g_mutex_unlock (&warningMutex);
      return TRUE;
   case ROUTING_STOPPED: // stopped by user
      route.n = 0;
      nIsoc = 0;
      printf ("Stopped by user\n");
      break;
   case ROUTING_ERROR: // error
      snprintf (str, MAX_SIZE_LINE, "Check logs");
      break;
   default:
      if (! isfinite (route.totDist) || (route.totDist <= 1)) {
         snprintf (str, MAX_SIZE_LINE,  "No route calculated. Check if wind !");
         break;
      }
      selectedPointInLastIsochrone = (nIsoc <= 1) ? 0 : isoDesc [nIsoc -1].closest; 
      saveRoute (&route);
      if (par.special == 0)
         routeGram ();
      logReport (0);
      exportRouteToGpx (&route, par.dumpRFileName);
      dumpIsocToFile (par.dumpIFileName);
      gtk_widget_queue_draw (drawing_area); // draw all including isochrones
   }
   waitMessageDestroy ();
   if (runThread != NULL)
      g_thread_unref (runThread);
   runThread = NULL;
   if (str [0] != '\0')
      infoMessage (str, GTK_MESSAGE_ERROR);
   return FALSE;
}

/*! launch routing */
static void onRunButtonClicked () {
   if (! gpsTrace)
      if (addTraceGPS (par.traceFileName))
         gpsTrace = true; // only one GPS trace per session
   if (par.special == 0)
      fCalendar (&startInfo);
}

/*! check if bestDepartureTime is terminated */
static gboolean bestDepartureCheck (gpointer data) {
   char str [MAX_SIZE_LINE] = "";
   int localRet = g_atomic_int_get (&chooseDeparture.ret); // atomic read
   switch (localRet) {
   case RUNNING: // not terminated
      g_mutex_lock (&warningMutex);
      if (chooseDeparture.tEnd  != 0) {
         snprintf (str, sizeof (str),"Evaluation count: %3d  %2.0lf%%, Best count: %3d, Best Departure Time After Grib: %02.0lf:%02.0lf", 
            chooseDeparture.count, 
            100.0 * (chooseDeparture.count * chooseDeparture.tInterval) / chooseDeparture.tEnd,
            chooseDeparture.bestCount,            
            floor (chooseDeparture.bestTime), 60.0 * fmod (chooseDeparture.bestTime, 1.0)
         );
         statusWarningMessage (statusbar, str);
      }
      g_mutex_unlock (&warningMutex);
      return TRUE;
   case NO_SOLUTION: 
      snprintf (str, MAX_SIZE_LINE, "No solution");
      break;
   case STOPPED:
      route.n = 0;
      nIsoc = 0;
      break;
   case EXIST_SOLUTION:
      selectedPointInLastIsochrone = (nIsoc <= 1) ? 0 : isoDesc [nIsoc -1].closest; 
      logReport (chooseDeparture.count);
      exportRouteToGpx (&route, par.dumpRFileName);
      dumpIsocToFile (par.dumpIFileName);
      routeGram ();
      simulationReport ();
      gtk_widget_queue_draw (drawing_area);
      break;
   default: 
      snprintf (str, MAX_SIZE_LINE, "In bestDepartureCheck: Unknown chooseDeparture.ret: %d\n", localRet);
      break;
   }
   waitMessageDestroy ();
   if (runThread != NULL)
      g_thread_unref (runThread);
   runThread = NULL;
   if (str [0] != '\0')
      infoMessage (str, GTK_MESSAGE_WARNING);
   return FALSE;
}

/*! when OK in departure box is clicked */
static void onOkButtonDepClicked (GtkWidget *widget, gpointer theWindow) {
   const double epsilon = 5.0 / 60.0; // 5 minutes interval min
   if (chooseDeparture.tInterval < epsilon) {
      infoMessage ("Choose a correct time interval", GTK_MESSAGE_WARNING);
      return;
   }
   if ((chooseDeparture.tEnd - chooseDeparture.tBegin) < chooseDeparture.tInterval) {
      infoMessage ("Choose a correct window time", GTK_MESSAGE_WARNING);
      return;
   }
   chooseDeparture.count = 0;
   g_atomic_int_set (&chooseDeparture.ret , RUNNING); // mean not terminated
   freeHistoryRoute ();                               // liberate history
   waitMessage ("Simulation Running", "It can take a while !!!\nWatch status bar...");
   runThread = g_thread_new ("chooseDepartureLaunch", bestTimeDeparture, NULL); // launch routing
   routingTimeout = g_timeout_add (ROUTING_TIME_OUT, bestDepartureCheck, NULL);

   if ((theWindow != NULL) && GTK_IS_WINDOW (theWindow))
      gtk_window_destroy (GTK_WINDOW (theWindow));
}

/*! launch all routing in order to choose best departure time */
static void onChooseDepartureButtonClicked (GtkWidget *widget, gpointer data) {
   int main = 0;
   par.pOr.lat = competitors .t [main].lat;
   par.pOr.lon = competitors .t [main].lon;
   if (! isInZone (par.pOr.lat, par.pOr.lon, &zone) && (par.constWindTws == 0)) {
     infoMessage ("Origin point not in wind zone", GTK_MESSAGE_WARNING);
     return;
   }
   if (! isInZone (par.pDest.lat, par.pDest.lon, &zone) && (par.constWindTws == 0)) {
     infoMessage ("Destination point not in wind zone", GTK_MESSAGE_WARNING);
     return;
   }
   memset (&chooseDeparture, 0, sizeof (ChooseDeparture));
   chooseDeparture.tInterval = 1.0;
   chooseDeparture.tEnd = zone.timeStamp [zone.nTimeStamp - 1];
   
   GtkWidget *depWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (depWindow), "Simulation");
   g_signal_connect (window, "destroy", G_CALLBACK (onParentDestroy), depWindow);

   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (depWindow), (GtkWidget *) vBox);

   GtkWidget *grid = gtk_grid_new ();   
   gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
   gtk_grid_set_row_spacing (GTK_GRID (grid), 5);

   /// time interval line
   GtkWidget *labelInterval = gtk_label_new ("Interval ");
   gtk_grid_attach (GTK_GRID (grid), labelInterval, 0, 0, 1, 1);

   GtkWidget *spinButtonHourInterval = gtk_spin_button_new_with_range (0, 12, 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinButtonHourInterval), floor (chooseDeparture.tInterval));
   g_signal_connect (spinButtonHourInterval, "value-changed", G_CALLBACK (hourSpinUpdate), &chooseDeparture.tInterval);
   gtk_label_set_xalign (GTK_LABEL (labelInterval), 0);
   GtkWidget *labelIntervalH = gtk_label_new ("H");
   gtk_grid_attach (GTK_GRID (grid), spinButtonHourInterval, 1, 0, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), labelIntervalH, 2, 0, 1, 1);

   GtkWidget *spinButtonMinInterval = gtk_spin_button_new_with_range (0, 59, 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinButtonMinInterval), 60 * fmod (chooseDeparture.tInterval, 1.0));
   g_signal_connect (spinButtonMinInterval, "value-changed", G_CALLBACK (minSpinUpdate), &chooseDeparture.tInterval);
   //gtk_label_set_xalign(GTK_LABEL (labelInterval), 0);
   labelInterval = gtk_label_new ("Hours");
   gtk_grid_attach (GTK_GRID (grid), spinButtonMinInterval, 3, 0, 1, 1);
   labelInterval = gtk_label_new ("Min");
   gtk_grid_attach (GTK_GRID (grid), labelInterval, 4, 0, 1, 1);
   
   /// time start line
   GtkWidget *labelMini = gtk_label_new ("Min Start After Grib");
   GtkWidget *spinButtonMini = gtk_spin_button_new_with_range (0, zone.timeStamp [zone.nTimeStamp - 1], 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinButtonMini), chooseDeparture.tBegin);
   g_signal_connect (spinButtonMini, "value-changed", 
      G_CALLBACK (intSpinUpdate), &chooseDeparture.tBegin);
   gtk_label_set_xalign(GTK_LABEL (labelMini), 0);
   GtkWidget *labelHours = gtk_label_new ("Hours");
   gtk_label_set_xalign(GTK_LABEL (labelHours), 0);
   gtk_grid_attach(GTK_GRID (grid), labelMini,      0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), spinButtonMini, 1, 1, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), labelHours,     2, 1, 1, 1);
   
   /// time max line
   GtkWidget *labelMaxi = gtk_label_new ("Max Start After Grib");
   GtkWidget *spinButtonMaxi = gtk_spin_button_new_with_range (1, zone.timeStamp [zone.nTimeStamp - 1], 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinButtonMaxi), chooseDeparture.tEnd);
   g_signal_connect (spinButtonMaxi, "value-changed", 
      G_CALLBACK (intSpinUpdate), &chooseDeparture.tEnd);
   labelHours = gtk_label_new ("Hours");
   gtk_label_set_xalign(GTK_LABEL (labelMaxi), 0);
   gtk_grid_attach (GTK_GRID (grid), labelMaxi,      0, 2, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), spinButtonMaxi, 1, 2, 1, 1);
   gtk_grid_attach (GTK_GRID (grid), labelHours,     2, 2, 1, 1);

   GtkWidget *hBox = OKCancelLine (onOkButtonDepClicked, depWindow);

   gtk_box_append (GTK_BOX (vBox), grid);
   gtk_box_append (GTK_BOX (vBox), hBox);
   
   gtk_window_present (GTK_WINDOW (depWindow));
}

/*! Callback for stop button */
static void onStopButtonClicked (GtkWidget *widget, gpointer data) {
   animation.active = NO_ANIMATION;
   if (animation.timer > 0)
      g_source_remove (animation.timer);
   animation.timer = 0;
}

/*! Callback for animation update */
static gboolean onPlayTimeout (gpointer userData) {
   theTime += par.tStep;
   if (theTime > zone.timeStamp [zone.nTimeStamp - 1]) {
      theTime = zone.timeStamp [zone.nTimeStamp - 1]; 
      if (animation.timer > 0)
         g_source_remove (animation.timer);
      animation.active = NO_ANIMATION;
      animation.timer = 0;
   }
   gtk_widget_queue_draw (drawing_area);
   gtk_range_set_value (GTK_RANGE (timeScale), theTime * MAX_TIME_SCALE / zone.timeStamp [zone.nTimeStamp - 1]);
   statusBarUpdate (statusbar);
   return animation.active;
}

/*! Callback for animation update */
static gboolean onLoopTimeout (gpointer userData) {
   theTime += par.tStep;
   if ((theTime > zone.timeStamp [zone.nTimeStamp - 1]) || 
      (theTime > par.startTimeInHours + route.duration))
      theTime = par.startTimeInHours;

   gtk_widget_queue_draw (drawing_area);
   gtk_range_set_value (GTK_RANGE (timeScale), theTime * MAX_TIME_SCALE / zone.timeStamp [zone.nTimeStamp - 1]);
   statusBarUpdate (statusbar);
   return animation.active;
}

/*! Callback for play button (animation) */
static void onPlayButtonClicked (GtkWidget *widget, gpointer userData) {
   if (animation.active == NO_ANIMATION) {
      animation.active = PLAY;
      animation.timer = g_timeout_add (animation.tempo [par.speedDisp], (GSourceFunc)onPlayTimeout, NULL);
   }
}

/*! Callback for loop  button (animation) */
static void onLoopButtonClicked (GtkWidget *widget, gpointer userData) {
   if (route.n == 0) {
      infoMessage ("No route !", GTK_MESSAGE_WARNING);
      return;
   }
   if (animation.active == NO_ANIMATION) {
      animation.active = LOOP;
      if ((theTime < par.startTimeInHours) || (theTime > par.startTimeInHours + route.duration))
         theTime = par.startTimeInHours; 
      gtk_widget_queue_draw (drawing_area);
      statusBarUpdate (statusbar);
      animation.timer = g_timeout_add (animation.tempo [par.speedDisp], (GSourceFunc)onLoopTimeout, NULL);
   }
}

/*! change animation timer */
static void changeAnimation () {
   if (animation.timer > 0 && animation.active != NO_ANIMATION) {
       g_source_remove (animation.timer);
   }
   animation.timer = g_timeout_add (animation.tempo [par.speedDisp], (GSourceFunc)onPlayTimeout, NULL);
}

/*! Callback for to start button 
static void onToStartButtonClicked (GtkWidget *widget, gpointer data) {
   theTime = 0.0;
   gtk_widget_queue_draw(drawing_area);
   statusBarUpdate (statusbar);
}

static void onToEndButtonClicked (GtkWidget *widget, gpointer data) {
   theTime = zone.timeStamp [zone.nTimeStamp - 1];
   gtk_widget_queue_draw (drawing_area);
   statusBarUpdate (statusbar);
}
*/

/*! Callback for reward button */
static void onRewardButtonClicked (GtkWidget *widget, gpointer data) {
   // theTime -= zone.timeStamp [1]-zone.timeStamp [0];
   theTime -= par.tStep;
   if (theTime < 0.0) theTime = 0.0;
   gtk_widget_queue_draw (drawing_area);
   gtk_range_set_value (GTK_RANGE (timeScale), theTime * MAX_TIME_SCALE / zone.timeStamp [zone.nTimeStamp - 1]);
   statusBarUpdate (statusbar);
}

/*! Callback for Forward button */
static void onForwardButtonClicked (GtkWidget *widget, gpointer data) {
   //theTime += zone.timeStamp [1]-zone.timeStamp [0];
   theTime += par.tStep;
   if (theTime > zone.timeStamp [zone.nTimeStamp - 1])
      theTime = zone.timeStamp [zone.nTimeStamp - 1]; 
   gtk_widget_queue_draw(drawing_area);
   gtk_range_set_value (GTK_RANGE (timeScale), theTime * MAX_TIME_SCALE / zone.timeStamp [zone.nTimeStamp - 1]);
   statusBarUpdate (statusbar);
}

/*! Callback for Now button */
static void onNowButtonClicked (GtkWidget *widget, gpointer data) {
   //theTime += zone.timeStamp [1]-zone.timeStamp [0];
   theTime = diffTimeBetweenNowAndGribOrigin (zone.dataDate [0], zone.dataTime [0]/100);
   if (theTime < 0) {
      infoMessage ("In time calculation", GTK_MESSAGE_ERROR);
      statusErrorMessage (statusbar, "In time calculation");
      return;
   }
   if (theTime > zone.timeStamp [zone.nTimeStamp - 1])
      theTime = zone.timeStamp [zone.nTimeStamp - 1]; 
   gtk_widget_queue_draw(drawing_area);
   gtk_range_set_value (GTK_RANGE (timeScale), theTime * MAX_TIME_SCALE / zone.timeStamp [zone.nTimeStamp - 1]);
   statusBarUpdate (statusbar);
}

/*! display isochrones */
static void isocDump () {
   char str [MAX_SIZE_LINE];
   const char *fileContents [] = {par.dumpIFileName};
   if (nIsoc == 0) {
      infoMessage ("No isochrone", GTK_MESSAGE_ERROR);
      return;
   }
   if (par.dumpIFileName [0] == '\0') {
      infoMessage ("No Isochrone File ", GTK_MESSAGE_ERROR);
      return;
   }
   formatThousandSep (str, sizeof (str), getFileSize (par.dumpIFileName));
   char *title = g_strdup_printf ("Dump: %d Isochrones", nIsoc);
   if (! myEditor (app, fileContents, 1, title, NULL))
      infoMessage ("Impossible to open Isochrone dump", GTK_MESSAGE_ERROR);
   g_free (title);
}

/*! display isochrone descriptors */
static void isocDescDump () {
   char footer [MAX_SIZE_LINE];
   char *buffer = NULL;
   size_t length = nIsoc * MAX_SIZE_LINE;
   if (route.n <= 0) {
      infoMessage ("No route calculated", GTK_MESSAGE_WARNING);
      return;
   }
   if ((buffer = (char *) malloc (length)) == NULL) {
      infoMessage ("Not enough memory", GTK_MESSAGE_ERROR); 
      return;
   }
   if (isoDescToStr (buffer, length)) {
      snprintf (footer, sizeof (footer), "Number of isochrones: %d", nIsoc); 
      displayText (app, buffer, strlen (buffer), "Isochrone Descriptor", footer);
   }
   else infoMessage ("Not enough space", GTK_MESSAGE_ERROR);
   free (buffer);
}

/*! display Points of Interest information */
static void historyReset  () {
   route.n = 0;
   freeHistoryRoute ();
   gtk_widget_queue_draw(drawing_area);
}

/*! Response for new Trace OK */
static void newTraceResponse (GtkWidget *widget, gpointer entryWindow) {
   FILE *f;
   printf ("Trace name OK: %s\n", traceName);
   if ((f = fopen (traceName, "r")) != NULL) { // check if file already exist
      infoMessage ("In newTraceResponse: This name already exist ! Retry...", GTK_MESSAGE_ERROR );
      fclose (f);
      return;
   }
   g_strlcpy (par.traceFileName, traceName, MAX_SIZE_FILE_NAME);
   if ((f = fopen (traceName, "w")) == NULL) {
      infoMessage ("In NewTraceResponse: Impossible to Write", GTK_MESSAGE_ERROR );
      return;
   }
   fprintf (f, "    lat;     lon;      epoch;         Date & Time; Cog;     Sog\n");
   fclose (f);
   if ((entryWindow != NULL) && GTK_IS_WINDOW (entryWindow))
      gtk_window_destroy (GTK_WINDOW (entryWindow));
   gtk_widget_queue_draw (drawing_area);
}

/*! new Trace */
static void newTrace () {
   g_strlcpy (traceName, par.traceFileName, MAX_SIZE_FILE_NAME);
   entryBox ("New Trace", "Trace: ", traceName, newTraceResponse);
}

/*! Edit trace  */
static void editTrace () {
   const char *fileContents [] = {par.traceFileName};
   if (! myEditor (app, fileContents, 1, "Trace edition", NULL))
      infoMessage ("Impossible to open Trace", GTK_MESSAGE_ERROR);
}

/*! Export way points to GPX file  */
static void exportWayPoints () {
   if (! exportWpToGpx (par.wpGpxFileName)) 
      infoMessage ("Impossible to write wayPoint GPX file, Check definition", GTK_MESSAGE_ERROR);
   else {
      char *message = g_strdup_printf ("Export done to: %s", par.wpGpxFileName);
      infoMessage (message, GTK_MESSAGE_INFO);
      g_free (message);
   }
}

/*! Export trace to GPX file  */
static void exportTrace () {
   char *gpxFileName = g_strdup_printf("%.*s.gpx", (int)(g_strrstr (par.traceFileName, ".csv") - par.traceFileName), par.traceFileName);
   if (! exportTraceToGpx (par.traceFileName, gpxFileName))
      infoMessage ("Impossible to write Trace GPX file", GTK_MESSAGE_ERROR);
   else {
      char *message = g_strdup_printf ("Export done to: %s", gpxFileName);
      infoMessage (message, GTK_MESSAGE_INFO);
      g_free (message);
   }
   g_free (gpxFileName);
}

/*! add POR to current trace */
static void traceAdd () {
   char fileName [MAX_SIZE_FILE_NAME];
   buildRootName (par.traceFileName, fileName, sizeof (fileName));
   if (competitors.n > 0) {
      int iMain = 0;
      addTracePt (fileName, competitors.t [iMain].lat, competitors.t [iMain].lon);
   }
   else
      infoMessage ("No competitor", GTK_MESSAGE_WARNING);
   gtk_widget_queue_draw (drawing_area);
}

/*! display trace report */
static void traceReport () {
   char fileName [MAX_SIZE_FILE_NAME];
   double od, ld, rd, sog;
   char str [MAX_SIZE_LINE];
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_NAME];
   char strLon [MAX_SIZE_NAME];
   double lat, lon, time;

   GtkWidget *traceWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW(traceWindow), "Trace Report");
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), traceWindow);

   GtkWidget *grid = gtk_grid_new();   
   gtk_window_set_child (GTK_WINDOW (traceWindow), (GtkWidget *) grid);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);

   buildRootName (par.traceFileName, fileName, sizeof (fileName));
   if (! distanceTraceDone (fileName, &od, &ld, &rd, &sog)) {
      infoMessage ("In distance trace calculation", GTK_MESSAGE_ERROR);
      return;
   }
   lineReport (grid, 0, "media-floppy-symbolic", "Name", fileName);
   snprintf (line, sizeof (line), "%.2lf", od);
   lineReport (grid, 2, "mail-forward-symbolic", "Trace Ortho Dist", line);
   snprintf (line, sizeof (line), "%.2lf", ld);
   lineReport (grid, 4, "mail-forward-symbolic", "Trace Loxo Dist", line);
   snprintf (line, sizeof (line), "%.2lf", rd);
   lineReport (grid, 6, "emblem-important-symbolic", "Trace Real Dist", line);

   snprintf (line, sizeof (line), "%.2lf", sog);
   lineReport (grid, 8, "emblem-important-symbolic", "Average SOG", line);

   if (findLastTracePoint (fileName, &lat, &lon, &time)) {
      snprintf (line, sizeof (line), "%s %s",\
         latToStr (par.pOr.lat, par.dispDms, strLat, sizeof (strLat)),\
         lonToStr (par.pOr.lon, par.dispDms, strLon, sizeof (strLon)));
      lineReport (grid, 10, "preferences-desktop-locale-symbolic", "Last Position", line);
      snprintf (line, sizeof (line), "%s", epochToStr (time, false, str, sizeof (str)));
      lineReport (grid, 12, "document-open-recent", "Last Time", line);
   }

   gtk_window_present (GTK_WINDOW (traceWindow));
}

/*! display log */
static void logDump () {
   const char *fileContents [] = {par.logFileName};
   if (! myEditor (app, fileContents, 1, "Log Dump" , NULL))
      infoMessage ("impossible to open log", GTK_MESSAGE_ERROR);
}

/*! call displayText with right parameters in order to dump text file */
static void fileDump (GSimpleAction *action, GVariant *parameter, gpointer user_data) {
   const char *fileName = (const char *) user_data;
   char str [MAX_SIZE_NAME];
   GError *error = NULL;
   char *content = NULL;
   gsize length;
   if (g_file_get_contents (fileName, &content, &length, &error)) {
      formatThousandSep (str, sizeof (str), getFileSize (fileName));
      char *footer = g_strdup_printf ("Size: %s Bytes", str);
      displayText (app, content, length, fileName, footer);
      g_free (footer);
      g_free (content);
   }
   else {
      fprintf (stderr, "In displayFile reading: %s, %s\n", fileName, error->message); 
      g_clear_error (&error);
   }
}

/* call back for openTrace */
static void cbOpenTrace (GObject* source_object, GAsyncResult* res, gpointer data) {
   GFile* file = gtk_file_dialog_open_finish ((GtkFileDialog *) source_object, res, NULL);
   if (file != NULL) {
      char *fileName = g_file_get_path (file);
      if (fileName != NULL) {
         g_strlcpy (par.traceFileName, fileName, MAX_SIZE_FILE_NAME); 
         g_free (fileName);
         traceReport ();
         gtk_widget_queue_draw (drawing_area);
      }
   }
}

/*! Select trace file and read trace file */
static void openTrace () {
   GtkFileDialog* fileDialog = selectFile ("Open Trace", "trace", "Trace Files", "*.csv", "*.csv", NULL);
   gtk_file_dialog_open (fileDialog, GTK_WINDOW (window), NULL, cbOpenTrace, NULL);
}

/*! find Points of Interest information */
static void poiDump () {
   char footer [MAX_SIZE_LINE];
   char *buffer = NULL;
   int count;

   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      fprintf (stderr, "In poiDump, Error Malloc %d\n", MAX_SIZE_BUFFER); 
      infoMessage ("Memory allocation issue", GTK_MESSAGE_ERROR);
      return;
   }
   count = poiToStr (true, buffer, MAX_SIZE_BUFFER);
   snprintf (footer, sizeof (footer), "Number of Points Of Interest: %d,    Number of visible: %d", nPoi, count);
   displayText (app, buffer, strlen (buffer), "POI Finder", footer);
   free (buffer);
}

/*! find Points of Interest information */
static void polygonDump () {
   char footer [MAX_SIZE_LINE];
   char *buffer = NULL;
   if (par.nForbidZone == 0) {
      infoMessage ("No polygon information", GTK_MESSAGE_WARNING);
      return;
   }
   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      fprintf (stderr, "In polygonDump, Error Malloc %d\n", MAX_SIZE_BUFFER); 
      infoMessage ("Memory allocation issue", GTK_MESSAGE_ERROR);
      return;
   }
   polygonToStr (buffer, MAX_SIZE_BUFFER);
   snprintf (footer, sizeof (footer), "Number of Polygons: %d", par.nForbidZone);
   displayText (app, buffer, strlen (buffer), "Polygons", footer);
   free (buffer);
}

/*! display competitors display */
static void competitorsDump () {
   char footer [MAX_SIZE_LINE];
   char *buffer = NULL;
   CompetitorsList *copy = NULL;

   if (competitors.n == 0) {
      infoMessage ("No competiton done", GTK_MESSAGE_ERROR);
      return;
   }
   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      fprintf (stderr, "In competitorDump: Malloc %d\n", MAX_SIZE_BUFFER); 
      infoMessage ("Memory allocation issue", GTK_MESSAGE_ERROR);
      return;
   }
   if ((copy = (CompetitorsList *) malloc (sizeof (CompetitorsList))) == NULL) {
      fprintf (stderr, "In competitorDump: Error Malloc CompetitorsList"); 
      infoMessage ("Memory allocation issue", GTK_MESSAGE_ERROR);
      free (buffer);
      return;
   }
   *copy = competitors; // copy because CompetitorsToStr sort the table
   competitorsToStr (copy, buffer, MAX_SIZE_BUFFER, footer, MAX_SIZE_LINE);
   displayText (app, buffer, strlen (buffer), "Competitors Dashboard", footer);
   free (buffer);
}

/*! print the route from origin to destination or best point */
static void historyRteDump (gpointer userData) {
   int k = GPOINTER_TO_INT (userData);
   char footer [MAX_SIZE_LINE];
   char title [MAX_SIZE_LINE];
   char *buffer = NULL;
   if (historyRoute.n <= 0) {
      infoMessage ("No route calculated", GTK_MESSAGE_WARNING);
      return;
   }
   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      fprintf (stderr, "In historyRteDump: Malloc %d\n", MAX_SIZE_BUFFER); 
      infoMessage ("Memory allocation issue", GTK_MESSAGE_ERROR);
      return;
   }
   routeToStr (&historyRoute.r[k], buffer, MAX_SIZE_BUFFER, footer, sizeof (footer)); 
   int cIndex = MAX (0, historyRoute.r[k].competitorIndex);
   snprintf (title, MAX_SIZE_LINE, "History: %2d %s", k, competitors.t [cIndex].name);
   displayText (app, buffer, strlen (buffer), title, footer);
   free (buffer);
}

/*! display history of routes */
static void routeHistory () {
   char str [MAX_SIZE_LINE];
   GtkWidget *itemHist; 
   GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   if ((historyRoute.n <= 1) || (competitors.n == 0)) {
      infoMessage ("No History", GTK_MESSAGE_WARNING);
      return;
   }
   if (historyRoute.n == 2) {
      historyRteDump (GINT_TO_POINTER (0));
      return;
   }
   for (int i = 0; i < historyRoute.n - 1; i++) {
      int cIndex = MAX (0, historyRoute.r[i].competitorIndex);
      snprintf (str, sizeof (str), "History: %2d %s\n", i, competitors.t [cIndex].name); 
      itemHist = gtk_button_new_with_label (str);
      g_signal_connect_swapped (itemHist, "clicked", G_CALLBACK (historyRteDump), GINT_TO_POINTER (i));
      gtk_box_append (GTK_BOX (box), itemHist);
   }
   menuHist = gtk_popover_new ();
   gtk_widget_set_parent (menuHist, window);
   g_signal_connect(window, "destroy", G_CALLBACK (onParentWindowDestroy), menuHist);
   gtk_popover_set_child ((GtkPopover*) menuHist, (GtkWidget *) box);
   gtk_popover_set_has_arrow ((GtkPopover*) menuHist, FALSE);
   GdkRectangle rect = {200, 100, 1, 1};
   gtk_popover_set_pointing_to ((GtkPopover*) menuHist, &rect);
   gtk_widget_set_visible (menuHist, true);
}

/*! Draw simulation */
static void cbSimulationReport (GtkDrawingArea *area, cairo_t *cr, 
   int width, int height, gpointer userData) {
   char str [MAX_SIZE_NUMBER];
   int x, y;
   cairo_set_line_width (cr, 1);

   if ((chooseDeparture.tStop - chooseDeparture.tBegin == 0) || ((int) chooseDeparture.maxDuration == 0)) {
      fprintf (stderr, "In cbSimulationReport: unapropriate values for chooseDeparture\n");
      return;
   }
   
   const int xLeft = 30;
   const int xRight = width - 20;
   const int yTop = 10;
   const int yBottom = height - 25;
   const int xk = (xRight - xLeft) / chooseDeparture.count;  
   const int yk = (yBottom - yTop) / (int) chooseDeparture.maxDuration;
   const int constDelta = 5;
   const int unreachY = 100;
   const int rectangleWidth = CLAMP (xk, 1, 20); // between 1 and 20

   // Draw horizontal line with arrow
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   cairo_move_to (cr, xLeft,  yBottom );
   cairo_line_to (cr, xRight, yBottom);
   cairo_line_to (cr, xRight - constDelta, yBottom + constDelta); // for arrow
   cairo_stroke (cr);
   cairo_move_to (cr, xRight, yBottom );
   cairo_line_to (cr, xRight - constDelta, yBottom - constDelta);
   cairo_stroke (cr);
   
   // Draw vertical line with arrow
   cairo_move_to (cr, xLeft, yBottom);
   cairo_line_to (cr, xLeft, yTop);
   cairo_line_to (cr, xLeft - constDelta, yTop + constDelta);
   cairo_stroke (cr);
   cairo_move_to (cr, xLeft, yTop);
   cairo_line_to (cr, xLeft + constDelta, yTop + constDelta);
   cairo_stroke (cr);
   int h; 
   int poulidorCount = -1; // to store the best value
   double poulidorVal = DBL_MAX; 

   CAIRO_SET_SOURCE_RGB_GRAY (cr);
   // one gray vertical rectangle for each value
   for (int count = 0; count < chooseDeparture.count; count += 1) {
      if (count != chooseDeparture.bestCount && chooseDeparture.t [count] != NIL) {
         h = (int) (chooseDeparture.t [count] * yk);
         if (chooseDeparture.t [count] < poulidorVal && chooseDeparture.t [count] >= 0) {
            poulidorVal = chooseDeparture.t [count];
            poulidorCount = count;
         }
         cairo_rectangle (cr, xLeft + count * xk, yBottom - h, rectangleWidth, h);
      }
   }
   cairo_fill (cr);
   CAIRO_SET_SOURCE_RGB_RED (cr); // unreachable values
   for (int count = 0; count < chooseDeparture.count; count += 1) {
      if (chooseDeparture.t [count] == NIL)
         cairo_rectangle (cr, xLeft + count * xk, yBottom - unreachY, rectangleWidth, unreachY);
   }
   cairo_fill (cr);
   // one green vertical rectangle for the best value
   CAIRO_SET_SOURCE_RGB_GREEN (cr);
   h = (int) chooseDeparture.t [chooseDeparture.bestCount] * yk;
   cairo_rectangle (cr, xLeft + chooseDeparture.bestCount * xk, yBottom - h, rectangleWidth, h);
   cairo_fill (cr);

   // oone orange vertical rectangle for the poulidor value
   if (poulidorCount != -1) {
      CAIRO_SET_SOURCE_RGB_ORANGE (cr);
      h = (int) chooseDeparture.t [poulidorCount] * yk;
      cairo_rectangle (cr, xLeft + poulidorCount * xk, yBottom - h, rectangleWidth, h);
      cairo_fill (cr);
   }

   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   // Draw time labels
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 10);

   int lastX = xLeft;
   const int minXLibelle = 50;
   // Horizontal libelles for time 
   for (int count = 0; count < chooseDeparture.count; count += 1) {
      x = xLeft + xk * count;
      if (((x - lastX) < minXLibelle) && (count != 0)) // avoid print too many closed libelles
         continue;
      cairo_move_to (cr, x, yBottom + 10);
      double t = chooseDeparture.tBegin + count * chooseDeparture.tInterval;
      snprintf (str, sizeof (str),"%02.0lf:%02.0lf", floor (t), 60.0 * fmod (t, 1.0)); 
      cairo_show_text (cr, str);
      lastX = x;
   }
   cairo_stroke(cr);

   // Draw vertical duration labels
   const int durationStep = (int) (chooseDeparture.maxDuration) / 10;
   for (int duration = 0; duration < chooseDeparture.maxDuration ; duration += durationStep) {
      y = yBottom - duration * yk;
      cairo_move_to (cr, xLeft - 20, y);
      snprintf (str, sizeof (str),"%2d", duration); 
      cairo_show_text (cr, str);
      CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr); // Horizontal lines
      cairo_move_to (cr, xLeft, y);
      cairo_line_to (cr, xRight, y);
      cairo_stroke (cr);
      CAIRO_SET_SOURCE_RGB_BLACK (cr);
   }
   cairo_stroke (cr);
}

/*! display the report of simulation for best time departure */
static void simulationReport () { 
   time_t now = time (NULL);
   struct tm *timeInfos = gmtime (&now);
   char text [MAX_SIZE_TEXT];
   char line [MAX_SIZE_LINE];
   char strDate [MAX_SIZE_DATE];
   if (chooseDeparture.count <= 0) {
      infoMessage ("No simulation available", GTK_MESSAGE_WARNING);
      return;
   }
   GtkWidget *simulationWindow = gtk_application_window_new (GTK_APPLICATION (app));
   gtk_window_set_title (GTK_WINDOW (simulationWindow), \
      "Simulation Report. Duration in hours as a function of time departure after grib time");
   gtk_window_set_default_size (GTK_WINDOW(simulationWindow), 1200, 500);
   g_signal_connect (window, "destroy", G_CALLBACK (onParentDestroy), simulationWindow);

   GtkWidget *vBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (simulationWindow), vBox);

   GtkWidget *simulDrawingArea = gtk_drawing_area_new ();
   gtk_widget_set_hexpand (simulDrawingArea, TRUE);
   gtk_widget_set_vexpand (simulDrawingArea, TRUE);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (simulDrawingArea), cbSimulationReport, NULL, NULL);

   // Copyable Text zone
   GtkWidget *textView = gtk_text_view_new();
   gtk_text_view_set_editable (GTK_TEXT_VIEW(textView), FALSE);  // Not editable
   gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(textView), GTK_WRAP_WORD_CHAR);
   gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW(textView), FALSE);

   GtkTextBuffer *textBuffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW(textView));
   newDate (zone.dataDate [0], zone.dataTime [0]/100 + chooseDeparture.bestTime, strDate, sizeof (strDate));
   
   snprintf (text, sizeof (text), "    Run time: %04d/%02d/%02d %02d:%02d:%02d UTC\n",
        timeInfos->tm_year+1900, timeInfos->tm_mon+1, timeInfos->tm_mday,
        timeInfos->tm_hour, timeInfos->tm_min, timeInfos->tm_sec);

   // Add text to GtkTextView
   snprintf (line, sizeof (line), \
      "    Min Duration : %02.0lf:%02.0lf hh:mm; Best Departure Time:  %02.0lf:%02.0lf hh:mm after beginning of Grib\n",\
      floor (chooseDeparture.minDuration), 60 * fmod (chooseDeparture.minDuration, 1.0),\
      floor (chooseDeparture.bestTime),  60 * fmod (chooseDeparture.bestTime, 1.0)
   );
   g_strlcat (text, line, sizeof (text));

   snprintf (line, sizeof (line), "    Isoc Time Step: %02.0f:%02.0f; Recommended start: %s UTC",\
      floor (route.isocTimeStep), 60 * fmod (route.isocTimeStep, 1.0), strDate);
   g_strlcat (text, line, sizeof (text));
 
   gtk_text_buffer_set_text (textBuffer, text, -1);

   // statusbar
   snprintf (line, sizeof (line), "Recommended start  : %s UTC", strDate);
   GtkWidget *simulStatusBar = gtk_label_new (line);
      
   gtk_box_append (GTK_BOX(vBox), simulDrawingArea);
   gtk_box_append (GTK_BOX(vBox), textView);
   gtk_box_append (GTK_BOX(vBox), simulStatusBar);

   gtk_window_present (GTK_WINDOW (simulationWindow));   
}

#define MAX_VAL_ROUTE 4 // for routeGramDraw
/*! Draw routegram */
static void routeGramDraw (GtkDrawingArea *area, cairo_t *cr, \
   int width, int height, gpointer userData) {

   int x, y;
   char str [MAX_SIZE_LINE], strDate [MAX_SIZE_LINE];
   const int legendXLeft = width - 80;
   const int legendYTop = 10;
   const int xLeft = 30;
   const int xRight = width - 100;
   const int yTop = 20;
   const int yBottom = height - 25;
   const int headY = 10;
   const int constDelta = 5;
   const int dayLG = 10;
   const int ySail = yTop + 15;
   const int ySailName = yTop + 10;
   const int decYSailName = 15;

   if ((route.duration + par.tStep) == 0) {
      fprintf (stderr, "In onRouteGramEvent: unapropriate values for route.duration. par.tStep\n");
      return;
   }
   
   const int xk = (xRight - xLeft) / (route.duration + par.tStep);  
   double u, v, head_x;

   cairo_set_line_width (cr, 1);
   cairo_set_font_size (cr, 12);

   const char *libelle [MAX_VAL_ROUTE] = {"Wind", "Gust", "Waves", "Staminai"}; 
   double colors [MAX_VAL_ROUTE] [3] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {1.0, 165.0/255.0, 0.0}}; // blue red green orange
   drawLegend (cr, legendXLeft, legendYTop, colors, libelle, MAX_VAL_ROUTE);

   // Draw horizontal line with arrow
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_move_to (cr, xLeft,  yBottom);
   cairo_line_to (cr, xRight, yBottom);
   cairo_line_to (cr, xRight - constDelta, yBottom + constDelta); // for arrow
   cairo_stroke (cr);
   cairo_move_to (cr, xRight, yBottom );
   cairo_line_to (cr, xRight - constDelta, yBottom - constDelta);
   cairo_stroke (cr);
   
   // Draw verical line with arrow
   cairo_move_to (cr, xLeft, yBottom);
   cairo_line_to (cr, xLeft, yTop);
   cairo_line_to (cr, xLeft - constDelta, yTop + constDelta);
   cairo_stroke (cr);
   cairo_move_to (cr, xLeft, yTop);
   cairo_line_to (cr, xLeft + constDelta, yTop + constDelta);
   cairo_stroke (cr);
   
   // Draw time labels and vertical lines
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 10);
   CAIRO_SET_SOURCE_RGB_BLACK (cr);

   struct tm tmTime;
   memcpy (&tmTime, &startInfo, sizeof (tmTime)); 
   double timeStep = (route.duration <= 120) ? 6 : 12;
   int lastDay = -1;

   // draw horizontal time 
   for (double i = 0.0; i <= route.duration; i += timeStep) {
      x = xLeft + xk * i;
      cairo_move_to (cr, x, yBottom + 10);
      snprintf (strDate, sizeof (strDate), \
         "%4d/%02d/%02d %02d:%02d", tmTime.tm_year + 1900, tmTime.tm_mon+1, tmTime.tm_mday, tmTime.tm_hour, tmTime.tm_min);
      cairo_show_text (cr, strrchr (strDate, ' ') + 1);  // hour is after space

      if (tmTime.tm_mday != lastDay) {                   // change day
         cairo_move_to (cr, x, yBottom + 20);
         strDate [dayLG] = '\0';                        // only date
         cairo_show_text (cr, strDate);
         CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr);
         cairo_move_to (cr, x, yBottom);
         cairo_line_to (cr, x, yTop);
         cairo_stroke (cr);
         CAIRO_SET_SOURCE_RGB_BLACK(cr);
      }
      lastDay = tmTime.tm_mday;
      tmTime.tm_min += timeStep * 60;
      mktime (&tmTime);
   }
   cairo_stroke(cr);
   
   // vertical arrival line
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   double lastX = xLeft + xk * route.duration;
   cairo_move_to (cr, lastX, yBottom);
   cairo_line_to (cr, lastX, yTop);
   cairo_stroke (cr);
   showUnicode (cr, DESTINATION_UNICODE, lastX, yTop + 50);
   
   // arrival date
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size (cr, 16);
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route.duration, strDate, sizeof (strDate));
   cairo_move_to (cr, lastX, yTop + 100);
   char *ptHour = strchr (strDate, ' ');
   if (ptHour != NULL) *ptHour = '\0'; // cut strdate with date and hour
   cairo_show_text (cr, strDate);      // the date
   cairo_move_to (cr, lastX , yTop + 120);
   if (ptHour != NULL) cairo_show_text (cr, ptHour + 1); // the time
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size (cr, 10);

   // Draw speeds labels and horizontal lines
   cairo_set_font_size (cr, 10);
   const double step = 5.0;
   double maxMax = MAX (MAX (MS_TO_KN * route.maxGust, route.maxTws), route.maxSog);
   if (maxMax <= 0) {
      fprintf (stderr, "In routeGramDraw: maxMax should be strictly positive\n");
      return;
   }
   const int yk = (yBottom - yTop) / maxMax;
   for (double speed = step; speed <= maxMax + step; speed += step) {
      y = yBottom - yk * speed;
      cairo_move_to (cr, xLeft - 20, y);
      snprintf (str, MAX_SIZE_LINE, "%02.0lf", speed);
      cairo_show_text (cr, str);
      CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr); // Horizontal lines
      cairo_move_to (cr, xLeft, y);
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
      head_x = xLeft + xk * i * par.tStep;
      u = -sin (DEG_TO_RAD * route.t[i].twd) * route.t[i].tws / MS_TO_KN;
      v = -cos (DEG_TO_RAD * route.t[i].twd) * route.t[i].tws / MS_TO_KN; 
      arrow (cr, head_x, headY, u, v, route.t [i].twd, route.t[i].tws, WIND);
   }

   // draw gust
   if (route.maxGust > 0) { 
      CAIRO_SET_SOURCE_RGB_RED (cr);
      for (int i = 0; i < route.n; i ++) {
         x = xLeft + xk * i * par.tStep;
         y = yBottom - yk * MAX (MS_TO_KN * route.t[i].g, route.t[i].tws);
         if (i == 0) cairo_move_to (cr, x, y);
         else cairo_line_to (cr, x, y);
      }
      if (route.destinationReached) 
         cairo_line_to (cr, lastX, yBottom - yk * MAX (MS_TO_KN * lastG, lastTws));
      cairo_stroke(cr);
   }

   // draw tws 
   CAIRO_SET_SOURCE_RGB_BLUE (cr);
   for (int i = 0; i < route.n; i++) {
      x = xLeft + xk * i * par.tStep;
      y = yBottom - yk * route.t[i].tws;
      if (i == 0) cairo_move_to (cr, x, y);
      else cairo_line_to (cr, x, y);
   }
   if (route.destinationReached) 
      cairo_line_to (cr, lastX, yBottom - yk * lastTws);
   cairo_stroke(cr);
   
   //draw Waves
   if (route.maxWave > 0) { 
      CAIRO_SET_SOURCE_RGB_GREEN (cr);
      for (int i = 0; i < route.n; i ++) {
         x = xLeft + xk * i * par.tStep;
         y = yBottom - yk * route.t[i].w;
         if (i == 0) cairo_move_to (cr, x, y);
         else cairo_line_to (cr, x, y);
      }
      if (route.destinationReached) 
         cairo_line_to (cr, lastX, yBottom - yk * lastW);
      cairo_stroke(cr);
   }

   //draw speed
   cairo_set_line_width (cr, 5);
   x = xLeft;
   y = yBottom - yk * route.t[0].sog;
   int motor = route.t[0].motor;
   int amure = route.t[0].amure;
   routeColor (cr, motor, amure);
   cairo_move_to (cr, x, y);
   int n = (route.destinationReached) ? route.n : route.n - 1; 
   for (int i = 1; i < n; i++) {
      x = xLeft + xk * i * par.tStep;
      y = yBottom - yk * route.t[i].sog;
      cairo_line_to (cr, x, y);
      if ((route.t[i].motor != motor) || (route.t[i].amure != amure)) {
         cairo_stroke (cr);
         amure = route.t[i].amure;
         motor = route.t[i].motor;
         routeColor (cr, motor, amure);
         cairo_move_to (cr, x, y);
      }
   }
   // last speed segment to destination
   cairo_line_to (cr, lastX, y);
   cairo_stroke (cr);

   // draw sail
   cairo_set_line_width (cr, 5);
   x = xLeft;
   int sail = route.t [0].sail;
   GdkRGBA color = sailColor [sail % MAX_N_SAIL];
   cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
   cairo_move_to (cr, x, ySail);
   for (int i = 1; i < route.n; i++) {
      x = xLeft + xk * i * par.tStep;
      cairo_line_to (cr, x, ySail);
      if (route.t[i].sail != sail) {
         cairo_stroke (cr);
         sail = route.t [i].sail;
         GdkRGBA color = sailColor [sail % MAX_N_SAIL];
         cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
         cairo_move_to (cr, x, ySail);
      }
   }
   cairo_line_to (cr, lastX, ySail);
   cairo_stroke (cr);

   // write sail name
   x = xLeft;
   cairo_set_font_size (cr, 11);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   CAIRO_SET_SOURCE_RGB_GRAY (cr);
   cairo_move_to (cr, x, ySailName);
   sail = route.t [0].sail;
   int dec = 0; // say if decalage or not alternately
   cairo_show_text (cr, sailName [sail % MAX_N_SAIL]);
   for (int i = 1; i < route.n; i++) {
      x = xLeft + xk * i * par.tStep;
      if (route.t[i].sail != sail) {
         dec = ! dec;
         cairo_move_to (cr, x, ySailName + dec * decYSailName);
         sail = route.t [i].sail;
         cairo_show_text (cr, sailName [sail % MAX_N_SAIL]);
      }
   }
   //draw stamina
   if (par.staminaVR >= 0) { // stamina information exist 
      CAIRO_SET_SOURCE_RGB_ORANGE (cr);
      for (int i = 0; i < route.n; i ++) {
         x = xLeft + xk * i * par.tStep;
         y = yBottom - yk * ((int) route.t[i].stamina) / 10;
         if (i == 0) cairo_move_to (cr, x, y);
         else cairo_line_to (cr, x, y);
      }
      if (route.destinationReached) 
         cairo_line_to (cr, lastX, y);
      cairo_stroke(cr);
   }
}
/*! return decorated label associated to str */
static GtkWidget *labelDeco (const char *str) {
   GtkWidget *label = gtk_label_new (str);
   char *pango_markup = g_strdup_printf (\
       "<span foreground='blue' font-style='italic' weight='bold' font_family='monospace'>%s</span>", str);
   gtk_label_set_markup (GTK_LABEL(label), pango_markup);
   g_free (pango_markup);
   
   gtk_label_set_yalign(GTK_LABEL(label), 0);
   gtk_label_set_xalign(GTK_LABEL(label), 0);   
   return label;
}

/*! one line for routeGram () */
static void lineRouteGramReport (GtkWidget *grid, int l, const char* iconName, const char *str0,
                                 const char *val0, const char *str1, const char *val1) {
   //GtkWidget *icon = gtk_button_new_from_icon_name (iconName);
   GtkWidget *label = gtk_label_new ("           ");  // space
   gtk_grid_attach (GTK_GRID (grid), label, 0, l, 1, 1);

   label = labelDeco (str0);
   gtk_grid_attach (GTK_GRID (grid), label, 1, l, 1, 1);
   label = labelDeco (val0);
   gtk_grid_attach (GTK_GRID (grid), label, 2, l, 1, 1);

   label = gtk_label_new ("            ");            // space between colummns
   gtk_grid_attach (GTK_GRID (grid), label, 3, l, 1, 1);

   label = labelDeco (str1);
   gtk_grid_attach (GTK_GRID (grid), label, 4, l, 1, 1);
   label = labelDeco (val1);
   gtk_grid_attach (GTK_GRID (grid), label, 5, l, 1, 1);

   gtk_widget_set_margin_end (GTK_WIDGET (label), 20);
}

/*! represent routegram */
static void routeGram () {
   char totalDate [MAX_SIZE_DATE]; 
   char str [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_NAME], strLon [MAX_SIZE_NAME];
   char str0 [MAX_SIZE_LINE];
   char str1 [MAX_SIZE_LINE];
   int cIndex = MAX (0, route.competitorIndex);

   if (route.n <= 0) {
      infoMessage ("No route calculated", GTK_MESSAGE_WARNING);
      return;
   }
   GtkWidget *routeWindow = gtk_application_window_new (GTK_APPLICATION (app));

   if (route.calculationTime > 0)
      snprintf (str, sizeof (str), "Destnation %s %s      Compute Time: %.2lf sec.", (route.destinationReached) ? 
       "Reached" : "Unreached", competitors.t [cIndex].name, route.calculationTime);
   else 
      snprintf (str, sizeof (str), "Desination %s %s", (route.destinationReached) ? "Reached" : "Unreached", competitors.t [cIndex].name);

   gtk_window_set_title (GTK_WINDOW (routeWindow), str);

   gtk_window_set_default_size (GTK_WINDOW (routeWindow), 1400, 500);
   g_signal_connect (window, "destroy", G_CALLBACK (onParentDestroy), routeWindow);

   GtkWidget *vBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (routeWindow), vBox);

   GtkWidget *routeDrawingArea = gtk_drawing_area_new ();
   gtk_widget_set_hexpand (routeDrawingArea, TRUE);
   gtk_widget_set_vexpand (routeDrawingArea, TRUE);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (routeDrawingArea), routeGramDraw, NULL, NULL);

   // setup drawing area for stat
   GtkWidget *hBox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);

   GtkWidget *grid = gtk_grid_new();   
   gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
   //gtk_grid_set_row_spacing (GTK_GRID (grid), 5);
   gtk_grid_set_row_homogeneous (GTK_GRID (grid), FALSE);
   gtk_grid_set_column_homogeneous (GTK_GRID(grid), FALSE);
   
   snprintf (str0, sizeof (str0), "%s",\
      newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours, 
      totalDate, sizeof (totalDate)));
   snprintf (str1, sizeof (str1), "%s",\
      newDate (zone.dataDate [0], zone.dataTime [0]/100 + par.startTimeInHours + route.duration, 
      totalDate, sizeof (totalDate)));
   lineRouteGramReport (grid, 0, "alarm-symbolic", "Departure", str0, "Arrival", str1);

   snprintf (str0, sizeof (str0), "%02d:%02d", (int) par.startTimeInHours,\
       (int) (60 * fmod (par.startTimeInHours, 1.0)));
   snprintf (str1, sizeof (str1), "%02d:%02d", (int) par.tStep, (int) (60 * fmod (par.tStep, 1.0)));
   lineRouteGramReport (grid, 1, "dialog-information-symbolic", "Time after grib origin", \
      str0, "Isoc. Time Step", str1);
   
   snprintf (str0, sizeof (str0), "%d", route.nIsoc);
   snprintf (str1, sizeof (str1), "%s %s",\
      latToStr (lastClosest.lat, par.dispDms, strLat, sizeof (strLat)),\
      lonToStr (lastClosest.lon, par.dispDms, strLon, sizeof (strLon)));
   lineRouteGramReport (grid, 2, "accessories-text-editor-symbolic", "Nb of isochrones", str0, "Best Point Reached", str1);

   snprintf (str0, sizeof (str0), "%.2lf", \
         (route.destinationReached) ? 0 : orthoDist (lastClosest.lat, lastClosest.lon,\
         par.pDest.lat, par.pDest.lon));
   snprintf (str1, MAX_SIZE_LINE, "%.2lf - %.2lf", route.avrSog, route.maxSog);

   lineRouteGramReport (grid, 3, "mail-forward-symbolic", "Distance To Dest. (NM)", str0, "Avr - Max Speed (Kn)", str1);

   snprintf (str0, sizeof (str0), "%.2lf", route.totDist);
   snprintf (str1, sizeof (str1), "%.2lf", route.motorDist);
   lineRouteGramReport (grid, 4, "emblem-important-symbolic", "Distance (NM)", str0, \
                        "Motor Distance (NM)", str1);
   
   snprintf (str0, sizeof (str0), "%s", durationToStr (route.duration, str, sizeof (str)));
   snprintf (str1, sizeof (str1), "%s", durationToStr (route.motorDuration, str, sizeof (str)));
   lineRouteGramReport (grid, 5, "user-away", "Duration", str0, "Motor Duration", str1);

   snprintf (str0, sizeof (str0), "%d", route.nSailChange);
   snprintf (str1, sizeof (str1), "%d", route.nAmureChange);
   lineRouteGramReport (grid, 6, "utilities-system-monitor-symbolic", "Sail changes",  str0, "Amure changes", str1);

   char *polarFileName = g_path_get_basename (par.polarFileName);
   lineRouteGramReport (grid, 7, "utilities-system-monitor-symbolic", "Polar File", polarFileName, "", "");
   g_free (polarFileName);

   GtkWidget *statDrawingArea = gtk_drawing_area_new ();
   gtk_widget_set_size_request (statDrawingArea,  REPORT_WIDTH / 2, 150);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (statDrawingArea), onStatEvent, NULL, NULL);

   GtkWidget *allureDrawingArea = gtk_drawing_area_new ();
   gtk_widget_set_size_request (allureDrawingArea,  REPORT_WIDTH / 2, 150);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (allureDrawingArea), onAllureEvent, NULL, NULL);

   gtk_box_append (GTK_BOX (hBox), statDrawingArea);
   gtk_box_append (GTK_BOX (hBox), allureDrawingArea);
   gtk_box_append (GTK_BOX (hBox), grid);

   snprintf (str, MAX_SIZE_LINE, "Avr TWS: %.2lf, Max TWS: %.2lf Kn, Max Gust: %.2lf Kn, Avr Waves: %.2lf m, Max Waves: %.2lf m",\
      route.avrTws, route.maxTws, MAX (route.maxTws, MS_TO_KN * route.maxGust), route.avrWave, route.maxWave);

   GtkWidget *routeStatusBar = gtk_label_new (str);
   GtkWidget *empty = gtk_label_new ("       ");
      
   gtk_box_append (GTK_BOX(vBox), routeDrawingArea);
   gtk_box_append (GTK_BOX(vBox), empty); // space
   gtk_box_append (GTK_BOX(vBox), hBox);
   gtk_box_append (GTK_BOX(vBox), routeStatusBar);

   gtk_window_present (GTK_WINDOW (routeWindow));   
}

/*! print the route from origin to destination or best point */
static void routeDump () {
   char footer [MAX_SIZE_LINE];
   char line [MAX_SIZE_LINE];
   char *buffer = NULL;

   if ((route.n <= 0) || (competitors.n == 0)) {
      infoMessage ("No route calculated", GTK_MESSAGE_WARNING);
      return;
   }
   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      fprintf (stderr, "In routeDump, Error Malloc %d\n", MAX_SIZE_BUFFER); 
      infoMessage ("Memory allocation issue", GTK_MESSAGE_ERROR);
      return;
   }
   routeToStr (&route, buffer, MAX_SIZE_BUFFER, footer, sizeof (footer)); 
   snprintf (line, sizeof (line), "%s %s", (route.destinationReached) ? "Destination reached" :\
      "Destination unreached. Route to best point", competitors.t [competitors.runIndex].name);
   displayText (app, buffer, strlen (buffer), line, footer);
   free (buffer);
}

/*! show parameters */
static void parDump () {
   char tempFileName [MAX_SIZE_FILE_NAME];
   writeParam (buildRootName (TEMP_FILE_NAME, tempFileName, sizeof (tempFileName)), true, false);
   fileDump (NULL, NULL, tempFileName); 
}

/* call back after poi edit */
static void cbAfterPoiEdit () {
   if ((windowEditor != NULL) && GTK_IS_WINDOW (windowEditor))
      gtk_window_destroy (GTK_WINDOW (windowEditor));
   nPoi = 0;
   if (par.poiFileName [0] != '\0')
      nPoi += readPoi (par.poiFileName);
   if (par.portFileName [0] != '\0')
      nPoi += readPoi (par.portFileName);
}

/*! edit poi or port */
static void poiEdit (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   const char *fileContents [] = {par.poiFileName, par.portFileName};
   if (! myEditor (app, fileContents, 2, "PoI and Ports Edition", cbAfterPoiEdit))
      infoMessage ("impossible to open Point of Interest", GTK_MESSAGE_ERROR);
}

/*!  Function to draw the speedometer */
static void drawSpeedometer(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
   MyGpsData  *dashboard = (MyGpsData *)data;
   double sog = (dashboard->OK) ? dashboard->sog : 0.0;
   if (isnan (sog)) sog = 0.0;

   double xc = width / 2.0;
   double yc = height / 2.0;
   
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   cairo_arc (cr, xc, yc, DASHBOARD_RADIUS, 0, 2 * G_PI);
   cairo_stroke (cr);

   // Draw the speed inside a rectangle at the center
   char speed_str[10];
   cairo_set_font_size (cr, 10);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   snprintf(speed_str, sizeof(speed_str), "%.2lfKn", sog);
   cairo_rectangle (cr, xc - 25, yc + 15, 60, 20);
   cairo_fill(cr);
   CAIRO_SET_SOURCE_RGB_WHITE(cr);
   cairo_move_to(cr, xc - 15, yc + 30);
   cairo_show_text(cr, speed_str);

   // Draw speed marks
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_set_line_width(cr, 2);
   for (int i = DASHBOARD_MIN_SPEED; i <= DASHBOARD_MAX_SPEED; ++i) {
      double angle = (i - DASHBOARD_MIN_SPEED) * (G_PI / (DASHBOARD_MAX_SPEED - DASHBOARD_MIN_SPEED)) - G_PI;
      double x1 = xc + (DASHBOARD_RADIUS - 10.0) * cos (angle);
      double y1 = yc + (DASHBOARD_RADIUS - 10.0) * sin (angle);
      double x2 = xc + DASHBOARD_RADIUS * cos (angle);
      double y2 = yc + DASHBOARD_RADIUS * sin (angle);
      cairo_move_to(cr, x1, y1);
      cairo_line_to(cr, x2, y2);
      cairo_stroke(cr);

      // Draw the speed numbers
      char speedText [4];
      snprintf (speedText, sizeof(speedText), "%d", i);
      cairo_text_extents_t extents;
      cairo_text_extents (cr, speedText, &extents);
      double tx = xc + (DASHBOARD_RADIUS - 20.0) * cos (angle) - extents.width / 2;
      double ty = yc + (DASHBOARD_RADIUS - 20.0) * sin (angle) + extents.height / 2;
      cairo_move_to (cr, tx, ty);
      cairo_show_text (cr, speedText);
   }

   // Draw the needle
   CAIRO_SET_SOURCE_RGB_RED(cr);
   cairo_set_line_width (cr, 3);
   double needleAngle = (sog - DASHBOARD_MIN_SPEED) * (G_PI / (DASHBOARD_MAX_SPEED - DASHBOARD_MIN_SPEED)) - G_PI;
   double needleX = xc + (DASHBOARD_RADIUS - 20.0) * cos (needleAngle);
   double needleY = yc + (DASHBOARD_RADIUS - 20.0) * sin (needleAngle);
   cairo_move_to (cr, xc, yc);
   cairo_line_to (cr, needleX, needleY);
   cairo_stroke (cr);
}

/*!  Function to draw the compass */
static void drawCompass (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
   MyGpsData  *dashboard = (MyGpsData *)data;
   double cog = (dashboard->OK) ? dashboard->cog : 0.0;
   if (isnan (cog)) cog = 0.0;
   double angle_rad = cog * DEG_TO_RAD;

   cairo_set_line_width(cr, 2.0);

   double xc = width / 2.0;
   double yc = height / 2.0;
   
   // Draw exterior circle for compass
   cairo_arc (cr, xc, yc, DASHBOARD_RADIUS, 0, 2.0 * G_PI);
   cairo_stroke(cr);

   // Draw main  directions (N, E, S, W) and secondary (NE, SE, SW, NW)
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size (cr, 20);

   struct {
      const char *label;
      double angle;
   } directions[] = {
      {"N", 0}, {"NE", 45}, {"E", 90}, {"SE", 135},
      {"S", 180}, {"SW", 225}, {"W", 270}, {"NW", 315}
   };

   for (int i = 0; i < 8; i++) {
      double direction_angle = directions[i].angle * DEG_TO_RAD;
      double label_x = xc + (DASHBOARD_RADIUS - 30.0) * sin (direction_angle) - 10.0;
      double label_y = yc - (DASHBOARD_RADIUS - 30.0) * cos (direction_angle) + 10.0;
      cairo_move_to(cr, label_x, label_y);
      cairo_show_text(cr, directions[i].label);
   }

   // Draw graduations
   cairo_set_line_width (cr, 1.0);
   for (int i = 0; i < 360; i += 10) {
      double tick_angle = i * DEG_TO_RAD;
      double inner_radius = DASHBOARD_RADIUS - (i % 30 == 0 ? 20.0 : 10.0);
      double tick_x1 = xc + DASHBOARD_RADIUS * sin(tick_angle);
      double tick_y1 = yc - DASHBOARD_RADIUS * cos(tick_angle);
      double tick_x2 = xc + inner_radius * sin(tick_angle);
      double tick_y2 = yc - inner_radius * cos(tick_angle);
      cairo_move_to(cr, tick_x1, tick_y1);
      cairo_line_to(cr, tick_x2, tick_y2);
      cairo_stroke(cr);
   }

   // Draw the speed inside a rectangle at the center
   char cog_str [10];
   cairo_set_font_size (cr, 10);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   snprintf(cog_str, sizeof (cog_str), "%.0lf°", cog);
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   cairo_rectangle (cr, xc - 25, yc + 15, 60, 20);
   cairo_fill (cr);
   CAIRO_SET_SOURCE_RGB_WHITE (cr);
   cairo_move_to (cr, xc - 15, yc + 30);
   cairo_show_text (cr, cog_str);

   double needle_length = DASHBOARD_RADIUS * 0.8;
   double needle_x = xc + needle_length * sin (angle_rad);
   double needle_y = yc - needle_length * cos (angle_rad);

   // Draw basis for arrow
   cairo_set_source_rgb (cr, 0, 0, 0);
   cairo_arc(cr, xc, yc, 10, 0, 2.0 * G_PI);
   cairo_fill(cr);

   // Draw line for arrow
   CAIRO_SET_SOURCE_RGB_RED (cr);
   cairo_set_line_width (cr, 4.0);
   cairo_move_to (cr, xc, yc);
   cairo_line_to (cr, needle_x, needle_y);
   cairo_stroke(cr);

   // Draw opposite side of arrow
   double opposite_x = xc - needle_length * sin (angle_rad);
   double opposite_y = yc + needle_length * cos (angle_rad);
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   cairo_move_to (cr, xc, yc);
   cairo_line_to (cr, opposite_x, opposite_y);
   cairo_stroke (cr);
}

/*! draw hour */
static void drawHourPos (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
   char strLat [MAX_SIZE_NAME], strLon [MAX_SIZE_NAME];
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
   cairo_arc (cr, xc, yc, DASHBOARD_RADIUS * 0.8, 0, 2.0 * G_PI);
   cairo_fill (cr);
   CAIRO_SET_SOURCE_RGB_WHITE (cr);
   cairo_arc (cr, xc, yc, DASHBOARD_RADIUS * 0.8 - 2, 0, 2.0 * G_PI);
   cairo_stroke (cr);

   layout = pango_cairo_create_layout(cr);
   pango_layout_set_text(layout, strchr (strDate, ' ') + 1, -1);

   // Set font description
   font_desc = pango_font_description_from_string ("DSEG7 Classic 12");
   pango_layout_set_font_description (layout, font_desc);
   pango_font_description_free (font_desc);

   CAIRO_SET_SOURCE_RGB_GREEN (cr);
   cairo_move_to (cr, xc - 40, yc - 20);
   pango_cairo_update_layout(cr, layout);
   pango_cairo_show_layout(cr, layout);

   g_object_unref(layout);

   // position
   CAIRO_SET_SOURCE_RGB_WHITE (cr);
   cairo_set_font_size (cr, 12);
   
   cairo_move_to (cr, xc - 38, yc + 25);
   snprintf (str, sizeof (str), "%s", latToStr (my_gps_data.lat, par.dispDms, strLat, sizeof (strLat)));
   cairo_show_text (cr, str);
   
   cairo_move_to (cr, xc - 38, yc + 40);
   snprintf (str, sizeof (str), "%s", lonToStr (my_gps_data.lon, par.dispDms, strLon, sizeof (strLon)));
   cairo_show_text (cr, str);
}

/*! Function to draw the text zone */
static void drawTextZone (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
   char str [MAX_SIZE_LINE];
   double nextLat, nextLon;
   int nextWp = (wayPoints.n == 0) ? -1 : 0; // to be revaluated
  
   double xc = width / 2.0;
   double yc = height / 2.0;
   
   CAIRO_SET_SOURCE_RGB_BLACK (cr);
   cairo_arc(cr, xc, yc, DASHBOARD_RADIUS * 0.8, 0, 2.0 * G_PI);
   cairo_fill(cr);
   CAIRO_SET_SOURCE_RGB_WHITE (cr);
   cairo_arc(cr, xc, yc, DASHBOARD_RADIUS * 0.8 - 2, 0, 2.0 * G_PI);
   cairo_stroke (cr);

   cairo_move_to (cr, xc, yc);
   cairo_set_font_size (cr, 12);
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   CAIRO_SET_SOURCE_RGB_WHITE (cr);

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
static void onDashboardWindowDestroy (GtkWidget *widget, gpointer data) {
   if (widgetDashboard.timeoutId != 0) {
      g_source_remove (widgetDashboard.timeoutId);
      widgetDashboard.timeoutId = 0;
   }
}

/*! show Dashboard */
static void dashboard () {
   const int dWidth = 250;
   const int dHeight = 250;
   const int dTimer = 1;
       
   GtkWidget *dashboardWindow = gtk_application_window_new (GTK_APPLICATION (app));
   gtk_window_set_title(GTK_WINDOW(dashboardWindow), "");
   gtk_window_set_default_size (GTK_WINDOW(dashboardWindow), 4 * dWidth, dHeight);
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), dashboardWindow);
   g_signal_connect (dashboardWindow, "destroy", G_CALLBACK (onDashboardWindowDestroy), NULL);

   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_window_set_child(GTK_WINDOW(dashboardWindow), box);

   // Hour Zone
   widgetDashboard.hourPosZone = gtk_drawing_area_new();
   gtk_widget_set_size_request (widgetDashboard.hourPosZone, dWidth, dHeight);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA(widgetDashboard.hourPosZone), drawHourPos, &my_gps_data, NULL);
   gtk_box_append (GTK_BOX(box), widgetDashboard.hourPosZone);

   // Speedometer
   widgetDashboard.speedometer = gtk_drawing_area_new();
   gtk_widget_set_size_request(widgetDashboard.speedometer, dWidth, dHeight);
   gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA (widgetDashboard.speedometer), drawSpeedometer, &my_gps_data, NULL);
   gtk_box_append(GTK_BOX(box), widgetDashboard.speedometer);

   // Compass
   widgetDashboard.compass = gtk_drawing_area_new();
   gtk_widget_set_size_request (widgetDashboard.compass, dWidth, dHeight);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA(widgetDashboard.compass), drawCompass, &my_gps_data, NULL);
   gtk_box_append (GTK_BOX(box), widgetDashboard.compass);

   // text Zone
   widgetDashboard.textZone = gtk_drawing_area_new();
   gtk_widget_set_size_request (widgetDashboard.textZone, dWidth, dHeight);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA(widgetDashboard.textZone), drawTextZone, &my_gps_data, NULL);
   gtk_box_append (GTK_BOX(box), widgetDashboard.textZone);

   // UTC Time
   widgetDashboard.timeoutId = g_timeout_add_seconds (dTimer, updateDashboard, NULL); // Update every second

   gtk_window_present (GTK_WINDOW (dashboardWindow));   
}

/*! reinit NMEA ports */
static void nmeaInit (GtkWidget *widget, gpointer theWindow) {
   char *command = NULL;
   char str [MAX_SIZE_LINE];
   if ((command = malloc (MAX_SIZE_LINE * par.nNmea)) == NULL) {
      fprintf (stderr, "In nmeaInit, Error Malloc: %d\n", MAX_SIZE_LINE);
      return;
   }
   command [0] = '\0';
   for (int i = 0; i < par.nNmea; i++) {
      snprintf (str, MAX_SIZE_LINE, "echo %s | sudo -S chmod 666 %s;", sysAdminPw, par.nmea [i].portName);
      g_strlcat (command, str, MAX_SIZE_LINE * par.nNmea);
   }
   memset (sysAdminPw, 0, MAX_SIZE_NAME); // reset to 0 for security purpose
   if ((theWindow != NULL) && GTK_IS_WINDOW (theWindow))
      gtk_window_destroy (GTK_WINDOW (theWindow));
   g_thread_new ("initNmea", commandRun, command);
}

/*! Callback to set up sysadmin password  */
static void sysAdminPasswordEntryChanged (GtkEditable *editable, gpointer userData) {
   const char *text = gtk_editable_get_text (editable);
   g_strlcpy (sysAdminPw, text, MAX_SIZE_NAME);
}

/*! show NMEA information and possibility to change */
static void nmeaConf () {
   char strNmea [MAX_SIZE_LINE];
   if (par.nNmea <= 0) {
      infoMessage ("No NMEA Port available", GTK_MESSAGE_WARNING);
      return;
   }
   nmeaInfo (strNmea, MAX_SIZE_LINE);
   if (windowsOS)
      infoMessage (strNmea, GTK_MESSAGE_INFO);
   else {
      // possibility to renit ports under Unix
      GtkWidget *nmeaWindow = gtk_application_window_new (app);
      gtk_widget_set_size_request (nmeaWindow, 100, -1);
      gtk_window_set_title (GTK_WINDOW (nmeaWindow), "NMEA ports status");
      GtkWidget *vBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
      gtk_window_set_child (GTK_WINDOW (nmeaWindow), vBox);
   
      GtkWidget *labelInfo = gtk_label_new (strNmea);
      GtkWidget *labelWarning = gtk_label_new (WARNING_NMEA);

      GtkWidget *hBox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
      
      GtkWidget *passwordLabel = gtk_label_new ("SysAdmin Password:");
      gtk_label_set_xalign(GTK_LABEL (passwordLabel), 0);
      GtkWidget *passwordEntry = gtk_entry_new ();
      gtk_entry_set_visibility (GTK_ENTRY (passwordEntry), FALSE);  
      gtk_entry_set_invisible_char (GTK_ENTRY (passwordEntry), '*');
      g_signal_connect (passwordEntry, "changed", G_CALLBACK (sysAdminPasswordEntryChanged), sysAdminPw);
      
      gtk_box_append (GTK_BOX(hBox0), passwordLabel);
      gtk_box_append (GTK_BOX(hBox0), passwordEntry);

      GtkWidget *hBox1 = OKCancelLine (nmeaInit, nmeaWindow); // lauch nmeaInit if OK

      gtk_box_append (GTK_BOX(vBox), labelInfo);
      gtk_box_append (GTK_BOX(vBox), labelWarning);
      gtk_box_append (GTK_BOX(vBox), hBox0);
      gtk_box_append (GTK_BOX(vBox), hBox1);

      gtk_window_present (GTK_WINDOW (nmeaWindow));
   }
}

/* show AIS information */
static void aisDump () {
   char footer [MAX_SIZE_LINE];
   if (false) {
      infoMessage ("No AIS  available", GTK_MESSAGE_WARNING);
      return;
   }
   char *buffer = NULL;
   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      fprintf (stderr, "In aisDump: Malloc %d\n", MAX_SIZE_BUFFER); 
      infoMessage ("Memory allocation issue", GTK_MESSAGE_ERROR);
      return;
   }
   int count = aisToStr (buffer, MAX_SIZE_BUFFER);
   snprintf (footer, sizeof (footer), "Number of AIS points: %d", count);
   displayText (app, buffer, strlen (buffer), "AIS Finder", footer);
   free (buffer);
}

/*! show GPS information */
static void gpsDump () {
   char line [MAX_SIZE_LINE], strLat [MAX_SIZE_NAME], strLon [MAX_SIZE_NAME], str [MAX_SIZE_LINE];

   GtkWidget *gpsWindow = gtk_application_window_new (GTK_APPLICATION (app));
   gtk_window_set_title (GTK_WINDOW(gpsWindow), "GPS Information");
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), gpsWindow);

   GtkWidget *grid = gtk_grid_new();   
   /*if (!my_gps_data.OK) {
      infoMessage ("No GPS position available", GTK_MESSAGE_WARNING);
      return;
   }*/
   gtk_window_set_child (GTK_WINDOW (gpsWindow), (GtkWidget *) grid);

   gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous (GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous (GTK_GRID(grid), FALSE);

   snprintf (line, sizeof (line), "%s", (my_gps_data.OK) ? "YES" : "NO");
   lineReport (grid, 0, "audio-input-microphone-symbolic", "GPS Available", line);
   
   snprintf (line, sizeof (line), "%s %s\n", \
      latToStr (my_gps_data.lat, par.dispDms, strLat, sizeof (strLat)),\
      lonToStr (my_gps_data.lon, par.dispDms, strLon, sizeof (strLon)));
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
   char *command = NULL;
   if ((command = malloc (MAX_SIZE_LINE)) == NULL) {
      fprintf (stderr, "In help, Error Malloc: %d\n", MAX_SIZE_LINE);
      return;
   }   
   snprintf (command, MAX_SIZE_LINE, "%s %s", par.webkit, par.helpFileName);
   g_thread_new ("help", commandRun, command);
}

/*! display info on the program */
static void helpInfo () {
   const char *authors[2] = {PROG_AUTHOR, NULL};
   char str [MAX_SIZE_LINE];
   char strVersion [MAX_SIZE_LINE * 2];
  
   snprintf (strVersion, sizeof (strVersion), "%s\nGTK version: %d.%d.%d\nGlib version: %d.%d.%d\nCairo Version:%s\n \
      ECCODES version from ECMWF: %s\n Curl version: %s\n Shapefil version: %s\n Compilation date: %s\n", 
      PROG_VERSION,
      gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
      GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION,
      CAIRO_VERSION_STRING, 
      ECCODES_VERSION_STR, LIBCURL_VERSION, "1.56", 
      __DATE__);

   GtkWidget *p_about_dialog = gtk_about_dialog_new ();
   gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (p_about_dialog), strVersion);
   gtk_about_dialog_set_program_name  (GTK_ABOUT_DIALOG (p_about_dialog), PROG_NAME);
   gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (p_about_dialog), authors);
   gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (p_about_dialog), PROG_WEB_SITE);
   GdkPixbuf *p_logo = gdk_pixbuf_new_from_file (buildRootName (PROG_LOGO, str, sizeof (str)), NULL);
   GdkTexture *texture_logo = gdk_texture_new_for_pixbuf (p_logo);
   gtk_about_dialog_set_logo (GTK_ABOUT_DIALOG (p_about_dialog), (GdkPaintable *) texture_logo);
   gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG (p_about_dialog), DESCRIPTION);
   gtk_window_set_modal (GTK_WINDOW(p_about_dialog), true);
   gtk_window_present (GTK_WINDOW (p_about_dialog));
}

/*! check if readGrib is terminated (wind) */
static gboolean readGribCheck (gpointer data) {
   char str [MAX_SIZE_STD] = "";
   int localReadGribRet = g_atomic_int_get (&readGribRet); // atomic read
   // printf ("readGribCheck: %d\n", localReadGribRet);
   switch (localReadGribRet) {
   case GRIB_RUNNING: // not terminated
      g_mutex_lock (&warningMutex);
      statusWarningMessage (statusbar, statusbarWarningStr);
      g_mutex_unlock (&warningMutex);
      return TRUE;
   case GRIB_ERROR: // error
      initScenario ();
      snprintf (str, MAX_SIZE_STD, "In readGribCheck (wind)");
      break;
   case GRIB_STOPPED: case GRIB_OK: case GRIB_UNCOMPLETE: // -2 stopped, 1 normal, 2 uncomplete
      theTime = 0.0;
      par.constWindTws = 0.0;
      initDispZone ();
      updatedColors = false;
      titleUpdate ();
      gtk_widget_queue_draw (drawing_area);
      destroySurface ();
      if (localReadGribRet == GRIB_UNCOMPLETE)
         infoMessage ("Grib shorter than expected, but working", GTK_MESSAGE_WARNING);
      break;
   case GRIB_ONLY_DOWNLOAD:
      snprintf (str, MAX_SIZE_STD, "%s Downloaded, open it when necessary", gribRequestData.fileName); 
      break;
   default:
      snprintf (str, MAX_SIZE_STD, "In readGribCheck: Error readGribRet: %d unknown\n", localReadGribRet);
      fprintf (stderr, "%s\n", str);
   }
   if (gribThread != NULL)
      g_thread_unref (gribThread);
   gribThread = NULL;
   waitMessageDestroy ();
   if (str [0] != '\0')
      infoMessage (str, GTK_MESSAGE_WARNING);
   return FALSE;
} 

/*! check if readGrib is terminated  (current) */
static gboolean readCurrentGribCheck (gpointer data) {
   char str [MAX_SIZE_STD] = "";
   int localReadGribRet = g_atomic_int_get (&readGribRet); // atomic read
   gtk_widget_queue_draw (drawing_area);
   switch (localReadGribRet) {
   case GRIB_RUNNING: // not terminated
      g_mutex_lock (&warningMutex);
      statusWarningMessage (statusbar, statusbarWarningStr);
      g_mutex_unlock (&warningMutex);
      return TRUE;
   case GRIB_ERROR:
      snprintf (str, MAX_SIZE_STD, "In readCurrentGribCheck (current)");
      break;
   case GRIB_STOPPED: case GRIB_OK: case GRIB_UNCOMPLETE: // either -2 stopped, 1 normal
      break;
   case GRIB_ONLY_DOWNLOAD:
      snprintf (str, MAX_SIZE_STD, "%s Downloaded, open it when necessary", gribRequestData.fileName); // should never happen yet
      break;
   default:
      snprintf (str, MAX_SIZE_STD, "In, readCurrentGribCheck: Error readGribRetCurrent: %d unknown\n", localReadGribRet);
   }
   if (gribThread != NULL)
      g_thread_unref (gribThread);
   gribThread = NULL;
   waitMessageDestroy ();
   if (str [0] != '\0')
      infoMessage (str, GTK_MESSAGE_WARNING);
   return FALSE;
}
   
/*! launch readGribAll wih Wind or current  parameters */   
static void *readGribLaunch () {
   g_atomic_int_set (&readGribRet, GRIB_RUNNING);
   int ret = g_atomic_int_get (&readGribRet);
   if ((typeFlow == WIND) && (par.gribFileName [0] != '\0'))
      ret = readGribAll (par.gribFileName, &zone, WIND);
   else if ((typeFlow == CURRENT) && (par.currentGribFileName [0] != '\0'))
      ret = readGribAll (par.currentGribFileName, &currentZone, CURRENT);
   g_atomic_int_set (&readGribRet, ret);
   return NULL;
}

/*! getMeteoConsult in a thread */
static void *getMeteoConsult () {
   char errMessage [MAX_SIZE_LINE];
   int nTry = 0;
   int ret = GRIB_RUNNING;
   int delay = (gribRequestData.urlType == WIND) ? METEO_CONSULT_WIND_DELAY : METEO_CONSULT_CURRENT_DELAY;
   
   do {
      if (nTry != 0)
         gribRequestData.hhZ = buildMeteoConsultUrl (gribRequestData.urlType, gribRequestData.index, delay, gribRequestData.url, sizeof (gribRequestData.url));

      g_mutex_lock (&warningMutex);
      snprintf (statusbarWarningStr, sizeof (statusbarWarningStr), 
         "MeteoConsult Download and decoding, Time Run: %d, Nb try: %d",gribRequestData.hhZ, nTry + 1);
      g_mutex_unlock (&warningMutex);

      if (curlGet (gribRequestData.url, gribRequestData.fileName, errMessage, sizeof (errMessage)))
         break; // success
      else {
         fprintf (stderr, "In getMeteoConsult: %s\n", errMessage); 
         statusErrorMessage (statusbar, g_strstrip (errMessage));
         delay += 6; // try another time with 6 hour older run
         nTry += 1;
      }
   } while (nTry < MAX_N_TRY);

   if (nTry >=  MAX_N_TRY) { // FAIL
      ret = GRIB_ERROR;
      g_atomic_int_set (&readGribRet, ret);
      return NULL;
   }
   if (gribRequestData.onlyDownload) {
      g_strlcpy (gribRequestData.fileName, gribRequestData.fileName, sizeof (gribRequestData.fileName));
      ret = GRIB_ONLY_DOWNLOAD;
   }
   else {// SUCCESS
      if (typeFlow == WIND) {
         g_strlcpy (par.gribFileName, gribRequestData.fileName, MAX_SIZE_FILE_NAME);
         ret = readGribAll (par.gribFileName, &zone, WIND); // Global variable
      }
      else {
         g_strlcpy (par.currentGribFileName, gribRequestData.fileName, MAX_SIZE_FILE_NAME);
         ret = readGribAll (par.currentGribFileName, &currentZone, CURRENT); // Global variable
      }
   }
   g_atomic_int_set (&readGribRet, ret);
   return NULL;
}

/*! produce times steps intetval and limit */
static void getTimeSteps (int type, int requestedTimeStep, int *step0, int *step1, int *limit0) {
   switch (type) {
   case ARPEGE_WIND: // allways 1
      *step0 = 1; // be careful with semantic
      *step1 = 1;
      *limit0 = 0; // useless because *step0 == *step1
      break;
   case ECMWF_WIND: // allow max 3 hours durig 144 hours, then 6 hours 
      *step0 = MAX (requestedTimeStep, 3);
      *step1 = MAX (requestedTimeStep, 6);
      *limit0 = 144;
      break;
   case AROME_WIND:
      *step0 = *step1 = MAX (requestedTimeStep, 1);
      *limit0 = 0; // useless if *step0 == *step1
      break;
   case NOAA_WIND:  // allow 1 hour during 120 hours, then 3 hours 
      *step0 = MAX (requestedTimeStep, 1);
      *step1 = MAX (requestedTimeStep, 3);
      *limit0 = 120;
      break;
   default: // should never happen
      *step0 = *step1 = *limit0 = 0;
   }
}

/*! produce current timeStep interval */
static int nextTimeStepInterval (int type, int requestedTimeStep, int currentTimeStep) {
   int step0, step1, limit0;
   getTimeSteps (type, requestedTimeStep, &step0, &step1, &limit0);
   return (currentTimeStep < limit0) ? step0 : step1;
}

/*! Download all NOOA or ECMWF files, concat them gribRequestData.fileName and load it */
static void *getGribWebAll (void *userData) {
   const int sleepBetweenDownload = 100000; //100 ms
   const char *providerID [4] = {"NOAA", "ECMWF", "ARPEGE", "AROME"};
   char directory [MAX_SIZE_DIR_NAME];
   GribRequestData *data = (GribRequestData *) userData;
   time_t now = time (NULL);
   struct tm *pTime = gmtime (&now);
   char fileName [MAX_SIZE_URL], prefix [MAX_SIZE_LINE];
   char strDate [MAX_SIZE_DATE];
   char errMessage [MAX_SIZE_LINE];
   int timeMax = data->timeMax * 24;
   int lastTimeStep = timeMax;   
   int i = 0;
   int ret = GRIB_RUNNING;
   const int maxIArpege = 4;
   const int arpegeStepMin [] = {0, 25, 49, 73};
   const int arpegeStepMax [] = {24, 48, 72, 102};
   const int maxI = MIN (maxIArpege, data->timeMax); // 4 days max. 102 hours possibly ?
   int concatStep0, concatStep1, concatLast, limit0;

   // printf ("In getGribWebAll0 readGribRet = %d\n", readGribRet);
   strftime (strDate, MAX_SIZE_DATE, "%Y-%m-%d", pTime); 
   snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir);
   snprintf (prefix, sizeof (prefix), "%sgrib/inter-", par.workingDir);
   removeAllTmpFilesWithPrefix (prefix); // we suppress the temp files in order to get clean

   if (data->typeWeb == ARPEGE_WIND) { // ARPEGE
      for (i = 0; i < maxI; i += 1) {
         if (g_atomic_int_get (&readGribRet) == GRIB_STOPPED) // stop by user
            return NULL;
         data->hhZ = buildGribUrl (data->typeWeb, data->latMax, data->lonLeft, data->latMin, data->lonRight, 
            arpegeStepMin [i], arpegeStepMax [i], data->url, sizeof (data->url));

         snprintf (fileName, sizeof (fileName), "%s%03d.tmp", prefix, i);
         printf ("URL: %s\nFileName: %s\n\n", data->url, fileName);

         g_mutex_lock (&warningMutex);
         snprintf (statusbarWarningStr, sizeof (statusbarWarningStr), 
            "Time Run: %02dZ Time Step: %d Time Stamp: %3d/%d %3.0lf%%", data->hhZ, data->timeStep, arpegeStepMin [i], timeMax, 100.0 * i / (maxI * 1.1));

         g_mutex_unlock (&warningMutex);

         if (! curlGet (data->url, fileName, errMessage, sizeof (errMessage))) {
            statusErrorMessage (statusbar, g_strstrip (errMessage));
            fprintf (stderr, "In getGribWebAll, Error: No file downloaded, %s\n", errMessage);
            g_atomic_int_set (&readGribRet, GRIB_ERROR);
            return NULL;
         }
         g_usleep (sleepBetweenDownload) ;
         compact (directory, fileName,\
            "10u/10v/prmsl", data->lonLeft, data->lonRight, data->latMin, data->latMax, fileName);
      }
      lastTimeStep = arpegeStepMax [maxI - 1];
      data->timeStep = 3;
   }
   else { // NOAA, ECMWF, AROME
      for (i = 0; i <= timeMax; i += nextTimeStepInterval (data->typeWeb, data->timeStep, i)) {
         if (g_atomic_int_get (&readGribRet) == GRIB_STOPPED) // stop by user
            return NULL;
         // printf ("In getGribWebAll i = %d readGribRet = %d\n", i, readGribRet);
         data->hhZ = buildGribUrl (data->typeWeb, data->latMax, data->lonLeft, data->latMin, data->lonRight, i, -1, data->url, sizeof (data->url));
         snprintf (fileName, sizeof (fileName), "%s%03d.tmp", prefix, i);
         printf ("URL: %s\nFileName: %s\n\n", data->url, fileName);

         g_mutex_lock (&warningMutex);
         snprintf (statusbarWarningStr, sizeof (statusbarWarningStr), 
            "Time Run: %02dZ Time Step: %d Time Stamp: %3d/%d %3.0lf%%", data->hhZ, data->timeStep, i, timeMax, 100.0 * i / (timeMax * 1.1));
         g_mutex_unlock (&warningMutex);
         //printf ("timeStep/timeMax = %d/%d\n", i, timeMax); 
      
         if (! curlGet (data->url, fileName, errMessage, sizeof (errMessage))) {
            fprintf (stderr, "In getGribWebAll: Grib could be uncomplete: %s\n", errMessage); 
            statusErrorMessage (statusbar, g_strstrip (errMessage));
            lastTimeStep = i - data->timeStep;
            break;
         }
         g_usleep (sleepBetweenDownload) ;

         if (data->typeWeb != NOAA_WIND)
            compact (directory, fileName,\
            "10u/10v/gust/msl/prmsl/prate", data->lonLeft, data->lonRight, data->latMin, data->latMax, fileName);
      }
   }
   g_usleep (sleepBetweenDownload);

   g_mutex_lock (&warningMutex);
   snprintf (statusbarWarningStr, sizeof (statusbarWarningStr), "Concat elementary files");
   g_mutex_unlock (&warningMutex);
   
   if (lastTimeStep <= 0) {
      fprintf (stderr, "In getGribWebAll, Error: No file downloaded\n");
      g_atomic_int_set (&readGribRet, GRIB_ERROR);
      return NULL;
   }
   
   snprintf (gribRequestData.fileName, sizeof (gribRequestData.fileName), \
      "%sgrib/%s-%s-%02dZ-%02d-%03d.grb", par.workingDir, providerID [data->typeWeb], strDate, data->hhZ, data->timeStep, lastTimeStep);

   // printf ("In getGribWebAll readGribRet end = %d\n", readGribRet);
   if (data->typeWeb == ARPEGE_WIND) {
      concatStep0 = data->timeStep = 1; // be careful with semantic
      concatStep1 = data->timeStep = 1;
      concatLast = maxI - 1;
      limit0 = 0; // useless if concatStep0 == concatStep1
   }
   else {
      getTimeSteps (data->typeWeb, data->timeStep, &concatStep0, &concatStep1, &limit0);
      concatLast = lastTimeStep;
   }

   if (concat (prefix, ".tmp", limit0, concatStep0, concatStep1, concatLast, gribRequestData.fileName)) {
      printf ("gribRequestData.fileName: %s\n", gribRequestData.fileName);
      if (gribRequestData.onlyDownload) {
         ret = GRIB_ONLY_DOWNLOAD;
      }
      else {
         g_strlcpy (par.gribFileName, gribRequestData.fileName, MAX_SIZE_FILE_NAME);
         g_atomic_int_set (&readGribRet, GRIB_RUNNING);
         ret = readGribAll (par.gribFileName, &zone, WIND);
         if (ret && (lastTimeStep < timeMax))
            ret = GRIB_UNCOMPLETE;
      }
   }
   else
      ret = GRIB_ERROR;
      
   g_mutex_lock (&warningMutex);
   snprintf (statusbarWarningStr, sizeof (statusbarWarningStr), "Download Done");
   g_mutex_unlock (&warningMutex);
   
   g_atomic_int_set (&readGribRet, ret);
   // printf ("In getGribWebAll readGribRet final = %d\n", readGribRet);
   
   return NULL;
}

/*! Manage response to Grib Request dialog box */
static void launchGribRequest (GtkWidget *widget, gpointer theWindow, bool onlyDownload) {
   printf ("onlyDownload: %d\n", onlyDownload);
   gribRequestData.onlyDownload = onlyDownload;
   g_atomic_int_set (&readGribRet, GRIB_RUNNING);
   if (gribRequestData.typeWeb != MAIL) { //Web
      waitMessage ("Grib Download and decoding",  "Watch status bar...");
      gribThread = g_thread_new ("readGribThread", getGribWebAll, &gribRequestData);
      gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readGribCheck, NULL);
   }
   else { // mail
      waitMessage ("Waiting for grib mail response", "Be patient\nWatch status bar...");
      gribThread = g_thread_new ("mailGribRequest", mailGribRequest, NULL);
      gribMailTimeout = g_timeout_add (MAIL_GRIB_TIME_OUT, mailGribCheck, NULL);
   }
   if ((theWindow != NULL) && GTK_IS_WINDOW (theWindow))
      gtk_window_destroy (GTK_WINDOW (theWindow));
}

/*! Manage response to Grib Request dialog box after OK buttton cjhoice */
static void onOkButtonGribRequestClicked (GtkWidget *widget, gpointer theWindow) {
   launchGribRequest (widget, theWindow, false);
}

/*! Manage response to Grib Request dialog box after onlyDownload button choice */
static void onOnlyDownloadButtonGribRequestClicked (GtkWidget *widget, gpointer theWindow) {
   launchGribRequest (widget, theWindow, true);
}

/*! return warning message based on timeStep and resolution */
static char *warningMessage (int service, int mailService, char *warning, size_t maxLen) {
   if ((service > N_WEB_SERVICES) || ((service == MAIL) && (mailService >= N_MAIL_SERVICES))) {
      snprintf (warning, maxLen, "Service Unknown");
      return warning;
   }
   if (service == MAIL)
      snprintf (warning, maxLen, "%s", mailServiceTab [mailService].warning);
   else // WEB with NOAA, ECMWF, ARPEGE, AROME..
      snprintf (warning, maxLen, "%s", serviceTab [service].warning);
   return warning;
}

/*! return number of values x n message wich give an evaluation
   of grib file size in bytes */
static int evalSize (int nShortName) {
   if ((par.gribResolution == 0  || (par.gribTimeStep == 0))) {
      fprintf (stderr, "In evalSize: par.gribResolution and par.gribTimeStep should be strictly positive\n");
      return 0;
   }
   int newLonRight = ((gribRequestData.lonLeft > 0) && (gribRequestData.lonRight < 0)) ?
                      gribRequestData.lonRight + 360: 
                      gribRequestData.lonRight;

   int nValue = ((fabs ((double) gribRequestData.latMax - (double)gribRequestData.latMin) / par.gribResolution) + 1) * \
      ((fabs ((double) gribRequestData.lonLeft-(double)newLonRight) / par.gribResolution) + 1);
   int nMessage = nShortName * (1 + (par.gribTimeMax / par.gribTimeStep));
   //printf ("nShortName: %d, par.gribTimeMax: %d, par.gribTimeStep: %d\n", nShortName, par.gribTimeMax, par.gribTimeStep);
   //printf ("in evalSize: nValue: %d, nMessage: %d\n", nValue, nMessage);
   return nMessage * nValue;
}

/*! updtate text fields in grib dialog box */
static void updateTextField (GribRequestData *data) {
   char str [MAX_SIZE_TEXT] = "";
   if (data->typeWeb != MAIL) { // NOAA or ECMWF or ARPEGE or AROME download
      data->hhZ = buildGribUrl (data->typeWeb, data->latMax, data->lonLeft, data->latMin, data->lonRight, 0, -1, data->url, MAX_SIZE_URL);
      formatThousandSep (str, sizeof (str), evalSize (howManyShortnames (data->typeWeb, data->mailService)));
      gtk_text_buffer_set_text (data->urlBuffer, data->url, -1);
      gtk_label_set_text (GTK_LABEL (data->sizeEval), str);
      snprintf (str, sizeof (str), "%02dZ", data->hhZ);
   }
   else {
      buildGribMail (data->mailService, data->latMin, data->lonLeft, data->latMax, data->lonRight, 
         data->object, data->body, MAX_SIZE_MESSAGE);
      formatThousandSep (str, sizeof (str), evalSize (howManyShortnames (data->typeWeb, data->mailService)));
      snprintf (data->url, sizeof (data->url), "Mailto: %s\nObject: %s\nbody: %s\n",
         mailServiceTab [data->mailService].address, data->object, data->body); 
      gtk_text_buffer_set_text (data->urlBuffer, data->url, -1);
      gtk_label_set_text (GTK_LABEL (data->sizeEval), str);
      snprintf (str, sizeof (str), "%s", "");
   }
   gtk_label_set_text (GTK_LABEL (data->hhZBuffer), str);
}

/*! update spin values for grib request box */
static void updateGribRequestSpins (GtkSpinButton *spin_button, gpointer userData) {
   GribRequestData *data = (GribRequestData *) userData;
   data->latMin = gtk_spin_button_get_value_as_int (data->latMinSpin);
   data->lonLeft = gtk_spin_button_get_value_as_int (data->lonLeftSpin);
   data->latMax = gtk_spin_button_get_value_as_int (data->latMaxSpin);
   data->lonRight = gtk_spin_button_get_value_as_int (data->lonRightSpin);
   data->timeMax = gtk_spin_button_get_value_as_int (data->timeMaxSpin);
   par.gribTimeStep = data->timeStep;
   par.gribTimeMax = 24 * data->timeMax;
   updateTextField (data);
}

/*! CallBack for service choice NOAA, ECMWF, MAIL */
static void cbDropDownServ (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   char strWarning [MAX_SIZE_LINE];
   GribRequestData *data = (GribRequestData *) userData;
   data->typeWeb = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   par.gribTimeMax = maxTimeRange (data->typeWeb, data->mailService);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (data->timeMaxSpin), par.gribTimeMax / 24);

   data->timeStep = DEFAULT_GRIB_TIME_STEP;

   updateTextField (data);
   gtk_drop_down_set_selected (GTK_DROP_DOWN (gribRequestData.dropDownTimeStep), DEFAULT_GRIB_TIME_STEP_INDEX);

   if (data->typeWeb != MAIL)
      gtk_drop_down_set_selected ((GtkDropDown *) data->dropDownMail, NOT_MAIL);
   else
      if (data->mailService == NOT_MAIL)
         gtk_drop_down_set_selected ((GtkDropDown *) data->dropDownMail, SAILDOCS_GFS);
   warningMessage (data->typeWeb, data->mailService, strWarning, sizeof (strWarning));
   gtk_label_set_text (GTK_LABEL (data->warning), strWarning);
   gtk_widget_queue_draw (drawing_area); // Draw all
}

/*! Callback resolution */
static void cbResolutionChanged (GtkSpinButton *spin_button, gpointer userData) {
   GribRequestData *data = (GribRequestData *) userData;
   par.gribResolution = gtk_spin_button_get_value (spin_button);
   updateTextField (data);
}

/*! Callback change mail provider */
static void cbMailDropDown (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   char strWarning [MAX_SIZE_LINE];
   GribRequestData *data = (GribRequestData *) userData;
   data->mailService = gtk_drop_down_get_selected (GTK_DROP_DOWN(dropDown));
   par.gribTimeMax = maxTimeRange (data->typeWeb, data->mailService);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (data->timeMaxSpin), par.gribTimeMax / 24);

   updateTextField (data);
   if (data->mailService == NOT_MAIL) 
      gtk_drop_down_set_selected ((GtkDropDown *) data->dropDownServ, NOAA_WIND);
   else
      gtk_drop_down_set_selected ((GtkDropDown *) data->dropDownServ, MAIL);
   warningMessage (data->typeWeb, data->mailService, strWarning, sizeof (strWarning));
   gtk_label_set_text (GTK_LABEL (data->warning), strWarning);
}

/*! CallBack for timeStep change  */
static void cbDropDownTimeStep (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   GribRequestData *data = (GribRequestData *) userData;
   const int intArrayTimeSteps [] = {1, 3, 6, 12, 24};
   int index = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   // printf ("index = %d\n", index);
   par.gribTimeStep = intArrayTimeSteps [MIN (index, 4)];
   data->timeStep = par.gribTimeStep;
   par.gribTimeMax = maxTimeRange (data->typeWeb, data->mailService);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON(data->timeMaxSpin), par.gribTimeMax / 24);
   updateTextField (data);
}

/*! Callback to set up mail password  */
static void mailPasswordEntryChanged (GtkEditable *editable, gpointer userData) {
   const char *text = gtk_editable_get_text (editable);
   g_strlcpy (par.mailPw, text, MAX_SIZE_NAME);
}

/*! Dialog box to Manage NOAA URL and launch downloading */
static void gribRequestBox (GribRequestData *data) {
   char str [MAX_SIZE_LINE] = "",  str1 [MAX_SIZE_LINE] = "";
   char strWarning [MAX_SIZE_LINE];
   GtkWidget *label;

   data->timeStep = par.gribTimeStep;
   par.gribTimeMax = maxTimeRange (data->typeWeb, data->mailService);
   data->timeMax = (par.gribTimeMax / 24);

   GtkWidget *gribWebWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (gribWebWindow), "Grib Request");
   gtk_widget_set_size_request (gribWebWindow, 800, -1);
   gtk_window_set_modal (GTK_WINDOW (gribWebWindow), TRUE);
   gtk_window_set_transient_for (GTK_WINDOW (gribWebWindow), GTK_WINDOW (window));

   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (gribWebWindow), (GtkWidget *) vBox);

   GtkWidget *grid = gtk_grid_new();

   data->hhZ = buildGribUrl (data->typeWeb, data->latMax, data->lonLeft, data->latMin, data->lonRight, 0, -1, data->url, sizeof (data->url));

   gtk_grid_set_column_spacing(GTK_GRID (grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID (grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID (grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID (grid), FALSE);

   // Create combo box for NOAA WIND Selection
   label = gtk_label_new ("Web or Mail service");
   gtk_grid_attach (GTK_GRID(grid), label, 0, 0, 1, 1);
   const char *arrayServ [] = {"NOAA", "ECMWF", "ARPEGE", "AROME", "MAIL", NULL};
   data->dropDownServ = gtk_drop_down_new_from_strings (arrayServ);
   gtk_drop_down_set_selected ((GtkDropDown *) data->dropDownServ, data->typeWeb);
   gtk_grid_attach (GTK_GRID(grid), data->dropDownServ, 1, 0, 1, 1);
   g_signal_connect (data->dropDownServ, "notify::selected", G_CALLBACK (cbDropDownServ), data);

   // Create GlistStore to store strings 
   GtkStringList *string_list = gtk_string_list_new (NULL);
   for (int i = 0;  i < N_MAIL_SERVICES; i++) {
      snprintf (str, sizeof (str), "%s", mailServiceTab [i].libelle);
      gtk_string_list_append (string_list, str);
   }
   data->dropDownMail = gtk_drop_down_new (G_LIST_MODEL(string_list), NULL); // Global variable

   gtk_drop_down_set_selected ((GtkDropDown *) data->dropDownMail, data->mailService);
   gtk_grid_attach (GTK_GRID(grid), data->dropDownMail, 2, 0, 1, 1);
   g_signal_connect (data->dropDownMail, "notify::selected", G_CALLBACK (cbMailDropDown), data);

   // firts lines : latMin, lonLeft, latMax, lonRight
   label = gtk_label_new ("Bottom/max Lat");
   gtk_label_set_xalign(GTK_LABEL (label), 0.0);
   data->latMinSpin = GTK_SPIN_BUTTON (gtk_spin_button_new_with_range (-90, 90, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->latMinSpin), data->latMin);
   gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(data->latMinSpin), 1, 1, 1, 1);

   label = gtk_label_new ("Top Lat");
   gtk_label_set_xalign(GTK_LABEL (label), 0.0);
   data->latMaxSpin = GTK_SPIN_BUTTON (gtk_spin_button_new_with_range (-90, 90, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON (data->latMaxSpin), data->latMax);
   gtk_grid_attach(GTK_GRID(grid), label, 2, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET (data->latMaxSpin), 3, 1, 1, 1);

   label = gtk_label_new ("Left Lon");
   gtk_label_set_xalign(GTK_LABEL (label), 0.0);
   data->lonLeftSpin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range (-180, 360, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON (data->lonLeftSpin), data->lonLeft);
   gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET (data->lonLeftSpin), 1, 2, 1, 1);

   label = gtk_label_new ("Right Lon");
   gtk_label_set_xalign(GTK_LABEL (label), 0.0);
   data->lonRightSpin = GTK_SPIN_BUTTON (gtk_spin_button_new_with_range (-180, 360, 1));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->lonRightSpin), data->lonRight);
   gtk_grid_attach(GTK_GRID(grid), label, 2, 2, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(data->lonRightSpin), 3, 2, 1, 1);

   // Create Resolution
   GtkWidget *label_resolution = gtk_label_new ("Resolution");
   GtkWidget *spin_button_resolution = gtk_spin_button_new_with_range  (0.25, 0.5, 0.25);
   gtk_spin_button_set_digits (GTK_SPIN_BUTTON(spin_button_resolution), 2); // 2 digits after . e.g.: 0.25
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_button_resolution), par.gribResolution);
   g_signal_connect (spin_button_resolution, "value-changed", G_CALLBACK (cbResolutionChanged), data);
   gtk_label_set_xalign(GTK_LABEL (label_resolution), 0);
   gtk_grid_attach(GTK_GRID (grid), label_resolution,       0, 3, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), spin_button_resolution, 1, 3, 1, 1);
   label_resolution = gtk_label_new ("Apply only for NOAA and Mail");
   gtk_grid_attach(GTK_GRID (grid), label_resolution,       2, 3, 1, 1);

   // Create combo box Time Step
   label = gtk_label_new ("Time Step");
   const char *arrayTimeSteps [] = {"1", "3", "6", "12", "24", NULL};
   gribRequestData.dropDownTimeStep = gtk_drop_down_new_from_strings (arrayTimeSteps);
   int index;
   char strGribTimeStep [3];
   snprintf (strGribTimeStep, 3, "%d", par.gribTimeStep);
   for (index = 0; arrayTimeSteps [index] != NULL; index++) { // find index of par.gribTimeStep
      if (strcmp (strGribTimeStep, arrayTimeSteps [index]) == 0) break;
   }

   gtk_drop_down_set_selected ((GtkDropDown *) gribRequestData.dropDownTimeStep, MIN (index, 4));
   gtk_label_set_xalign(GTK_LABEL (label), 0);
   gtk_grid_attach(GTK_GRID (grid), label,             0, 4, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), gribRequestData.dropDownTimeStep, 1, 4, 1, 1);
   g_signal_connect (gribRequestData.dropDownTimeStep, "notify::selected", G_CALLBACK (cbDropDownTimeStep), data);

   label = gtk_label_new ("Forecast Time in Days");
   gtk_label_set_xalign(GTK_LABEL (label), 0.0);
   data->timeMaxSpin = GTK_SPIN_BUTTON (gtk_spin_button_new_with_range (1, MAX_N_DAYS_WEATHER, 1));
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (data->timeMaxSpin), par.gribTimeMax / 24);
   gtk_grid_attach(GTK_GRID (grid), label, 2, 4, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), GTK_WIDGET (data->timeMaxSpin), 3, 4, 1, 1);

   label = gtk_label_new ("Number of values");
   gtk_label_set_xalign (GTK_LABEL (label), 0.0);
   formatThousandSep (str1, sizeof (str1), evalSize (howManyShortnames (data->typeWeb, data->mailService)));
   data -> sizeEval = gtk_label_new (str1);
   gtk_widget_set_halign (data->sizeEval, GTK_ALIGN_START);
   gtk_grid_attach(GTK_GRID (grid), label         , 0, 5, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), data->sizeEval, 1, 5, 1, 1);

   label = gtk_label_new ("Time run");
   gtk_label_set_xalign(GTK_LABEL (label), 0.0);
   data->hhZBuffer = gtk_label_new ("");
   gtk_widget_set_halign (data->hhZBuffer, GTK_ALIGN_START);
   gtk_grid_attach(GTK_GRID (grid), label          , 2, 5, 1, 1);
   gtk_grid_attach(GTK_GRID (grid), data->hhZBuffer, 3, 5, 1, 1);

   GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach(GTK_GRID (grid), separator, 0, 6, 3, 1);
   
   if (par.mailPw [0] == '\0') { // password needed if does no exist 
      label = gtk_label_new ("Mail Password");
      gtk_label_set_xalign(GTK_LABEL (label), 0);
      GtkWidget *password_entry = gtk_entry_new ();
      gtk_entry_set_visibility (GTK_ENTRY (password_entry), FALSE);  
      gtk_entry_set_invisible_char (GTK_ENTRY (password_entry), '*');
      g_signal_connect (password_entry, "changed", G_CALLBACK (mailPasswordEntryChanged), par.mailPw);
      gtk_grid_attach (GTK_GRID (grid), label,          0, 7, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), password_entry, 1, 7, 1, 1);
   }

   // hBox definition with 2 buttons; OK, OnlyDownload, Cancel
   GtkWidget *hBox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *OKbutton = gtk_button_new_with_label ("OK");
   GtkWidget *onlyDownloadButton = gtk_button_new_with_label ("Only DownLoad");
   GtkWidget *cancelButton = gtk_button_new_with_label ("Cancel");
   gtk_box_append (GTK_BOX(hBox), OKbutton);
   gtk_box_append (GTK_BOX(hBox), onlyDownloadButton);
   gtk_box_append (GTK_BOX(hBox), cancelButton);
   g_signal_connect (OKbutton, "clicked", G_CALLBACK (onOkButtonGribRequestClicked), gribWebWindow);
   g_signal_connect (cancelButton, "clicked", G_CALLBACK (onCancelButtonClicked), gribWebWindow);
   g_signal_connect (onlyDownloadButton, "clicked", G_CALLBACK (onOnlyDownloadButtonGribRequestClicked), gribWebWindow);

   // Multi-line zone to display URL
   GtkWidget *textView = gtk_text_view_new ();
   gtk_text_view_set_editable(GTK_TEXT_VIEW (textView), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW (textView), GTK_WRAP_WORD_CHAR);
   data->urlBuffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textView));
   gtk_text_buffer_set_text (data->urlBuffer, data->url, -1);
   updateTextField (data);
   
   warningMessage (data->typeWeb, data->mailService, strWarning, sizeof (strWarning));
   data->warning =  gtk_label_new (strWarning);
   gtk_label_set_xalign (GTK_LABEL (data->warning), 0.0);     // Align to the left

   PangoAttrList *attrs = pango_attr_list_new();
   PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
   pango_attr_list_insert(attrs, attr);
   attr = pango_attr_foreground_new (65535, 0, 0);      // red
   pango_attr_list_insert(attrs, attr);
   gtk_label_set_attributes (GTK_LABEL(data->warning), attrs);

   // spin button connection
   g_signal_connect(data->latMinSpin, "value-changed", G_CALLBACK (updateGribRequestSpins), data);
   g_signal_connect(data->lonLeftSpin, "value-changed", G_CALLBACK (updateGribRequestSpins), data);
   g_signal_connect(data->latMaxSpin, "value-changed", G_CALLBACK (updateGribRequestSpins), data);
   g_signal_connect(data->lonRightSpin, "value-changed", G_CALLBACK (updateGribRequestSpins), data);
   g_signal_connect(data->timeMaxSpin, "value-changed", G_CALLBACK (updateGribRequestSpins), data);

   gtk_box_append (GTK_BOX (vBox), grid);
   gtk_box_append (GTK_BOX (vBox), textView);
   gtk_box_append (GTK_BOX (vBox), data->warning);
   gtk_box_append (GTK_BOX (vBox), hBox);

   gtk_window_present (GTK_WINDOW (gribWebWindow));
}

/*! Dialog box to Manage NOAA URL and launch downloading */
static void gribWeb (GSimpleAction *action, GVariant *parameter, gpointer *userData) {
   int type = GPOINTER_TO_INT (userData); // NOAA or ECMWF or MAIL or MAIL with current
   if (type == MAIL_SAILDOCS_CURRENT)  {  // initiam value config
      gribRequestData.typeWeb = MAIL;
      gribRequestData.mailService = SAILDOCS_CURR;
   }
   else {
      gribRequestData.typeWeb = type;
      gribRequestData.mailService = NOT_MAIL;
   }
   gribRequestData.latMin = (int) zone.latMin; 
   gribRequestData.latMax = (int) zone.latMax;
   gribRequestData.lonLeft = (int) zone.lonLeft;
   gribRequestData.lonRight = (int) zone.lonRight;
   gribRequestBox (&gribRequestData);
}

/*! callback for openGrib */
static void cbOpenGrib (GObject* source_object, GAsyncResult* res, gpointer data) {
   GFile* file = gtk_file_dialog_open_finish ((GtkFileDialog *) source_object, res, NULL);
   typeFlow = GPOINTER_TO_INT (data);
   if (file == NULL)
      return;
   char *fileName = g_file_get_path(file);
   if (fileName == NULL)
      return;
   snprintf (statusbarWarningStr, sizeof (statusbarWarningStr), "Grib decoding: %s", fileName);
   if (typeFlow == WIND) { // wind
      g_strlcpy (par.gribFileName, fileName, MAX_SIZE_FILE_NAME);
      waitMessage ("Grib File decoding", "Be patient\nWatch status bar...");
      gribThread = g_thread_new ("readGribThread", readGribLaunch, NULL);
      gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readGribCheck, NULL);
   }
   else {
      g_strlcpy (par.currentGribFileName, fileName, MAX_SIZE_FILE_NAME);
      waitMessage ("Current Grib File decoding", "Be patient\nWatch status bar...");
      gribThread = g_thread_new ("readCurrentGribThread", readGribLaunch, NULL);
      gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readCurrentGribCheck, NULL);
   }
   g_free (fileName);
}

/*! Select grib file and read grib file, fill gribData */
static void openGrib (GSimpleAction *action, GVariant *parameter, gpointer data) {
   int iFlow = GPOINTER_TO_INT (data);
   memset (&gribRequestData, 0, sizeof(GribRequestData));
   GtkFileDialog* fileDialog;
   if (iFlow == WIND)
      fileDialog = selectFile ("Open Grib", "grib", "Grib Files", "*.gr*", ".*gr*", NULL);
   else
      fileDialog = selectFile ("Open Current Grib", "currentgrib", "Current GribFiles", "*.gr*", "*.gr", NULL);
   gtk_file_dialog_open (fileDialog, GTK_WINDOW (window), NULL, cbOpenGrib, data);
}

/*! callback for openPolar */
static void cbOpenPolar (GObject* source_object, GAsyncResult* res, gpointer data) {
   char sailPolFileName [MAX_SIZE_NAME] = "";
   char errMessage [MAX_SIZE_TEXT] = "";
   GFile* file = gtk_file_dialog_open_finish ((GtkFileDialog *) source_object, res, NULL);
   if (file != NULL) {
      char *fileName = g_file_get_path(file);
      if (fileName != NULL) {
         if (strstr (fileName, "polwave.csv") == NULL) {
            if (readPolar (true, fileName, &polMat, errMessage, sizeof (errMessage))) {
               g_strlcpy (par.polarFileName, fileName, sizeof (par.polarFileName));
               printf ("Polar loaded   : %s\n", fileName);
               polarType = WIND_POLAR;
               newFileNameSuffix (par.polarFileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
               if (readPolar (false, sailPolFileName, &sailPolMat, errMessage, sizeof (errMessage)))
                  printf ("Sail Pol.loaded: %s\n", sailPolFileName);
               else
                  fprintf (stderr, "In initScenario, Sail pol. not loaded: %s : %s\n", sailPolFileName, errMessage);
               polarDraw ();
            }
            else {
               fprintf (stderr, "In cbOpenPolar, Error loading polar file: %s: %s\n", fileName, errMessage);
               infoMessage (errMessage, GTK_MESSAGE_ERROR);
            } 
         }
         else { // wave polar
            if (readPolar (true, fileName, &wavePolMat, errMessage, sizeof (errMessage))) {
               g_strlcpy (par.wavePolFileName, fileName, sizeof (par.wavePolFileName));
               printf ("Wave Pol.loaded: %s\n", fileName);
               polarType = WAVE_POLAR;
               polarDraw ();
            } 
            else {
               fprintf (stderr, "In cbOpenPolar, Error loading wave polar file: %s: %s\n", fileName, errMessage);
               infoMessage (errMessage, GTK_MESSAGE_ERROR);
            } 
         }
      }
      g_free(fileName);
   }
}

/*! Select polar file and read polar file, fill polMat */
static void openPolar () {
   GtkFileDialog* fileDialog = selectFile ("Open Polar", "pol", "Polar Files", "*.csv*", "*.pol*", NULL);
   gtk_file_dialog_open (fileDialog, GTK_WINDOW (window), NULL, cbOpenPolar, NULL);
}

/*! callback for openPolar */
static void cbSaveScenario (GObject* source_object, GAsyncResult* res, gpointer data) {
   GFile* file = gtk_file_dialog_save_finish ((GtkFileDialog *) source_object, res, NULL);
   if (file != NULL) {
      char *fileName = g_file_get_path(file);
      if (fileName != NULL) {
         printf ("File: %s\n", fileName);
         printf ("Mail Pw Exist: %d\n", par.storeMailPw);
         writeParam (fileName, false, par.storeMailPw);
         g_free(fileName);
      }
   }
}

/*! Select polar file and read polar file, fill polMat */
static void saveScenario () {
   GtkFileDialog *fileDialog = selectFile ("Save as", "par", "Parameter Files", "*.par", "*.par", parameterFileName);
   gtk_file_dialog_save (fileDialog, GTK_WINDOW (window), NULL, cbSaveScenario, NULL);
}

/*! Make initialization following parameter file load */
static void initScenario () {
   char str [MAX_SIZE_LINE];
   char errMessage [MAX_SIZE_TEXT] = "";
   char sailPolFileName [MAX_SIZE_NAME] = "";
   int poiIndex = -1;
   double unusedTime;
   int ret = 0;

   // initWayPoints ();
   freeHistoryRoute();

   if (par.gribFileName [0] != '\0') {
      ret = readGribAll (par.gribFileName, &zone, WIND);
      if (ret == 0) {
         fprintf (stderr, "In initScenario: Unable to read grib file: %s\n ", par.gribFileName);
      }
      else {
         printf ("Grib loaded    : %s\n", par.gribFileName);
         printf ("Grib DateTime0 : %s\n",\
            gribDateTimeToStr (zone.dataDate [0], zone.dataTime [0], str, sizeof (str)));
         theTime = zone.timeStamp [0];
         updatedColors = false; 
         initDispZone ();
      }
   }

   if (par.currentGribFileName [0] != '\0') {
      ret = readGribAll (par.currentGribFileName, &currentZone, CURRENT);
      printf ("Cur grib loaded: %s\n", par.currentGribFileName);
      printf ("Grib DateTime0 : %s\n", \
         gribDateTimeToStr (currentZone.dataDate [0], currentZone.dataTime [0], str, sizeof (str)));
   }
   if (readPolar (true, par.polarFileName, &polMat, errMessage, sizeof (errMessage))) {
      printf ("Polar loaded   : %s\n", par.polarFileName);
      newFileNameSuffix (par.polarFileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
      if (readPolar (false, sailPolFileName, &sailPolMat, errMessage, sizeof (errMessage)))
         printf ("Sail Pol.loaded: %s\n", sailPolFileName);
      else
         fprintf (stderr, "In initScenario, Sail pol. not loaded: %s : %s\n", sailPolFileName, errMessage);
   }
   else
      fprintf (stderr, "In initScenario, Error loading polar file: %s : %s\n", par.polarFileName, errMessage);
      
   if (readPolar (true, par.wavePolFileName, &wavePolMat, errMessage, sizeof (errMessage)))
      printf ("Wave Pol.loaded: %s\n", par.wavePolFileName);
   else
      fprintf (stderr, "In initScenario, Error loading wave polar file: %s : %s\n", par.wavePolFileName, errMessage);
   
   nPoi = 0;
   if (par.poiFileName [0] != '\0')
      nPoi += readPoi (par.poiFileName);
   if (par.portFileName [0] != '\0')
      nPoi += readPoi (par.portFileName);
   
   if ((poiIndex = findPoiByName (par.pOrName, &par.pOr.lat, &par.pOr.lon)) != -1)
      g_strlcpy (par.pOrName, tPoi [poiIndex].name, sizeof (par.pOrName));

   else par.pOrName [0] = '\0';
   if ((poiIndex = findPoiByName (par.pDestName, &par.pDest.lat, &par.pDest.lon)) != -1)
      g_strlcpy (par.pDestName, tPoi [poiIndex].name, sizeof (par.pDestName));
   else par.pDestName [0] = '\0';
   
   if (par.pOr.id != -1) // not initialized
      findLastTracePoint (par.traceFileName, &par.pOr.lat, &par.pOr.lon, &unusedTime);
   
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   initStart (&startInfo);
   stopChildThread ();

   wayPoints.t [wayPoints.n].lat = par.pDest.lat;
   wayPoints.t [wayPoints.n].lon = par.pDest.lon;
}

/*! callback after edit scenario */
static void cbScenarioEdit () {
   char directory [MAX_SIZE_DIR_NAME];
   if ((windowEditor != NULL) && GTK_IS_WINDOW (windowEditor))
      gtk_window_destroy (GTK_WINDOW (windowEditor));
   readParam (parameterFileName);
   if (par.mostRecentGrib) {                     // most recent grib will replace existing grib
      snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir); 
      mostRecentFile (directory, ".gr", par.gribFileName, sizeof (par.gribFileName));
   }
   initScenario ();
   destroySurface ();
   gtk_widget_queue_draw (drawing_area);
}

/*! Edit parameter file then load the file  */
static void editScenario () {
   const char *fileContents [] = {parameterFileName};
   if (! myEditor (app, fileContents, 1, "Parameters Edition", cbScenarioEdit))
      infoMessage ("Impossible to open parameter File", GTK_MESSAGE_ERROR);
}

/* call back for openScenario */
static void cbOpenScenario (GObject* source_object, GAsyncResult* res, gpointer data) {
   GFile* file = gtk_file_dialog_open_finish ((GtkFileDialog *) source_object, res, NULL);
   if (file != NULL) {
      char *fileName = g_file_get_path(file);
      if (fileName != NULL) {
         g_strlcpy (parameterFileName, fileName, MAX_SIZE_FILE_NAME);
         readParam (fileName);
         initScenario ();
         titleUpdate ();
         g_free (fileName);
         gtk_widget_queue_draw (drawing_area);
      }
   }
}

/*! Read parameter file and initialize context */
static void openScenario () {
   GtkFileDialog* fileDialog = selectFile ("Open Parameters", "par", "Parameter Files", "*.par", "*.par", NULL);
   gtk_file_dialog_open (fileDialog, GTK_WINDOW (window), NULL, cbOpenScenario, NULL);
}

/*! provides meta information about grib file, called by gribInfo */
static void gribInfoDisplay (const char *fileName, Zone *zone, int type) {
   char buffer [MAX_SIZE_TEXT];
   char line [MAX_SIZE_LINE] = "";
   char str [MAX_SIZE_NAME] = "";
   char str1 [MAX_SIZE_NAME] = "";
   char strTmp [MAX_SIZE_LINE] = "";
   char strLat0 [MAX_SIZE_NAME] = "", strLon0 [MAX_SIZE_NAME] = "";
   char strLat1 [MAX_SIZE_NAME] = "", strLon1 [MAX_SIZE_NAME] = "";
   char centreName [MAX_SIZE_NAME] = "";
   int l = 0;
   bool isTimeStepOK = true;
 
   for (size_t i = 0; i < N_METEO_ADMIN; i++) // search name of center
     if (meteoTab [i].id == zone->centreId) 
        g_strlcpy (centreName, meteoTab [i].name, sizeof (centreName));
         
   snprintf (line, sizeof (line), "Centre ID: %ld %s   Ed. number: %ld", zone->centreId, centreName, zone->editionNumber);

   GtkWidget *gribWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (gribWindow), line);
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), gribWindow);

   GtkWidget *grid = gtk_grid_new();   
   gtk_window_set_child (GTK_WINDOW (gribWindow), (GtkWidget *) grid);
   gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing (GTK_GRID(grid), 5);

   gtk_grid_set_row_homogeneous (GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous (GTK_GRID(grid), FALSE);

   newDate (zone->dataDate [0], zone->dataTime [0]/100, strTmp, sizeof (strTmp));
   lineReport (grid, 0, "document-open-recent", "Date From", strTmp);
   
   newDate (zone->dataDate [0], zone->dataTime [0]/100 + zone->timeStamp [zone->nTimeStamp -1], strTmp, sizeof (strTmp));
   lineReport (grid, l+=2, "document-open-recent", "Date To", strTmp);

   snprintf (line, sizeof (line), "%d", zone->nMessage);
   lineReport (grid, l+=2, "zoom-original-symbolic", "Nb. Messages", line);

   snprintf (line, sizeof (line), "%ld", zone->stepUnits);
   lineReport (grid, l+=2, "document-page-setup", "Step Unit", line);

   snprintf (line, sizeof (line), "From: %s, %s To: %s %s",\
      latToStr (zone->latMax, par.dispDms, strLat0, sizeof (strLat0)),\
      lonToStr (lonCanonize (zone->lonLeft), par.dispDms, strLon0, sizeof (strLon0)),\
      latToStr (zone->latMin, par.dispDms, strLat1, sizeof (strLat1)),\
      lonToStr (lonCanonize (zone->lonRight), par.dispDms, strLon1, sizeof (strLon1)));

   lineReport (grid, l+=2, "network-workgroup-symbolic", "Zone ", line);

   snprintf (line, sizeof (line), "%.3f° - %.3f°\n", zone->latStep, zone->lonStep);
   lineReport (grid, l+=2, "dialog-information-symbolic", "Lat Step - Lon Step", line);

   snprintf (line, sizeof (line), "%ld x %ld = %ld\n", zone->nbLat, zone->nbLon, zone->numberOfValues);
   lineReport (grid, l+=2 , "preferences-desktop-locale-symbolic", "Nb. Lat x Nb. Lon = Nb Values", line);

   if (zone->nTimeStamp == 0) {
      isTimeStepOK = false;
   }
   else {
      // check if regular
      for (size_t i = 1; i < zone->intervalLimit - 1; i++) {
         if ((zone->timeStamp [i] - zone->timeStamp [i-1]) != zone->intervalBegin) {
            isTimeStepOK = false;
            break;
         // printf ("timeStep: %ld other timeStep: %ld\n", timeStep, zone->timeStamp [i] - zone->timeStamp [i-1]);
         }
      }
      if (isTimeStepOK) {
         for (size_t i = zone->intervalLimit; i < zone->nTimeStamp - 1; i++) {
            if ((zone->timeStamp [i] - zone->timeStamp [i-1]) != zone->intervalEnd) {
               isTimeStepOK = false;
               break;
            // printf ("timeStep: %ld other timeStep: %ld\n", timeStep, zone->timeStamp [i] - zone->timeStamp [i-1]);
            }
         }  
      }
   }

   snprintf (strTmp, sizeof (strTmp), "%zu %s TimeStamps", zone->nTimeStamp, (isTimeStepOK) ? "regular" : "UNREGULAR");
   if (zone->nTimeStamp < 8 || ! isTimeStepOK) {
      g_strlcpy (buffer, "[ ", sizeof (buffer));
      for (size_t k = 0; k < zone->nTimeStamp; k++) {
         if ((k > 0) && ((k % 20) == 0)) 
            g_strlcat (buffer, "\n", sizeof (buffer));
         snprintf (str, sizeof (str), "%ld ", zone->timeStamp [k]);
         if (strlen (buffer) > sizeof (buffer) - 10)
            break;
         g_strlcat (buffer, str, sizeof (buffer));
      }
      g_strlcat (buffer, "]\n", sizeof (buffer));
   }
   else if (zone->intervalEnd == zone->intervalBegin) {
      snprintf (buffer, sizeof (buffer), "[%ld, %ld, ..%ld]\n", 
         zone->timeStamp [0], zone->timeStamp [1], zone->timeStamp [zone->nTimeStamp - 1]);
   }
   else {
      snprintf (buffer, sizeof (buffer), "[%ld, %ld, ..%ld] [%ld, %ld, ..%ld]\n", 
         zone->timeStamp [0], zone->timeStamp [1], zone->timeStamp [zone->intervalLimit - 1],
         zone->timeStamp [zone->intervalLimit], zone->timeStamp [zone->intervalLimit + 1], zone->timeStamp [zone->nTimeStamp - 1]
      );
   }
   lineReport (grid, l+=2, "view-list-symbolic", strTmp, buffer);
   
   g_strlcpy (line, "[ ", sizeof (line));
   for (size_t k = 0; k < MIN (zone->nShortName -1, MAX_VISIBLE_SHORTNAME); k++) {
      snprintf (str, sizeof (str), "%s ", zone->shortName [k]);
      g_strlcat (line, str, sizeof (line));
   }
   if (zone->nShortName > 0) {
      if (zone->nShortName -1 <  MAX_VISIBLE_SHORTNAME) {
         snprintf (str, sizeof (str), "%s ]\n", zone->shortName [zone->nShortName -1]);
         g_strlcat (line, str, sizeof (line));
      }
      else
         g_strlcat (line, ", ...]", sizeof (line));
   }
   snprintf (strTmp, sizeof (strTmp), "%zu shortnames", zone->nShortName);
   lineReport (grid, l+=2, "non-starred-symbolic", strTmp, line);

   snprintf (line, sizeof (line), "%s\n", (zone->wellDefined) ? (zone->allTimeStepOK) ? "Well defined" \
                                             : "All TimeSteps are not defined" : "Undefined");
   lineReport (grid, l+=2 , (zone->wellDefined) ? "weather-clear" : "weather-showers", "Zone is", line);
   
   lineReport (grid, l+=2 , "mail-attachment-symbolic", "Grib File Name", fileName);
   
   formatThousandSep (str1, sizeof (str1), getFileSize (fileName));
    
   lineReport (grid, l+=2, "document-properties-symbolic", "Grib File size", str1);

   buffer [0] = '\0';
   if (! checkGribInfoToStr (type, zone, buffer, sizeof (buffer))) {
      lineReport (grid, l+=2, "software-update-urgent-symbolic", "Warning: \n", buffer);
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
         gribInfoDisplay (par.gribFileName, &zone, WIND);
      
   }
   else { // current
      if (currentZone.nbLat == 0)
         infoMessage ("No current data grib available", GTK_MESSAGE_ERROR);
      else 
         gribInfoDisplay (par.currentGribFileName, &currentZone, CURRENT);
   }
}

/*! callback to check status of mail request */
static gboolean mailGribCheck (gpointer data) {
   char str [MAX_SIZE_STD] = "";
   int localStatusMailRequest = g_atomic_int_get (&gloStatusMailRequest); // atomic read
   switch (localStatusMailRequest) {
   case GRIB_STOPPED: // stop
      break;
   case GRIB_ERROR:   // error
      snprintf (str, MAX_SIZE_STD, "Perhaps Email size limit exceeded. See Mail provider"); 
      break;
   case GRIB_RUNNING: // normal operation. No new mail found yet.
      g_mutex_lock (&warningMutex);
      statusWarningMessage (statusbar, statusbarWarningStr);
      g_mutex_unlock (&warningMutex);
      return TRUE;
      break; 
   case GRIB_OK: // Mail available 
      g_source_remove (gribMailTimeout);  // timer stopped
      waitMessageDestroy ();
      if (gribThread != NULL)
         g_thread_unref (gribThread);     // close previous mail thread
      gribThread = NULL;  
      // then launching next thread...
      if (gribRequestData.onlyDownload) {
         snprintf (str, MAX_SIZE_STD, "%s Downloaded, open it when necessary", gribRequestData.fileName); 
         break;
      }

      typeFlow = (gribRequestData.mailService == SAILDOCS_CURR) ? CURRENT: WIND;
      if (typeFlow == WIND) { // wind  
         g_strlcpy (par.gribFileName, gribRequestData.fileName, MAX_SIZE_FILE_NAME);
         waitMessage ("Grib File decoding", "Be patient\nWatch status bar...");
         gribThread = g_thread_new ("readGribThread", readGribLaunch, NULL);
         gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readGribCheck, NULL);
      }
      else {
         g_strlcpy (par.currentGribFileName, gribRequestData.fileName, MAX_SIZE_FILE_NAME);
         waitMessage ("Grib Current File decoding", "Be patient\nWatch status bar...");
         gribThread = g_thread_new ("readCurrentGribThread", readGribLaunch, NULL);
         gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readCurrentGribCheck, NULL);
      }
      return FALSE;
      break;
   default:
      snprintf (str, MAX_SIZE_STD, "Unknown value: %d in mailGribCheck\n", localStatusMailRequest);
      break;
   }
   waitMessageDestroy ();
   if (gribThread != NULL)
      g_thread_unref (gribThread);
   gribThread = NULL;
   if (str [0] != '\0')
      infoMessage (str, GTK_MESSAGE_WARNING);
   return FALSE;
}

/*! mail Grib Request in thread */
static void *mailGribRequest () {
   char path [MAX_SIZE_LINE];
   int count = 0;
   g_atomic_int_set (&gloStatusMailRequest, GRIB_RUNNING);

   if (markAsRead (par.imapServer, par.imapUserName, par.mailPw, par.imapMailBox))
      printf ("markAsRead OK for box: %s\n", par.imapMailBox);
   else printf ("markAsRead failed. Perhaps no message already in box: %s\n", par.imapMailBox);
   
   g_mutex_lock (&warningMutex);
   snprintf (statusbarWarningStr, sizeof (statusbarWarningStr), "markAsRead done");
   g_mutex_unlock (&warningMutex);

   if (smtpSend (mailServiceTab [gribRequestData.mailService].address, gribRequestData.object, gribRequestData.body)) {
      if (g_atomic_int_get (&gloStatusMailRequest) == GRIB_STOPPED) return NULL;
      
      snprintf (path, sizeof (path), "%s%s", par.workingDir, (gribRequestData.mailService == SAILDOCS_CURR) ? "currentgrib" : "grib");

      g_mutex_lock (&warningMutex);
      snprintf (statusbarWarningStr, sizeof (statusbarWarningStr), "Message sent, waiting for mail response");
      g_mutex_unlock (&warningMutex);
   
      while (g_atomic_int_get (&gloStatusMailRequest) == GRIB_RUNNING) {
         g_mutex_lock (&warningMutex);
         snprintf (statusbarWarningStr, sizeof (statusbarWarningStr), "Mailto: %s, Count: %d", 
            mailServiceTab [gribRequestData.mailService].address, count);
         g_mutex_unlock (&warningMutex);

         int ret = imapGetUnseen (par.imapServer, par.imapUserName, par.mailPw, par.imapMailBox, 
            path, gribRequestData.fileName, sizeof (gribRequestData.fileName));
         g_atomic_int_set (&gloStatusMailRequest, ret);
         sleep (MAIL_TIME_OUT);
         count += 1;
      }
   }
   return NULL;
}

/*! check Wind Grib, current Grib and consistency */
static void checkGribDump () {
   char *buffer = NULL;
   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      fprintf (stderr, "In checkGribDump, Error malloc %d\n", MAX_SIZE_BUFFER); 
      infoMessage ("Memory allocation issue", GTK_MESSAGE_ERROR);
      return;
   }
   checkGribToStr (buffer, MAX_SIZE_BUFFER);
   if (buffer [0] != '\0') {
      displayText (app, buffer, strlen (buffer), "Grib Wind and Current Consistency", "Consistency");
   }
   else
      infoMessage ("All is correct", GTK_MESSAGE_INFO);
   free (buffer);
}

/*! for testing */
static gboolean onOkButtonCalClickedBis (gpointer data) {
   static int count = 0;
   printf ("exec no: %d\n", count);
   competitors.runIndex = -1;
   onOkButtonCalClicked (NULL, NULL);
   count += 1;
   return TRUE;
}

/*! for testing */
static gboolean readGribLaunchBis (gpointer data) {
   static int count = 0;
   printf ("exec no: %d\n", count);
   readGribLaunch (NULL);
   count += 1;
   return TRUE;
}

/*! for testing */
static gboolean getGribWebAllBis (gpointer data) {
   static int count = 0;
   if (gribThread != NULL)
      g_thread_unref (gribThread);
   gribThread = NULL;
   printf ("exec no: %d\n", count);
   gribRequestData.typeWeb = NOAA_WIND;
   gribRequestData.hhZ = 0;
   gribRequestData.latMax = -40;
   gribRequestData.latMin = -60;
   gribRequestData.lonLeft = -150;
   gribRequestData.lonRight = -100;
   gribRequestData.timeStep = 3;
   gribRequestData.timeMax = 8;
   gribThread = g_thread_new ("thread", getGribWebAll, &gribRequestData);
   count += 1;
   return TRUE;
}

/*! call Virtual Regatta calculator */
static void virtualRegStaminaCalculator () {
   staminaCalculator (app);
};

/*! Import and display virtual Regatta dashboard file */
static void virtualRegDashboardImport () {
   char *buffer = NULL;
   char footer [MAX_SIZE_LINE];
   char fileName [MAX_SIZE_FILE_NAME];
   char title [MAX_SIZE_LINE];
   char directory [MAX_SIZE_DIR_NAME];

   snprintf (directory, sizeof (directory), "%sVRdashboard/", par.workingDir); 
   if (! mostRecentFile (directory, ".csv", fileName, sizeof (fileName))) { // most recent csv file found
      infoMessage ("No Virtual Regatta dashboard file found", GTK_MESSAGE_WARNING);
      return;
   }
   g_strlcpy (par.dashboardVR, fileName, sizeof (par.dashboardVR));
   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      infoMessage ("Not enough memory", GTK_MESSAGE_ERROR); 
      return;
   }
   snprintf (title, MAX_SIZE_LINE, "Virtual Regatta Dump: %s", fileName);
   startInfo = dashboardImportParam (fileName, &competitors, buffer, MAX_SIZE_BUFFER, footer, sizeof (footer)); 
   par.startTimeInHours = getDepartureTimeInHour (&startInfo); 
   par.pOr.lat = competitors.t [0].lat;
   par.pOr.lon = competitors.t [0].lon;
   competitors.runIndex = 0;
   
   displayText (app, buffer, strlen (buffer), title, footer);
   free (buffer);
}

/*! CallBack for test choice */
static void cbDropDownSel (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   char str [MAX_SIZE_TEXT];
   int index = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   printf ("test index: %d\n", index);
   switch (index) {
   case 0:
      readGribLaunchBis (NULL);
      g_timeout_add (EXEC_TIME_OUT, readGribLaunchBis, NULL);
      break;
   case 1:
      par.special = 1;
      onOkButtonCalClickedBis (NULL);
      g_timeout_add (EXEC_TIME_OUT, onOkButtonCalClickedBis, NULL);
      break;
   case 2:
      snprintf (str, sizeof (str), 
         "\nZone: latmin: %.2lf, latMax: %.2lf, lonLeft: %.2lf, lonRight: %.2lf\n\nDispZone: xL: %u, xR: %u, yB: %u, yT: %u\nlatMin: %.2lf, latMax: %.2lf, lonLeft: %.2lf, lonRight: %.2lf\nzoom: %.2lf\nAntemeridian: %d\n", 
         zone.latMin, zone.latMax, zone.lonLeft, zone.lonRight, 
         dispZone.xL, dispZone.xR, dispZone.yB, dispZone.yT,
         dispZone.latMin, dispZone.latMax, dispZone.lonLeft, dispZone.lonRight, 
         dispZone.zoom,
         dispZone.anteMeridian);

      infoMessage (str, GTK_MESSAGE_INFO);
      printf ("%s", str);
      break;
   case 3:
      getGribWebAllBis (NULL);
      g_timeout_add (EXEC_TIME_OUT, getGribWebAllBis, NULL);
      break;
   case 4:
      testAisTable ();
      break;

   default:;
   }
}

/*! Test select */
static void testSelection () {
   GtkWidget *testWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW(testWindow), "Test");
   g_signal_connect (testWindow, "destroy", G_CALLBACK(onParentDestroy), testWindow);

   const char *arrayTest [] = {"readGribLaunch", "calculate", "disp Zone", "getGribWeb", "testAis", NULL};
   GtkWidget *dropDownSel = gtk_drop_down_new_from_strings (arrayTest);
   gtk_drop_down_set_selected ((GtkDropDown *) dropDownSel, 0);
   g_signal_connect (dropDownSel, "notify::selected", G_CALLBACK (cbDropDownSel), NULL);
   gtk_window_set_child (GTK_WINDOW (testWindow), dropDownSel);
   
   gtk_window_present (GTK_WINDOW (testWindow));   
}

/*! Display label in col c and line l of tab */
static void labelCreate (GtkWidget *tab, const char *name, int c, int l) {
   GtkWidget *label = gtk_label_new (name);
   gtk_grid_attach(GTK_GRID(tab), label, c, l, 1, 1);
   gtk_widget_set_margin_start(label, 10);
   gtk_label_set_xalign(GTK_LABEL(label), 0.0);
}

/*! Dest Lat update for change */
static void onDestLatChange (GtkWidget *widget, gpointer data) {
   int poiIndex;
   const char *str = gtk_editable_get_text (GTK_EDITABLE (widget));
   if (isalpha (str [0])) { // If lat contain pDest Name
      if ((poiIndex = findPoiByName (str, &par.pDest.lat, &par.pDest.lon)) != -1)
      g_strlcpy (par.pDestName, tPoi [poiIndex].name, sizeof (par.pDestName));
   }
   else par.pDest.lat = getCoord (str, MIN_LAT, MAX_LAT);
   destPressed = true;
   gtk_widget_queue_draw (drawing_area);
}

/*! Dest Lon update for change */
static void onDestLonChange (GtkWidget *widget, gpointer data) {
   par.pDest.lon = getCoord (gtk_editable_get_text (GTK_EDITABLE (widget)), MIN_LON, MAX_LON);
   par.pDest.lon = lonCanonize (par.pDest.lon);
   destPressed = true;
   gtk_widget_queue_draw (drawing_area);
}

/*! set par.pOr according to main competitor */
static void mainCompetitorUpdate () {
   if (competitors.n > 0) {
      par.pOr.lat = competitors.t [0].lat;
      par.pOr.lon = competitors.t [0].lon;
      competitors.runIndex = 0;
   }
}

/*! Origin Lat and Lon update for change */
static void onLatLonChange (GtkWidget *widget, gpointer data) {
   int i = GPOINTER_TO_INT (data);
   double lat, lon;
   if (analyseCoord (gtk_editable_get_text (GTK_EDITABLE (widget)), &lat, &lon)) {
      competitors.t [i].lat = lat;
      competitors.t [i].lon = lonCanonize (lon);
      gtk_widget_queue_draw (drawing_area);
      mainCompetitorUpdate ();
   }
   else
      fprintf (stderr, "In onLatLonChange: analyseCoord failed\n"); 
}

/*! Callback Radio button for Wind colors */ 
static void radioButtonColors (GtkToggleButton *button, gpointer userData) {
   par.showColors = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw (drawing_area); // affiche le tout
   destroySurface ();
}

/*! Radio button for Wind Colar */
static GtkWidget *createRadioButtonColors (const char *name, GtkWidget *tabDisplay, GtkWidget *from, int i) {
   GtkWidget *choice = gtk_check_button_new_with_label (name);
   g_object_set_data (G_OBJECT(choice), "index", GINT_TO_POINTER(i));
   g_signal_connect (choice, "toggled", G_CALLBACK (radioButtonColors), NULL);
   gtk_grid_attach (GTK_GRID(tabDisplay), choice,           i+1, 1, 1, 1);
   if (from != NULL)
      gtk_check_button_set_group ((GtkCheckButton *) choice, (GtkCheckButton *) from); 
   if (i == par.showColors)
      gtk_check_button_set_active ((GtkCheckButton *) choice, TRUE);
   return choice;
}

/*! Callback wind representation */
static void radioButtonWindDisp (GtkToggleButton *button, gpointer userData) {
   par.windDisp = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
   gtk_widget_queue_draw (drawing_area);
}

/*! Radio button for Wind disp */
static GtkWidget *createRadioButtonWindDisp (const char *name, GtkWidget *tabDisplay, GtkWidget *from, int i) {
   GtkWidget *choice = gtk_check_button_new_with_label (name);
   g_object_set_data (G_OBJECT(choice), "index", GINT_TO_POINTER(i));
   g_signal_connect (choice, "toggled", G_CALLBACK (radioButtonWindDisp), NULL);
   gtk_grid_attach (GTK_GRID(tabDisplay), choice,           i+1, 2, 1, 1);
   if (from != NULL)
      gtk_check_button_set_group ((GtkCheckButton *) choice, (GtkCheckButton *) from); 
   if (i == par.windDisp)
      gtk_check_button_set_active ((GtkCheckButton *) choice, TRUE);
   return choice;
}

/*! CallBack for disp choice Avr or Gust or Rain or Pressure */
static void cbDropDownDisp (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   par.indicatorDisp = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   gtk_widget_queue_draw (drawing_area); // Draw all
   destroySurface ();
}

/*! CallBack for time step choice */
static void cbDropDownTStep (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
#define MAX_INDEX 5
   double values [MAX_INDEX] = {0.25, 0.5, 1.0, 2.0, 3.0};
   int index = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   if ((index >= 0) && (index < MAX_INDEX))
      par.tStep = values [index];
   gtk_widget_queue_draw (drawing_area); // Draw all
}

/*! CallBack for Isochrone choice */
static void cbDropDownIsoc (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   par.style = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   gtk_widget_queue_draw (drawing_area); // Draw all
}

/*! CallBack for DMS choice */
static void cbDropDownDMS (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   par.dispDms = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropDown));
   statusBarUpdate (statusbar);
}

/*! Callback activated or not  */
static void onCheckBoxToggled (GtkWidget *checkbox, gpointer userData) {
   gboolean *flag = (gboolean *) userData;
   *flag = gtk_check_button_get_active ((GtkCheckButton *) checkbox);
   gtk_widget_queue_draw (drawing_area);
}

/*! Callback for poi visibility change level */
static void onLevelPoiVisibleChanged (GtkScale *scale, gpointer label) {
   par.maxPoiVisible = gtk_range_get_value (GTK_RANGE(scale));
   char *value_str = g_strdup_printf ("POI: %d", par.maxPoiVisible);
   gtk_label_set_text (GTK_LABEL (label), value_str);
   g_free (value_str);
   gtk_widget_queue_draw (drawing_area);
}

/*! Callback for speed display change  */
static void onTempoDisplayChanged (GtkScale *scale, gpointer label) {
   par.speedDisp = gtk_range_get_value(GTK_RANGE(scale)) ;
   char *value_str = g_strdup_printf("Display Speed: %d", par.speedDisp);
   gtk_label_set_text(GTK_LABEL(label), value_str);
   g_free (value_str);
   changeAnimation ();
}

/*! Translate double to entry */
static GtkWidget *doubleToEntry (double x) {
   char str [MAX_SIZE_NAME];
   snprintf (str, sizeof (str), "%.4f", x);
   return gtk_entry_new_with_buffer(gtk_entry_buffer_new(str, -1));
}

/*! Translate lat to entry */
static GtkWidget *latToEntry (double lat) {
   char strLat [MAX_SIZE_NAME];
   latToStr (lat, par.dispDms, strLat, sizeof (strLat));
   return gtk_entry_new_with_buffer(gtk_entry_buffer_new (strLat, -1));
}

/*! Translate lon to entry */
static GtkWidget *lonToEntry (double lon) {
   char strLon [MAX_SIZE_NAME];
   lonToStr (lon, par.dispDms, strLon, sizeof (strLon));
   return gtk_entry_new_with_buffer(gtk_entry_buffer_new (strLon, -1));
}

/*! Translate lat and lon to entry */
static GtkWidget *latLonToEntry (double lat, double lon) {
   char strLat [MAX_SIZE_NAME];
   char strLon [MAX_SIZE_NAME];
   char strLatLon [MAX_SIZE_LINE];
   latToStr (lat, par.dispDms, strLat, sizeof (strLat));
   lonToStr (lon, par.dispDms, strLon, sizeof (strLon));
   snprintf (strLatLon, sizeof (strLatLon), "%s - %s", strLat, strLon);
   return gtk_entry_new_with_buffer(gtk_entry_buffer_new (strLatLon, -1));
}

/*! double update for change */
static void doubleUpdate (GtkWidget *widget, gpointer data) {
   double *val = (double *)data;
   *val = strtod (gtk_editable_get_text(GTK_EDITABLE(widget)), NULL);
   if (fabs (*val) < EPSILON)
      *val = 0;
   gtk_widget_queue_draw  (drawing_area); 
}

/*! wind Tws update for change */
static void windTwsUpdate (GtkWidget *widget, gpointer data) {
   par.constWindTws = strtod (gtk_editable_get_text(GTK_EDITABLE(widget)), NULL);
   if (par.constWindTws < EPSILON)
      par.constWindTws = 0;
   /*if (par.constWindTws != 0)
	   initZone (&zone);*/
   gtk_widget_queue_draw  (drawing_area); 
}

/*! Retasse competitorss */
static void updateCompetitors() {
   int writeIndex = 0; // Index pour retasser la liste

   for (int i = 0; i < MAX_N_COMPETITORS; i++) {
      g_strstrip (competitors.t[i].name);

      if (competitors.t[i].name[0] != '\0') {
         if (i != writeIndex) {
            g_strlcpy (competitors.t[writeIndex].name, competitors.t[i].name, MAX_SIZE_NAME);
            competitors.t[writeIndex].lat = competitors.t[i].lat;
            competitors.t[writeIndex].lon = competitors.t[i].lon;
         }
         writeIndex++;
      }
   }

   // delete the rest
   for (int i = writeIndex; i < MAX_N_COMPETITORS; i++) {
      competitors.t[i].name[0] = '\0';
      competitors.t[i].lat = 0;
      competitors.t[i].lon = 0;
   }

   competitors.n = writeIndex;
   printf("Number of competitors: %d\n", competitors.n);
}

/*! callback for name change */
static void entryNameChange (GtkEditable *editable, gpointer userData) {
   char * str = (char *) userData;
   const char *text = gtk_editable_get_text (editable);
   g_strlcpy (str, text, MAX_SIZE_NAME);
   updateCompetitors ();
   gtk_widget_queue_draw (drawing_area);
}

/*! callback for color index change */
static void colorIndexUpdate (GtkSpinButton *spin_button, gpointer userData) {
   int *value = (int *) userData;
   *value = gtk_spin_button_get_value_as_int (spin_button);
   gtk_widget_queue_draw (drawing_area);
}

/*! Change parameters and pOr and pDest coordinates */ 
static void change () {
   // Create dialog box
   GtkWidget *changeWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW(changeWindow), "Settings");
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), changeWindow);
   gtk_window_set_modal (GTK_WINDOW (changeWindow), TRUE);
   gtk_window_set_transient_for (GTK_WINDOW (changeWindow), GTK_WINDOW (window));

   // vBox creation 
   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   GtkWidget *OKbutton = gtk_button_new_from_icon_name ("emblem-default");
   //GtkWidget *OKbutton = gtk_button_new_with_label ("Terminate Change");
   g_signal_connect (OKbutton, "clicked", G_CALLBACK (onCancelButtonClicked), changeWindow); // yes we display OK and call Cancel

   // Create notebook
   GtkWidget *notebook = gtk_notebook_new();
   gtk_window_set_child (GTK_WINDOW (changeWindow), (GtkWidget *) vBox);

   gtk_box_append (GTK_BOX (vBox), notebook);
   gtk_box_append (GTK_BOX (vBox), OKbutton);

   // "Display" notebook
   GtkWidget *tabDisplay = gtk_grid_new();
   gtk_notebook_append_page (GTK_NOTEBOOK (notebook), tabDisplay, gtk_label_new ("Display"));
   gtk_widget_set_halign (GTK_WIDGET (tabDisplay), GTK_ALIGN_START);
   gtk_widget_set_valign (GTK_WIDGET (tabDisplay), GTK_ALIGN_START);
   gtk_grid_set_row_spacing (GTK_GRID (tabDisplay), 20);
   gtk_grid_set_column_spacing (GTK_GRID (tabDisplay), 5);

   // "Parameter" notebook
   GtkWidget *tabParam = gtk_grid_new();
   gtk_notebook_append_page (GTK_NOTEBOOK(notebook), tabParam, gtk_label_new ("Parameter"));
   
   gtk_grid_set_row_spacing (GTK_GRID (tabParam), 5);
   gtk_grid_set_column_spacing (GTK_GRID (tabParam), 5);
   
   // "Technical" notebook
   GtkWidget *tabTec = gtk_grid_new();
   if (par.techno)
      gtk_notebook_append_page (GTK_NOTEBOOK(notebook), tabTec, gtk_label_new ("Technical"));
   
   gtk_grid_set_row_spacing (GTK_GRID(tabTec), 5);
   gtk_grid_set_column_spacing (GTK_GRID(tabTec), 5);

   // "Competitors" notebook
   GtkWidget *tabComp = gtk_grid_new();
   gtk_notebook_append_page (GTK_NOTEBOOK(notebook), tabComp, gtk_label_new ("Competitors"));
   gtk_widget_set_halign(GTK_WIDGET (tabComp), GTK_ALIGN_START);
   gtk_widget_set_valign(GTK_WIDGET (tabComp), GTK_ALIGN_START);
   gtk_grid_set_row_spacing (GTK_GRID (tabComp), 10);
   gtk_grid_set_column_spacing (GTK_GRID (tabComp), 5);

   // tabParam

   labelCreate (tabParam, "", 0, 0); // just for space on top

   // Create destination
   GtkWidget *entryDestLat = latToEntry (par.pDest.lat);
   GtkWidget *entryDestLon = lonToEntry (par.pDest.lon);
   labelCreate (tabParam, "Name or Destination Lat",          0, 1);
   gtk_grid_attach (GTK_GRID(tabParam), entryDestLat,   1, 1, 1, 1);
   labelCreate (tabParam, "Destination Lon",            0, 2);
   gtk_grid_attach( GTK_GRID(tabParam), entryDestLon,   1, 2, 1, 1);
   g_signal_connect (entryDestLat, "changed", G_CALLBACK (onDestLatChange), NULL);
   g_signal_connect (entryDestLon, "changed", G_CALLBACK (onDestLonChange), NULL);

   // Create xWind
   labelCreate (tabParam, "xWind",                    0, 3);
   GtkWidget *entryXWind = doubleToEntry (par.xWind);
   gtk_grid_attach (GTK_GRID (tabParam), entryXWind,  1, 3, 1, 1);
   g_signal_connect (entryXWind, "changed", G_CALLBACK (doubleUpdate), &par.xWind);

   // Create max Wind
   labelCreate (tabParam, "Max Wind",                   0, 4);
   GtkWidget *entryMaxWind = doubleToEntry (par.maxWind);
   gtk_grid_attach(GTK_GRID(tabParam), entryMaxWind,    1, 4, 1, 1);
   g_signal_connect (entryMaxWind, "changed", G_CALLBACK (doubleUpdate), &par.maxWind);

   // Create penalty
   labelCreate (tabParam, "Tack",                     0, 5);
   GtkWidget *spinPenalty0 = gtk_spin_button_new_with_range (0, 60, 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON(spinPenalty0), par.penalty0);
   gtk_grid_attach(GTK_GRID (tabParam), spinPenalty0, 1, 5, 1, 1);
   g_signal_connect (spinPenalty0, "value-changed", G_CALLBACK (intSpinUpdate), &par.penalty0);

   labelCreate (tabParam, "Gybe",                     0, 6);
   GtkWidget *spinPenalty1 = gtk_spin_button_new_with_range (0, 60, 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinPenalty1), par.penalty1);
   gtk_grid_attach(GTK_GRID (tabParam), spinPenalty1, 1, 6, 1, 1);
   g_signal_connect (spinPenalty1, "value-changed", G_CALLBACK (intSpinUpdate), &par.penalty1);

   labelCreate (tabParam, "Sail Change",              0, 7);
   GtkWidget *spinPenalty2 = gtk_spin_button_new_with_range (0, 60, 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinPenalty2), par.penalty2);
   gtk_grid_attach(GTK_GRID (tabParam), spinPenalty2, 1, 7, 1, 1);
   g_signal_connect (spinPenalty2, "value-changed", G_CALLBACK (intSpinUpdate), &par.penalty2);

   // Create motorSpeed, threshold
   labelCreate (tabParam, "Motor Speed          ", 0, 8);
   GtkWidget *entrySog = doubleToEntry (par.motorSpeed);
   gtk_grid_attach(GTK_GRID(tabParam), entrySog,   1, 8, 1, 1);
   g_signal_connect (entrySog, "changed", G_CALLBACK (doubleUpdate), &par.motorSpeed);

   labelCreate (tabParam, "Threshold for Motor",       0, 9);
   GtkWidget *entryThreshold = doubleToEntry (par.threshold);
   gtk_grid_attach(GTK_GRID(tabParam), entryThreshold, 1, 9, 1, 1);
   g_signal_connect (entryThreshold, "changed", G_CALLBACK (doubleUpdate), &par.threshold);

   // Create dayEfficiency nightEfficiency
   labelCreate (tabParam, "Day Efficiency",                 0, 10);
   GtkWidget *entryDayEfficiency = doubleToEntry (par.dayEfficiency);
   gtk_grid_attach (GTK_GRID(tabParam), entryDayEfficiency, 1, 10, 1, 1);
   g_signal_connect (entryDayEfficiency, "changed", G_CALLBACK (doubleUpdate), &par.dayEfficiency);

   labelCreate (tabParam, "Night Efficiency",                 0, 11);
   GtkWidget *entryNightEfficiency = doubleToEntry (par.nightEfficiency);
   gtk_grid_attach (GTK_GRID(tabParam), entryNightEfficiency, 1, 11, 1, 1);
   g_signal_connect (entryNightEfficiency, "changed", G_CALLBACK (doubleUpdate), &par.nightEfficiency);

   // Create combo box for Time Step
   labelCreate (tabParam, "", 0, 0); // just for space on top
   labelCreate (tabParam, "Time Step",                 0, 12);
   GtkWidget *dropDownTStep = gtk_drop_down_new_from_strings (arrayTStep);
   int indice = (par.tStep == 0.25) ? 0 : (par.tStep == 0.5) ? 1 : (par.tStep == 1) ? 2 : (par.tStep == 2) ? 3 : 4; 
   gtk_drop_down_set_selected ((GtkDropDown *) dropDownTStep, indice);
   gtk_grid_attach (GTK_GRID(tabParam), dropDownTStep, 1, 12, 1, 1);
   g_signal_connect (dropDownTStep, "notify::selected", G_CALLBACK (cbDropDownTStep), NULL);

   // Create cog step and cog range
   labelCreate (tabParam, "Cog Step",            0, 13);
   GtkWidget *spinCog = gtk_spin_button_new_with_range (1, 20, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinCog), par.cogStep);
   gtk_grid_attach (GTK_GRID(tabParam), spinCog, 1, 13, 1, 1);
   g_signal_connect (spinCog, "value-changed", G_CALLBACK (intSpinUpdate), &par.cogStep);

   labelCreate (tabParam, "Cog Range",             0, 14);
   GtkWidget *spinRange = gtk_spin_button_new_with_range (50, 180, 5);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinRange), par.rangeCog);
   gtk_grid_attach (GTK_GRID(tabParam), spinRange, 1, 14, 1, 1);
   g_signal_connect (spinRange, "value-changed", G_CALLBACK (intSpinUpdate), &par.rangeCog);

   // Create startTime 
   labelCreate (tabTec, "", 0, 0); // just for space on top

   labelCreate (tabTec, "Start Time in hours",         0, 1);
   GtkWidget *entryStartTime = doubleToEntry (par.startTimeInHours);
   gtk_grid_attach (GTK_GRID (tabTec), entryStartTime, 1, 1, 1, 1);
   g_signal_connect (entryStartTime, "changed", G_CALLBACK (doubleUpdate),&par.startTimeInHours);
    
   // Create Opt
   labelCreate (tabTec, "Opt",                 0, 2);
   GtkWidget *spinOpt = gtk_spin_button_new_with_range (0, 4, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinOpt), par.opt);
   gtk_grid_attach (GTK_GRID(tabTec), spinOpt, 1, 2, 1, 1);
   g_signal_connect (spinOpt, "value-changed", G_CALLBACK (intSpinUpdate), &par.opt);
   
   // Create JFactor
   labelCreate (tabTec, "jFactor",                 0, 3);
   GtkWidget *spinJFactor = gtk_spin_button_new_with_range (0, 1000, 10);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinJFactor), par.jFactor);
   gtk_grid_attach (GTK_GRID(tabTec), spinJFactor, 1, 3, 1, 1);
   g_signal_connect (spinJFactor, "value-changed", G_CALLBACK (intSpinUpdate), &par.jFactor);
   
   // Create k Factor and N sector
   labelCreate (tabTec, "k Factor",                0, 4);
   GtkWidget *spinKFactor = gtk_spin_button_new_with_range(0, 4, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinKFactor), par.kFactor);
   gtk_grid_attach (GTK_GRID(tabTec), spinKFactor, 1, 4, 1, 1);
   g_signal_connect (spinKFactor, "value-changed", G_CALLBACK (intSpinUpdate), &par.kFactor);

   labelCreate (tabTec, "N sectors",                0, 5);
   GtkWidget *spinNSectors = gtk_spin_button_new_with_range (0, MAX_N_SECTORS, 10);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinNSectors), par.nSectors);
   gtk_grid_attach (GTK_GRID(tabTec), spinNSectors, 1, 5, 1, 1);
   g_signal_connect (spinNSectors, "value-changed", G_CALLBACK (intSpinUpdate), &par.nSectors);
   
   // Create constWind Twd and Tws
   labelCreate (tabTec, "Const Wind Twd",            0, 6);
   GtkWidget *entryWindTwd = doubleToEntry (par.constWindTwd);
   gtk_grid_attach (GTK_GRID (tabTec), entryWindTwd, 1, 6, 1, 1);
   g_signal_connect (entryWindTwd, "changed", G_CALLBACK (doubleUpdate), &par.constWindTwd);

   labelCreate (tabTec, "Const Wind Tws",           0, 7);
   GtkWidget *entryWindTws = doubleToEntry (par.constWindTws);
   gtk_grid_attach (GTK_GRID(tabTec), entryWindTws, 1, 7, 1, 1);
   g_signal_connect (entryWindTws, "changed", G_CALLBACK (windTwsUpdate), NULL);
   
   // Create constCurrent Twd and Tws
   labelCreate (tabTec, "Const Current Twd",           0, 8);
   GtkWidget *entryCurrentTwd = doubleToEntry (par.constCurrentD);
   gtk_grid_attach (GTK_GRID(tabTec), entryCurrentTwd, 1, 8, 1, 1);
   g_signal_connect (entryCurrentTwd, "changed", G_CALLBACK (doubleUpdate), &par.constCurrentD);

   labelCreate (tabTec, "Const Current Tws",           0, 9);
   GtkWidget *entryCurrentTws = doubleToEntry (par.constCurrentS);
   gtk_grid_attach (GTK_GRID(tabTec), entryCurrentTws, 1, 9, 1, 1);
   g_signal_connect (entryCurrentTws, "changed", G_CALLBACK (doubleUpdate), &par.constCurrentS);
   
   // Create constWave
   labelCreate (tabTec, "Const Wave Height",     0, 10);
   GtkWidget *entryWave = doubleToEntry (par.constWave);
   gtk_grid_attach (GTK_GRID(tabTec), entryWave, 1, 10, 1, 1);
   g_signal_connect (entryWave, "changed", G_CALLBACK (doubleUpdate), &par.constWave);

   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach (GTK_GRID(tabTec), separator, 0, 11, 11, 1);

   // checkbox: "focal closest"
   GtkWidget *checkboxClosest = gtk_check_button_new_with_label("Closest");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxClosest, par.closestDisp);
   g_signal_connect (G_OBJECT (checkboxClosest), "toggled", G_CALLBACK (onCheckBoxToggled), &par.closestDisp);
   gtk_grid_attach (GTK_GRID (tabTec), checkboxClosest,  0, 12, 1, 1);

   // checkbox: "focal point"
   GtkWidget *checkboxFocal = gtk_check_button_new_with_label("Focal Point");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxFocal, par.focalDisp);
   g_signal_connect(G_OBJECT(checkboxFocal), "toggled", G_CALLBACK (onCheckBoxToggled), &par.focalDisp);
   gtk_grid_attach(GTK_GRID (tabTec), checkboxFocal, 1, 12, 1, 1);

   // checkbox: "allway sea"
   GtkWidget *checkboxAllwaysSea = gtk_check_button_new_with_label("Ignore earth and polygons");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxAllwaysSea, par.allwaysSea);
   g_signal_connect(G_OBJECT(checkboxAllwaysSea), "toggled", G_CALLBACK (onCheckBoxToggled), &par.allwaysSea);
   gtk_grid_attach(GTK_GRID (tabTec), checkboxAllwaysSea, 0, 13, 1, 1);

   // tabDisplay 

   labelCreate (tabDisplay, "",              0, 0); // just for space on top
   
   // Radio Button Colors
   labelCreate (tabDisplay, "Colors",                     0, 1);
   GtkWidget *choice0 = createRadioButtonColors ("None", tabDisplay, NULL, 0);
   GtkWidget *choice1 = createRadioButtonColors ("B.& W.", tabDisplay, choice0, 1);
   createRadioButtonColors ("Colored", tabDisplay, choice1, 2);
   
   // Radio Button Wind
   labelCreate (tabDisplay, "Wind",                       0, 2);
   choice0 = createRadioButtonWindDisp ("None", tabDisplay, NULL, 0);
   choice1 = createRadioButtonWindDisp ("Arrow", tabDisplay, choice0, 1);
   createRadioButtonWindDisp ("Barbule", tabDisplay, choice1, 2);
   
   // Create combo box for Avr/gust/rain/pressure
   labelCreate (tabDisplay, "Wind/Gust/Rain/Pressure",     0, 3);
   const char *arrayDisp [] = {"Wind", "Gust", "Waves", "Rain", "Pressure", NULL};
   GtkWidget *dropDownDisp = gtk_drop_down_new_from_strings (arrayDisp);
   gtk_drop_down_set_selected ((GtkDropDown *) dropDownDisp, par.indicatorDisp);
   gtk_grid_attach (GTK_GRID(tabDisplay), dropDownDisp, 1, 3, 1, 1);
   g_signal_connect (dropDownDisp, "notify::selected", G_CALLBACK (cbDropDownDisp), NULL);

   // Create combo box for DMS (Degree Minutes Second) representaton
   labelCreate (tabDisplay, "DMS",                       2, 3);
   const char *arrayDMS [] = {"Basic", "DD", "DM", "DMS", NULL};
   GtkWidget *dropDownDMS = gtk_drop_down_new_from_strings (arrayDMS);
   gtk_drop_down_set_selected ((GtkDropDown *) dropDownDMS, par.dispDms);
   gtk_grid_attach (GTK_GRID(tabDisplay), dropDownDMS, 3, 3, 1, 1);
   g_signal_connect (dropDownDMS, "notify::selected", G_CALLBACK (cbDropDownDMS), NULL);

   // Create combo box for Isochrones
   labelCreate (tabDisplay, "Isoc.",                       0, 4);
   const char *arrayIsoc [] = {"None", "Points", "Segment", "Bezier", NULL};
   GtkWidget *dropDownIsoc = gtk_drop_down_new_from_strings (arrayIsoc);
   gtk_drop_down_set_selected ((GtkDropDown *) dropDownIsoc, par.style);
   gtk_grid_attach (GTK_GRID(tabDisplay), dropDownIsoc, 1, 4, 1, 1);
   g_signal_connect (dropDownIsoc, "notify::selected", G_CALLBACK (cbDropDownIsoc), NULL);

   // Create spin for step Isoc display representaton
   labelCreate (tabDisplay, "ISOC interval",               2, 4);
   GtkWidget *spinInterval = gtk_spin_button_new_with_range (1, 20, 1);
   gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinInterval), par.stepIsocDisp);
   gtk_grid_attach(GTK_GRID (tabDisplay), spinInterval, 3, 4, 1, 1);
   g_signal_connect (spinInterval, "value-changed", G_CALLBACK (intSpinUpdateWithQueueDraw), &par.stepIsocDisp);

   // Checkbox: "waves"
   GtkWidget *checkboxWave = gtk_check_button_new_with_label("Waves");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxWave, par.waveDisp);
   g_signal_connect(G_OBJECT (checkboxWave), "toggled", G_CALLBACK (onCheckBoxToggled), &par.waveDisp);
   gtk_grid_attach(GTK_GRID(tabDisplay), checkboxWave,          0, 5, 1, 1);

   // Checkbox: "current"
   GtkWidget *checkboxCurrent = gtk_check_button_new_with_label("Current");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxCurrent, par.currentDisp);
   g_signal_connect (G_OBJECT(checkboxCurrent), "toggled", G_CALLBACK (onCheckBoxToggled), &par.currentDisp);
   gtk_grid_attach(GTK_GRID (tabDisplay), checkboxCurrent,      1, 5, 1, 1);

   // Checkbox: "Grid" for parallels and meridians
   GtkWidget *checkboxGrid = gtk_check_button_new_with_label("Grid");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxGrid, par.gridDisp);
   g_signal_connect (G_OBJECT(checkboxGrid), "toggled", G_CALLBACK (onCheckBoxToggled), &par.gridDisp);
   gtk_grid_attach(GTK_GRID (tabDisplay), checkboxGrid,         2, 5, 1, 1);

   // Checkbox: "info display"
   GtkWidget *checkboxInfoDisp = gtk_check_button_new_with_label("Info Display");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxInfoDisp, par.infoDisp);
   g_signal_connect (G_OBJECT(checkboxInfoDisp), "toggled", G_CALLBACK (onCheckBoxToggled), &par.infoDisp);
   gtk_grid_attach (GTK_GRID (tabDisplay), checkboxInfoDisp,     0, 6, 1, 1);

   // Checkbox: "ais display"
   GtkWidget *checkboxAisDisp = gtk_check_button_new_with_label("AIS Display");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxAisDisp, par.aisDisp);
   g_signal_connect (G_OBJECT(checkboxAisDisp), "toggled", G_CALLBACK (onCheckBoxToggled), &par.aisDisp);
   gtk_grid_attach (GTK_GRID (tabDisplay), checkboxAisDisp,     1, 6, 1, 1);

   // Checkbox: "SHP display as points"
   GtkWidget *checkboxShpDisp = gtk_check_button_new_with_label("SHP Display as points");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxShpDisp, par.shpPointsDisp);
   g_signal_connect (G_OBJECT(checkboxShpDisp), "toggled", G_CALLBACK (onCheckBoxToggled), &par.shpPointsDisp);
   gtk_grid_attach (GTK_GRID (tabDisplay), checkboxShpDisp,     2, 6, 1, 1);

   GtkWidget *separator1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_grid_attach (GTK_GRID(tabDisplay), separator1, 0, 7, 10, 1);

   // Create GtkScale POI widget
   char *poiMessage = g_strdup_printf ("POI: %d", par.maxPoiVisible); 
   GtkWidget *labelNumber = gtk_label_new (poiMessage);
   g_free (poiMessage);
   gtk_grid_attach(GTK_GRID (tabDisplay), labelNumber, 0, 8, 1, 1);
   gtk_widget_set_margin_start (labelNumber, 4);
   gtk_label_set_xalign(GTK_LABEL(labelNumber), 0.0);
   
   GtkWidget *levelVisible = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 1, MAX_LEVEL_POI_VISIBLE, 1);
   gtk_widget_set_tooltip_text (levelVisible, "Select Visibility level");
   gtk_range_set_value (GTK_RANGE(levelVisible), par.maxPoiVisible);
   gtk_scale_set_value_pos (GTK_SCALE(levelVisible), GTK_POS_TOP);
   gtk_widget_set_size_request (levelVisible, 200, -1);
   g_signal_connect (levelVisible, "value-changed", G_CALLBACK (onLevelPoiVisibleChanged), labelNumber);
   gtk_grid_attach(GTK_GRID(tabDisplay), levelVisible, 1, 8, 2, 1);
  
   // Create GtkScale Display Speed widget
   char *speedMessage = g_strdup_printf ("Display Speed: %d", par.speedDisp); 
   GtkWidget *labelSpeedDisplay = gtk_label_new (speedMessage);
   g_free (speedMessage);
   gtk_grid_attach(GTK_GRID (tabDisplay), labelSpeedDisplay, 0, 9, 1, 1);
   gtk_widget_set_margin_start (labelSpeedDisplay, 4);
   gtk_label_set_xalign (GTK_LABEL(labelSpeedDisplay), 0.0);
   
   GtkWidget *levelSpeedDisplay = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, MAX_N_ANIMATION - 1, 1);
   gtk_widget_set_tooltip_text (levelSpeedDisplay, "Select Speed");
   gtk_range_set_value (GTK_RANGE(levelSpeedDisplay), par.speedDisp);
   gtk_scale_set_value_pos (GTK_SCALE(levelSpeedDisplay), GTK_POS_TOP);
   gtk_widget_set_size_request (levelSpeedDisplay, 200, -1);
   g_signal_connect (levelSpeedDisplay, "value-changed", G_CALLBACK (onTempoDisplayChanged), labelSpeedDisplay);
   gtk_grid_attach (GTK_GRID(tabDisplay), levelSpeedDisplay,   1, 9, 2, 1);

   // competitors grid
   labelCreate (tabComp, "", 0, 0); // just for space on top

   for (int i = 0; i < MAX_N_COMPETITORS; i+=1) {
      GtkEntryBuffer *bufferName = gtk_entry_buffer_new ((i < competitors.n) ? competitors.t[i].name : "", -1);
      GtkWidget *entryCompName = gtk_entry_new_with_buffer(bufferName);
      gtk_widget_set_size_request (entryCompName, 200, -1); 

      GtkWidget *spinCompIndex = gtk_spin_button_new_with_range (0, MAX_N_COLOR_SHIP - 1, 1);
      gtk_spin_button_set_value (GTK_SPIN_BUTTON(spinCompIndex), competitors.t [i].colorIndex);

      GtkWidget *entryCompPos = (i < competitors.n) ? latLonToEntry (competitors.t [i].lat, competitors.t [i].lon) : latLonToEntry (0.0, 0.0);
      gtk_widget_set_size_request (entryCompPos, 250, -1); // minimum width...

      gtk_grid_attach (GTK_GRID(tabComp), entryCompName, 0, i + 1, 1, 1);
      gtk_grid_attach (GTK_GRID(tabComp), spinCompIndex, 1, i + 1, 1, 1);
      gtk_grid_attach (GTK_GRID(tabComp), entryCompPos,  2, i + 1, 1, 1);
      g_signal_connect (entryCompName, "changed", G_CALLBACK (entryNameChange), competitors.t[i].name);
      g_signal_connect (entryCompPos, "changed", G_CALLBACK (onLatLonChange), GINT_TO_POINTER (i));
      g_signal_connect (spinCompIndex, "value-changed", G_CALLBACK (colorIndexUpdate), &competitors.t [i].colorIndex);
   }
  
   gtk_window_present (GTK_WINDOW (changeWindow));
}
    
/*! zoom in */ 
static void onZoomInButtonClicked (GtkWidget *widget, gpointer data) {
   dispZoom (0.6);
   gtk_widget_queue_draw (drawing_area);
}

/*! zoom out */ 
static void onZoomOutButtonClicked (GtkWidget *widget, gpointer data) {
   dispZoom (1.4);
   gtk_widget_queue_draw (drawing_area);
}

/*! zoom original */ 
static void onZoomOriginalButtonClicked (GtkWidget *widget, gpointer data) {
   initDispZone ();
   gtk_widget_queue_draw (drawing_area);
}

/*! go up button */ 
static void onUpButtonClicked (GtkWidget *widget, gpointer data) {
   dispTranslate (1.0, 0);
   gtk_widget_queue_draw (drawing_area);
}

/*! go down button */ 
static void onDownButtonClicked (GtkWidget *widget, gpointer data) {
   dispTranslate (-1.0, 0);
   gtk_widget_queue_draw (drawing_area);
}

/*! go left button */ 
static void onLeftButtonClicked (GtkWidget *widget, gpointer data) {
   dispTranslate (0, -1.0);
   gtk_widget_queue_draw (drawing_area);
}

/*! go right button */ 
static void onRightButtonClicked (GtkWidget *widget, gpointer data) {
   dispTranslate (0, 1.0);
   gtk_widget_queue_draw (drawing_area);
}

/*! center the map on GPS position */ 
static void onCenterMap (GtkWidget *widget, gpointer data) {
   centerDispZone (par.pOr.lon, par.pOr.lat);
   destroySurface ();
   gtk_widget_queue_draw (drawing_area);
}

/*! Response for poi change */
static void poiNameResponse (GtkWidget *widget, gpointer entryWindow) {
   if (menuWindow != NULL) popoverFinish (menuWindow);
   tPoi [nPoi].lon = xToLon (whereIsPopup.x);
   tPoi [nPoi].lat = yToLat (whereIsPopup.y);
   tPoi [nPoi].level = 1;
   tPoi [nPoi].type = NEW;
   nPoi += 1;
   writePoi (par.poiFileName);
   gtk_widget_queue_draw (drawing_area);
   if ((entryWindow != NULL) && GTK_IS_WINDOW (entryWindow))
      gtk_window_destroy (GTK_WINDOW (entryWindow));
}

/*! poi name */ 
static void poiNameChoose () {
   g_strlcpy (tPoi [nPoi].name, "example", MAX_SIZE_POI_NAME);
   if (nPoi >= MAX_N_POI) {
      infoMessage ("Number of poi exceeded", GTK_MESSAGE_ERROR);
      return;
   }
   entryBox ("Poi Name", "Name: ", tPoi [nPoi].name, poiNameResponse);
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
   else infoMessage ("Number of waypoints exceeded", GTK_MESSAGE_ERROR);
}

/*! update point of origin */
static void originSelected () {
   if (menuWindow != NULL) popoverFinish (menuWindow);
   if (competitors.n == 0) {
      infoMessage ("No competitor", GTK_MESSAGE_WARNING);
      return;
   }
   destPressed = false;
   int iMain = 0;
   competitors.t [iMain].lat = par.pOr.lat = yToLat (whereIsPopup.y);
   competitors.t [iMain].lon = par.pOr.lon = xToLon (whereIsPopup.x);
   competitors.runIndex = 0;
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
   calculateOrthoRoute ();
   niceWayPointReport (); 
   par.pOr.id = -1;
   par.pOr.father = -1;
   par.pDest.id = 0;
   par.pDest.father = 0;
   //par.pDestName [0] = '\0';
   gtk_widget_queue_draw (drawing_area);
} 

/*! update start polygon for forbidden zone */
static void startPolygonSelected () {
   if (menuWindow != NULL) popoverFinish (menuWindow);
   if ((forbidZones [par.nForbidZone].points = (Point *) malloc (MAX_SIZE_FORBID_ZONE * sizeof(Point))) == NULL) {
      fprintf (stderr, "In startPolygonSelected, Error malloc forbidZones with k = %d\n", par.nForbidZone);
      infoMessage ("In startPolygonSelected,  Error malloc forbidZones", GTK_MESSAGE_ERROR);
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
static gboolean onMotionEvent (GtkEventControllerMotion *controller, double x, double y, gpointer userData) {
   whereIsMouse.x = x;
   whereIsMouse.y = y;
   if (selecting) {
      gtk_widget_queue_draw (drawing_area); // Redraw fo selection rectangle drawing
   }
   else statusBarUpdate (statusbar);
   return TRUE;
}

/*! Draw ship at position (x, y) according to type and direction (cog) */
static void drawShip (cairo_t *cr, const char *name, double x, double y, int type, int cog) {
   const GdkRGBA color = colShip [MIN (MAX_N_COLOR_SHIP - 1, type)];

   cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
   cairo_move_to (cr, x+10, y);
   cairo_show_text (cr, name);

   cairo_save (cr);
   cairo_translate(cr, x, y);
   cairo_rotate (cr, cog * DEG_TO_RAD);

   // Boat as a triangle
   const double boatLength = 30.0; // (type == 0) ? 40 : 20.0; // bigger for type 0
   const double boatWidth = 15.0;  // (type == 0) ? 20 : 10.0;
    
   cairo_move_to (cr, 0, -boatLength / 2);               // front point
   cairo_line_to (cr, boatWidth / 2, boatLength / 2);    // right back point
   cairo_line_to (cr, 0, 0.8 * boatLength / 2);          // middle back
   cairo_line_to (cr, -boatWidth / 2, boatLength / 2);   // left back point
   cairo_close_path (cr);
   cairo_fill(cr);
   cairo_restore(cr);
}

/*! draw AIS points  */
static void drawAis (cairo_t *cr) {
   double x, y;
   int type;
   cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size (cr, 10);

   // use GHashTableIter to iterare on all elements
   GHashTableIter iter;
   gpointer key, value;
   g_hash_table_iter_init (&iter, aisTable);

   while (g_hash_table_iter_next(&iter, &key, &value)) {
      AisRecord *ship = (AisRecord *)value;
      x = getX (ship->lon);
      y = getY (ship->lat);
      type =  (ship->minDist < 0) ? 3
            : (ship->minDist <= 100) ? 1
            : (ship->minDist <= 1000) ? 2 
            : 3;
      drawShip (cr, ship->name, x, y, type, ship->cog); 
   }
}

/*! change the route selecting another point in last isochrone */
static void changeLastPoint () {
   double dXy, xLon, yLat, minDxy = DBL_MAX;
   double x = whereIsPopup.x;
   double y = whereIsPopup.y;

   if (menuWindow != NULL) popoverFinish (menuWindow);
   for (int i = 0; i < isoDesc [nIsoc - 1 ].size; i++) {
      xLon = getX (isocArray [(nIsoc - 1) * MAX_SIZE_ISOC + i].lon);
      yLat = getY (isocArray [(nIsoc - 1) * MAX_SIZE_ISOC + i].lat);
      dXy = (xLon - x)*(xLon -x) + (yLat - y) * (yLat - y);
      if (dXy < minDxy) {
         minDxy = dXy;
         selectedPointInLastIsochrone = i;
      }
   }
   lastClosest = isocArray [(nIsoc - 1) * MAX_SIZE_ISOC + selectedPointInLastIsochrone];
   storeRoute (&route, &par.pOr, &lastClosest, 0);
   routeGram ();
}

/*! select action associated to right click */ 
static void onRightClickEvent (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userData) {
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

   char *strPt = g_strdup_printf ("Point of Origin: %s", competitors.t [0].name);
   GtkWidget *originButton = gtk_button_new_with_label (strPt);
   g_free (strPt);
   g_signal_connect (originButton, "clicked", G_CALLBACK (originSelected), NULL);

   snprintf (str, sizeof (str), "Waypoint no: %d",  wayPoints.n);
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

   g_signal_connect(drawing_area, "destroy", G_CALLBACK (onParentWindowDestroy), menuWindow);
   GdkRectangle rect = {x, y, 1, 1};
   gtk_popover_set_pointing_to ((GtkPopover*) menuWindow, &rect);
   gtk_widget_set_visible (menuWindow, true);
}

/*! pop over left clic simple (no selection) */
static void simpleLeftClic (double x, double y) {
   char strLat [MAX_SIZE_NAME], strLon [MAX_SIZE_NAME];
   //double u, v, g, w, twd, tws;
   double lat = yToLat (y);
   double lon = xToLon (x);
   double cap = fmod (directCap (par.pOr.lat, par.pOr.lon, lat, lon) + 360, 360.0);
   double dist = orthoDist (par.pOr.lat, par.pOr.lon, lat, lon);
   char *str;

   GtkWidget *leftClickWindow = gtk_popover_new ();
   gtk_widget_set_parent (leftClickWindow, drawing_area);
   g_signal_connect (drawing_area, "destroy", G_CALLBACK (onParentWindowDestroy), leftClickWindow);
   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_popover_set_child ((GtkPopover*) leftClickWindow, (GtkWidget *) vBox);
   GdkRectangle rect = {x, y, 1, 1};

   gtk_popover_set_has_arrow ((GtkPopover*) leftClickWindow, TRUE);
   gtk_popover_set_pointing_to ((GtkPopover*) leftClickWindow, &rect);

   latToStr (lat, par.dispDms, strLat, sizeof (strLat));
   lonToStr (lon, par.dispDms, strLon, sizeof (strLon));

   str = g_strdup_printf ("%s   %s\nFrom %s: \n%03.0lf°   %.2lf Kn", 
         strLat, strLon, competitors.t [0].name, cap, dist);
   
   /*if (isInZone (lat, lon, &zone)) {
	   findWindGrib (lat, lon, theTime, &u, &v, &g, &w, &twd, &tws);
      str = g_strdup_printf ("%s   %s\nTWD: %.0lf°\nTWS: %.2lf Kn, Gust: %.2lf Kn", 
         strLat, strLon, 
         fmod ((twd + 360.0), 360.0), tws, MS_TO_KN * g);
   }
   else {
      str = g_strdup_printf ("%s   %s", strLat, strLon);
   }*/
   /*double x = getX (par.pOr.lon);
   double y = getY (par.pOr.lat);
   CAIRO_SET_SOURCE_RGB_LIGHT_GRAY (cr);
   cairo_move_to (cr, x, y);
   x = getX (lon);
   y = getY (lat);
   cairo_stroke (cr);
   */
   GtkWidget *label = gtk_label_new (str);
   gtk_box_append (GTK_BOX (vBox), label);
   g_free (str);
   
   gtk_widget_set_visible (leftClickWindow, true);
}

/*! Callback  associated to mouse left clic release event
    Action after selection. Launch smtp request for grib file */
static void onLeftReleaseEvent (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userData) {
   if (n_press == 1) {
      if (selecting && \
         ((whereIsMouse.x - whereWasMouse.x) > MIN_MOVE_FOR_SELECT) && \
         ((whereIsMouse.x - whereWasMouse.x) > MIN_MOVE_FOR_SELECT)) {
         gribRequestData.latMin= floor (yToLat (whereIsMouse.y));
         gribRequestData.lonLeft = floor (xToLon (whereWasMouse.x));
         gribRequestData.latMax = ceil (yToLat (whereWasMouse.y));
         gribRequestData.lonRight = ceil (xToLon (whereIsMouse.x));
         gribRequestData.mailService = NOT_MAIL;
         gribRequestData.typeWeb = NOAA_WIND;
         gribRequestBox (&gribRequestData);
      }
      else {
         simpleLeftClic (x, y);
      }
   }
   selecting = false;
   gtk_widget_queue_draw (drawing_area); // Redessiner pour effacer le rectangle de sélection
}

/*! Callback  associated to mouse left clic event */
static void onLeftClickEvent (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userData) {
   selecting = !selecting;
   whereWasMouse.x = whereIsMouse.x = x;
   whereWasMouse.y = whereIsMouse.y = y;
   gtk_widget_queue_draw (drawing_area); // Redessiner pour mettre à jour le rectangle de sélection
}

/*! Zoom associated to mouse scroll event */
static gboolean onScrollEvent (GtkEventController *controller, double deltaX, double deltaY, gpointer userData) {
   if (deltaY > 0) {
      dispZoom (1.4);
   } else if (deltaY < 0) {
	   dispZoom (0.6);
   }
   gtk_widget_queue_draw (drawing_area);
   return TRUE;
}

/*! Callback when fullscreen or back */ 
static void onFullscreenNotify (void) {
   destroySurface ();
   gtk_widget_queue_draw (drawing_area);
}

/*! Callback when keyboard key pressed */
static gboolean onKeyEvent (GtkEventController *controller, guint keyval, \
   guint keycode, GdkModifierType modifiers, gpointer userData) {
   switch (keyval) {
   case GDK_KEY_Escape:
      printf ("Key Escape\n");
      stopChildThread ();
      break;
   case GDK_KEY_F1:
      if (!my_gps_data.OK)
         infoMessage ("No GPS position available", GTK_MESSAGE_WARNING);
      else if (competitors.n == 0)
         infoMessage ("No competitor", GTK_MESSAGE_WARNING);
      else {
         competitors.t[0].lat = par.pOr.lat = my_gps_data.lat;
         competitors.t[0].lon = par.pOr.lon =  my_gps_data.lon;
         infoMessage ("Point of Origin = GPS position", GTK_MESSAGE_WARNING);
      }
      break;
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
   case GDK_KEY_F2:
      printf ("Techno Disp\n");
      par.techno = !par.techno;
      break;
   default:
      break;
   }
   gtk_widget_queue_draw (drawing_area);
   return TRUE; // event has been managed with success
}

/*! Draw meteogram */
static void onMeteogramEvent (GtkDrawingArea *area, cairo_t *cr, int width, \
   int height, gpointer userData) {

#define MAX_VAL_MET 4
   const int legendXLeft = width - 80;
   const int legendYTop = 10;
   char str [MAX_SIZE_LINE];
   int x, y;
   double u, v, g = 0, w, twd, tws, head_x, maxTws = 0, maxG = 0, maxMax = 0, maxWave = 0;
   double uCurr, vCurr, currTwd, currTws, maxCurrTws = 0; // current
   long tMax = zone.timeStamp [zone.nTimeStamp -1];
   if (tMax <= 0) {
      fprintf (stderr, "In onMeteogramEvent: tMax should be strictly posditive\n");
      return;
   }
   double tDeltaCurrent = zoneTimeDiff (&currentZone, &zone);
   double tDeltaNow = 0; // time between beginning of grib and now in hours
   double lat, lon;
   char totalDate [MAX_SIZE_DATE]; 
   int timeMeteo, initTimeMeteo;
   lat = yToLat (whereIsPopup.y);
   lon = xToLon (whereIsPopup.x);

   if (zone.nTimeStamp < 2) return;
   cairo_set_line_width (cr, 1);
   const int xLeft = 30;
   const int xRight = width - 20;
   const int yTop = 40;
   const int yBottom = height - 25;
   const int headY = 20;
   const int xk = (xRight - xLeft) / tMax;  
   const int constDelta = 5;
   const int dayLG = 10;

   const char *libelle [MAX_VAL_MET] = {"Wind", "Gust", "Waves", "Current"}; 
   double colors[MAX_VAL_MET][3] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {1, 165.0/255.0, 0}}; // blue red blue orange
   drawLegend (cr, legendXLeft, legendYTop, colors, libelle, MAX_VAL_MET);

   // graphical difference between up to now and after now 
   // tDeltaNow is the number of hours between beginning of grib to now */
   time_t now = time (NULL);
   tDeltaNow = (now - vOffsetLocalUTC)/ 3600.0; // offset between local and UTC in hours
   tDeltaNow -= gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]) / 3600.0;
   //printf ("tDeltaNow: %.2lf\n", tDeltaNow);
   CAIRO_SET_SOURCE_RGB_YELLOW(cr);
   if (tDeltaNow > 0) {
      x = xLeft + xk * tDeltaNow;
      x = MIN (x, xRight);
      cairo_rectangle (cr, xLeft, yTop, x - xLeft, yBottom - yTop); // past is gray
      cairo_fill (cr);
      if (x < xRight -10) {
         CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr); // line for now
         cairo_move_to (cr, x, yBottom);
         cairo_line_to (cr, x, yTop);
         cairo_stroke (cr);
      }
   } 
   
   // Draw horizontal line with arrow
   CAIRO_SET_SOURCE_RGB_BLACK(cr);
   cairo_move_to (cr, xLeft,  yBottom );
   cairo_line_to (cr, xRight, yBottom);
   cairo_line_to (cr, xRight - constDelta, yBottom + constDelta); // for arrow
   cairo_stroke (cr);
   cairo_move_to (cr, xRight, yBottom );
   cairo_line_to (cr, xRight - constDelta, yBottom - constDelta);
   cairo_stroke (cr);
   
   // Draw vertical line with arrow
   cairo_move_to (cr, xLeft, yBottom);
   cairo_line_to (cr, xLeft, yTop);
   cairo_line_to (cr, xLeft - constDelta, yTop + constDelta);
   cairo_stroke (cr);
   cairo_move_to (cr, xLeft, yTop);
   cairo_line_to (cr, xLeft + constDelta, yTop + constDelta);
   cairo_stroke (cr);

   // find max Tws max gust max waves max current and draw twd arrows
   for (int i = 0; i < tMax; i += 1) {
      findWindGrib (lat, lon, i, &u, &v, &g, &w, &twd, &tws);
	   findCurrentGrib (lat, lon, i- tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);

      head_x = xLeft + xk * i;
      if ((i % (tMax / 24)) == 0) {
         arrow (cr, head_x, headY, u, v, twd, tws, WIND);
         arrow (cr, head_x, headY + 20, uCurr, vCurr, currTwd, currTws, CURRENT);
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
      x = xLeft + xk * i;
      cairo_move_to (cr, x, yBottom + 10);
      newDate (zone.dataDate [0], timeMeteo, totalDate, sizeof (totalDate));
      cairo_show_text (cr, strrchr (totalDate, ' ') + 1); // hour is after space
      if ((timeMeteo % ((par.gribTimeMax <= 240) ? 24 : 48)) == initTimeMeteo) { 
         // only year month day considered
         cairo_move_to (cr, x, yBottom + 20);
         totalDate [dayLG] = '\0'; // date
         cairo_show_text (cr, totalDate);
         CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr);
         cairo_move_to (cr, x, yBottom);
         cairo_line_to (cr, x, yTop);
         cairo_stroke (cr);
         CAIRO_SET_SOURCE_RGB_BLACK(cr);
      }
   }
   cairo_stroke(cr);

   if (maxMax <= 0) {
      fprintf (stderr, "In onMeteogramEvent: maxMax should be strictly posditive\n");
      return;
   }
   const int yk = (yBottom - yTop) / maxMax;
   
   CAIRO_SET_SOURCE_RGB_BLACK(cr);

   // Draw speed labels and horizontal lines
   cairo_set_font_size (cr, 10);
   double step = (maxMax > 50) ? 10 : 5;
   for (double speed = step; speed <= maxMax; speed += step) {
      y = yBottom - yk * speed;
      cairo_move_to (cr, xLeft - 20, y);
      snprintf (str, MAX_SIZE_LINE, "%02.0lf", speed);
      cairo_show_text (cr, str);
      CAIRO_SET_SOURCE_RGB_ULTRA_LIGHT_GRAY(cr); // horizontal line
      cairo_move_to (cr, xLeft, y);
      cairo_line_to (cr, xRight, y);
      cairo_stroke (cr);
      CAIRO_SET_SOURCE_RGB_BLACK(cr);
   }
   cairo_stroke(cr);
  
   CAIRO_SET_SOURCE_RGB_BLUE(cr);
   // draw tws 
   for (int i = 0; i < tMax; i += 1) {
      findWindGrib (lat, lon, i, &u, &v, &g, &w, &twd, &tws);
      x = xLeft + xk * i;
      y = yBottom - yk * tws;
      if (i == 0) cairo_move_to (cr, x, y);
      else cairo_line_to (cr, x, y);
   }
   cairo_stroke(cr);
   
   // draw gust
   if (maxG > 0) { 
      CAIRO_SET_SOURCE_RGB_RED(cr);
      for (int i = 0; i < tMax; i += 1) {
         findWindGrib (lat, lon, i, &u, &v, &g, &w, &twd, &tws);
         x = xLeft + xk * i;
         y = yBottom - yk * MAX (g * MS_TO_KN, tws);
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
         x = xLeft + xk * i;
         y = yBottom - yk * w;
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
         x = xLeft + xk * i;
         y = yBottom - yk * currTws;
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
      infoMessage ("Wind is constant !", GTK_MESSAGE_WARNING);
      return;
   }
   popoverFinish (menuWindow);
   Pp pt;
   pt.lat = yToLat (whereIsPopup.y);
   pt.lon = xToLon (whereIsPopup.x);
   snprintf (line, sizeof (line), \
      "Meteogram for %s %s beginning %s during %ld hours",\
      latToStr (pt.lat, par.dispDms, strLat, sizeof (strLat)),\
      lonToStr (pt.lon, par.dispDms, strLon, sizeof (strLon)),\
      newDate (zone.dataDate [0], (zone.dataTime [0]/100), totalDate, sizeof (totalDate)),\
      zone.timeStamp [zone.nTimeStamp -1]);

   GtkWidget *meteoWindow = gtk_application_window_new (GTK_APPLICATION (app));
   gtk_window_set_title (GTK_WINDOW(meteoWindow), line);
   gtk_window_set_default_size (GTK_WINDOW(meteoWindow), 1400, 400);
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), meteoWindow);
   
   GtkWidget *meteogramDrawingArea = gtk_drawing_area_new ();

   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (meteogramDrawingArea), onMeteogramEvent, NULL, NULL);
   gtk_window_set_child (GTK_WINDOW (meteoWindow), meteogramDrawingArea);
   gtk_window_present (GTK_WINDOW (meteoWindow));   
}

/*! submenu with label */
static void subMenu (GMenu *vMenu, const char *str, const char *app) {
   GMenuItem *menu_item = g_menu_item_new (str, app);
   g_menu_append_item (vMenu, menu_item);
   g_object_unref (menu_item);
}

/*! make separator in menu */
static void separatorMenu (GMenu *vMenu, int n, const char *app) {
   char *strSep = g_strnfill (n, '-');
   GMenuItem *menu_item = g_menu_item_new (strSep, app);
   g_menu_append_item (vMenu, menu_item);
   g_object_unref (menu_item);
   g_free (strSep); 
}

/*! launch meteoconsult request */
static void launchMeteoConsultRequest (GtkWidget *widget, gpointer theWindow, bool onlyDownload) {
   printf ("onlyDownload: %d\n", onlyDownload);
   gribRequestData.onlyDownload = onlyDownload;
   g_strlcat (gribRequestData.fileName, strrchr (gribRequestData.url, '/'), sizeof (gribRequestData.fileName));
   printf ("gribRequestData.fileName: %s\n", gribRequestData.fileName);
   waitMessage ("MeteoConsult Download and decoding", "Info coming...");

   g_atomic_int_set (&readGribRet, GRIB_RUNNING); 
   gribThread = g_thread_new ("readGribThread", getMeteoConsult, NULL);

   if (typeFlow == WIND)
      gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readGribCheck, NULL);
   else
      gribReadTimeout = g_timeout_add (READ_GRIB_TIME_OUT, readCurrentGribCheck, NULL);

   if ((theWindow != NULL) && GTK_IS_WINDOW (theWindow))
      gtk_window_destroy (GTK_WINDOW (theWindow));
}

/*! Manage response to meteoconsult Grib Request dialog box */
static void onOkButtonMeteoConsultRequestClicked (GtkWidget *widget, gpointer theWindow) {
   launchMeteoConsultRequest (widget, theWindow, false);
}

/*! Manage response to meteoconsult Grib Request dialog box */
static void onOnlyDownloadButtonMeteoConsultRequestClicked (GtkWidget *widget, gpointer theWindow) {
   launchMeteoConsultRequest (widget, theWindow, true);
}

/*! callback for url change */
static void entryUrlChange (GtkEditable *editable, gpointer userData) {
   const char *text = gtk_editable_get_text (editable);
   g_strlcpy (gribRequestData.url, text, MAX_SIZE_TEXT);
}

/*! Callback change zone for meteo consult */
static void cbZoneDropDown (GObject *dropDown, GParamSpec *pspec, gpointer userData) {
   gribRequestData.index = gtk_drop_down_get_selected (GTK_DROP_DOWN(dropDown));
   int delay = (gribRequestData.urlType == WIND) ? METEO_CONSULT_WIND_DELAY : METEO_CONSULT_CURRENT_DELAY;
   snprintf (gribRequestData.fileName, sizeof (gribRequestData.fileName), (typeFlow == WIND) ? "%sgrib": "%scurrentgrib", par.workingDir);
   gribRequestData.hhZ = buildMeteoConsultUrl (gribRequestData.urlType, gribRequestData.index, delay, gribRequestData.url, sizeof (gribRequestData.url));
   GtkEntryBuffer *buffer = gtk_entry_get_buffer (GTK_ENTRY (gribRequestData.urlEntry));
   gtk_entry_buffer_set_text (buffer, gribRequestData.url, -1);
}

/*! Grib URL selection for Meteo Consult */
static void gribMeteoConsult (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   typeFlow = GPOINTER_TO_INT (data);
   char str [MAX_SIZE_LINE] = "";
   GtkWidget *label;

   GtkWidget *gribMeteoConsultWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (gribMeteoConsultWindow), "MeteoConsult Request");
   gtk_widget_set_size_request (gribMeteoConsultWindow, 800, -1);
   g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), gribMeteoConsultWindow);

   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (gribMeteoConsultWindow), (GtkWidget *) vBox);

   GtkWidget *grid = gtk_grid_new();

   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
   gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);

   label = gtk_label_new ("Zone");
   // Create GlistStore to store strings 
   GtkStringList *stringList = gtk_string_list_new (NULL);

   if (typeFlow == WIND) {
      for (int i = 0;  i < N_METEO_CONSULT_WIND_URL; i++) {
         snprintf (str, sizeof (str), "%s", METEO_CONSULT_WIND_URL [i * 2]);
         gtk_string_list_append (stringList, str);
      }
   }
   else { // CURRENT
      for (int i = 0;  i < N_METEO_CONSULT_CURRENT_URL; i++) {
         snprintf (str, sizeof (str), "%s", METEO_CONSULT_CURRENT_URL [i * 2]);
         gtk_string_list_append (stringList, str);
      }
   }

   GtkWidget *dropDownZone= gtk_drop_down_new (G_LIST_MODEL(stringList), NULL);

   gtk_grid_attach (GTK_GRID(grid), label,        0, 0, 1, 1);
   gtk_grid_attach (GTK_GRID(grid), dropDownZone, 1, 0, 1, 1);
   g_signal_connect (dropDownZone, "notify::selected", G_CALLBACK (cbZoneDropDown), NULL);

   label = gtk_label_new ("Time Run");
   gtk_label_set_xalign(GTK_LABEL (label), 0.0);
   gtk_grid_attach (GTK_GRID(grid), label,        0, 1, 1, 1);

   gribRequestData.urlType = typeFlow;
   gribRequestData.index = 0;
   int delay = (gribRequestData.urlType == WIND) ? METEO_CONSULT_WIND_DELAY : METEO_CONSULT_CURRENT_DELAY;
   snprintf (gribRequestData.fileName, sizeof (gribRequestData.fileName), (typeFlow == WIND) ? "%sgrib": "%scurrentgrib", par.workingDir);
   gribRequestData.hhZ = buildMeteoConsultUrl (gribRequestData.urlType, gribRequestData.index, delay, gribRequestData.url, sizeof (gribRequestData.url));

   char *strHhZ = g_strdup_printf ("%02dZ", gribRequestData.hhZ);
   gribRequestData.hhZBuffer = gtk_label_new (strHhZ);
   g_free (strHhZ);
   gtk_label_set_xalign(GTK_LABEL (gribRequestData.hhZBuffer), 0.0);
   gtk_grid_attach (GTK_GRID (grid), gribRequestData.hhZBuffer , 1, 1, 1, 1);

   GtkEntryBuffer *buffer = gtk_entry_buffer_new (gribRequestData.url, -1);
   gribRequestData.urlEntry = gtk_entry_new_with_buffer (buffer);
   int min_width = MAX (300, strlen (gribRequestData.url) * 10);
   gtk_widget_set_size_request (gribRequestData.urlEntry, min_width, -1);
   g_signal_connect (gribRequestData.urlEntry, "changed", G_CALLBACK (entryUrlChange), NULL);

   // hBox definition with 3 buttons: OK, OnlyDownload, Cancel
   GtkWidget *hBox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *OKbutton = gtk_button_new_with_label ("OK");
   GtkWidget *onlyDownloadButton = gtk_button_new_with_label ("Only DownLoad");
   GtkWidget *cancelButton = gtk_button_new_with_label ("Cancel");
   gtk_box_append (GTK_BOX(hBox), OKbutton);
   gtk_box_append (GTK_BOX(hBox), onlyDownloadButton);
   gtk_box_append (GTK_BOX(hBox), cancelButton);
   g_signal_connect (OKbutton, "clicked", G_CALLBACK (onOkButtonMeteoConsultRequestClicked), gribMeteoConsultWindow);
   g_signal_connect (onlyDownloadButton, "clicked", G_CALLBACK (onOnlyDownloadButtonMeteoConsultRequestClicked), gribMeteoConsultWindow);
   g_signal_connect (cancelButton, "clicked", G_CALLBACK (onCancelButtonClicked), gribMeteoConsultWindow);

   gtk_box_append (GTK_BOX (vBox), grid);
   gtk_box_append (GTK_BOX (vBox), gribRequestData.urlEntry);
   gtk_box_append (GTK_BOX (vBox), hBox);

   gtk_window_present (GTK_WINDOW (gribMeteoConsultWindow));
}

/*! Callback exit */
static void quitActivated (GSimpleAction *action, GVariant *parameter, GApplication *application) {
   printf ("Quit...\n");
   if (G_IS_APPLICATION (application))
     g_application_quit (application);
   else {
      cleanAll ();
      exit (EXIT_SUCCESS);
   }
}

/*! Callback main window destroy */
static void onMainWindowdestroy (GtkWindow *window, gpointer data) {
   printf ("The End...\n");
   cleanAll ();
   exit (EXIT_SUCCESS);
}
   
/*! Callback polar_draw */
static void polarDrawActivated (GSimpleAction *action, GVariant *parameter, gpointer *data) {
   polarType = GPOINTER_TO_INT (data);
   polarDraw (); 
}

/*! create a button within toolBox, with an icon, tool tip description and a callback function */
static void createButton (GtkWidget *toolBox, const char *iconName, const char *description, void *callBack) { 
   GtkWidget *button = gtk_button_new_from_icon_name (iconName);
   g_signal_connect (button, "clicked", G_CALLBACK (callBack), NULL);
   gtk_widget_set_tooltip_text (button, description);
   gtk_box_append (GTK_BOX (toolBox), button);
}

/*! Update info GPS */ 
static gboolean updateGpsCallback (gpointer data) {
   char str [MAX_SIZE_LINE], strDate [MAX_SIZE_DATE], strLat [MAX_SIZE_NAME], strLon [MAX_SIZE_NAME];
   GtkWidget *label = GTK_WIDGET(data);
   if (my_gps_data.OK)
      snprintf (str, sizeof (str), "%s UTC   Lat: %s Lon: %s  COG: %.0lf°  SOG: %.2lf Kn",\
         epochToStr (my_gps_data.time, false, strDate, sizeof (strDate)),\
         latToStr (my_gps_data.lat, par.dispDms, strLat, sizeof (strLat)),\
         lonToStr (my_gps_data.lon, par.dispDms, strLon, sizeof (strLon)),\
         my_gps_data.cog, my_gps_data.sog);
   else 
      snprintf (str, sizeof (str), "   No GPS Info");
   gtk_label_set_text (GTK_LABEL(label), str);
   return G_SOURCE_CONTINUE;
}

/*! Callback to change time  */
static void onTimeScaleValueChanged (GtkScale *scale, gpointer data) {
   GtkWidget *label= GTK_WIDGET (data);
   char totalDate [MAX_SIZE_DATE];
   double value = gtk_range_get_value (GTK_RANGE (scale));

   theTime = value * zone.timeStamp [zone.nTimeStamp - 1] / MAX_TIME_SCALE;
   newDateWeekDayVerbose (zone.dataDate [0], (zone.dataTime [0]/100) + theTime, totalDate, sizeof (totalDate));
   //gtk_scale_clear_marks (scale);
   //gtk_scale_add_mark (scale, value, GTK_POS_TOP, totalDate);
   char *strInfo = g_strdup_printf ("%s   %3.2lf/%3ld", totalDate, theTime, zone.timeStamp[zone.nTimeStamp - 1]);
   gtk_label_set_text (GTK_LABEL(label), strInfo);
   g_free (strInfo);
   if (drawing_area != NULL)
      gtk_widget_queue_draw (drawing_area);
   
   statusBarUpdate (statusbar);
}

/*! Main window set up */
static void appActivate (GApplication *application) {
   // avoid having several instances
   GList *windows = gtk_application_get_windows (GTK_APPLICATION (app));
   if (windows) {
      // if a window allready exist, show it...
      GtkWindow *window = GTK_WINDOW (windows->data);
      gtk_window_present (window);
      return;
   }


   app = GTK_APPLICATION (application);
   window = gtk_application_window_new (app);
   titleUpdate ();
   gtk_window_set_default_size (GTK_WINDOW (window), MAIN_WINDOW_DEFAULT_WIDTH, MAIN_WINDOW_DEFAULT_HEIGHT);
   g_signal_connect(window, "destroy", G_CALLBACK (onMainWindowdestroy), application);
   gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (window), TRUE);
   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (window), vBox);

   GtkWidget *toolBox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);

   createButton (toolBox, "system-run", "Launch Routing", onRunButtonClicked);
   createButton (toolBox, "starred", "Choose Best Departure Time", onChooseDepartureButtonClicked);
   createButton (toolBox, "preferences-system", "Change Parameters", change);
   createButton (toolBox, "zoom-in", "Zoom In", onZoomInButtonClicked);
   createButton (toolBox, "zoom-out", "Zoom Out", onZoomOutButtonClicked);
   createButton (toolBox, "zoom-original", "Zoom Original", onZoomOriginalButtonClicked);
   createButton (toolBox, "pan-start-symbolic", "Left", onLeftButtonClicked);
   createButton (toolBox, "pan-up-symbolic", "Up", onUpButtonClicked);
   createButton (toolBox, "pan-down-symbolic", "Down", onDownButtonClicked);
   createButton (toolBox, "pan-end-symbolic", "End", onRightButtonClicked);
   createButton (toolBox, "find-location-symbolic", "Center", onCenterMap);
   createButton (toolBox, "edit-select-all", "Palette", paletteDraw);
   createButton (toolBox, "applications-engineering-symbolic", "Test Tools", testSelection);
   
   GtkWidget *gpsInfo = gtk_label_new (" GPS Info coming...");
   gtk_box_append (GTK_BOX (toolBox), gpsInfo);

   // label line for route info in bold red left jutified (Global Variable)
   labelInfoRoute = gtk_label_new ("");
   //gtk_label_set_justify (GTK_LABEL(labelInfoRoute), GTK_JUSTIFY_LEFT);
   PangoAttrList *attrs = pango_attr_list_new();
   PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
   pango_attr_list_insert (attrs, attr);
   attr = pango_attr_foreground_new (65535, 0, 0);      // red
   pango_attr_list_insert(attrs, attr);
   gtk_label_set_attributes (GTK_LABEL(labelInfoRoute), attrs);
   gtk_label_set_xalign(GTK_LABEL (labelInfoRoute), 0); // left justified

   // Create controlTimebar (Global Variable)
   GtkWidget *controlTimeBar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   createButton (controlTimeBar, "media-playback-pause", "Pause", onStopButtonClicked);
   createButton (controlTimeBar, "media-playback-start", "Start", onPlayButtonClicked);
   createButton (controlTimeBar, "media-playlist-repeat", "Loop", onLoopButtonClicked);
   createButton (controlTimeBar, "media-seek-backward", "Back", onRewardButtonClicked);
   createButton (controlTimeBar, "media-seek-forward", "Forward", onForwardButtonClicked);
   createButton (controlTimeBar, "emblem-urgent", "Now", onNowButtonClicked);
 
   // GtkScale widget creation
   timeScale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, MAX_TIME_SCALE, 1);
   gtk_widget_set_tooltip_text (timeScale, "Select Time");
   gtk_widget_set_size_request (timeScale, 800, -1);  // Adjust GtkScale size
   // Disable focusable for the scale to avoid keyboard interactions
   gtk_widget_set_focusable (GTK_WIDGET (timeScale), FALSE);
   // Add scale to the container
   gtk_box_append (GTK_BOX (controlTimeBar), GTK_WIDGET(timeScale));

   // label str info 
   char totalDate [MAX_SIZE_DATE];
   newDateWeekDayVerbose (zone.dataDate [0], (zone.dataTime [0]/100), totalDate, sizeof (totalDate));
   char *strInfo = g_strdup_printf("%s   %3.2lf/%3ld", totalDate, theTime, zone.timeStamp[zone.nTimeStamp - 1]);
   GtkWidget *timeInfo = gtk_label_new (strInfo);
   gtk_box_append (GTK_BOX (controlTimeBar), timeInfo);
   g_free (strInfo);
   g_signal_connect (timeScale, "value-changed", G_CALLBACK (onTimeScaleValueChanged), timeInfo);

   // Create statusbar (Global Variable)
   statusbar = gtk_label_new ("statusBar");             // global variable
   gtk_label_set_xalign(GTK_LABEL (statusbar), 0);      // left justified

   // Create drawing area (Global Variable)
   drawing_area = gtk_drawing_area_new();
   gtk_widget_set_hexpand(drawing_area, TRUE);
   gtk_widget_set_vexpand(drawing_area, TRUE);
   gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (drawing_area), drawGribCallback, NULL, NULL);

   // Create event controller for mouse motion
   GtkEventController *motion_controller = gtk_event_controller_motion_new ();
   g_signal_connect (motion_controller, "motion", G_CALLBACK (onMotionEvent), NULL);
   gtk_widget_add_controller (drawing_area, motion_controller);

   // Create left clic manager
   GtkGesture *click_gesture_left = gtk_gesture_click_new ();
   gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click_gesture_left), GDK_BUTTON_PRIMARY);
   gtk_widget_add_controller (drawing_area, GTK_EVENT_CONTROLLER (click_gesture_left));
   g_signal_connect (click_gesture_left, "pressed", G_CALLBACK (onLeftClickEvent), NULL);
   // Connexion du signal pour le relâchement du clic gauche
   g_signal_connect (click_gesture_left, "released", G_CALLBACK (onLeftReleaseEvent), NULL);   

   // Create right clic manager
   GtkGesture *click_gesture_right = gtk_gesture_click_new ();
   gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click_gesture_right), GDK_BUTTON_SECONDARY);
   GtkEventController *click_controller_right = GTK_EVENT_CONTROLLER (click_gesture_right);
   gtk_widget_add_controller (drawing_area, GTK_EVENT_CONTROLLER (click_controller_right));
   g_signal_connect (click_gesture_right, "pressed", G_CALLBACK (onRightClickEvent), NULL);

   // Create event controller for mouse scroll
   GtkEventController *scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
   gtk_widget_add_controller (drawing_area, scroll_controller);
   g_signal_connect(scroll_controller, "scroll", G_CALLBACK (onScrollEvent), NULL);

   // Create event controller for keyboard keys
   GtkEventController *key_controller = gtk_event_controller_key_new();
   gtk_widget_add_controller (window, key_controller); // window and not drawing_area required !!!
   g_signal_connect (key_controller, "key-pressed", G_CALLBACK (onKeyEvent), NULL);

   // Connext signal notiy to detect change in window (e.g. fullscreen)
   g_signal_connect (window, "notify", G_CALLBACK (onFullscreenNotify), NULL);

   g_timeout_add_seconds (GPS_TIME_INTERVAL, updateGpsCallback, gpsInfo);

   GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

   gtk_box_append (GTK_BOX (vBox), toolBox);
   gtk_box_append (GTK_BOX (vBox), labelInfoRoute);
   gtk_box_append (GTK_BOX (vBox), drawing_area);
   gtk_box_append (GTK_BOX (vBox), separator);
   gtk_box_append (GTK_BOX (vBox), controlTimeBar);
   gtk_box_append (GTK_BOX (vBox), statusbar);
   gtk_window_present (GTK_WINDOW (window));  
   //gtk_window_maximize (GTK_WINDOW (window)); 
}

/*! for app_startup */
static void createAction (const char *actionName, void *callBack) {
   GSimpleAction *actItem = g_simple_action_new (actionName, NULL);
   g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (actItem));
   g_signal_connect (actItem, "activate", G_CALLBACK (callBack), NULL);
}

/*! for app_startup */
static void createActionWithParam (const char *actionName, void *callBack, int param) {
   GSimpleAction *actItem = g_simple_action_new (actionName, NULL);
   g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (actItem));
   g_signal_connect_data (actItem, "activate", G_CALLBACK (callBack), GINT_TO_POINTER (param), NULL, 0);
}

/*! for app_startup */
static void createActionWithPt (const char *actionName, void *callBack, void *ptParam) {
    GSimpleAction *actItem = g_simple_action_new (actionName, NULL);
    g_action_map_add_action (G_ACTION_MAP(app), G_ACTION (actItem));
    g_signal_connect_data (actItem, "activate", callBack, ptParam, NULL, 0);
}

/*! Menu set up */
static void appStartup (GApplication *application) {
   if (setlocale (LC_ALL, "C") == NULL) {
      fprintf (stderr, "In appStartup: setlocale failed");
      return;
   }
   GtkApplication *app = GTK_APPLICATION (application);

   createActionWithParam ("GribOpen", openGrib, WIND);
   createActionWithParam ("GribInfo", gribInfo, WIND);
   createActionWithParam ("GribRequest", gribWeb, NOAA_WIND);
   createActionWithParam ("WindMeteoConsultSelect", gribMeteoConsult, WIND);
   createActionWithParam ("CurrentGribOpen", openGrib, CURRENT);
   createActionWithParam ("CurrentGribInfo", gribInfo, CURRENT);
   createActionWithParam ("CurrentMeteoConsultSelect", gribMeteoConsult, CURRENT);
   createActionWithParam ("GribRequestCurrent", gribWeb, MAIL_SAILDOCS_CURRENT);
   createAction ("CheckGribDump", checkGribDump);
   createAction ("Quit", quitActivated);

   createAction ("ScenarioOpen", openScenario);
   createAction ("ScenarioSettings", change);
   createAction ("ScenarioShow", parDump);
   createAction ("ScenarioSave", saveScenario);
   createAction ("ScenarioEdit", editScenario);

   createAction ("PolarOpen", openPolar);
   createActionWithParam ("PolarDraw", polarDrawActivated, WIND_POLAR);
   createActionWithParam ("WavePolarDraw", polarDrawActivated, WAVE_POLAR);

   createAction ("SimulationReport", simulationReport);
   createAction ("Dashboard", dashboard);

   createAction ("Isochrones", isocDump);
   createAction ("IsochronesDesc", isocDescDump);
   createAction ("RouteGram", routeGram);
   createAction ("SailRoute", routeDump);
   createAction ("SailHistory", routeHistory);
   createAction ("HistoryReset", historyReset);

   createAction ("OrthoReport", niceWayPointReport);
   createAction ("ExportWayPoints", exportWayPoints);

   createAction ("PoiDump", poiDump);
   createAction ("PoiEdit", poiEdit);
   
   createAction ("TraceAdd", traceAdd);
   createAction ("TraceReport", traceReport);
   createAction ("OpenTrace", openTrace);
   createAction ("NewTrace", newTrace);
   createAction ("EditTrace", editTrace);
   createAction ("ExportTrace", exportTrace);

   createAction ("PolygonDump", polygonDump);
   createAction ("CompetitorsDump", competitorsDump);
   createAction ("LogDump", logDump);
   createAction ("VirtualRegDashboardImport", virtualRegDashboardImport);
   createAction ("VirtualRegStaminaCalculator", virtualRegStaminaCalculator);

   createAction ("Nmea", nmeaConf);
   createAction ("Gps", gpsDump);
   createAction ("Ais", aisDump);

   createAction ("Windy", windy);
   createAction ("WindyApi", windyAPI);
   createAction ("GoogleMap", googleMap);
   createActionWithParam ("OSM0", openMap, 0); // open Street Map
   createActionWithParam ("OSM1", openMap, 1); // open Sea Map
   createAction ("Shom", shom);
   createAction ("VirtualRegatta", virtualRegatta);

   createAction ("Help", help);
   createActionWithPt ("InfoDump", fileDump, par.parInfoFileName);
   createActionWithPt ("CliDump", fileDump, par.cliHelpFileName);
   createAction ("Info", helpInfo);

   GMenu *menubar = g_menu_new ();
   GMenuItem *file_menu = g_menu_item_new ("_Grib", NULL);
   GMenuItem *polar_menu = g_menu_item_new ("_Polar", NULL);
   GMenuItem *scenario_menu = g_menu_item_new ("_Scenario", NULL);
   GMenuItem *route_menu = g_menu_item_new ("_Routes", NULL);
   GMenuItem *waypoint_menu = g_menu_item_new ("_Way Points", NULL);
   GMenuItem *poi_menu = g_menu_item_new ("PO_I", NULL);
   GMenuItem *trace_menu = g_menu_item_new ("_Trace", NULL);
   GMenuItem *misc_menu = g_menu_item_new ("_Misc.", NULL);
   GMenuItem *display_menu = g_menu_item_new ("_Display", NULL);
   GMenuItem *ais_gps_menu = g_menu_item_new ("_AIS-GPS", NULL);
   GMenuItem *web_menu = g_menu_item_new ("_Web-sites", NULL);
   GMenuItem *help_menu = g_menu_item_new ("_Help", NULL);

   // Add elements for "Grib" menu
   GMenu *file_menu_v = g_menu_new ();
   subMenu (file_menu_v, "Wind: Open Grib", "app.GribOpen");
   subMenu (file_menu_v, "Wind: Grib Info", "app.GribInfo");
   subMenu (file_menu_v, "Wind: Meteoconsult", "app.WindMeteoConsultSelect");
   subMenu (file_menu_v, "Wind: Grib Request", "app.GribRequest");
   separatorMenu (file_menu_v, SEP_WIDTH, "app.separator");
   subMenu (file_menu_v, "Current: Open Grib", "app.CurrentGribOpen");
   subMenu (file_menu_v, "Current: Grib Info", "app.CurrentGribInfo");
   subMenu (file_menu_v, "Current: Meteoconsult", "app.CurrentMeteoConsultSelect");
   subMenu (file_menu_v, "Current: Grib Request", "app.GribRequestCurrent");
   separatorMenu (file_menu_v, SEP_WIDTH, "app.separator");
   subMenu (file_menu_v, "Wind and Current check", "app.CheckGribDump");
   subMenu (file_menu_v, "Quit", "app.Quit");

   // Add elements for "Polar" menu
   GMenu *polar_menu_v = g_menu_new ();
   subMenu (polar_menu_v, "Polar or Wave Polar open", "app.PolarOpen");
   subMenu (polar_menu_v, "Polar Draw", "app.PolarDraw");
   subMenu (polar_menu_v, "Wave Polar Draw", "app.WavePolarDraw");

   // Add elements for "Scenario" menu
   GMenu *scenario_menu_v = g_menu_new ();
   subMenu (scenario_menu_v, "Open", "app.ScenarioOpen");
   subMenu (scenario_menu_v, "Settings", "app.ScenarioSettings");
   subMenu (scenario_menu_v, "Show", "app.ScenarioShow");
   subMenu (scenario_menu_v, "Save", "app.ScenarioSave");
   subMenu (scenario_menu_v, "Edit", "app.ScenarioEdit");
   
   // Add elements for "Routes" menu
   GMenu *route_menu_v = g_menu_new ();
   if (par.techno) {
      subMenu (route_menu_v, "Isochrones", "app.Isochrones");
      subMenu (route_menu_v, "Isochrones Descriptors", "app.IsochronesDesc");
   }
   subMenu (route_menu_v, "Routegram", "app.RouteGram");
   subMenu (route_menu_v, "Sail Route", "app.SailRoute");
   subMenu (route_menu_v, "Sail History Routes", "app.SailHistory");
   subMenu (route_menu_v, "Reset", "app.HistoryReset");

   // Add elements for "Way Points" menu
   GMenu *waypoint_menu_v = g_menu_new ();
   subMenu (waypoint_menu_v, "Way Points Report", "app.OrthoReport");
   subMenu (waypoint_menu_v, "Export GPX", "app.ExportWayPoints");

   // Add elements for "Poi" (Points of interest) menu
   GMenu *poi_menu_v = g_menu_new ();
   subMenu (poi_menu_v, "Find", "app.PoiDump");
   subMenu (poi_menu_v, "Edit PoI and Ports", "app.PoiEdit");

   // Add elements for "Trace" menu
   char strAdd [MAX_SIZE_LINE];
   snprintf (strAdd, MAX_SIZE_LINE, "Add %s", competitors.t [0].name); 
   GMenu *trace_menu_v = g_menu_new ();
   subMenu (trace_menu_v, strAdd, "app.TraceAdd");
   subMenu (trace_menu_v, "Report", "app.TraceReport");
   subMenu (trace_menu_v, "Open", "app.OpenTrace");
   subMenu (trace_menu_v, "New", "app.NewTrace");
   subMenu (trace_menu_v, "Edit", "app.EditTrace");
   subMenu (trace_menu_v, "Export GPX", "app.ExportTrace");

   // Add elements for "Misc" menu
   GMenu *misc_menu_v = g_menu_new ();
   subMenu (misc_menu_v, "Polygon Dump", "app.PolygonDump");
   subMenu (misc_menu_v, "Competitors Dump", "app.CompetitorsDump");
   subMenu (misc_menu_v, "Simulation Report", "app.SimulationReport");
   subMenu (misc_menu_v, "Log Dump", "app.LogDump");
   subMenu (misc_menu_v, "Virtual Regatta Dashboard Import", "app.VirtualRegDashboardImport");
   subMenu (misc_menu_v, "Virtual Regatta Stamina Calculator","app.VirtualRegStaminaCalculator");

   // Add elements for "Display" menu
   GMenu *display_menu_v = g_menu_new ();
   subMenu (display_menu_v, "Dashboard", "app.Dashboard");

   // Add elements for "ais gps" menu
   GMenu *ais_gps_menu_v = g_menu_new ();
   subMenu (ais_gps_menu_v, "NMEA Ports", "app.Nmea");
   subMenu (ais_gps_menu_v, "GPS", "app.Gps");
   subMenu (ais_gps_menu_v, "AIS", "app.Ais");

   // Add elements for "Web" menu
   GMenu *web_menu_v = g_menu_new ();
   subMenu (web_menu_v, "Windy", "app.Windy");
   subMenu (web_menu_v, "Windy API", "app.WindyApi");
   subMenu (web_menu_v, "Google Map", "app.GoogleMap");
   subMenu (web_menu_v, "Open Street Map", "app.OSM0");
   subMenu (web_menu_v, "Open Sea Map", "app.OSM1");
   subMenu (web_menu_v, "SHOM", "app.Shom");
   subMenu (web_menu_v, "Virtual Regatta", "app.VirtualRegatta");

   // Add elements for "Help" menu
   GMenu *help_menu_v = g_menu_new ();
   subMenu (help_menu_v, "Help", "app.Help");
   subMenu (help_menu_v, "Parameters Help", "app.InfoDump");
   subMenu (help_menu_v, "CLI mode", "app.CliDump");
   subMenu (help_menu_v, "Info", "app.Info");

   //g_menu_item_set_submenu (menu_item_menu, G_MENU_MODEL (menu));
   g_menu_item_set_submenu (file_menu, G_MENU_MODEL (file_menu_v));
   g_object_unref (file_menu_v);
   g_menu_item_set_submenu (polar_menu, G_MENU_MODEL (polar_menu_v));
   g_object_unref (polar_menu_v);
   g_menu_item_set_submenu (scenario_menu, G_MENU_MODEL (scenario_menu_v));
   g_object_unref (scenario_menu_v);
   g_menu_item_set_submenu (route_menu, G_MENU_MODEL (route_menu_v));
   g_object_unref (route_menu_v);
   g_menu_item_set_submenu (waypoint_menu, G_MENU_MODEL (waypoint_menu_v));
   g_object_unref (waypoint_menu_v);
   g_menu_item_set_submenu (poi_menu, G_MENU_MODEL (poi_menu_v));
   g_object_unref (poi_menu_v);
   g_menu_item_set_submenu (trace_menu, G_MENU_MODEL (trace_menu_v));
   g_object_unref (trace_menu_v);
   g_menu_item_set_submenu (misc_menu, G_MENU_MODEL (misc_menu_v));
   g_object_unref (misc_menu_v);
   g_menu_item_set_submenu (display_menu, G_MENU_MODEL (display_menu_v));
   g_object_unref (display_menu_v);
   g_menu_item_set_submenu (ais_gps_menu, G_MENU_MODEL (ais_gps_menu_v));
   g_object_unref (ais_gps_menu_v);
   g_menu_item_set_submenu (web_menu, G_MENU_MODEL (web_menu_v));
   g_object_unref (web_menu_v);
   g_menu_item_set_submenu (help_menu, G_MENU_MODEL (help_menu_v));
   g_object_unref (help_menu_v);

   g_menu_append_item (menubar, file_menu);
   g_menu_append_item (menubar, polar_menu);
   g_menu_append_item (menubar, scenario_menu);
   g_menu_append_item (menubar, route_menu);
   g_menu_append_item (menubar, waypoint_menu);
   g_menu_append_item (menubar, poi_menu);
   g_menu_append_item (menubar, trace_menu);
   g_menu_append_item (menubar, misc_menu);
   g_menu_append_item (menubar, display_menu);
   g_menu_append_item (menubar, ais_gps_menu);
   g_menu_append_item (menubar, web_menu);
   g_menu_append_item (menubar, help_menu);

   g_object_unref (file_menu);
   g_object_unref (polar_menu);
   g_object_unref (scenario_menu);
   g_object_unref (route_menu);
   g_object_unref (waypoint_menu);
   g_object_unref (poi_menu);
   g_object_unref (trace_menu);
   g_object_unref (misc_menu);
   g_object_unref (display_menu);
   g_object_unref (ais_gps_menu);
   g_object_unref (web_menu);
   g_object_unref (help_menu);

   gtk_application_set_menubar (GTK_APPLICATION (app), G_MENU_MODEL (menubar));
}

/*! clean all resources */
static void cleanAll () {
   freeDisplayTextResources ();
   g_hash_table_destroy (aisTable);
   freeSHP ();
   free (tIsSea);
   free (isoDesc);
   free (isocArray);
   free (route.t);
   freeHistoryRoute ();
   free (tGribData [WIND]); 
   free (tGribData [CURRENT]); 
   curl_global_cleanup();
}

/*! Display main menu and make initializations */
int main (int argc, char *argv[]) {
   bool ret = true;
   char directory [MAX_SIZE_DIR_NAME];
   char threadName [MAX_SIZE_NAME];

   if (curl_global_init (CURL_GLOBAL_DEFAULT) != 0) {
      fprintf (stderr, "In main, Error failed to initialize cURL.\n");
      return EXIT_FAILURE;
   }
   g_setenv ("GTK_A11Y", "none", TRUE); // mitigation problems of copy paste

   g_mutex_init (&warningMutex);

   aisTableInit ();

   vOffsetLocalUTC = offsetLocalUTC (); // difference in seconds between local time and UTC
   printf ("LocalTime - UTC: %.0lf hours\n", vOffsetLocalUTC / 3600.0);

   if (setlocale (LC_ALL, "C") == NULL) {              // very important for scanf decimal numbers
      fprintf (stderr, "In main, Error setlocale failed");
      return EXIT_FAILURE;
   }

   g_strlcpy (parameterFileName, PARAMETERS_FILE, sizeof (parameterFileName) - 1);
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
            g_strlcpy (parameterFileName, argv [1], sizeof (parameterFileName));
         }
         break;
      case 3: // two parameters
         if (argv [1][0] == '-') {
            ret = readParam (argv [2]);
            g_strlcpy (parameterFileName, argv [2], sizeof (parameterFileName));
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

   // one thread per port
   for (int i = 0; i < par.nNmea; i++) {
      snprintf (threadName, sizeof(threadName), "NMEA-%d", i);
      g_thread_new (threadName, getNmea, GINT_TO_POINTER (i));      // launch GPS or AIS
   }

   initZone (&zone);
   initDispZone ();
   
   if (par.mostRecentGrib) {                     // most recent grib will replace existing grib
      snprintf (directory, sizeof (directory), "%sgrib/", par.workingDir); 
      mostRecentFile (directory, ".gr", par.gribFileName, sizeof (par.gribFileName));
   }
   initScenario ();

   if (par.isSeaFileName [0] != '\0')
      readIsSea (par.isSeaFileName);
   updateIsSeaWithForbiddenAreas ();
   printf ("update isSea   : with Forbid Areas done\n");
   
   for (int i = 0; i < par.nShpFiles; i++) {
      initSHP (par.shpFileName [i]);
      printf ("SHP file loaded: %s\n", par.shpFileName [i]);
   }

   printf ("Working dir    : %s\n", par.workingDir); 
   printf ("poi File Name  : %s\n", par.poiFileName);
   printf ("portFile Name  : %s\n", par.portFileName);
   printf ("nPoi           : %d\n", nPoi);
  
   app = gtk_application_new (APPLICATION_ID, G_APPLICATION_DEFAULT_FLAGS);
   g_signal_connect (app, "startup", G_CALLBACK (appStartup), NULL); 
   g_signal_connect (app, "activate", G_CALLBACK (appActivate), NULL);
   ret = g_application_run (G_APPLICATION (app), 0, NULL);

   printf ("In main        : exit application\n");

   cleanAll ();
   g_object_unref (app);
   
   return ret;
}

