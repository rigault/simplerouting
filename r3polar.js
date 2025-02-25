const DEFAULT_TWS = 15;

/* show polar info */
function polarInfo(polarName) {
    const formData = `type=4&polar=pol/${polarName}`;

    fetch(apiUrl, {
        method: "POST",
        headers: {
            "Content-Type": "application/x-www-form-urlencoded"
        },
        body: formData
    })
    .then(response => response.ok ? response.json() : Promise.reject(`Erreur ${response.status}: ${response.statusText}`))
    .then(data => generatePolarPlotly(data))
    .catch(error => {
        Swal.fire({
            title: "Erreur",
            text: error,
            icon: "error",
            confirmButtonColor: "#FFA500",
            confirmButtonText: "OK"
        });
    });
}

// dump polar table
function showPolarTable(data) {
    let windSpeeds = data.array[0].slice(1).map(v => parseFloat(v)).filter(v => !isNaN(v));
    let twaValues = data.array.slice(1).map(row => parseFloat(row[0])).filter(v => !isNaN(v));

    let tableHTML = "<table border='1' style='border-collapse: collapse; width: 100%; text-align: center;'>";
    tableHTML += "<tr style='font-weight: bold; background-color: #FFA500; color: white;'><th>TWA / TWS</th>";

    windSpeeds.forEach(tws => {
        tableHTML += `<th>${tws} kn</th>`;
    });

    tableHTML += "</tr>";

    data.array.slice(1).forEach(row => {
        tableHTML += `<tr><td style='font-weight: bold; background-color: #f0f0f0;'>${row[0]}°</td>`;
        row.slice(1).forEach(value => {
            tableHTML += `<td>${value.toFixed(2)}</td>`;
        });
        tableHTML += "</tr>";
    });

    tableHTML += "</table>";

   Swal.fire({
      title: "Tableau des vitesses",
      html: tableHTML,
      width: "80%",
      confirmButtonColor: "#FFA500",
      confirmButtonText: "Retour au graphique"
   }).then(() => {
      generatePolarPlotly(data); // Recharge la polaire après fermeture du tableau
   });

}

// Fonction pour interpoler les vitesses
function interpolate(value, x0, y0, x1, y1) {
    return x1 === x0 ? y0 : y0 + ((value - x0) * (y1 - y0)) / (x1 - x0);
}

function interpolateSpeeds(tws, windSpeeds, data) {
    if (tws <= windSpeeds[0]) return data.array.slice(1).map(row => parseFloat(row[1]));
    if (tws >= windSpeeds[windSpeeds.length - 1]) return data.array.slice(1).map(row => parseFloat(row[windSpeeds.length]));

    let i = 0;
    while (i < windSpeeds.length - 1 && windSpeeds[i + 1] < tws) i++;

    let speeds0 = data.array.slice(1).map(row => parseFloat(row[i + 1]));
    let speeds1 = data.array.slice(1).map(row => parseFloat(row[i + 2]));

    return speeds0.map((s0, index) => interpolate(tws, windSpeeds[i], s0, windSpeeds[i + 1], speeds1[index]));
}

// Symétriser les données polaires
function symmetrizeData(twaValues, speeds) {
    let fullTwa = [...twaValues];
    let fullSpeeds = [...speeds];

    for (let i = twaValues.length - 2; i >= 0; i--) {
        fullTwa.push(360 - twaValues[i]);
        fullSpeeds.push(speeds[i]);
    }

    return { fullTwa, fullSpeeds };
}

// Calculs des valeurs max et VMG
function findMaxSpeed(speeds) {
    return Math.max(...speeds);
}

function bestVmg(twaValues, speeds) {
    let bestSpeed = -1, bestAngle = -1;
    for (let i = 0; i < twaValues.length; i++) {
        if (twaValues[i] > 90) break;
        let vmg = speeds[i] * Math.cos(twaValues[i] * Math.PI / 180);
        if (vmg > bestSpeed) {
            bestSpeed = vmg;
            bestAngle = twaValues[i];
        }
    }
    return { angle: bestAngle, speed: bestSpeed };
}

function bestVmgBack(twaValues, speeds) {
    let bestSpeed = -1, bestAngle = -1;
    for (let i = 0; i < twaValues.length; i++) {
        if (twaValues[i] < 90) continue;
        let vmg = Math.abs(speeds[i] * Math.cos(twaValues[i] * Math.PI / 180));
        if (vmg > bestSpeed) {
            bestSpeed = vmg;
            bestAngle = twaValues[i];
        }
    }
    return { angle: bestAngle, speed: bestSpeed };
}

