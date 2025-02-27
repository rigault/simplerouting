/* jshint esversion: 6 */

function displayBestTimeHistogram(report, startEpoch) {
    if (!report || !report.array || report.array.length === 0) {
        Swal.fire({
            icon: 'warning',
            title: 'Aucune donnée',
            text: 'Aucune information disponible pour le meilleur temps.',
        });
        return;
    }

    let times = report.array;
    let tInterval = report.tInterval;
    let bestTime = Math.min(...times); // Trouver la meilleure durée (minimale)

    // Conversion des durées en heures
    let timesInHours = times.map(time => time / 3600);

    // Calcul des timestamps pour l'axe X
    let timeLabels = times.map((_, index) => new Date((startEpoch + index * tInterval) * 1000));

    // Définition des couleurs (gris par défaut, vert pour la meilleure valeur)
    let colors = times.map(time => time === bestTime ? 'green' : 'gray');

    let trace = {
        x: timeLabels,
        y: timesInHours,
        type: 'bar',
        marker: { color: colors },
        orientation: 'v'
    };

    let layout = {
        title: 'Durée de route en fonction de l heure de départ',
        xaxis: { title: 'Heure de départ', type: 'date', zeroline: true },
        yaxis: { title: 'Durée (h)', zeroline: true, zerolinewidth: 2, zerolinecolor: '#000', rangemode: 'tozero' },
        bargap: 0.2
    };

    // Affichage avec SweetAlert2
    Swal.fire({
        title: 'Analyse du Meilleur Temps',
        html: `<p><strong>Nombre de simulations :</strong> ${report.count}</p>
               <p><strong>Plage temporelle :</strong> ${new Date((startEpoch + report.tBegin * tInterval) * 1000).toLocaleString()} - 
               ${new Date((startEpoch + report.tEnd * tInterval) * 1000).toLocaleString()}</p>
               <p><strong>Durée minimale :</strong> ${(report.minDuration / 3600).toFixed(2)} h</p>
               <p><strong>Durée maximale :</strong> ${(report.maxDuration / 3600).toFixed(2)} h</p>
               <div id="plotContainer" style="width:100%;height:400px;"></div>`,
        didOpen: () => Plotly.newPlot('plotContainer', [trace], layout),
        width: '80%',
        confirmButtonText: 'Fermer',
    });
}

