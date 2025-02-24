const boatName = "pistache";
const REQ_TYPE = 3;
let routeParam = {};
routeParam.isoStep = 3600 // 1 h;
routeParam.startTime = new Date ();
routeParam.model = 0;
routeParam.forbid =  0;
routeParam.polar = "";
let rCubeInfo = "© René Rigault";
let index = 0;
const options = {
   key: rCubeKey,
   lat: 60.0,                                
   lon: 0.0,
   zoom: 4,
};
let bounds;
let animation;
let marker;
let map;
let store;
let pOr = [47, -2];
let pDest = [46, -5];
let loxoRoute = null;
let origine = null;
let destination = null;
// Initialisation des tables
let POIs = [];
let myWayPoints = [];
myWayPoints.push (pOr);
myWayPoints.push (pDest);
let polylineMyWayPoints = null; // Stocke la ligne
let route = null;  // Variable globale pour stocker la route
let isochroneLayerGroup;

const MARKER = encodeURIComponent(`<?xml version="1.0" encoding="UTF-8" standalone="no"?>
        <!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">
        <svg width="100%" height="100%" viewBox="0 0 14 14" version="1.1" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" xml:space="preserve" style="fill-rule:evenodd;clip-rule:evenodd;stroke-linejoin:round;stroke-miterlimit:1.41421;">
        <path d="M4.784,13.635c0,0 -0.106,-2.924 0.006,-4.379c0.115,-1.502 0.318,-3.151 0.686,-4.632c0.163,-0.654 0.45,-1.623 0.755,-2.44c0.202,-0.54 0.407,-1.021 0.554,-1.352c0.038,-0.085 0.122,-0.139 0.215,-0.139c0.092,0 0.176,0.054 0.214,0.139c0.151,0.342 0.361,0.835 0.555,1.352c0.305,0.817 0.592,1.786 0.755,2.44c0.368,1.481 0.571,3.13 0.686,4.632c0.112,1.455 0.006,4.379 0.006,4.379l-4.432,0Z" style="fill:rgb(0,46,252);"/><path d="M5.481,12.731c0,0 -0.073,-3.048 0.003,-4.22c0.06,-0.909 0.886,-3.522 1.293,-4.764c0.03,-0.098 0.121,-0.165 0.223,-0.165c0.103,0 0.193,0.067 0.224,0.164c0.406,1.243 1.232,3.856 1.292,4.765c0.076,1.172 0.003,4.22 0.003,4.22l-3.038,0Z" style="fill:rgb(255,255,255);fill-opacity:0.846008;"/>
    </svg>`);
const MARKER_ICON_URL = `data:image/svg+xml;utf8,${MARKER}`;
const BoatIcon = L.icon({
   iconUrl: MARKER_ICON_URL,
   iconSize: [24, 24],
   iconAnchor: [12, 12],
   popupAnchor: [0, 0],
});

function printReport(report) {
  // Déstructuration des propriétés du rapport
  const { nComp, startTimeStr, isocTimeStep, polar, array } = report;

  // Construction du contenu HTML pour les métadonnées
  let htmlContent = `
    <div style="text-align: left;">
      <h2>Métadonnées</h2>
      <p><strong>nComp :</strong> ${nComp}</p>
      <p><strong>Start Time :</strong> ${startTimeStr}</p>
      <p><strong>Isoc Time Step :</strong> ${isocTimeStep} sec</p>
      <p><strong>Polar :</strong> ${polar}</p>
    </div>
  `;

  // Si le tableau 'array' existe et contient des éléments, on crée un tableau HTML
  if (Array.isArray(array) && array.length > 0) {
    htmlContent += `
      <h3>Données du tableau</h3>
      <table style="width:100%; border-collapse: collapse;" border="1">
        <thead>
          <tr>
            <th>Nom</th>
            <th>Latitude</th>
            <th>Longitude</th>
            <th>Dist To Main</th>
            <th>ETA</th>
            <th>Dist Done</th>
            <th>To Best Delay</th>
            <th>To Main Delay</th>
          </tr>
        </thead>
        <tbody>
    `;
    // Pour chaque élément, on crée une ligne de tableau
    array.forEach(item => {
      htmlContent += `
          <tr>
            <td>${item.name}</td>
            <td>${item.lat}</td>
            <td>${item.lon}</td>
            <td>${item.distToMain}</td>
            <td>${item.ETA}</td>
            <td>${item.distDone}</td>
            <td>${item.toBestDelay}</td>
            <td>${item.toMainDelay}</td>
          </tr>
      `;
    });
    htmlContent += `
        </tbody>
      </table>
    `;
  }

  // Utilisation de Swal.fire pour afficher le popup avec le contenu HTML construit
  Swal.fire({
    title: 'Report',
    html: htmlContent,
    width: '600px',
    showCloseButton: true,
    focusConfirm: false,
    confirmButtonText: 'OK',
    confirmButtonColor: '#FFA500' // Code hexadécimal pour l'orange
  });
}


