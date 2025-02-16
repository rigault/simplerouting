let index = rCubeIndex;
const options = {
   //key: 'C2A8tBkbZ9LmTk3JEfj7jSTmmxEYG53q',  // My windy key
   key: rCubeKey,
   lat: 60.0,                                
   lon: 0.0,
   zoom: 4,
   timestamp: rCubeEpoch * 1000,            // on ms for javascript
};
let time = new Date(options.timestamp);

/**
 * Calcule le cap orthodromique (heading) en degrés d'un bateau entre deux positions.
 * @param {Object|Array} prev - La position précédente (ex: {lat: 48.8566, lon: 2.3522} ou [48.8566, 2.3522]).
 * @param {Object|Array} curr - La position actuelle.
 * @returns {number} Le cap en degrés, avec 0° = nord.
 */
function orthoCap (prev, curr) {
    // Extraction des coordonnées selon le format (objet ou tableau)
    let lat1, lon1, lat2, lon2;
    
    if (Array.isArray(prev)) {
        lat1 = prev[0];
        lon1 = prev[1];
    } else {
        lat1 = prev.lat;
        lon1 = prev.lng !== undefined ? prev.lng : prev.lon;
    }
    
    if (Array.isArray(curr)) {
        lat2 = curr[0];
        lon2 = curr[1];
    } else {
        lat2 = curr.lat;
        lon2 = curr.lng !== undefined ? curr.lng : curr.lon;
    }

    // Conversion des degrés en radians
    const toRadians = deg => deg * Math.PI / 180;
    const toDegrees = rad => rad * 180 / Math.PI;
    
    lat1 = toRadians(lat1);
    lon1 = toRadians(lon1);
    lat2 = toRadians(lat2);
    lon2 = toRadians(lon2);

    // Calcul de la différence de longitude
    const dLon = lon2 - lon1;

    // Formule de calcul du cap initial (bearing) en radians
    const y = Math.sin(dLon) * Math.cos(lat2);
    const x = Math.cos(lat1) * Math.sin(lat2) - Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLon);
    let heading = Math.atan2(y, x);

    return (toDegrees (heading) + 360) % 360;
}


