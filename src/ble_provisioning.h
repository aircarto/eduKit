/**
 * ============================================
 *    BLE PROVISIONING - Kit Pedagogique CO2
 * ============================================
 *
 * Service BLE pour configuration WiFi via l'app Cyan Sensor.
 * Implementation BTstackLib pour Raspberry Pi Pico W.
 *
 * UUIDs identiques a ceux de l'app pour compatibilite :
 * - Service UUID : 4fafc201-1fb5-459e-8fcc-c5c9c331914b
 * - Device Info   : beb5483e-36e1-4688-b7f5-ea07361b26a8
 * - WiFi Networks : beb5483e-36e1-4688-b7f5-ea07361b26a9
 * - WiFi Config   : beb5483e-36e1-4688-b7f5-ea07361b26aa
 * - Status        : beb5483e-36e1-4688-b7f5-ea07361b26ab
 * - CO2 Data      : beb5483e-36e1-4688-b7f5-ea07361b26ac
 * - Settings      : beb5483e-36e1-4688-b7f5-ea07361b26ad
 */

#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include <Arduino.h>

// ============================================
// UUIDs (doivent correspondre a l'app)
// ============================================

#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_DEVICE_INFO_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_WIFI_NETWORKS_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHAR_WIFI_CONFIG_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define CHAR_STATUS_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26ab"
#define CHAR_CO2_DATA_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26ac"
#define CHAR_SETTINGS_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26ad"

// ============================================
// Statuts de provisioning
// ============================================

enum ProvisioningStatus {
  PROV_IDLE = 0,
  PROV_CONNECTING = 1,
  PROV_CONNECTED = 2,
  PROV_FAILED = 3,
  PROV_WRONG_PASSWORD = 4
};

// ============================================
// Fonctions BLE publiques
// ============================================

/**
 * Initialise le BLE en mode provisioning complet (WiFi + settings)
 * Nom BLE: "edukit-XXXXXX" (6 derniers chars du deviceId)
 */
void initBLEProvisioning(const String &deviceId);

/**
 * Initialise le BLE en mode configuration legere (settings uniquement)
 * Nom BLE: "edukit-cfg-XXXXXX"
 */
void initBLEConfigMode(const String &deviceId);

/**
 * Arrete le BLE proprement
 */
void stopBLE();

/**
 * Traitement BLE a appeler dans loop()
 * Gere les callbacks et l'advertising
 */
void processBLE();

// ============================================
// Getters / Setters
// ============================================

/**
 * Retourne true si une config WiFi a ete recue via BLE
 */
bool hasReceivedWiFiConfig();

/**
 * Retourne le SSID recu via BLE
 */
String getReceivedSSID();

/**
 * Retourne le password recu via BLE
 */
String getReceivedPassword();

/**
 * Reset le flag de config WiFi recue
 */
void clearReceivedWiFiConfig();

/**
 * Met a jour la valeur CO2 exposee via BLE
 */
void updateBLECO2Value(int co2ppm);

/**
 * Met a jour le statut de provisioning expose via BLE
 */
void updateBLEStatus(ProvisioningStatus status);

/**
 * Retourne true si un client BLE est connecte
 */
bool isBLEClientConnected();

/**
 * Retourne le nom BLE du device
 */
String getBLEDeviceName();

// ============================================
// Fonctions NVS (stockage persistant)
// ============================================

/**
 * Verifie si un WiFi est configure dans NVS
 */
bool hasStoredWiFiConfig();

/**
 * Recupere la config WiFi stockee
 */
bool getStoredWiFiConfig(String &ssid, String &password);

/**
 * Sauvegarde la config WiFi dans NVS
 */
void saveWiFiConfig(const String &ssid, const String &password);

/**
 * Efface la config WiFi (pour reset)
 */
void clearWiFiConfig();

/**
 * Charge le refresh rate depuis NVS
 * Retourne la valeur stockee ou 60 par defaut
 */
int loadRefreshRateFromNVS();

/**
 * Retourne le refresh rate actuel en secondes
 */
int getRefreshRateSeconds();

/**
 * Retourne le refresh rate actuel en millisecondes
 */
unsigned long getRefreshRateMs();

/**
 * Verifie et reset le flag de changement de refresh rate
 */
bool checkAndResetRefreshRateChanged();

#endif // BLE_PROVISIONING_H
