/**
 * ============================================
 *    BLE PROVISIONING - Kit Pédagogique CO2
 * ============================================
 *
 * Service BLE pour configuration WiFi via l'app AirCarto.
 * Compatible avec le protocole ModuleAir/Neuma existant.
 *
 * UUIDs identiques à ceux de l'app pour compatibilité :
 * - Service UUID : 4fafc201-1fb5-459e-8fcc-c5c9c331914b
 * - Device Info : beb5483e-36e1-4688-b7f5-ea07361b26a8
 * - WiFi Networks : beb5483e-36e1-4688-b7f5-ea07361b26a9
 * - WiFi Config : beb5483e-36e1-4688-b7f5-ea07361b26aa
 * - Status : beb5483e-36e1-4688-b7f5-ea07361b26ab
 */

#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

// ============================================
// UUIDs (doivent correspondre à l'app)
// ============================================

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_DEVICE_INFO_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_WIFI_NETWORKS_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHAR_WIFI_CONFIG_UUID "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define CHAR_STATUS_UUID "beb5483e-36e1-4688-b7f5-ea07361b26ab"
#define CHAR_SETTINGS_UUID "beb5483e-36e1-4688-b7f5-ea07361b26ad"

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
// Variables globales
// ============================================

static BLEServer *pServer = nullptr;
static BLECharacteristic *pDeviceInfoChar = nullptr;
static BLECharacteristic *pWifiNetworksChar = nullptr;
static BLECharacteristic *pWifiConfigChar = nullptr;
static BLECharacteristic *pStatusChar = nullptr;
static BLECharacteristic *pSettingsChar = nullptr;

static bool deviceConnected = false;
static bool oldDeviceConnected = false;
static bool wifiConfigReceived = false;
static bool settingsConfigReceived = false;
static ProvisioningStatus currentStatus = PROV_IDLE;

static String receivedSSID = "";
static String receivedPassword = "";

// Settings configurables (valeurs par defaut)
static int refreshRateSeconds =
    60; // Intervalle d'envoi en secondes (10, 30, ou 60)

// Variable partagee pour indiquer un changement de refresh rate
// Non-static pour etre accessible depuis main.cpp via extern
bool refreshRateChanged = false;

static String bleDeviceName = "";
static String bleDeviceId = "";

static Preferences nvs;

// ============================================
// Callbacks BLE
// ============================================

class EduKitServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("[BLE] Client connecté");
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("[BLE] Client déconnecté");
    // Restart advertising pour permettre reconnexion
    pServer->startAdvertising();
  }
};

class WiFiConfigCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string stdValue = pCharacteristic->getValue();
    String value = String(stdValue.c_str());
    Serial.println("[BLE] WiFi config reçue");

    // Parser le JSON { "ssid": "...", "password": "..." }
    int ssidStart = value.indexOf("\"ssid\":\"");
    int pwdStart = value.indexOf("\"password\":\"");

    if (ssidStart != -1 && pwdStart != -1) {
      ssidStart += 8; // Longueur de "ssid":"
      int ssidEnd = value.indexOf("\"", ssidStart);
      receivedSSID = value.substring(ssidStart, ssidEnd);

      pwdStart += 12; // Longueur de "password":"
      int pwdEnd = value.indexOf("\"", pwdStart);
      receivedPassword = value.substring(pwdStart, pwdEnd);

      Serial.printf("[BLE] SSID: %s\n", receivedSSID.c_str());
      Serial.println("[BLE] Password: ********");

      wifiConfigReceived = true;
    } else {
      Serial.println("[BLE] Format JSON invalide");
    }
  }
};

/**
 * Callback pour la configuration des settings (refreshRate)
 * Format JSON attendu: { "refreshRate": 10|30|60 }
 * Pas de listener permanent - callback event-driven uniquement
 */
class SettingsConfigCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string stdValue = pCharacteristic->getValue();
    String value = String(stdValue.c_str());
    Serial.println("[BLE] Settings config reçue: " + value);

    // Parser le JSON { "refreshRate": XX }
    int rateStart = value.indexOf("\"refreshRate\":");
    if (rateStart != -1) {
      rateStart += 14; // Longueur de "refreshRate":
      // Trouver la fin du nombre (virgule, accolade, ou fin de string)
      int rateEnd = rateStart;
      while (rateEnd < (int)value.length() && value[rateEnd] >= '0' &&
             value[rateEnd] <= '9') {
        rateEnd++;
      }

      if (rateEnd > rateStart) {
        int newRate = value.substring(rateStart, rateEnd).toInt();

        // Valider les valeurs acceptees (10, 30, ou 60 secondes)
        if (newRate == 10 || newRate == 30 || newRate == 60) {
          refreshRateSeconds = newRate;
          settingsConfigReceived = true;
          refreshRateChanged = true;
          Serial.printf("[BLE] Nouveau refresh rate: %d secondes\n",
                        refreshRateSeconds);

          // Sauvegarder immédiatement en NVS
          Preferences prefs;
          prefs.begin("edukit", false);
          prefs.putInt("refresh_rate", refreshRateSeconds);
          prefs.end();
          Serial.println("[NVS] Refresh rate sauvegarde");
        } else {
          Serial.printf("[BLE] Valeur invalide: %d (doit etre 10, 30 ou 60)\n",
                        newRate);
        }
      }
    } else {
      Serial.println("[BLE] Format JSON invalide pour settings");
    }
  }
};

// ============================================
// Fonctions NVS (stockage persistant)
// ============================================

/**
 * Vérifie si un WiFi est configuré dans NVS
 */
bool hasStoredWiFiConfig() {
  nvs.begin("wifi", true); // Mode lecture seule
  String ssid = nvs.getString("ssid", "");
  nvs.end();
  return ssid.length() > 0;
}

/**
 * Récupère la config WiFi stockée
 */
bool getStoredWiFiConfig(String &ssid, String &password) {
  nvs.begin("wifi", true);
  ssid = nvs.getString("ssid", "");
  password = nvs.getString("password", "");
  nvs.end();

  if (ssid.length() > 0) {
    Serial.printf("[NVS] Config WiFi trouvée: %s\n", ssid.c_str());
    return true;
  }
  Serial.println("[NVS] Pas de config WiFi stockée");
  return false;
}

/**
 * Sauvegarde la config WiFi dans NVS
 */
void saveWiFiConfig(const String &ssid, const String &password) {
  nvs.begin("wifi", false);
  nvs.putString("ssid", ssid);
  nvs.putString("password", password);
  nvs.end();
  Serial.printf("[NVS] Config WiFi sauvegardée: %s\n", ssid.c_str());
}

/**
 * Efface la config WiFi (pour reset)
 */
void clearWiFiConfig() {
  nvs.begin("wifi", false);
  nvs.clear();
  nvs.end();
  Serial.println("[NVS] Config WiFi effacée");
}

/**
 * Charge le refresh rate depuis NVS
 * Retourne la valeur stockee ou 60 par defaut
 */
int loadRefreshRateFromNVS() {
  Preferences prefs;
  prefs.begin("edukit", true);                 // Mode lecture seule
  int rate = prefs.getInt("refresh_rate", 60); // Default 60 secondes
  prefs.end();

  // Valider la valeur
  if (rate != 10 && rate != 30 && rate != 60) {
    rate = 60; // Valeur par defaut si invalide
  }

  refreshRateSeconds = rate;
  Serial.printf("[NVS] Refresh rate charge: %d secondes\n", rate);
  return rate;
}

/**
 * Verifie et reset le flag de changement de refresh rate
 * Retourne true si le refresh rate a change depuis le dernier appel
 */
bool checkAndResetRefreshRateChanged() {
  if (refreshRateChanged) {
    refreshRateChanged = false;
    return true;
  }
  return false;
}

/**
 * Retourne le refresh rate actuel en secondes
 */
int getRefreshRateSeconds() { return refreshRateSeconds; }

/**
 * Retourne le refresh rate actuel en millisecondes (pour la loop)
 */
unsigned long getRefreshRateMs() {
  return (unsigned long)refreshRateSeconds * 1000UL;
}

/**
 * Genere le JSON des settings actuels
 */
String getSettingsJson() {
  String json = "{\"refreshRate\":" + String(refreshRateSeconds) + "}";
  return json;
}

// ============================================
// Fonctions API (utilisées pendant BLE provisioning)
// ============================================

// URL de l'API getPublicName (doit correspondre au serveur)
#define GET_PUBLIC_NAME_API_URL                                                \
  "https://api.aircarto.fr/capteurs/getPublicName.php"