windyInit (options, windyAPI => {
   const { map } = windyAPI;
   const { store } = windyAPI;
   const MARKER = encodeURIComponent(`<?xml version="1.0" encoding="UTF-8" standalone="no"?>
        <!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">
        <svg width="100%" height="100%" viewBox="0 0 14 14" version="1.1" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" xml:space="preserve" style="fill-rule:evenodd;clip-rule:evenodd;stroke-linejoin:round;stroke-miterlimit:1.41421;">
        <path d="M4.784,13.635c0,0 -0.106,-2.924 0.006,-4.379c0.115,-1.502 0.318,-3.151 0.686,-4.632c0.163,-0.654 0.45,-1.623 0.755,-2.44c0.202,-0.54 0.407,-1.021 0.554,-1.352c0.038,-0.085 0.122,-0.139 0.215,-0.139c0.092,0 0.176,0.054 0.214,0.139c0.151,0.342 0.361,0.835 0.555,1.352c0.305,0.817 0.592,1.786 0.755,2.44c0.368,1.481 0.571,3.13 0.686,4.632c0.112,1.455 0.006,4.379 0.006,4.379l-4.432,0Z" style="fill:rgb(0,46,252);"/><path d="M5.481,12.731c0,0 -0.073,-3.048 0.003,-4.22c0.06,-0.909 0.886,-3.522 1.293,-4.764c0.03,-0.098 0.121,-0.165 0.223,-0.165c0.103,0 0.193,0.067 0.224,0.164c0.406,1.243 1.232,3.856 1.292,4.765c0.076,1.172 0.003,4.22 0.003,4.22l-3.038,0Z" style="fill:rgb(255,255,255);fill-opacity:0.846008;"/>
    </svg>`);
   const MARKER_ICON_URL = `data:image/svg+xml;utf8,${MARKER}`;

   let popup = L.popup();
   L.popup()
      .setLatLng([options.lat, options.lon])
      .setContent('Here')
      .openOn(map);

   const BoatIcon = L.icon({
      iconUrl: MARKER_ICON_URL,
      iconSize: [24, 24],
      iconAnchor: [12, 12],
      popupAnchor: [0, 0],
   });

   const markers = [];

   const updateIconStyle = () => {
      for (const marker of markers) {
         const icon = marker._icon;
         if (!icon) {
            continue;
         }
         const heading = icon.getAttribute('data-heading');
         if (icon.style.transform.indexOf('rotateZ') === -1) {
            icon.style.transform = `${
               icon.style.transform
            } rotateZ(${heading}deg)`;
            icon.style.transformOrigin = 'center';
         }
      }
   };

   fetch ('windyboats.json')
      .then(response => response.json())
      .then(result => result.result)
      .then(result => {
         try {
            for (const boatName of Object.keys(result)) {

               const boat = result[boatName];
               console.log (boatName + "rank: " + boat.rank) ;

               let pOr = L.circle(wayPoints [0], {
                  color: 'red',
                  fillColor: '#f03',
                  fillOpacity: 0.5,
                  radius: 1000
               }).addTo(map);

               let pDest = L.circle(wayPoints [wayPoints.length - 1], {
                  color: 'black',
                  fillColor: '#f03',
                  fillOpacity: 0.5,
                  radius: 1000
               }).addTo(map);

               let wayPointRoute = L.polyline(wayPoints, {
                  color: 'green', 
                  weight: 2,      
                  opacity: 0.8  
               }).addTo(map);
               
               function onKeyClick(n) {
                  /*popup
                     .setLatLng(e.latlng)
                     .setContent("Pos: " + e.latlng.toString() + "index: " + index + "\nTime:" + time.toISOString())
                     .openOn(map);*/
                  // Vérifier que le bateau existe et mettre à jour sa position
                  let heading = 0;
                  index += n;
                  if (index >= boat.track.length || index < 0) {
                     index = rCubeIndex;
                     time = new Date(options.timestamp);
                  }
                  else {
                     time = new Date(time.getTime() + n * rCubeStep * 1000);
                  }
                  let newLatLng = boat.track[index]; // New position
                  marker.setLatLng(newLatLng);       // Move the mark

                  // Center map on new boat position
                  map.setView(newLatLng, map.getZoom());
                  store.set ('timestamp', time);     // update Windy time
                  markers.push(marker);
                  if (index < boat.track.length - 1)
                     heading = orthoCap(boat.track[index], boat.track[index + 1]);
                  else
                     heading = computeHeading(boat.track[index-1], boat.track[index]);
                  marker._icon.setAttribute('data-heading', heading);
                  marker.bindPopup(boatName);
                  updateIconStyle();
               }
               document.addEventListener('keydown', function(event) {
                  console.log('Key pressed:', event.code);
                  event.preventDefault(); // Optionnal
                  onKeyClick ((event.code === 'Space') ? 1 : -1);
               });

               const layer = L.polyline(boat.track, {
                  color: (boat.rank === 0) ? 'black' : 'pink',
                  weight: 2,
               }).addTo(map);

               layer.on('mouseover', function() {
                  layer.setStyle({
                     weight: 4,
                  });
               });

               layer.on('mouseout', function() {
                  layer.setStyle({
                     weight: 2,
                  });
               });
                    
               const marker = L.marker(boat.track[index], {
                  icon: BoatIcon,
               }).addTo(map);

               markers.push(marker);
               marker._icon.setAttribute('data-heading', boat.heading);
               marker.bindPopup(boatName);
               updateIconStyle();
            }
         } catch (error) {
            console.error(`Error querying boats: ${error.message}`);
         }
      })
      .catch(error => {
         console.error(`Error querying boats: ${error.message}`);
      });

   // Handle some events. We need to update the rotation of icons ideally each time
   // leaflet re-renders. them.
   map.on('zoom', updateIconStyle);
   map.on('zoomend', updateIconStyle);
   map.on('viewreset', updateIconStyle);
});
