/*! 
   Download grib files from ECMWF or NOAA
   see synopsys : <mode (1=NOAA, 2=ECMWF)> <maxStep> <topLat> <leftLon> <bottomLat> <rightLon>
   Compilation: gcc -o r3gribget r3gribget.c -leccodes -lcurl `pkg-config --cflags --libs glib-2.0`
   calculate automatically the run depending on UTC tme and DELAY
*/

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <glib.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>

#define GRIB_DIR           "grib"
#define MAX_CMD            512
#define MAX_STEP           384   
#define MAX_SIZE_LINE      1024
#define SYNOPSYS           "<mode (1=NOAA, 2=ECMWF)> <maxStep> <topLat> <leftLon> <bottomLat> <rightLon>"

// NOAA parameters
#define NOAA_ROOT          "R3_NOAA_Inter"
#define NOAA_DELAY         4
#define NOAA_BASE_URL      "https://nomads.ncep.noaa.gov/cgi-bin/filter_gfs_0p25.pl"
#define MAX_STEP_NOAA      384 // 16 days

// ECMWF parameters
#define ECMWF_ROOT         "R3_ECMWF_Inter"
#define ECMWF_DELAY        10
#define ECMWF_BASE_URL     "https://data.ecmwf.int/forecasts"
#define ECMWF_SHORTNAMES   "10u/10v/gust"
#define MAX_STEP_ECMWF      240 // 10 days

enum {NOAA = 1, ECMWF = 2};

/*! remove all .tmp file with prefix */
void removeAllTmpFilesWithPrefix (const char *prefix) {
   char *directory = g_path_get_dirname (prefix); 
   char *base_prefix = g_path_get_basename (prefix);

   GDir *dir = g_dir_open (directory, 0, NULL);
   if (!dir) {
      fprintf (stderr, "In removeAllTmpFilesWithPrefix: Fail opening directory: %s", directory);
      g_free (directory);
      g_free (base_prefix);
      return;
   }
   const char *filename = NULL;
   while ((filename = g_dir_read_name (dir))) {
      if (g_str_has_prefix (filename, base_prefix) && g_str_has_suffix(filename, ".tmp")) {
         char *filepath = g_build_filename (directory, filename, NULL);
         remove (filepath);
         g_free (filepath);
     }
   }
   g_dir_close(dir);
   g_free(directory);
   g_free(base_prefix);
}

/*! treatment for ECMWF or ARPEGE or AROME files. DO NO USE FOR NOAA: USELESS because already reduced and may bug.
   Select shortnames and subregion defined by lon and lat values and produce output file
   return true if all OK */
static bool reduce (const char *dir, const char *inFile, const char *shortNames, double latMax, double lonLeft, double latMin, double lonRight, const char *outFile) {
   char toRemove [MAX_SIZE_LINE], toRemove2 [MAX_SIZE_LINE];
   const char* tempCompact = "compacted.tmp";
   const char* tempCompact2 = "compacted2.tmp";
   char command [MAX_SIZE_LINE];
   char fullFileName [MAX_SIZE_LINE];
   snprintf (toRemove, sizeof (toRemove),"%s/%s", dir, tempCompact);
   snprintf (toRemove2, sizeof (toRemove2),"%s/%s", dir, tempCompact2);
   snprintf (command, MAX_SIZE_LINE, "grib_copy -w shortName=%s %s %s/%s", shortNames, inFile, dir, tempCompact);
   printf ("Command: %s\n", command);
   if (system(command) != 0) {
      fprintf (stderr, "In reduce, Error call: %s\n", command);
      remove (toRemove);
      return false;
   }

   snprintf (fullFileName, MAX_SIZE_LINE, "%s/%s", dir, tempCompact);
   if (access (fullFileName, F_OK) != 0) {
      fprintf (stderr, "In reduce, Error: file does no exist: %s\n", fullFileName);
      remove (toRemove);
      return false;
   }

   snprintf (command, MAX_SIZE_LINE, "cdo -sellonlatbox,%.2lf,%.2lf,%.2lf,%.2lf %s/%s %s/%s",  
      lonLeft, lonRight, latMin, latMax, dir, tempCompact, dir, tempCompact2);

   printf ("First cdo: %s\n", command);
   //cdo -sellonlatbox,-25.0,-1.0,20.0,50.0 grib/compacted.tmp grib/output.grb

   if (system (command) != 0) {
      fprintf (stderr, "In reduce, Error call: %s\n", command);
      remove (toRemove);
      remove (toRemove2);
      return false;
   }
   snprintf (command, MAX_SIZE_LINE, "cdo -invertlat %s/%s %s", dir, tempCompact2, outFile);
   printf ("Second cdo: %s\n", command);
   if (system (command) != 0) {
      fprintf (stderr, "In reduce, Error call: %s\n", command);
      remove (toRemove);
      remove (toRemove2);
      return false;
   }
   //cdo -invertlat extract.grib extract0.grib
   remove (toRemove);
   remove (toRemove2);
   return true;
}

