#Introduction

Routing est un logiciel de routage. Il calcule la route allant d’un point origine pOr à un point destination pDest à partir d’une date startTime en utilisant les fichiers météo Grib et la polaire du bateau.

#Ajouts dans la version du 10 février 2024

- gestion nouvelle des isochrones. On peut retrouver l'ancien algo en positionnant OPT:1 ou OPT:2 (fichier .par et boite de dialogue Setting (change))
- menu Grib / Wind:Meteoconsult Grib / Current: Meteoconsult : permet de sélectionner plusieurs zones et calcule avec 
plus de précision le nom du fichier à charger xxZ 
- clic droit : permet maintenant d'afficher un Météogramme (vent, rafales, vagues, courant) à condirion d'avoir les fichiers grib ad-hoc.
Permet toujours de choisir l'origine et la destination, d'insérer des PoI et Way points.
- double clic lorque isochrones présents : permet le recalcul de la route vers une sortie d'isochrones autre que celle proposée initialement.
- affiche les infos de manières plus esthétiques pour : Grib/Wind grib info, Grib / Current grib info, report, Display/Sail report, Display/ GPS, POI/dump
- Polaire : ajout d'un Widget Scale qui permet l'affichage dynamique de la polaire en fonction du vent.
- Format des latitudes et longitudes. Elles pouvaient déjà être affichées en basic en degré, en degré minutes ou en degré minutes secondes (boite de dialogie Setting, onglet Display...) ou toujours via le fichier .par
On peut aussi les saisir maintenant dans n'importe quel format dans les fichiers .par (et poi) et aussi dans la boite de dialogue Settings (change) pour pOr et pDest.