function drawPolyline(coords, color) {
    if (!coords || coords.length < 2) return;

    L.polyline(coords, {
        color: color,
        weight: 1,
        opacity: 0.7
    }).addTo(isochroneLayerGroup);
}

function updateWindyMap(route, isocArray = []) {
    // Tracer la route principale
   if (!route || !route[boatName] || !route[boatName].track) {
      console.error("Données de route invalides !");
      return;
   }
     // Vider les anciens isochrones
    isochroneLayerGroup.clearLayers();

    // Si _isoc est présent, tracer chaque isochrone
    if (Array.isArray(isocArray)) {
        isocArray.forEach(isochrone => {
            drawPolyline(isochrone, "blue"); // Couleur rouge pour les isochrones
        });
    }

   const track = route[boatName].track;
   gribName = route[boatName].grib;

    // Convertir le format pour Windy (Leaflet)
   const latlngs = track.map(([lat, lon]) => [lat, lon]);
   //console.log("Coordonnées de la route :", latlngs);
   console.log(JSON.stringify(latlngs, null, 2));  // Affichage propre avec indentation

   // Si exite, l'ancienne  route change de couleur
   if (window.routePolyline) {
      window.routePolyline.setStyle({ color: 'pink' }); 
   }
   window.routePolyline = L.polyline(latlngs, {color: 'black'}).addTo(map);
   if (marker)
      marker.remove ();
      marker = L.marker(track[index], {
      icon: BoatIcon,
   }).addTo(map);

   marker._icon.setAttribute('data-heading', 45);
   marker.bindPopup(boatName);
   //updateIconStyle();
   updateHeading (track);

   console.log("Carte mise à jour avec la nouvelle route !");
}

function request () {
   // Préparer les données
   const timeStep = 3600;  // Remplace par la vraie valeur
   const timeStart = 3600; // Remplace par la vraie valeur
   //const waypoints = myWayPoints.map(wp => `${wp[0]},${wp[1]}`).join(";"); 
   const waypoints = myWayPoints.slice(1) // to remove first elemnt
      .map(wp => `${wp[0]},${wp[1]}`)
      .join(";");

   const boats = `${boatName}, ${myWayPoints[0][0]}, ${myWayPoints[0][1]}`; // name, lat, lon
   const epoch = Math.floor (routeParam.startTime.getTime () / 1000);
   const requestBody = `type=${REQ_TYPE}&boat=${boats}&waypoints=${waypoints}&timeStep=${routeParam.isoStep}&timeStart=${epoch}&polar=${routeParam.polar}&model=${routeParam.model}&forbid=${routeParam.forbid}&isoc=${routeParam.isoc}`;

   console.log ("request: " + requestBody);
   const spinnerOverlay = document.getElementById("spinnerOverlay");
      
   // Afficher le spinner sur la carte
   spinnerOverlay.style.display = "flex";

   fetch(apiUrl, {
      method: "POST",
      headers: {
        "Content-Type": "application/x-www-form-urlencoded"
      },
      body: requestBody
   })
   .then(response => response.json())  // Si la réponse est JSON
   .then(data => {
      console.log(JSON.stringify(data, null, 2));  // Affichage propre avec indentation
      firstKey = Object.keys(data)[0]; // name of the key
      console.log("REPORT:" + JSON.stringify(data._report, null, 2));
      printReport (data._report);
      if (firstKey.startsWith("_"))
         Swal.fire({
            title: "Warning",
            text: firstKey + ": "+ data [firstKey],
            icon: "warning",
            confirmButtonText: "OK"
         });
      else {
         route = data;
         if ("_isoc" in data) {
            const isocArray = data["_isoc"];
            console.log("Tableau _isoc :", isocArray);
            updateWindyMap(route, isocArray);
         } else {
            //console.warn("Clé _isoc non trouvée dans la réponse du serveur");
            updateWindyMap(route, []);
         }
         updateInfo ();
      }
   })
   .catch(error => {
      console.error ("Erreur lors de la requête :", error);
   })
   .finally(() => {
      // Masquer le spinner après que la requête est terminée
      spinnerOverlay.style.display = "none";
   });
}

