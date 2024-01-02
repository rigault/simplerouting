/*! compilation gcc -Wall -c curlutil.c -lcurl */
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>

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

