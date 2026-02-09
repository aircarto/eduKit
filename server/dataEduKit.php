<?php
/**
 * dataEduKit.php
 * 
 * Endpoint pour récupérer les données historiques CO2 d'un capteur EduKit
 * 
 * Usage: GET /API_V2/capteurs/dataEduKit?capteurID=MonCapteur&start=-1h&token=F44CF67CE5A4
 * 
 * Paramètres:
 *   - capteurID: Nom public du capteur (ex: "MonCapteur_Classe3A")
 *   - start: Début de la période (ex: "-1h", "-6h", "-24h", "-7d")
 *   - end: Fin de la période (optionnel, défaut: "now()")
 *   - token: Device ID pour authentification
 *   - format: "JSON" ou "CSV" (optionnel, défaut: "JSON")
 * 
 * Retour JSON:
 * [
 *   {"time": "2024-01-07T10:00:00Z", "sensorId": "MonCapteur", "co2": 850},
 *   {"time": "2024-01-07T10:01:00Z", "sensorId": "MonCapteur", "co2": 862},
 *   ...
 * ]
 */

$user = get_current_user();
include '/home/' . $user . '/security/pgSQL_connexion.php';
include '/home/' . $user . '/security/influx_connexion.php';
require 'db_connect.php';
require '/var/www/composer/vendor/autoload.php';

use InfluxDB2\Client;
use InfluxDB2\Model\WritePrecision;
use InfluxDB2\Point;

header('Content-Type: application/json; charset=UTF-8');
header('Access-Control-Allow-Origin: *');

// ============================================
// VALIDATION DES PARAMETRES
// ============================================

if (!isset($_GET['capteurID']) || empty($_GET['capteurID'])) {
    http_response_code(400);
    echo json_encode([
        "error" => "invalid_request",
        "message" => "Paramètre capteurID manquant"
    ], JSON_PRETTY_PRINT);
    exit();
}

if (!isset($_GET['start']) || empty($_GET['start'])) {
    http_response_code(400);
    echo json_encode([
        "error" => "invalid_request",
        "message" => "Paramètre start manquant"
    ], JSON_PRETTY_PRINT);
    exit();
}

$capteurID = $_GET['capteurID'];
$start = $_GET['start'];
$end = $_GET['end'] ?? 'now()';
$token = $_GET['token'] ?? '';
$format = $_GET['format'] ?? 'JSON';
$freq = $_GET['freq'] ?? null;

// ============================================
// VERIFICATION DU TOKEN
// ============================================

// Récupérer le token correct pour ce capteur (comme dataModuleAir.php)
$query = "SELECT token FROM capteurs.capteurs WHERE nom = '" . pg_escape_string($capteurID) . "' LIMIT 1";
$result = pg_query($dbconn_server1, $query) or die('Query failed: ' . pg_last_error());

$correct_token = null;
$row = pg_fetch_assoc($result);
if ($row) {
    $correct_token = $row['token'];
}

// Fermer la connexion PostgreSQL
pg_close($dbconn_server1);

// Vérifier le token
if ($token !== $correct_token) {
    http_response_code(401);
    echo json_encode([
        "error" => "unauthorized",
        "message" => "Token invalide ou non fourni"
    ], JSON_PRETTY_PRINT);
    exit();
}

// ============================================
// CONNEXION INFLUXDB
// ============================================

$org = 'AirCarto';
$bucket = 'edukit';

try {
    $client = new Client([
        "url" => INFLUX_URL_AIRLAB_1,
        "token" => INFLUX_TOKEN_AIRLAB_1,
    ]);

    // ============================================
    // CONSTRUCTION DE LA REQUETE FLUX
    // ============================================

    if (isset($freq) && !empty($freq)) {
        // Avec agrégation temporelle
        $fluxQuery = '
        from(bucket: "' . $bucket . '") 
        |> range(start: ' . $start . ')
        |> filter(fn: (r) => r["_measurement"] == "' . $capteurID . '")
        |> filter(fn: (r) => r["_field"] == "co2")
        |> aggregateWindow(every: ' . $freq . ', fn: mean, createEmpty: false)
        |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")  
        |> yield(name: "mean")
        ';
    } else {
        // Sans agrégation (données brutes)
        $fluxQuery = '
        from(bucket: "' . $bucket . '") 
        |> range(start: ' . $start . ')
        |> filter(fn: (r) => r["_measurement"] == "' . $capteurID . '")
        |> filter(fn: (r) => r["_field"] == "co2")
        |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")  
        ';
    }

    // ============================================
    // EXECUTION DE LA REQUETE
    // ============================================

    $tables = $client->createQueryApi()->query($fluxQuery, $org);
    $array = [];

    foreach ($tables as $table) {
        foreach ($table->records as $record) {
            $array[] = [
                "time" => $record["_time"],
                "timestamp" => $record["_time"],
                "sensorId" => $capteurID,
                "co2" => $record["co2"] ?? null
            ];
        }
    }

    $client->close();

    // ============================================
    // RETOUR DES DONNEES
    // ============================================

    if ($format === "CSV") {
        header('Content-Type: text/csv; charset=UTF-8');
        header("Content-Disposition: attachment; filename=data_edukit_" . $capteurID . ".csv");
        
        $output = fopen("php://output", 'w');
        fputcsv($output, ['time', 'sensorId', 'co2']); // Header
        
        foreach ($array as $row) {
            fputcsv($output, [$row['time'], $row['sensorId'], $row['co2']]);
        }
        
        fclose($output);
    } else {
        echo json_encode($array, JSON_PRETTY_PRINT);
    }

} catch (Exception $e) {
    http_response_code(500);
    echo json_encode([
        "error" => "server_error",
        "message" => "Erreur lors de la récupération des données",
        "detail" => $e->getMessage()
    ], JSON_PRETTY_PRINT);
}
