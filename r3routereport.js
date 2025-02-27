/* jshint esversion: 6 */

function formatDuration(seconds) {
   let days = Math.floor(seconds / (24 * 3600));
   let hours = Math.floor((seconds % (24 * 3600)) / 3600);
   let minutes = Math.floor((seconds % 3600) / 60);
   let secs = seconds % 60;

   return `${days}j ${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
}

function showRouteReport(routeData, boatName) {
   if (!routeData[boatName]) {
      Swal.fire({
         icon: 'error',
         title: 'Bateau introuvable',
         text: `Le bateau "${boatName}" n'existe pas dans les données.`,
      });
      return;
   }

   let boat = routeData[boatName];
   let durationFormatted = formatDuration(boat.duration);
   let trackPoints = boat.track.length;

   // Générer le contenu HTML formaté
   let htmlContent = `
      <div style="text-align: left;">
         <strong>🛥️ Bateau :</strong> ${boatName} <br>
         <strong>🧭 Cap :</strong> ${boat.heading}° <br>
         <strong>🏆 Classement :</strong> ${boat.rank} <br>
         <strong>⏳ Durée :</strong> ${durationFormatted} <br>
         <strong>📏 Distance :</strong> ${boat.totDist} NM <br>
         <strong>📍 Nombre de points :</strong> ${trackPoints} <br>
         <strong>🖥️ Temps de calcul :</strong> ${boat.calculationTime} s <br>
         <strong>🌊 Polaire :</strong> ${boat.polar} <br>
         <strong>📄 GRIB :</strong> ${boat.grib} <br>
      </div>
   `;

   // Afficher Swal
   Swal.fire({
       title: '📊 Métadonnées de la route',
       html: htmlContent,
       icon: 'info',
       confirmButtonText: 'OK',
       confirmButtonColor: 'orange',
       background: '#fefefe',
       showCloseButton: true
   });
}