/**
 * Récupère le nom public depuis l'API serveur
 * Utilisé pendant le provisioning BLE pour mettre à jour le device info
 * @param deviceId L'identifiant unique du capteur
 * @return Le nom public ou une chaîne vide en cas d'erreur
 */
String fetchPublicNameForBLE(const String &deviceId) {
  Serial.println("[API] Recuperation du nom public...");

  HTTPClient http;
  String url = String(GET_PUBLIC_NAME_API_URL) + "?device_id=" + deviceId;

  Serial.println("[API] URL: " + url);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.GET();
  String publicName = "";

  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("[API] Reponse: " + response);

    // Parser le JSON pour extraire public_name
    int startIdx = response.indexOf("\"public_name\":\"");
    if (startIdx != -1) {
      startIdx += 15; // Longueur de "public_name":"
      int endIdx = response.indexOf("\"", startIdx);
      if (endIdx != -1) {
        publicName = response.substring(startIdx, endIdx);
        Serial.println("[API] Nom public: " + publicName);

        // Sauvegarder en NVS
        Preferences prefs;
        prefs.begin("edukit", false);
        prefs.putString("public_name", publicName);
        prefs.end();
      }
    }
  } else {
    Serial.printf("[API] Erreur HTTP: %d\n", httpCode);
  }

  http.end();
  return publicName;
}

// ============================================
// Fonctions BLE
// ============================================

/**
 * Génère le JSON device info
 * publicName = nom du capteur dans la base de données
 */
String getDeviceInfoJson(const String &publicName = "") {
  String json = "{\"chipId\":\"" + bleDeviceId + "\",";
  json += "\"version\":\"EduKit-v1.0\",";
  // publicName = nom serveur si disponible, sinon nom BLE par défaut
  if (publicName.length() > 0) {
    json += "\"publicName\":\"" + publicName + "\"";
  } else {
    json += "\"publicName\":\"" + bleDeviceName + "\"";
  }
  json += "}";
  return json;
}

/**
 * Scanne les réseaux WiFi et retourne le JSON
 */
String scanWiFiNetworksJson() {
  Serial.println("[BLE] Scan des réseaux WiFi...");
  int n = WiFi.scanNetworks();

  String json = "[";
  for (int i = 0; i < n && i < 10; i++) { // Max 10 réseaux
    if (i > 0)
      json += ",";
    // Format compact pour économiser de la place
    json += "{\"s\":\"" + WiFi.SSID(i) + "\",";
    json += "\"r\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"e\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? 1 : 0);
    json += "}";
  }
  json += "]";

  WiFi.scanDelete();
  Serial.printf("[BLE] %d réseaux trouvés\n", n);
  return json;
}

/**
 * Met à jour le statut et la caractéristique BLE
 */
void updateProvisioningStatus(ProvisioningStatus status) {
  currentStatus = status;
  if (pStatusChar) {
    pStatusChar->setValue(String(status).c_str());
    pStatusChar->notify();
  }
  Serial.printf("[BLE] Statut: %d\n", status);
}

/**
 * Initialise le serveur BLE
 */
