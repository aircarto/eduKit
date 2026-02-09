<?php

/*
    ACTIF!
    Endpoint for data coming from EduKit sensors over WIFI
    -> save to influxDB (https://influx.aircarto.fr) - bucket: edukit
    -> update sensor metadata on PostgreSQL database

Les données des EduKit arrivent sous ce format:

  {
    "device_id": "4430353234313938",
    "signal_quality": -22,
    "co2": 793.00
  }

Codes de retour:
  - 200: OK, données enregistrées
  - 400: Capteur inconnu (nouveau capteur créé automatiquement)
  - 401: JSON invalide ou device_id manquant
  - 500: Erreur serveur (base de données)

check logs with:
tail -f /var/log/apache2/edukit/error.log | ccze -A
*/

$user = get_current_user();

require '/var/www/composer/vendor/autoload.php';
include '/home/' . $user . '/security/pgSQL_connexion.php';
include '/home/' . $user . '/security/influx_connexion.php';
require 'db_connect.php';

use InfluxDB2\Client;
use InfluxDB2\Model\WritePrecision;
use InfluxDB2\Point;

$needToLog = true; // Activer les logs pour debug

date_default_timezone_set('Europe/Paris');
$t = time();
$fullDate = date("Y-m-d H:i:s", $t);

date_default_timezone_set('UTC');
$fullDateUTC = date('Y-m-d H:i:s');

// Initialisation des variables
$co2 = -1;
$lat = -1;
$lon = -1;
$signal = -1;
$nom_capteur = "";
$local_ip = "";

// ============================================
// FONCTION DE REPONSE JSON
// ============================================

function sendJsonResponse($code, $status, $message, $data = null) {
    http_response_code($code);
    header('Content-Type: application/json');
    
    $response = [
        "code" => $code,
        "status" => $status,
        "message" => $message,
        "timestamp" => date("Y-m-d H:i:s")
    ];
    
    if ($data !== null) {
        $response["data"] = $data;
    }
    
    echo json_encode($response);
    exit();
}

// ============================================
// RECEPTION ET VALIDATION DU JSON
// ============================================

$json = file_get_contents("php://input");

if (empty($json)) {
    if ($needToLog) { error_log("[EduKit] ERROR: Empty request body", 0); }
    sendJsonResponse(401, "error", "Corps de la requete vide");
}

$json_decoded = json_decode($json, true);

if ($json_decoded === null) {
    if ($needToLog) { error_log("[EduKit] ERROR: Invalid JSON: " . $json, 0); }
    sendJsonResponse(401, "error", "JSON invalide", ["received" => substr($json, 0, 100)]);
}

// Vérifier que device_id est présent
if (!isset($json_decoded["device_id"]) || empty($json_decoded["device_id"])) {
    if ($needToLog) { error_log("[EduKit] ERROR: Missing device_id", 0); }
    sendJsonResponse(401, "error", "device_id manquant");
}

$token = $json_decoded["device_id"];

if ($needToLog) { error_log("[EduKit] Received data from device: " . $token, 0); }

// ============================================
// RECHERCHE DU CAPTEUR DANS LA BASE
// ============================================

$query = "SELECT * FROM capteurs.capteurs WHERE token = '" . pg_escape_string($token) . "' ";
$result = pg_query($query);

if (!$result) {
    if ($needToLog) { error_log("[EduKit] ERROR: PostgreSQL query failed: " . pg_last_error(), 0); }
    sendJsonResponse(500, "error", "Erreur base de donnees", ["detail" => "Query failed"]);
}

$myarray = pg_fetch_all($result);

if ($myarray) {
    foreach ($myarray as $value) {
        $nom_capteur = $value['nom'];
    }
}

// ============================================
// SI CAPTEUR INCONNU: CREATION AUTOMATIQUE
// ============================================

if ($nom_capteur == "") {
    $message_log1 = "[EduKit] Unknown device token: " . $token . " - Creating new sensor";
    if ($needToLog) { error_log($message_log1, 0); }

    // Création automatique du capteur dans la base
    $query_insert = "INSERT INTO capteurs.capteurs (nom, token, lat, long, last_seen, last_uplink, owner, capteur_type, displaymap, \"push_uSpot\") VALUES ('XX_new_edukit','" . pg_escape_string($token) . "', '0', '0', '$fullDate','$fullDate','AirCarto', 'EduKit', 'OUI', 'NON');";

    // Ajout de la localisation dans capteurs.locations
    $query2 = "
    INSERT INTO capteurs.locations (lat, long, from_date, to_date, table_capteurs_id) 
    SELECT capteurs.capteurs.lat, capteurs.capteurs.long,'$fullDateUTC','2050-01-01 00:00:00', capteurs.capteurs.id 
    FROM capteurs.capteurs 
    WHERE capteurs.capteurs.token = '" . pg_escape_string($token) . "'
    ";

    $insertSuccess = false;

    if ($sendToServerLab_1) {
        $result2 = pg_query($dbconn_server1, $query_insert);
        if ($result2) {
            $result2 = pg_query($dbconn_server1, $query2);
            $insertSuccess = true;
        }
        pg_close($dbconn_server1);
    }
    if ($sendToServerLab_2) {
        $result2 = pg_query($dbconn_server2, $query_insert);
        if ($result2) {
            $result2 = pg_query($dbconn_server2, $query2);
            $insertSuccess = true;
        }
        pg_close($dbconn_server2);
    }

    if ($insertSuccess) {
        if ($needToLog) { error_log("[EduKit] New sensor created: XX_new_edukit with token " . $token, 0); }
    }

    sendJsonResponse(400, "error", "Capteur inconnu - nouveau capteur cree", [
        "token" => $token,
        "action" => "Nouveau capteur cree avec nom 'XX_new_edukit'. Veuillez configurer ce capteur dans la base de donnees."
    ]);
}

