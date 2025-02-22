function gribInfo() {
    const formData = `type=5&grib=${gribName}`;

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

