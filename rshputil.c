#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include "rtypes.h"
#include "shapefil.h"

/*! for shp file */
int  nTotEntities = 0;              // cumulated number of entities
Entity *entities = NULL;

/*! build entities based on .shp file */
bool initSHP (const char* nameFile) {
   int nEntities = 0;         // number of entities in current shp file
   int nShapeType;
   int k = 0;
   int nMaxPart = 0;
   double minBound[4], maxBound[4];
   SHPHandle hSHP = SHPOpen (nameFile, "rb");
   if (hSHP == NULL) {
      fprintf (stderr, "In InitSHP, Error: Cannot open Shapefile: %s\n", nameFile);
      return false;
   }

   SHPGetInfo (hSHP, &nEntities, &nShapeType, minBound, maxBound);
   printf ("Geo nEntities  : %d, nShapeType: %d\n", nEntities, nShapeType);
   printf ("Geo limits     : %.2lf, %.2lf, %.2lf, %.2lf\n", minBound[0], minBound[1], maxBound[0], maxBound[1]);

   Entity *temp = (Entity *) realloc (entities, sizeof(Entity) * (nEntities + nTotEntities));
   if (temp == NULL) {
      fprintf (stderr, "In initSHP, Error realloc failed\n");
      free (entities); // Optionnal if entities is NULL at beginning, but sure..
      return false;
   } else {
      entities = temp;
   }

   for (int i = 0; i < nEntities; i++) {
      SHPObject *psShape = SHPReadObject(hSHP, i);
      if (psShape == NULL) {
         fprintf (stderr, "In initSHP, Error Reading entity: %d\n", i);
         continue;
      }
      k = nTotEntities + i;

      entities[k].numPoints = psShape->nVertices;
      entities[k].nSHPType = psShape->nSHPType;
      if ((psShape->nSHPType == SHPT_POLYGON) || (psShape->nSHPType == SHPT_POLYGONZ) || (psShape->nSHPType == SHPT_POLYGONM) ||
          (psShape->nSHPType == SHPT_ARC) || (psShape->nSHPType == SHPT_ARCZ) || (psShape->nSHPType == SHPT_ARCM)) {
         entities[k].latMin = psShape->dfYMin;
         entities[k].latMax = psShape->dfYMax;
         entities[k].lonMin = psShape->dfXMin;
         entities[k].lonMax = psShape->dfXMax;  
      }    

      // printf ("Bornes: %d, LatMin: %.2lf, LatMax: %.2lf, LonMin: %.2lf, LonMax: %.2lf\n", k, entities [i].latMin, entities [i].latMax, entities [i].lonMin, entities [i].lonMax);
      if ((entities[k].points = malloc (sizeof(Point) * entities[k].numPoints)) == NULL) {
         fprintf (stderr, "In initSHP, Error: malloc entity [k].numPoints failed with k = %d\n", k);
         free (entities);
         return false;
      }
      for (int j = 0; (j < psShape->nParts) && (j < MAX_INDEX_ENTITY); j++) {
         if (j < MAX_INDEX_ENTITY)
            entities[k].index [j] = psShape->panPartStart[j];
         else {
            fprintf (stderr, "In InitSHP, Error MAX_INDEX_ENTITY reached: %d\n", j);
            break;
         } 
      }
      entities [k].maxIndex = psShape->nParts;
      if (psShape->nParts > nMaxPart) 
         nMaxPart = psShape->nParts;

      for (int j = 0; j < psShape->nVertices; j++) {
         // mercatorToLatLon (psShape->padfY[j], psShape->padfX[j], &entities[k].points[j].lat, &entities[k].points[j].lon);
         entities[k].points[j].lon = psShape->padfX[j];
         entities[k].points[j].lat = psShape->padfY[j];
      }
         
      SHPDestroyObject(psShape);
   }
   SHPClose(hSHP);
   nTotEntities += nEntities;
   // printf ("nMaxPart = %d\n", nMaxPart);
   return true;
}

/*! free memory allocated */
void freeSHP (void) {
   for (int i = 0; i < nTotEntities; i++) {
      free (entities[i].points);
   }
   free(entities);
}