/*! Check if a file exists and is non-empty */
static bool fileExists (const char *filename) {
   struct stat buffer;
   return (stat (filename, &buffer) == 0 && buffer.st_size > 0);
}

/*! Download a file using cURL */
static bool downloadFile (const char *url, const char *output) {
   CURL *curl = curl_easy_init();
   if (!curl) {
       fprintf(stderr, "❌ Error: Unable to initialize cURL.\n");
       return false;
   }

   printf("📥 Downloading: %s\n", url);

   FILE *fp = fopen(output, "wb");
   if (!fp) {
       fprintf(stderr, "❌ Error: Unable to create file %s\n", output);
       curl_easy_cleanup(curl);
       return false;
   }

   curl_easy_setopt (curl, CURLOPT_URL, url);
   curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, NULL);
   curl_easy_setopt (curl, CURLOPT_WRITEDATA, fp);
   curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L);
   curl_easy_setopt (curl, CURLOPT_FAILONERROR, 1L);

   CURLcode res = curl_easy_perform(curl);
   fclose (fp);
   curl_easy_cleanup (curl);

   if (res != CURLE_OK) {
      fprintf(stderr, "⚠️ Download failed: %s\n", url);
      remove (output); // Remove empty or broken file
      return false;
   }

   printf ("✅ Download successful: %s\n", output);
   return true;
}

/*! Get the latest NOAA or ECMWF run datetime */
static void getRunDatetime (int mode, int delay, char *YYYYMMDD, char *HH) {
   time_t now = time(NULL);
   struct tm t = *gmtime(&now); // Copie locale de la structure tm
    
   // Apply delay
   t.tm_hour -= delay;
    
   // Normalize timegm() to manage day month year change
   time_t adj_time = timegm(&t);
   t = *gmtime(&adj_time);
   
   // Calculate last run acailable
   if (mode == ECMWF) 
      t.tm_hour = (t.tm_hour / 12) * 12; // ECMWF : 0 ou 12
   else 
      t.tm_hour = (t.tm_hour / 6) * 6;   // NOAA : 0, 6, 12 ou 18

   strftime(YYYYMMDD, 9, "%Y%m%d", &t);
   snprintf(HH, 3, "%02d", t.tm_hour);
}

/*! Concatenate reduced GRIB files into a single file */
static void concatenateGrib (const char *root, const char *prefix, const char *YYYYMMDD, const char *HH, int lastStep) {
   char finalFile [MAX_SIZE_LINE];
   char toRemove [MAX_SIZE_LINE];
   snprintf (finalFile, sizeof(finalFile), "%s/R3_%s_%s_%s_%03d.grb", GRIB_DIR, prefix, YYYYMMDD, HH, lastStep);

   printf ("🔄 Concatenating files into %s\n", finalFile);
   FILE *fOut = fopen (finalFile, "wb");
   if (!fOut) {
      fprintf(stderr, "❌ Error: Unable to create final file %s\n", finalFile);
      return;
   }

   for (int step = 0; step <= MAX_STEP; step++) {
      char inputFile [MAX_SIZE_LINE];
      snprintf (inputFile, sizeof (inputFile), "%s/%s_reduced_%03d.tmp", GRIB_DIR, root, step);
      // printf ("concat: %s\n", inputFile);

      if (!fileExists (inputFile)) continue;

      FILE *fIn = fopen (inputFile, "rb");
      if (!fIn) {
         fprintf (stderr, "⚠️ Warning: Could not open %s\n", inputFile);
         continue;
      }

      char buffer [4096];
      size_t bytes;
      while ((bytes = fread (buffer, 1, sizeof (buffer), fIn)) > 0) {
         fwrite (buffer, 1, bytes, fOut);
      }

      fclose (fIn);
   }

   fclose (fOut);
   snprintf (toRemove, sizeof (toRemove),"%s/%s", GRIB_DIR, root);
   removeAllTmpFilesWithPrefix (toRemove);
   printf ("✅ Concatenation completed.\n");
}

