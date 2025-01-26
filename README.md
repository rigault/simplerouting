
# RCube

RCube est un logiciel de routage. Il calcule la route allant d’un point origine `pOr` à un point destination `pDest` à partir d’une date `START_TIME` en utilisant les fichiers météo Grib et la polaire du bateau.

## Méthode des isochrones

La méthode utilisée est celle des isochrones. À partir du point Origine `pOr`, on calcule tous les points accessibles dans un intervalle de temps `T_STEP`. C’est l’isochrone 0. Puis à partir de cet isochrone 0, on calcule tous les points accessibles dans l’intervalle `T_STEP`. On réitère jusqu’à ce que l’on soit arrivé à la destination `pDest` lorsque c’est possible (destination atteinte). Si la couverture de temps météo est insuffisante, on arrête le processus (destination non atteinte).

On propose alors une route qui est soit la route optimale allant de `pOr` à `pDest`, soit la route allant de `pOr` au point du dernier isochrone le plus proche de la destination.

## Fichiers Grib

Les fichiers Grib (General Regularly-distributed Information in Binary) contiennent, pour un ensemble de coordonnées (lat, lon), des valeurs météorologiques.

Les valeurs qui influencent le routage sont :
- Le vent à 10 mètres de hauteur,
- Les vagues.

Il existe aussi des fichiers Grib qui donnent la valeur des courants. Ils sont également utilisables par RCube.

## La polaire

La polaire décrit la vitesse du bateau en fonction de :
- La vitesse du vent réel (`TWS` : True Wind Speed),
- L’angle du vent avec l’axe du bateau (`TWA` : True Wind Angle).

Il existe également des polaires de vagues qui décrivent, en fonction de la hauteur des vagues et de leur angle avec le bateau, un coefficient multiplicateur de la vitesse du bateau.  
Typiquement :
- Des vagues face au bateau diminueront sa vitesse (exemple : 60 %),
- Des vagues arrière l’augmenteront (exemple : 120 %).

## Paramètres influençant le routage

- `DAY_EFFICIENCY` et `NIGHT_EFFICIENCY` : Coefficients traduisant la capacité de l’équipage. Par exemple, une valeur de 80 % indique que la vitesse prise en compte sera 80 % de celle donnée par la polaire. Ces paramètres permettent de différencier jour et nuit.
- `THRESHOLD` : Définit la vitesse à la voile en dessous de laquelle le bateau passe au moteur. La vitesse moteur est définie par le paramètre `MOTOR_S`.
- `X_WIND` : Multiplicateur de la vitesse du vent. Exemple : si le vent à prendre en compte est 20 % supérieur à celui des fichiers Grib, on donne à `X_WIND` la valeur `1.2`.
- `MAX_WIND` : Vitesse maximale de vent. Le routage privilégiera une route évitant les vents dont la vitesse excède ce seuil.
- `PENALTY0` : Nombre de minutes perdues lors d’un virement de bord.
- `PENALTY1` : Nombre de minutes perdues lors d’un empannage.
- `PENALTY2` : Nombre de minutes perdues lors d’un changement de voile.
- `RANGE_COG` : Amplitude de -90° à +90° par rapport à la route directe vers la destination.
- `COG_STEP` : Granularité (exemple : 5 degrés).

## Abréviations

- **Lat** : Latitude  
- **Lon** : Longitude  
- **pOr** : Point Origine  
- **pDest** : Point Destination  

## Acronymes

- **TWS** : True Wind Speed  
- **TWD** : True Wind Direction  
- **TWA** : True Wind Angle (angle du bateau par rapport au vent)  
- **SOG** : Speed over Ground (vitesse sur le fond)  
- **COG** : Course over Ground (cap sur le fond)  
- **AWA** : Apparent Wind Angle  
- **AWS** : Apparent Wind Speed  
- **HDG** : Heading (angle de l’axe du bateau avec le nord)  
- **VMG** : Velocity Made Good  
- **VMC** : Velocity Made on Course  

**Au près** : `VMG = cos(TWA) * TWS`.

## Distances et vitesses

- Les distances sont en miles nautiques.  
- Les vitesses sont en nœuds.  
- Les fichiers Grib donnent les vitesses en mètres/seconde.