// Fonction pour calculer une orthodromie entre deux points
function getGreatCirclePath(lat0, lon0, lat1, lon1, n = 100) {
   let path = [];
   let φ1 = (lat0 * Math.PI) / 180;
   let λ1 = (lon0 * Math.PI) / 180;
   let φ2 = (lat1 * Math.PI) / 180;
   let λ2 = (lon1 * Math.PI) / 180;

   for (let i = 0; i <= n; i++) {
      let f = i / n;
      let A = Math.sin((1 - f) * Math.acos(Math.sin(φ1) * Math.sin(φ2) + Math.cos(φ1) * Math.cos(φ2) * Math.cos(λ2 - λ1)));
      let B = Math.sin(f * Math.acos(Math.sin(φ1) * Math.sin(φ2) + Math.cos(φ1) * Math.cos(φ2) * Math.cos(λ2 - λ1)));

      let x = A * Math.cos(φ1) * Math.cos(λ1) + B * Math.cos(φ2) * Math.cos(λ2);
      let y = A * Math.cos(φ1) * Math.sin(λ1) + B * Math.cos(φ2) * Math.sin(λ2);
      let z = A * Math.sin(φ1) + B * Math.sin(φ2);

      let φ = Math.atan2(z, Math.sqrt(x * x + y * y));
      let λ = Math.atan2(y, x);

      path.push([φ * (180 / Math.PI), λ * (180 / Math.PI)]);
   }
   return path;
}

// Fonction pour tracer toutes les orthodromies du tableau
function drawGreatCircles(wayPoints) {
   wayPoints.forEach((wp, index) => {
      if (index < wayPoints.length - 1) {
         let lat0 = wp[0], lon0 = wp[1];
         let lat1 = wayPoints[index + 1][0], lon1 = wayPoints[index + 1][1];

         let path = getGreatCirclePath(lat0, lon0, lat1, lon1, 100);
         polylineMyWayPoints = L.polyline(path, { color: 'green', weight: 3, dashArray: '5,5' }).addTo(map);
      }
   });
}

// Fonction pour ajouter un POI
function addPOI(lat, lon) {
   Swal.fire({
      title: "POI",
      input: "text",
      inputPlaceholder: "Name",
      showCancelButton: true,
      confirmButtonText: "Add",
      cancelButtonText: "Cancel",
      inputValidator: (value) => {
         if (!value) {
            return "Name cannot be empty";
         }
      }
   }).then((result) => {
      if (result.isConfirmed) {
         POIs.push({ lat, lon, name });
         console.log("POIs: ", POIs);
         L.marker([lat, lon]).addTo(map).bindPopup(`POI: ${name}`).openPopup();
      }
   });
   closeContextMenu();
}

// Fonction pour mettre à jour la ligne reliant les waypoints
function updateMyWayPointsRoute() {
    // Supprimer l'ancienne polyline si elle existe
   //if (pOr) pOr.remove ();
   //if (origin) origin.remove ();
   origin = L.circle(myWayPoints [0], {
      color: 'red',
      fillColor: '#f03',
      fillOpacity: 0.5,
      radius: 1000
   }).addTo(map);

   destination = L.circle(myWayPoints [myWayPoints.length - 1], {
      color: 'black',
      fillColor: '#f03',
      fillOpacity: 0.5,
      radius: 1000
   }).addTo(map);

   if (polylineMyWayPoints) {
        map.removeLayer(polylineMyWayPoints);
   }
   loxoRoute = L.polyline(myWayPoints, {
      color: 'gray', 
      weight: 2,      
      opacity: 0.8  
   }).addTo(map);

   drawGreatCircles(myWayPoints);
}

// Clear all
function clearAll () {
   resetWaypoint ();
}

// reset
function resetWaypoint () {
   myWayPoints = [];
   if (origin) origin.remove ();
   if (destination) destination.remove ();
   if (loxoRoute) loxoRoute.remove ();
   updateMyWayPointsRoute();
}

// Fonction pour ajouter un Waypoint
function addWaypoint(lat, lon) {
   myWayPoints.push([lat, lon]);
   var circle = L.circle({lat, lon}, {
      color: 'green',
      fillColor: '#f03',
      fillOpacity: 0.5,
      radius: 800
   }).addTo(map);
   console.log("Waypoints:", myWayPoints);
   updateMyWayPointsRoute();
   closeContextMenu();
}