/*! Process NOAA GRIB downloads with maxStep limit */
static void fetchNoaa (int maxStep, double topLat, double leftLon, double bottomLat, double rightLon) {
   char yyyymmdd [9], hh [3];
   getRunDatetime (NOAA, NOAA_DELAY, yyyymmdd, hh);
   maxStep = MIN (maxStep, MAX_STEP_NOAA);

   printf("📅 NOAA Run selected: %s/%s (Max Step: %d)\n", yyyymmdd, hh, maxStep);

   for (int step = 0; step <= maxStep; step += (step >= 120 ? 3 : 1)) {
      char fileUrl [1024], outputFile [256], reducedFile [256];

      snprintf (fileUrl, sizeof (fileUrl),
             "%s?file=gfs.t%sz.pgrb2.0p25.f%03d&dir=/gfs.%s/%s/atmos"
             "&subregion=&toplat=%.2f&leftlon=%.2f&rightlon=%.2f&bottomlat=%.2f"
             "&var_GUST=on&var_UGRD=on&var_VGRD=on"
             "&lev_10_m_above_ground=on&lev_surface=on&lev_mean_sea_level=on",
             NOAA_BASE_URL, hh, step, yyyymmdd, hh, topLat, leftLon, rightLon, bottomLat);

      snprintf (outputFile, sizeof (outputFile), "%s/%s_%03d.tmp", GRIB_DIR, NOAA_ROOT, step);
      snprintf (reducedFile, sizeof (reducedFile), "%s/%s_reduced_%03d.tmp", GRIB_DIR, NOAA_ROOT, step);

      if (!downloadFile (fileUrl, outputFile)) {
         printf("⚠️ Download failed at step %d, stopping further downloads.\n", step);
         maxStep = step;
         remove (outputFile);
         break;
      }
      rename (outputFile, reducedFile);
   }
   concatenateGrib (NOAA_ROOT, "NOAA",  yyyymmdd, hh, maxStep);
}

/*! Process ECMWF GRIB downloads with maxStep limit */
static void fetchEcmwf (int maxStep, double topLat, double leftLon, double bottomLat, double rightLon) {
   char yyyymmdd [9], hh [3];
   getRunDatetime (ECMWF, ECMWF_DELAY, yyyymmdd, hh);

   maxStep = MIN (maxStep, MAX_STEP_ECMWF);

   printf ("📅 ECMWF Run selected: %s/%s (Max Step: %d)\n", yyyymmdd, hh, maxStep);

   for (int step = 0; step <= maxStep; step += (step >= 144 ? 6 : 3)) {
      char fileUrl [1024], outputFile [256], reducedFile [256];

      snprintf(fileUrl, sizeof(fileUrl),
             "%s/%s/%sz/ifs/0p25/oper/%4d%02d%02d%02d0000-%dh-oper-fc.grib2",
             ECMWF_BASE_URL, yyyymmdd, hh,
             atoi (yyyymmdd) / 10000, atoi (yyyymmdd) % 10000 / 100, atoi (yyyymmdd) % 100,
             atoi (hh), step);

      snprintf(outputFile, sizeof (outputFile), "%s/%s_%03d.tmp", GRIB_DIR, ECMWF_ROOT, step);
      snprintf(reducedFile, sizeof (reducedFile), "%s/%s_reduced_%03d.tmp", GRIB_DIR, ECMWF_ROOT, step);

      if (!downloadFile(fileUrl, outputFile)) {
         printf("⚠️ Download failed at step %d, stopping further downloads.\n", step);
         maxStep = step;
         break;
      }
      reduce (GRIB_DIR, outputFile, ECMWF_SHORTNAMES, topLat, leftLon, bottomLat, rightLon, reducedFile);
   }
   concatenateGrib (ECMWF_ROOT, "ECMWF", yyyymmdd, hh, maxStep);
}

int main(int argc, char *argv[]) {
   if (argc != 7) {
      fprintf (stderr, "Usage: %s %s\n", argv[0], SYNOPSYS);
      return EXIT_FAILURE;
   }

   int mode = atoi (argv[1]);
   int maxStep = atoi (argv[2]);
   double topLat = atof (argv[3]);
   double leftLon = atof (argv[4]);
   double bottomLat = atof (argv[5]);
   double rightLon = atof (argv[6]);

   g_mkdir_with_parents (GRIB_DIR, 0755);

   if (mode == 1)
     fetchNoaa (maxStep, topLat, leftLon, bottomLat, rightLon);
   else if (mode == 2)
     fetchEcmwf (maxStep, topLat, leftLon, bottomLat, rightLon);
   else {
      fprintf (stderr, "Invalid mode: Use 1 for NOAA or 2 for ECMWF.\n");
      return EXIT_FAILURE;
   }  
   printf ("✅ Processing completed.\n");
   return EXIT_SUCCESS;
}

