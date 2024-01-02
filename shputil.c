/*! compilation ! gcc -Wall -c shputil.c -lshp */
#include <stdlib.h>
#include <stdio.h>
#include "shapefil.h"
#include "rtypes.h"

int nEntities = 0;      // number of entities in current shp file
int nTotEntities = 0;   // cumulated number of entities
Entity *entities;

/*! build entities based on .shp file */
bool initSHP (char* nameFile) {
   int nShapeType;
   int k;
   SHPHandle hSHP = SHPOpen (nameFile, "rb");
   if (hSHP == NULL) {
      printf("Cannot open Shapefile: %s\n", nameFile);
      return false;
   }

   SHPGetInfo(hSHP, &nEntities, &nShapeType, NULL, NULL);
   printf ("In initSHP, nEntities: %d, nShapeType: %d\n", nEntities, nShapeType);

   if (nTotEntities == 0) entities = malloc(sizeof(Entity) * nEntities); 
   // ATTENTION NE MARCHE QUE POUR 1 SHP
   for (int i = 0; i < nEntities; i++) {
      SHPObject *psShape = SHPReadObject(hSHP, i);
      if (psShape == NULL) {
         printf("Erreur lors de la lecture de l'entité %d\n", i);
         continue;
      }
      k = nTotEntities + i;

      entities[k].numPoints = psShape->nVertices;
      entities[k].nSHPType = psShape->nSHPType;
      entities[k].points = malloc(sizeof(Point) * entities[k].numPoints);

      for (int j = 0; j < psShape->nVertices; j++) {
         entities[k].points[j].lon = psShape->padfX[j];
         entities[k].points[j].lat = psShape->padfY[j];
      }
      SHPDestroyObject(psShape);
   }
   SHPClose(hSHP);
   nTotEntities += nEntities;
   return true;
}

/*! free memory allocated */
void freeSHP () {
   for (int i = 0; i < nEntities; i++) {
      free (entities[i].points);
   }
   free(entities);
}

