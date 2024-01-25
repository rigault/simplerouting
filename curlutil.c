/*! compilation gcc -Wall -c curlutil.c -lcurl */
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>
#include <math.h>
#include <string.h>
#include "rtypes.h"
#include "rutil.h"

#define FROM_ADDR    "<meteoinfoforrr@orange.fr>"
#define FROM_MAIL "RENE RIGAULT " FROM_ADDR
#define TO_MAIL   "GFS"
#define SMTP_SERVER  "smtp://smtp.orange.fr"

static char payload_text [1024]; 

struct upload_status {
  size_t bytes_read;
};

static size_t payload_source (char *ptr, size_t size, size_t nmemb, void *userp) {
  struct upload_status *upload_ctx = (struct upload_status *)userp;
  const char *data;
  size_t room = size * nmemb;
  if((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
    return 0;
  }
  data = &payload_text[upload_ctx->bytes_read];
  if(data) {
    size_t len = strlen(data);
    if(room < len)
      len = room;
    memcpy(ptr, data, len);
    upload_ctx->bytes_read += len;
    return len;
  }
  return 0;
}

static void smtpRequest (char *toAddr, char *subject, char *body) {
  CURL *curl;
  CURLcode res = CURLE_OK;
  struct curl_slist *recipients = NULL;
  struct upload_status upload_ctx = { 0 };
  curl = curl_easy_init();
  if(curl) {
    /* This is the URL for your mailserver */
    curl_easy_setopt(curl, CURLOPT_URL, SMTP_SERVER);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, FROM_ADDR);
    recipients = curl_slist_append(recipients, toAddr);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    
    sprintf (payload_text, "To: %s\r\nFrom: %s\r\nSubject: %s\r\n\r\n%s\r\n", 
      TO_MAIL, FROM_MAIL, subject, body);
 
    /* Send the message */
    res = curl_easy_perform(curl);
    /* Check for errors */
    if (res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    /* Free the list of recipients */
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
  }
}

/*! send SMTP request wih right parameters to grib mail provider */
void smtpGribRequestCurl (int type, double lat1, double lon1, double lat2, double lon2) {
   int i;
   char temp [MAX_SIZE_LINE];
   if (lon1 > 180) lon1 -= 360;
   if (lon2 > 180) lon2 -= 360;
   char *tWho [] = {"gfs", "ECMWF", "ICON", "RTOFS"};
   char body [MAX_SIZE_BUFFER] = "";
   char subject [MAX_SIZE_BUFFER] = "";
   char *suffix = "WIND,WAVES"; 
   switch (type) {
   case SAILDOCS_GFS: 
   case SAILDOCS_ECMWF: 
   case SAILDOCS_ICON: 
   case SAILDOCS_CURR:
      printf ("smtp saildocs curl with: %s\n", tWho [type]);
      if (type == SAILDOCS_CURR) strcpy (suffix, "CURRENT");
      sprintf (body, "send %s:%d%c,%d%c,%d%c,%d%c|%.1lf,%.1lf|0,%d,..%d|%s\n", tWho [type],\
         (int) fabs (round(lat1)), (lat1 > 0) ? 'N':'S', (int) fabs (round(lat2)), (lat2 > 0) ? 'N':'S',\
		   (int) fabs (round(lon1)), (lon1 > 0) ? 'E':'W', (int) fabs (round(lon2)), (lon2 > 0) ? 'E':'W',\
		   par.gribLatStep, par.gribLonStep, par.gribTimeStep, par.gribTimeMax, suffix);
      printf ("in C : %s", body);
      smtpRequest (par.smtpTo [type], "grib", body);
      break;
   case MAILASAIL:
      printf ("smtp mailasail curl\n");
      sprintf (subject, "grib gfs %d%c:%d%c:%d%c:%d%c ", 
         (int) fabs (round(lat1)), (lat1 > 0) ? 'N':'S', (int) fabs (round(lon1)), (lon1 > 0) ? 'E':'W',\
         (int) fabs (round(lat2)), (lat2 > 0) ? 'N':'S', (int) fabs (round(lon2)), (lon2 > 0) ? 'E':'W');
      for (i = 0; i < par.gribTimeMax; i+= par.gribTimeStep) {
         sprintf (temp, "%d,", i);
	      strcat (subject, temp);
      }
      sprintf (temp, "%d GRD,WAVE", i);
      strcat (subject, temp);   
      smtpRequest (par.smtpTo [type], subject, "");
      break;
   default:;
   }
}

static size_t WriteCallback (void* contents, size_t size, size_t nmemb, void* userp) {
   FILE* fp = (FILE*)userp;
   return fwrite (contents, size, nmemb, fp);
}


/*! return true if URL has been dowlmoaded */
bool curlGet (char *url, char* outputFile) {
   long http_code;
   CURL* curl = curl_easy_init();
   if (!curl) {
      fprintf (stderr, "In curlGet,  Error: impossible to initialize curl\n");
      return false;
   }
   FILE* fp = fopen (outputFile, "wb");
   if (!fp) {
      fprintf (stderr, "In curlGet, Error: opening ouput file: %s\n", outputFile);
      return false;
   }

   curl_easy_setopt(curl, CURLOPT_URL, url);
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

   CURLcode res = curl_easy_perform(curl);
   
   if (res != CURLE_OK) {
      fprintf(stderr, "In curlGet, Error downloading: %s\n", curl_easy_strerror(res));
   }
   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
   curl_easy_cleanup(curl);
   fclose(fp);
   if (http_code >= 400 || res != CURLE_OK) {
      fprintf(stderr, "In curlget, Error HTTP response code: %ld\n", http_code);
      return false;
   }
   return true;
}