function generatePolarPlotly(data) {
    if (!data.array || !Array.isArray(data.array) || data.array.length < 2) return;

    const windSpeeds = data.array[0].slice(1).map(v => parseFloat(v)).filter(v => !isNaN(v));
    const twaValues = data.array.slice(1).map(row => parseFloat(row[0])).filter(v => !isNaN(v));
    let maxTWS = Math.ceil(windSpeeds[windSpeeds.length - 1] * 1.1);
    let initialTWS = 15;

    const chartContainer = document.createElement("div");
    chartContainer.innerHTML = `
        <label for="windSpeed">Vitesse du vent (TWS) :</label>
        <input type="range" id="windSpeedSlider" min="${windSpeeds[0]}" max="${maxTWS}" step="0.1" value="${initialTWS}">
        <span id="windSpeedValue">${initialTWS} kn</span>
        <div id="polarPlotly" style="width: 100%; height: 400px;"></div>
        <p><strong>Vitesse max:</strong> <span id="maxSpeed">-</span> kn</p>
        <p><strong>VMG au près:</strong> <span id="bestVmg">-</span> kn à <span id="bestVmgAngle">-</span>°</p>
        <p><strong>VMG au portant:</strong> <span id="bestVmgBack">-</span> kn à <span id="bestVmgBackAngle">-</span>°</p>
        <button id="showTable" style="margin-top: 10px; padding: 5px 10px; background-color: #FFA500; color: white; border: none; cursor: pointer;">
            Afficher le tableau
        </button>
    `;

    Swal.fire({ title: `Polar: ${polarName}`, html: chartContainer, width: "60%", showConfirmButton: false });

    function updatePlot(tws) {
        document.getElementById("windSpeedValue").innerText = `${tws.toFixed(1)} kn`;

        let speeds = interpolateSpeeds(tws, windSpeeds, data);
        let { fullTwa, fullSpeeds } = symmetrizeData(twaValues, speeds);

        let maxSpeed = findMaxSpeed(fullSpeeds);
        let vmg = bestVmg(fullTwa, fullSpeeds);
        let vmgBack = bestVmgBack(fullTwa, fullSpeeds);

        document.getElementById("maxSpeed").innerText = maxSpeed.toFixed(2);
        document.getElementById("bestVmg").innerText = vmg.speed.toFixed(2);
        document.getElementById("bestVmgAngle").innerText = vmg.angle.toFixed(0);
        document.getElementById("bestVmgBack").innerText = vmgBack.speed.toFixed(2);
        document.getElementById("bestVmgBackAngle").innerText = vmgBack.angle.toFixed(0);

        const trace = {
            type: "scatterpolar",
            mode: "lines+markers",
            r: fullSpeeds,
            theta: fullTwa,
            name: `Vitesse bateau (kn) pour TWS ${tws.toFixed(1)} kn`,
            line: { color: "orange" }
        };

        const layout = {
            polar: {
                radialaxis: { visible: true, range: [0, maxSpeed || 1] },
                angularaxis: { direction: "clockwise", rotation: 90 }
            },
            showlegend: false
        };

        Plotly.newPlot("polarPlotly", [trace], layout);
    }

    document.getElementById("windSpeedSlider").addEventListener("input", function() {
        updatePlot(parseFloat(this.value));
    });

    document.getElementById("showTable").addEventListener("click", function() {
        showPolarTable(data);
    });

    updatePlot(initialTWS);
}

/* choose a polar then launch polarInfo */
function choosePolar(currentPolar) {
    const formData = "type=6&dir=pol&sortByName=true";

    fetch(apiUrl, {
        method: "POST",
        headers: {
            "Content-Type": "application/x-www-form-urlencoded"
        },
        body: formData
    })
    .then(response => response.json())
    .then(data => {
        if (!Array.isArray(data) || data.length === 0) {
            Swal.fire("Erreur", "Aucun fichier polaire trouvé", "error");
            return;
        }

        // Création du menu déroulant SANS la taille et la date
        const fileOptions = data.map(file => {
            const selected = file[0] === currentPolar ? "selected" : "";
            return `<option value="${file[0]}" ${selected}>${file[0]}</option>`;
        }).join("");

        // Affichage de la boîte de dialogue
        Swal.fire({
            title: "Sélectionnez un fichier",
            html: `
                <div class="swal-wide">
                    <select id="polarSelect" class="swal2-select">
                        ${fileOptions}
                    </select>
                </div>
            `,
            showCancelButton: true,
            confirmButtonText: "Valider",
            cancelButtonText: "Annuler",
            customClass: { popup: "swal-wide" },
            preConfirm: () => {
                const selectedFile = document.getElementById("polarSelect").value;
                if (!selectedFile) {
                    Swal.showValidationMessage("Veuillez sélectionner un fichier.");
                }
                return selectedFile;
            }
        }).then(result => {
            if (result.isConfirmed) {
                polarName = result.value; // Mise à jour du fichier sélectionné
                polarInfo(polarName);
            }
        });
    })
    .catch(error => {
        console.error("Erreur lors de la requête :", error);
        Swal.fire("Erreur", "Impossible de récupérer la liste des fichiers", "error");
    });
}