function toDMSString(lat, lon) {
   function convert(coord, isLat) {
      const absolute = Math.abs(coord);
      const degrees = Math.floor(absolute);
      const minutes = Math.floor((absolute - degrees) * 60);
      const seconds = Math.floor((absolute - degrees - minutes / 60) * 3600);
      const direction = isLat ? (coord >= 0 ? 'N' : 'S') : (coord >= 0 ? 'E' : 'W');

      return `${String(degrees).padStart(2, '0')}°${String(minutes).padStart(2, '0')}'${String(seconds).padStart(2, '0')}"${direction}`.trim();
   }
   return `${convert(lat, true)}, ${convert(lon, false)}`.trim ();
}

const updateIconStyle = () => {
   const icon = marker._icon;
   if (!icon) {
      return;
   }
   const heading = icon.getAttribute('data-heading');
   if (icon.style.transform.indexOf('rotateZ') === -1) {
      icon.style.transform = `${
         icon.style.transform
      } rotateZ(${heading}deg)`;
      icon.style.transformOrigin = 'center'
   }
};

function move(firstTrack, n) {
   index += n;
   if (index >= firstTrack.length) {
      index = 0;
   }
   else if (index < 0) {
      index = firstTrack.length - 1;
   }
   let time = new Date(routeParam.startTime.getTime() + index * routeParam.isoStep * 1000);
   //let time = routeParam.epoch + index * routeParam.isoStep;
   console.log ("index:" + index + " time: " + time); 
   let newLatLng = firstTrack[index]; // New position
   marker.setLatLng(newLatLng);       // Move the mark

   // Center map on new boat position
   // map.setView(newLatLng, map.getZoom());
   store.set ('timestamp', time);     // update Windy time
   updateHeading (firstTrack);
}

function updateHeading (firstTrack) {
   let heading;
   if (index < firstTrack.length - 1)
      heading = orthoCap(firstTrack[index], firstTrack[index + 1]);
   else
      heading = orthoCap(firstTrack[index-1], firstTrack[index]);
   marker._icon.setAttribute('data-heading', heading);
   updateIconStyle();
}

function goBegin () {
   index = 0;
   move (route[boatName].track, 0); // to move tme and pos
}

function backWard () {
   move (route[boatName].track, -1);
}

function forWard () {
   move (route[boatName].track, 1);
}

function playAnim() {
   if (animation) return; // Évite les doublons d'animation
   let firstTrack = route [boatName].track;
   animation = setInterval(() => {
      if (index >= firstTrack.length) {
         stopAnim();
         return;
      }
      if (index >= firstTrack.length || index < 0) {
         index = 0;
      }
      let time = new Date(routeParam.startTime.getTime() + index * routeParam.isoStep * 1000);
      let newLatLng = firstTrack[index]; // New position
      marker.setLatLng(newLatLng);       // Move the mark
      store.set ('timestamp', time);     // update Windy time
      marker.setLatLng(firstTrack[index]); // Déplace le marqueur
      // map.panTo(firstTrack[index]); // Centre la carte sur le marqueur
      updateHeading (firstTrack);
      index += 1;
   }, 500); // Intervalle de mise à jour en ms
}

function stopAnim() {
    clearInterval(animation);
    animation = null;
}

/*! convert value in seconds to hours, minutes */
function convertToHHMM (value) {
    let hours = Math.floor(value/3600);
    let minutes = Math.round((value/3600 - hours) * 60);
    return `${hours}:${minutes.toString().padStart(2, '0')}`;
}

function updateInfo () {
   findBounds (myWayPoints);
   let firstTrack = [];
   let n = 0, duration = 0, totDist = 0;
   let polar = "";
   if (route) {
      firstTrack = route[boatName].track;
      n = firstTrack.length - 1;
      duration = route[boatName].duration * 3600;
      totDist = route[boatName].totDist;
      polar = route[boatName].polar;
   }
   const lastDate = new Date(routeParam.startTime.getTime() + n * routeParam.isoStep * 1000);
   document.getElementById("infoRoute").innerHTML = boatName + "    ⏱️Isoc Type Step: " + convertToHHMM (routeParam.isoStep);
   document.getElementById("infoRoute").innerHTML += "    🚀Start Time: " + routeParam.startTime.toLocaleString() + "    🎯ETA: " + lastDate.toLocaleString ();
   document.getElementById("infoRoute").innerHTML += "    ⏱️Duration: " + convertToHHMM (duration);
   document.getElementById("infoRoute").innerHTML += "    📏Distance: " + totDist;
   document.getElementById("infoRoute").innerHTML += "    ⛵Polar: " + routeParam.polar + "   " + rCubeInfo; 
}