void initBLEProvisioning(const String &deviceId) {
  bleDeviceId = deviceId;

  // Nom BLE : edukit-XXXXXX (6 derniers caractères du chipId)
  String suffix = deviceId;
  if (suffix.length() > 6) {
    suffix = suffix.substring(suffix.length() - 6);
  }
  bleDeviceName = "edukit-" + suffix;

  Serial.println("[BLE] ================================");
  Serial.printf("[BLE] Initialisation: %s\n", bleDeviceName.c_str());
  Serial.printf("[BLE] Device ID: %s\n", deviceId.c_str());

  // Initialiser BLE
  BLEDevice::init(bleDeviceName.c_str());

  // Créer le serveur
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new EduKitServerCallbacks());

  // Créer le service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Caractéristique Device Info (lecture seule)
  pDeviceInfoChar = pService->createCharacteristic(
      CHAR_DEVICE_INFO_UUID, BLECharacteristic::PROPERTY_READ);
  pDeviceInfoChar->setValue(getDeviceInfoJson().c_str());

  // Caractéristique WiFi Networks (lecture seule)
  pWifiNetworksChar = pService->createCharacteristic(
      CHAR_WIFI_NETWORKS_UUID, BLECharacteristic::PROPERTY_READ);
  pWifiNetworksChar->setValue(scanWiFiNetworksJson().c_str());

  // Caractéristique WiFi Config (écriture)
  pWifiConfigChar = pService->createCharacteristic(
      CHAR_WIFI_CONFIG_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWifiConfigChar->setCallbacks(new WiFiConfigCallback());

  // Caractéristique Status (lecture + notification)
  pStatusChar = pService->createCharacteristic(
      CHAR_STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pStatusChar->setValue("0");

  // Caractéristique Settings (lecture + écriture)
  // Permet de configurer le refresh rate sans listener permanent
  pSettingsChar = pService->createCharacteristic(
      CHAR_SETTINGS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pSettingsChar->setValue(getSettingsJson().c_str());
  pSettingsChar->setCallbacks(new SettingsConfigCallback());

  // Démarrer le service
  pService->start();

  // Démarrer l'advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising démarré");
  Serial.println("[BLE] En attente de connexion...");
  Serial.println("[BLE] ================================");
}

/**
 * Arrête le BLE proprement
 */
void stopBLEProvisioning() {
  if (pServer) {
    BLEDevice::deinit(true);
    pServer = nullptr;
    Serial.println("[BLE] Arrêté");
  }
}

/**
 * Tente de se connecter au WiFi avec les credentials reçus
 * Retourne true si succès
 */
bool tryConnectWiFi(const String &ssid, const String &password) {
  updateProvisioningStatus(PROV_CONNECTING);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.printf("[WiFi] Connexion à %s...\n", ssid.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connecté! IP: %s\n",
                  WiFi.localIP().toString().c_str());
    updateProvisioningStatus(PROV_CONNECTED);
    return true;
  } else {
    Serial.println("[WiFi] Échec de connexion");
    // Déterminer si c'est un mauvais mot de passe
    if (WiFi.status() == WL_CONNECT_FAILED) {
      updateProvisioningStatus(PROV_WRONG_PASSWORD);
    } else {
      updateProvisioningStatus(PROV_FAILED);
    }
    return false;
  }
}

/**
 * Boucle principale du mode provisioning
 * Bloque jusqu'à ce que le WiFi soit configuré avec succès
 * Retourne true si configuré, false si annulé
 */
bool runBLEProvisioningLoop(void (*displayCallback)(const char *)) {
  Serial.println("[BLE] Mode provisioning actif");

  if (displayCallback) {
    displayCallback("Mode config BLE");
  }

  while (true) {
    // Gérer la reconnexion BLE
    if (!deviceConnected && oldDeviceConnected) {
      delay(500);
      pServer->startAdvertising();
      oldDeviceConnected = deviceConnected;
    }
    if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
    }

    // Si config WiFi reçue, tenter la connexion
    if (wifiConfigReceived) {
      wifiConfigReceived = false;

      if (displayCallback) {
        displayCallback("Connexion WiFi...");
      }

      if (tryConnectWiFi(receivedSSID, receivedPassword)) {
        // Succès ! Sauvegarder la config WiFi
        saveWiFiConfig(receivedSSID, receivedPassword);

        // Récupérer le publicName depuis l'API (pendant que BLE est encore
        // actif)
        if (displayCallback) {
          displayCallback("Config serveur...");
        }

        String fetchedPublicName = fetchPublicNameForBLE(bleDeviceId);

        // Mettre à jour le device info BLE avec le publicName
        if (pDeviceInfoChar && fetchedPublicName.length() > 0) {
          String updatedDeviceInfo = getDeviceInfoJson(fetchedPublicName);
          pDeviceInfoChar->setValue(updatedDeviceInfo.c_str());
          Serial.println("[BLE] Device info mis à jour avec publicName: " +
                         fetchedPublicName);
        }

        // Attendre que l'app lise le device info mis à jour
        delay(3000);

        // Arrêter BLE et retourner
        stopBLEProvisioning();

        if (displayCallback) {
          displayCallback("WiFi OK!");
        }
        delay(1000);

        return true;
      } else {
        // Échec, l'utilisateur peut réessayer
        if (displayCallback) {
          displayCallback("Echec WiFi");
        }
        delay(2000);
        if (displayCallback) {
          displayCallback("Mode config BLE");
        }
      }
    }

    delay(100);
  }

  return false;
}

