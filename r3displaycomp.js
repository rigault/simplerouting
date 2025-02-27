/* jshint esversion: 6 */

function displayComp (report) {
    if (!report || !report.array || report.array.length === 0) {
        Swal.fire({
            icon: 'warning',
            title: 'Aucun compétiteur',
            text: 'Aucune donnée disponible pour comparaison.',
        });
        return;
    }

    // Construction des méta-données
    let metaData = `
        <p><strong>Nombre de compétiteurs :</strong> ${report.nComp}</p>
        <p><strong>Heure de départ :</strong> ${report.startTimeStr}</p>
        <p><strong>Pas de temps isochrone :</strong> ${report.isocTimeStep} sec</p>
        <p><strong>Polaire utilisée :</strong> ${report.polar}</p>
        <hr>
    `;

    // Construction du tableau des compétiteurs
    let table = `<table class="comp-table">
        <thead>
            <tr>
                <th>Nom</th>
                <th>Latitude</th>
                <th>Longitude</th>
                <th>Distance au principal</th>
                <th>ETA</th>
                <th>Distance parcourue</th>
                <th>Retard sur le meilleur</th>
                <th>Retard sur le principal</th>
            </tr>
        </thead>
        <tbody>
    `;
    
    report.array.forEach(comp => {
        table += `
            <tr>
                <td>${comp.name}</td>
                <td>${comp.lat.toFixed(4)}</td>
                <td>${comp.lon.toFixed(4)}</td>
                <td>${comp.distToMain.toFixed(2)} NM</td>
                <td>${comp.ETA}</td>
                <td>${comp.distDone.toFixed(2)} NM</td>
                <td>${comp.toBestDelay} sec</td>
                <td>${comp.toMainDelay} sec</td>
            </tr>
        `;
    });
    
    table += `</tbody></table>`;

    // Affichage avec SweetAlert2
    Swal.fire({
        title: 'Comparaison des compétiteurs',
        html: metaData + table,
        width: '80%',
        confirmButtonText: 'Fermer',
    });
}

