/* jshint esversion: 6 */

/* display meta information about remote grib file */
function gribInfo (gribName) {
    const formData = `type=5&grib=grib/${gribName}`;

    console.log("Requête envoyée:", formData);

    fetch(apiUrl, {
        method: "POST",
        headers: {
            "Content-Type": "application/x-www-form-urlencoded"
        },
        body: formData
    })
    .then(response => {
        console.log("Réponse brute:", response);
        if (!response.ok) {
            throw new Error(`Erreur ${response.status}: ${response.statusText}`);
        }
        return response.json();
    })
    .then(data => {
        console.log("Données JSON reçues:", data);
        let shortNames = Array.isArray(data.shortNames) ? data.shortNames.join(", ") : "Non disponible";
        let timeStamps = Array.isArray(data.timeStamps) ? data.timeStamps.join(", ") : "Non disponible";

        let content = `<div style='text-align: left; font-family: Arial, sans-serif;'>` +
                      `<b>Centre:</b> ${data.centreName} (ID: ${data.centreID})<br>` +
                      `<b>Période:</b> ${data.runStart} - ${data.runEnd}<br>` +
                      `<b>Fichier:</b> ${data.fileName} (${data.fileSize} octets)<br>` +
                      `<b>Dimensions:</b> ${data.nLat} x ${data.nLon} (lat: ${data.topLat} à ${data.bottomLat}, lon: ${data.leftLon} à ${data.rightLon})<br>` +
                      `<b>Paramètres:</b> ${shortNames}<br>` +
                      `<b>Time Steps:</b> ${timeStamps}` +
                      `</div>`;

        Swal.fire({
            title: "Grib Info",
            html: content,
            icon: "success",
            confirmButtonText: "OK",
            width: "50%",
            confirmButtonColor: "#FFA500"
        });
    })
    .catch(error => {
        console.error("Erreur attrapée:", error);
        Swal.fire({
            title: "Erreur",
            text: error.message,
            icon: "error",
            confirmButtonText: "OK",
            confirmButtonColor: "#FFA500"
        });
    });
}


/* Choose a grib file then lauche gribInfo () */
function chooseGrib (fileName) {
    const formData = "type=6&dir=grib";
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
            Swal.fire("Erreur", "Aucun fichier grib trouvé", "error");
            return;
        }

        // Création du menu déroulant SANS la taille et la date
        const fileOptions = data.map(file => {
            const selected = file[0] === fileName ? "selected" : "";
            return `<option value="${file[0]}" ${selected}>${file[0]}</option>`;
        }).join("");

        // Affichage de la boîte de dialogue
        Swal.fire({
            title: "Sélectionnez un fichier",
            html: `
                <div class="swal-wide">
                    <select id="gribSelect" class="swal2-select">
                        ${fileOptions}
                    </select>
                </div>
            `,
            showCancelButton: true,
            confirmButtonText: "Valider",
            cancelButtonText: "Annuler",
            customClass: { popup: "swal-wide" },
            preConfirm: () => {
            const selectedFile = document.getElementById("gribSelect").value;
                if (!selectedFile) {
                    Swal.showValidationMessage("Veuillez sélectionner un fichier.");
                }
                return selectedFile;
            }
        }).then(result => {
            if (result.isConfirmed) {
                gribName = result.value; // Mise à jour du fichier sélectionné
                gribInfo(gribName);
            }
        });
    })
    .catch(error => {
        console.error("Erreur lors de la requête :", error);
        Swal.fire("Erreur", "Impossible de récupérer la liste des fichiers", "error");
    });
}