// ============================================
// EXTRACTION DES VALEURS
// ============================================

if ($needToLog) { error_log("[EduKit] Processing data for sensor: " . $nom_capteur, 0); }

// CO2
if (isset($json_decoded["co2"])) {
    $co2 = floatval($json_decoded["co2"]);
    if ($needToLog) { error_log("[EduKit] CO2: " . $co2 . " ppm", 0); }
}

// Signal WiFi
if (isset($json_decoded["signal_quality"])) {
    $signal = intval($json_decoded["signal_quality"]);
    if ($needToLog) { error_log("[EduKit] Signal: " . $signal . " dBm", 0); }
}

// Coordonnées GPS (optionnel)
if (isset($json_decoded["latitude"])) {
    $lat = floatval($json_decoded["latitude"]);
}
if (isset($json_decoded["longitude"])) {
    $lon = floatval($json_decoded["longitude"]);
}

// IP locale (optionnel)
if (isset($json_decoded["local_ip"])) {
    $local_ip = $json_decoded["local_ip"];
}

// ============================================
// MISE A JOUR POSTGRESQL
// ============================================

$query2 = "
UPDATE capteurs.capteurs 
SET 
last_seen = '$fullDate',
last_seen_utc = '$fullDateUTC',
type_conn = 'WIFI',
wifi_signal = '$signal',
co2_last = '$co2',
local_ip = '$local_ip' 
WHERE nom ='" . pg_escape_string($nom_capteur) . "'";

$pgUpdateSuccess = false;

if ($sendToPublicCloud) {
    $result2 = pg_query($dbconn, $query2);
    if ($result2) { $pgUpdateSuccess = true; }
    pg_close($dbconn);
}
if ($sendToServerLab_1) {
    $result2 = pg_query($dbconn_server1, $query2);
    if ($result2) { $pgUpdateSuccess = true; }
    pg_close($dbconn_server1);
}
if ($sendToServerLab_2) {
    $result2 = pg_query($dbconn_server2, $query2);
    if ($result2) { $pgUpdateSuccess = true; }
    pg_close($dbconn_server2);
}

if (!$pgUpdateSuccess) {
    if ($needToLog) { error_log("[EduKit] ERROR: Failed to update PostgreSQL", 0); }
    sendJsonResponse(500, "error", "Erreur mise a jour base de donnees");
}

if ($needToLog) { error_log("[EduKit] PostgreSQL updated for " . $nom_capteur, 0); }

// ============================================
// ENVOI VERS INFLUXDB
// ============================================

$org = 'AirCarto';
$bucket = 'edukit';
$influxSuccess = false;

// Infomaniak public cloud
if ($sendToPublicCloud) {
    try {
        $client = new Client([
            "url" => INFLUX_URL,
            "token" => INFLUX_TOKEN,
            "debug" => false
        ]);

        $writeApi = $client->createWriteApi();

        // Format line protocol: measurement,tags fields
        $data = "" . $nom_capteur . ",sensorId=" . $nom_capteur . " co2=" . $co2 . "";

        $writeApi->write($data, WritePrecision::S, $bucket, $org);
        $client->close();
        $influxSuccess = true;
        
        if ($needToLog) { error_log("[EduKit] InfluxDB (public cloud) updated", 0); }
    } catch (Exception $e) {
        if ($needToLog) { error_log("[EduKit] ERROR InfluxDB public: " . $e->getMessage(), 0); }
    }
}

// AirLabServer1
if ($sendToServerLab_1) {
    try {
        $client = new Client([
            "url" => INFLUX_URL_AIRLAB_1,
            "token" => INFLUX_TOKEN_AIRLAB_1,
            "debug" => false
        ]);

        $writeApi = $client->createWriteApi();
        $data = "" . $nom_capteur . ",sensorId=" . $nom_capteur . " co2=" . $co2 . "";
        $writeApi->write($data, WritePrecision::S, $bucket, $org);
        $client->close();
        $influxSuccess = true;
        
        if ($needToLog) { error_log("[EduKit] InfluxDB (AirLab1) updated", 0); }
    } catch (Exception $e) {
        if ($needToLog) { error_log("[EduKit] ERROR InfluxDB AirLab1: " . $e->getMessage(), 0); }
    }
}

// AirLabServer2
if ($sendToServerLab_2) {
    try {
        $client = new Client([
            "url" => INFLUX_URL_AIRLAB_2,
            "token" => INFLUX_TOKEN_AIRLAB_2,
            "debug" => false
        ]);

        $writeApi = $client->createWriteApi();
        $data = "" . $nom_capteur . ",sensorId=" . $nom_capteur . " co2=" . $co2 . "";
        $writeApi->write($data, WritePrecision::S, $bucket, $org);
        $client->close();
        $influxSuccess = true;
        
        if ($needToLog) { error_log("[EduKit] InfluxDB (AirLab2) updated", 0); }
    } catch (Exception $e) {
        if ($needToLog) { error_log("[EduKit] ERROR InfluxDB AirLab2: " . $e->getMessage(), 0); }
    }
}

// ============================================
// REPONSE FINALE
// ============================================

if ($needToLog) { 
    error_log("[EduKit] SUCCESS - Data saved for " . $nom_capteur, 0);
    error_log("--------------------------", 0); 
}

sendJsonResponse(200, "success", "Donnees enregistrees avec succes", [
    "sensor" => $nom_capteur,
    "co2" => $co2,
    "signal" => $signal,
    "timestamp" => $fullDate,
    "influx_updated" => $influxSuccess
]);