// ============================================
// MODE CONFIGURATION SETTINGS (léger, sans provisioning WiFi)
// ============================================

/**
 * Initialise le BLE en mode configuration uniquement
 * Plus léger que le provisioning complet - juste pour lire/écrire les settings
 * L'EduKit reste connecté au WiFi pendant ce mode
 */
void initBLEConfigMode(const String &deviceId) {
  bleDeviceId = deviceId;

  // Nom BLE : edukit-cfg-XXXXXX (6 derniers caractères du chipId)
  // Prefixe "cfg" pour différencier du mode provisioning WiFi
  String suffix = deviceId;
  if (suffix.length() > 6) {
    suffix = suffix.substring(suffix.length() - 6);
  }
  bleDeviceName = "edukit-cfg-" + suffix;

  Serial.println("[BLE-CONFIG] ================================");
  Serial.printf("[BLE-CONFIG] Mode configuration: %s\n", bleDeviceName.c_str());

  // Initialiser BLE
  BLEDevice::init(bleDeviceName.c_str());

  // Créer le serveur
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new EduKitServerCallbacks());

  // Créer le service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Caractéristique Device Info (lecture seule) - pour identification
  pDeviceInfoChar = pService->createCharacteristic(
      CHAR_DEVICE_INFO_UUID, BLECharacteristic::PROPERTY_READ);

  // Charger le publicName depuis NVS pour le device info
  Preferences prefs;
  prefs.begin("edukit", true);
  String storedPublicName = prefs.getString("public_name", "");
  prefs.end();
  pDeviceInfoChar->setValue(getDeviceInfoJson(storedPublicName).c_str());

  // Caractéristique Settings (lecture + écriture)
  pSettingsChar = pService->createCharacteristic(
      CHAR_SETTINGS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pSettingsChar->setValue(getSettingsJson().c_str());
  pSettingsChar->setCallbacks(new SettingsConfigCallback());

  // Caractéristique Status (pour compatibilité avec l'app)
  pStatusChar = pService->createCharacteristic(
      CHAR_STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pStatusChar->setValue("2"); // PROV_CONNECTED - déjà configuré

  // Démarrer le service
  pService->start();

  // Démarrer l'advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[BLE-CONFIG] Advertising démarré");
  Serial.println("[BLE-CONFIG] En attente de connexion...");
  Serial.println("[BLE-CONFIG] ================================");
}

/**
 * Boucle du mode configuration
 * Attend une connexion BLE, permet la lecture/écriture des settings
 * Retourne après un timeout ou si les settings ont été modifiés
 *
 * @param timeoutMs Timeout en ms (0 = pas de timeout, attend indéfiniment)
 * @param displayCallback Callback pour afficher le statut sur l'écran
 * @return true si les settings ont été modifiés, false sinon
 */
bool runBLEConfigLoop(unsigned long timeoutMs,
                      void (*displayCallback)(const char *)) {
  Serial.println("[BLE-CONFIG] Mode configuration actif");

  if (displayCallback) {
    displayCallback("Mode Config BLE");
  }

  unsigned long startTime = millis();
  settingsConfigReceived = false;
  refreshRateChanged = false;

  while (true) {
    // Vérifier le timeout si défini
    if (timeoutMs > 0 && (millis() - startTime >= timeoutMs)) {
      Serial.println("[BLE-CONFIG] Timeout - arrêt du mode config");
      break;
    }

    // Gérer la reconnexion BLE
    if (!deviceConnected && oldDeviceConnected) {
      delay(500);
      pServer->startAdvertising();
      oldDeviceConnected = deviceConnected;
    }
    if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
      Serial.println("[BLE-CONFIG] Client connecté - en attente de commandes");
      if (displayCallback) {
        displayCallback("App connectee");
      }
    }

    // Si les settings ont été modifiés, attendre un peu puis sortir
    if (settingsConfigReceived) {
      Serial.println("[BLE-CONFIG] Settings modifiés!");
      if (displayCallback) {
        displayCallback("Config OK!");
      }
      delay(2000); // Laisser le temps à l'app de lire la confirmation
      break;
    }

    delay(100);
  }

  // Arrêter le BLE
  stopBLEProvisioning();

  return refreshRateChanged;
}

#endif // BLE_PROVISIONING_H
