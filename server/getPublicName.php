<?php
/**
 * getPublicName.php
 * 
 * Endpoint pour récupérer le nom public d'un capteur EduKit à partir de son device_id
 * 
 * Usage: GET /API_V2/capteurs/getPublicName?device_id=F44CF67CE5A4
 * 
 * Retour:
 * {
 *   "status": "success",
 *   "device_id": "F44CF67CE5A4",
 *   "public_name": "MonCapteur_Classe3A",
 *   "capteur_type": "EduKit"
 * }
 */

$user = get_current_user();
include '/home/' . $user . '/security/pgSQL_connexion.php';
require 'db_connect.php';

header('Content-Type: application/json; charset=UTF-8');
header('Access-Control-Allow-Origin: *');

// ============================================
// VALIDATION DES PARAMETRES
// ============================================

if (!isset($_GET['device_id']) || empty($_GET['device_id'])) {
    http_response_code(400);
    echo json_encode([
        "status" => "error",
        "message" => "Paramètre device_id manquant"
    ], JSON_PRETTY_PRINT);
    exit();
}

$device_id = pg_escape_string($_GET['device_id']);

// ============================================
// RECHERCHE DU CAPTEUR
// ============================================

$query = "SELECT nom, capteur_type, owner, lat, long, last_seen 
          FROM capteurs.capteurs 
          WHERE token = '$device_id' 
          LIMIT 1";

// Utiliser directement $dbconn_server1 (comme les autres fichiers API)
$result = pg_query($dbconn_server1, $query) or die('Query failed: ' . pg_last_error());

$row = pg_fetch_assoc($result);

if (!$row) {
    http_response_code(404);
    echo json_encode([
        "status" => "error",
        "message" => "Capteur non trouvé",
        "device_id" => $device_id
    ], JSON_PRETTY_PRINT);
    exit();
}

// ============================================
// REPONSE SUCCES
// ============================================

http_response_code(200);
echo json_encode([
    "status" => "success",
    "device_id" => $device_id,
    "public_name" => $row['nom'],
    "capteur_type" => $row['capteur_type'],
    "owner" => $row['owner'],
    "location" => [
        "lat" => floatval($row['lat']),
        "lng" => floatval($row['long'])
    ],
    "last_seen" => $row['last_seen']
], JSON_PRETTY_PRINT);

// Fermer la connexion
pg_close($dbconn_server1);