function findBounds (wayPoints) {
   // Définir les limites de la carte avec les extrêmes de la route
   if (wayPoints.length < 2) return;
   let lat0 = wayPoints [0][0];
   let lat1 = wayPoints [wayPoints.length - 1][0];
   let lon0 = wayPoints [0][1];
   let lon1 = wayPoints [wayPoints.length - 1][1];
   
   bounds = [
      [Math.floor (Math.min (lat0, lat1)), Math.floor (Math.min (lon0, lon1))],  // Coin inférieur gauche
      [Math.ceil (Math.max (lat0, lat1)), Math.ceil (Math.max (lon0, lon1))]     // Coin supérieur droit
   ];
   console.log ("bounds: " + bounds);
} 

// Fonction pour fermer le menu contextuel
function closeContextMenu() {
    let oldMenu = document.getElementById("context-menu");
    if (oldMenu) {
        document.body.removeChild(oldMenu);
    }
}

// Création du menu contextuel avec un design moderne
function showContextMenu(e) {
    //e.preventDefault();
    closeContextMenu(); // Ferme l'ancien menu si présent

    const lat = e.latlng.lat.toFixed(6);
    const lon = e.latlng.lng.toFixed(6);

    let menu = document.createElement("div");
    menu.id = "context-menu";
    menu.style.top = `${e.originalEvent.clientY}px`;
    menu.style.left = `${e.originalEvent.clientX}px`;

    menu.innerHTML = `
        <button class="context-button" onclick="addPOI(${lat}, ${lon})">📍 Ajouter POI</button>
        <button class="context-button" onclick="addWaypoint(${lat}, ${lon})">📌 Ajouter Waypoint</button>
        <button class="context-button" onclick="resetWaypoint()">Reset Waypoint</button>
    `;

    document.body.appendChild(menu);

    // Fermer le menu si on clique ailleurs
    document.addEventListener("click", closeContextMenu, { once: true });
}

/**
 * Calcule le cap orthodromique (heading) en degrés d'un bateau entre deux positions.
 * @param {Object|Array} prev - La position précédente (ex: {lat: 48.8566, lon: 2.3522} ou [48.8566, 2.3522]).
 * @param {Object|Array} curr - La position actuelle.
 * @returns {number} Le cap en degrés, avec 0° = nord.
 */
function orthoCap (prev, curr) {
    // Conversion des degrés en radians
    const toRadians = deg => deg * Math.PI / 180;
    const toDegrees = rad => rad * 180 / Math.PI;
    
    const lat1 = toRadians(prev[0]);
    const lon1 = toRadians(prev[1]);
    const lat2 = toRadians(curr[0]);
    const lon2 = toRadians(curr[1]);

    const dLon = lon2 - lon1;

    // Formule de calcul du cap initial (bearing) en radians
    const y = Math.sin(dLon) * Math.cos(lat2);
    const x = Math.cos(lat1) * Math.sin(lat2) - Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLon);
    const heading = Math.atan2(y, x);

    return (toDegrees (heading) + 360) % 360;
}

windyInit (options, windyAPI => {
   windy = windyAPI;
   map = windy.map;
   store = windy.store;
   map.fitBounds (bounds); // ajust le recentrage

   isochroneLayerGroup = L.layerGroup().addTo(map);
   var popup = L.popup();

   // Associer l'événement contextmenu à la carte
   map.on("contextmenu", showContextMenu);
   //document.addEventListener("click", closeContextMenu);  // Fermer le menu sur clic ailleurs

   map.on('mousemove', function (event) {
        let lat = event.latlng.lat; //
        let lon = event.latlng.lng; 
        document.getElementById('coords').textContent = toDMSString (lat, lon);
    });
   updateMyWayPointsRoute ();
   // Handle some events. We need to update the rotation of icons ideally each time
   // leaflet re-renders. them.
   map.on('zoom', updateIconStyle);
   map.on('zoomend', updateIconStyle);
   map.on('viewreset', updateIconStyle);
});
