/**
 * ============================================
 *         KIT PEDAGOGIQUE CO2
 * ============================================
 *
 * Materiel:
 *   - ESP32 DevKit
 *   - Capteur CO2 SensAir S88
 *   - Ecran OLED 0.96" SSD1306 (I2C)
 *
 * Cablage:
 *   OLED:
 *     - GND  -> GND
 *     - VDD  -> 3.3V
 *     - SCK  -> GPIO 22 (horloge I2C)
 *     - SDA  -> GPIO 21 (I2C Data)
 *
 *   SensAir S88:
 *     - G0       -> GND
 *     - G+       -> 5V (ATTENTION: le capteur a besoin de 5V!)
 *     - UART_TxD -> GPIO 16 (RX2 de l'ESP32)
 *     - UART_RxD -> GPIO 17 (TX2 de l'ESP32)
 *
 * Note: Le capteur S88 utilise le protocole Modbus RTU a 9600 bauds.
 */

#include "SensAir_S88.h"
#include "ble_provisioning.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <SSD1306Wire.h>
#include <WiFi.h>
#include <Wire.h>

// ============================================
// CONFIGURATION SERVEUR
// ============================================

const char *ENDPOINT_URL =
    "https://data.moduleair.fr/eduKit/edukit_endpoint.php";

const char *GET_PUBLIC_NAME_URL =
    "https://api.aircarto.fr/capteurs/getPublicName.php";

// Identifiant unique du capteur (basé sur l'eFuse MAC)
String deviceId = "";

// Nom public du capteur (récupéré depuis le serveur)
String publicName = "";

// Configuration WiFi (chargée depuis NVS ou BLE provisioning)
String wifiSSID = "";
String wifiPassword = "";

// ============================================
// CONFIGURATION DES PINS
// ============================================

// Pins I2C pour l'ecran OLED (defaut ESP32)
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDRESS 0x3C

// Pins UART pour le capteur CO2 (Serial2)
#define S88_RX 16 // GPIO16 = RX2 (recevoir du capteur)
#define S88_TX 17 // GPIO17 = TX2 (envoyer vers le capteur)

// ============================================
// CONFIGURATION
// ============================================

// Intervalle de lecture du CO2 (en ms)
#define CO2_READ_INTERVAL 2000

// Intervalle d'envoi des donnees au serveur - configurable via BLE
// Valeur par defaut: 60000ms (1 minute), peut etre 10000, 30000 ou 60000
// La vraie valeur est chargee depuis NVS via loadRefreshRateFromNVS()
unsigned long dataSendInterval = 60000;

// Seuils de CO2 pour les couleurs
#define CO2_GOOD 800    // Vert: < 800 ppm
#define CO2_MEDIUM 1200 // Orange: 800-1200 ppm
#define CO2_BAD 1500    // Rouge: > 1200 ppm

// ============================================
// OBJETS GLOBAUX
// ============================================

// Ecran OLED 128x64
SSD1306Wire display(OLED_ADDRESS, OLED_SDA, OLED_SCL);

// Capteur CO2
SensAir_S88 co2Sensor;

// Variables
int currentCO2 = 0;
unsigned long lastCO2Read = 0;
unsigned long lastDataSend = 0;
bool sensorReady = false;
bool wifiConnected = false;

// ============================================
// ECRAN DE CHARGEMENT
// ============================================

void showLoadingScreen() {
  int progressBarWidth = 100;
  int progressBarHeight = 10;
  int progressBarX = (128 - progressBarWidth) / 2;
  int progressBarY = 40;

  for (int progress = 0; progress <= 100; progress += 2) {
    display.clear();

    // Titre
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 5, "ATELIER");
    display.drawString(64, 22, "PEDAGOGIQUE");

    // Barre de progression (contour)
    display.drawRect(progressBarX, progressBarY, progressBarWidth,
                     progressBarHeight);

    // Barre de progression (remplissage)
    int fillWidth = (progress * (progressBarWidth - 4)) / 100;
    display.fillRect(progressBarX + 2, progressBarY + 2, fillWidth,
                     progressBarHeight - 4);

    // Pourcentage
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 52, String(progress) + "%");

    display.display();
    delay(100); // Duree totale ~5 secondes
  }
}

// ============================================
// CONNEXION WIFI
// ============================================

/**
 * Genere un identifiant unique base sur l'eFuse MAC de l'ESP32
 * Meme methode que ModuleAir pour coherence
 */
void generateDeviceId() {
  // Utilise l'eFuse MAC (identifiant unique grave en usine)
  deviceId = String((uint16_t)(ESP.getEfuseMac() >> 32), HEX);
  deviceId += String((uint32_t)ESP.getEfuseMac(), HEX);
  deviceId.toUpperCase();
  Serial.printf("Device ID (eFuse MAC): %s\n", deviceId.c_str());
}

/**
 * Affiche l'ecran de connexion WiFi
 */
void displayWiFiConnecting(const String &ssid) {
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 10, "Connexion");
  display.drawString(64, 28, "WiFi...");
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 50, ssid);
  display.display();
}

/**
 * Affiche le statut WiFi sur l'ecran
 */
void displayWiFiStatus(bool connected) {
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  if (connected) {
    display.drawString(64, 10, "WiFi OK!");
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 35, WiFi.localIP().toString());
  } else {
    display.drawString(64, 10, "WiFi");
    display.drawString(64, 30, "ECHEC!");
  }
  display.display();
  delay(2000);
}

/**
 * Connecte le WiFi avec les credentials stockes
 */
bool connectWiFi() {
  Serial.printf("Connexion au WiFi: %s\n", wifiSSID.c_str());
  displayWiFiConnecting(wifiSSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connecte! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal WiFi: %d dBm\n", WiFi.RSSI());
    wifiConnected = true;
    displayWiFiStatus(true);
    return true;
  } else {
    Serial.println("Echec connexion WiFi!");
    wifiConnected = false;
    displayWiFiStatus(false);
    return false;
  }
}

// ============================================
// RECUPERATION DU NOM PUBLIC
// ============================================

/**
 * Recupere le nom public du capteur depuis le serveur
 * Appele une fois apres connexion WiFi
 */
bool fetchPublicName() {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println(
        "[API] WiFi non connecte, impossible de recuperer le nom public");
    return false;
  }

  HTTPClient http;
  String url = String(GET_PUBLIC_NAME_URL) + "?device_id=" + deviceId;

  Serial.println("[API] Recuperation du nom public...");
  Serial.println("[API] URL: " + url);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.GET();

  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("[API] Reponse: " + response);

    // Parser le JSON pour extraire public_name
    // Format: {"status":"success","public_name": "MonCapteur",...} (avec ou
    // sans espace)
    int startIdx = response.indexOf("\"public_name\":");
    if (startIdx != -1) {
      startIdx += 14; // Longueur de "public_name":
      // Sauter les espaces et le guillemet ouvrant
      while (startIdx < (int)response.length() &&
             (response[startIdx] == ' ' || response[startIdx] == '"')) {
        startIdx++;
      }
      int endIdx = response.indexOf("\"", startIdx);
      if (endIdx != -1) {
        publicName = response.substring(startIdx, endIdx);
        Serial.println("[API] Nom public recupere: " + publicName);

        // Sauvegarder en NVS pour usage futur
        Preferences prefs;
        prefs.begin("edukit", false);
        prefs.putString("public_name", publicName);
        prefs.end();

        http.end();
        return true;
      }
    }
    Serial.println("[API] ERREUR: Impossible de parser le nom public");
  } else {
    Serial.printf("[API] ERREUR: Code HTTP %d\n", httpCode);
    if (httpCode == 404) {
      Serial.println("[API] Capteur non trouve dans la base de donnees");
    }
  }

  http.end();
  return false;
}

/**
 * Charge le nom public depuis NVS si disponible
 */
void loadPublicNameFromNVS() {
  Preferences prefs;
  prefs.begin("edukit", true); // Mode lecture seule
  publicName = prefs.getString("public_name", "");
  prefs.end();

  if (publicName.length() > 0) {
    Serial.println("[NVS] Nom public charge: " + publicName);
  } else {
    Serial.println("[NVS] Pas de nom public stocke");
  }
}

// ============================================
// ENVOI DES DONNEES AU SERVEUR
// ============================================

// Variable globale pour stocker le dernier message d'erreur
String lastHttpError = "";

/**
 * Retourne un message descriptif selon le code HTTP
 */
String getHttpErrorMessage(int code) {
  switch (code) {
  case 200:
    return "OK";
  case 400:
    return "Capteur inconnu";
  case 401:
    return "JSON invalide";
  case 500:
    return "Erreur serveur";
  case -1:
    return "Connexion echouee";
  case -2:
    return "Envoi echoue";
  case -3:
    return "Timeout";
  case -4:
    return "Pas de reponse";
  case -5:
    return "Connexion perdue";
  case -6:
    return "WiFi deconnecte";
  case -7:
    return "Pas de memoire";
  case -8:
    return "Non autorise";
  case -9:
    return "Encodage invalide";
  case -10:
    return "Coupure stream";
  case -11:
    return "Lecture timeout";
  default:
    if (code < 0) {
      return "Erreur reseau";
    } else if (code >= 500) {
      return "Erreur serveur";
    } else if (code >= 400) {
      return "Erreur requete";
    }
    return "Erreur inconnue";
  }
}

/**
 * Envoie les donnees CO2 au serveur via HTTPS
 * Retourne le code HTTP ou -1 en cas d'erreur
 */
int sendDataToServer() {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] WiFi non connecte, tentative de reconnexion...");
    if (!connectWiFi()) {
      Serial.println("[HTTP] ERREUR: Impossible de se reconnecter au WiFi");
      return -6;
    }
  }

  HTTPClient http;

  Serial.println("[HTTP] ================================");
  Serial.printf("[HTTP] Envoi des donnees a %s\n", ENDPOINT_URL);

  // Construire le JSON
  String jsonPayload = "{";
  jsonPayload += "\"device_id\":\"" + deviceId + "\",";
  jsonPayload += "\"signal_quality\":" + String(WiFi.RSSI()) + ",";
  jsonPayload += "\"co2\":" + String(currentCO2);
  jsonPayload += "}";

  Serial.printf("[HTTP] Payload: %s\n", jsonPayload.c_str());

  // Initialiser la connexion HTTPS
  http.begin(ENDPOINT_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000); // Timeout de 10 secondes

  // Envoyer la requete POST
  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("[HTTP] Code reponse: %d\n", httpResponseCode);

    // Afficher la reponse complete du serveur
    Serial.println("[HTTP] === REPONSE SERVEUR ===");
    Serial.println(response);
    Serial.println("[HTTP] === FIN REPONSE ===");

    // Parser le message d'erreur de la reponse JSON si possible
    // Format attendu: {"code":X,"status":"...","message":"..."}
    int msgStart = response.indexOf("\"message\":\"");
    if (msgStart != -1) {
      msgStart += 11; // Longueur de "message":"
      int msgEnd = response.indexOf("\"", msgStart);
      if (msgEnd != -1) {
        String serverMessage = response.substring(msgStart, msgEnd);
        Serial.printf("[HTTP] Message serveur: %s\n", serverMessage.c_str());
      }
    }

    // Log detaille selon le code
    if (httpResponseCode == 200) {
      Serial.println("[HTTP] SUCCESS: Donnees enregistrees!");
    } else if (httpResponseCode == 400) {
      Serial.println("[HTTP] ERREUR 400: Capteur inconnu");
      Serial.println("[HTTP] -> Un nouveau capteur a ete cree automatiquement");
      Serial.println(
          "[HTTP] -> Configurez-le dans PostgreSQL avec le token ci-dessus");
    } else if (httpResponseCode == 401) {
      Serial.println("[HTTP] ERREUR 401: Requete invalide");
      Serial.println("[HTTP] -> Verifiez le format JSON");
      Serial.println("[HTTP] -> Verifiez que device_id est present");
    } else if (httpResponseCode == 500) {
      Serial.println("[HTTP] ERREUR 500: Erreur serveur");
      Serial.println("[HTTP] -> Verifiez les logs Apache/PHP sur le serveur");
      Serial.println("[HTTP] -> Verifiez la connexion PostgreSQL/InfluxDB");
    }
  } else {
    // Erreurs de connexion (codes negatifs)
    Serial.printf("[HTTP] ERREUR CONNEXION (code %d)\n", httpResponseCode);
    Serial.printf("[HTTP] Detail: %s\n",
                  http.errorToString(httpResponseCode).c_str());

    // Messages explicites pour chaque type d'erreur
    switch (httpResponseCode) {
    case -1:
      Serial.println("[HTTP] -> Connexion au serveur impossible");
      break;
    case -2:
      Serial.println("[HTTP] -> Envoi des donnees echoue");
      break;
    case -3:
      Serial.println("[HTTP] -> Timeout - le serveur ne repond pas");
      break;
    case -4:
      Serial.println("[HTTP] -> Pas de reponse du serveur");
      break;
    case -5:
      Serial.println("[HTTP] -> Connexion perdue pendant le transfert");
      break;
    case -11:
      Serial.println("[HTTP] -> Timeout de lecture");
      break;
    default:
      Serial.println("[HTTP] -> Erreur reseau inconnue");
    }
  }

  Serial.println("[HTTP] ================================");
  http.end();
  return httpResponseCode;
}

// ============================================
// AFFICHAGE DU CO2
// ============================================

/**
 * Retourne un message selon le niveau de CO2
 */
const char *getCO2Message(int co2) {
  if (co2 < CO2_GOOD) {
    return "Air excellent";
  } else if (co2 < CO2_MEDIUM) {
    return "Correct";
  } else if (co2 < CO2_BAD) {
    return "Aerer SVP!";
  } else {
    return "AERER VITE!";
  }
}

/**
 * Affiche le niveau de CO2 sur l'ecran
 */
void displayCO2(int co2) {
  display.clear();

  // Titre "CO2"
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "CO2 (ppm)");

  // Valeur CO2 en gros
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  if (co2 > 0) {
    display.drawString(64, 18, String(co2));
  } else {
    display.drawString(64, 18, "---");
  }

  // Message selon le niveau
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 46, getCO2Message(co2));

  display.display();
}

/**
 * Affiche un message d'erreur
 */
void displayError(const char *message) {
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 10, "ERREUR");
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 35, message);
  display.display();
}

// ============================================
// LECTURE DU CAPTEUR
// ============================================

/**
 * Lit le capteur CO2 et met a jour la valeur
 */
void readCO2Sensor() {
  int co2Value = co2Sensor.getCO2();

  if (co2Value > 0) {
    currentCO2 = co2Value;
    sensorReady = true;
    Serial.printf("CO2: %d ppm\n", currentCO2);
  } else {
    uint8_t error = co2Sensor.getLastError();
    Serial.printf("Erreur lecture CO2: %d\n", error);

    switch (error) {
    case 1:
      Serial.println("  -> Port serie non initialise");
      break;
    case 2:
      Serial.println("  -> Timeout (capteur non connecte?)");
      break;
    case 3:
      Serial.println("  -> Reponse invalide");
      break;
    }
  }
}

// ============================================
// SETUP
// ============================================

void setup() {
  // Port serie pour debug
  Serial.begin(115200);
  Serial.println();
  Serial.println("================================");
  Serial.println("   KIT PEDAGOGIQUE CO2");
  Serial.println("================================");
  Serial.println();

  // Initialiser l'ecran OLED
  Serial.println("Initialisation de l'ecran OLED...");
  display.init();
  display.flipScreenVertically(); // Ajuster si l'ecran est a l'envers
  display.setContrast(255);

  // Generer l'identifiant unique du capteur
  generateDeviceId();

  // Afficher l'ecran de chargement
  showLoadingScreen();

  // ============================================
  // GESTION WIFI / BLE PROVISIONING
  // ============================================

  // Verifier si une config WiFi est stockee dans NVS
  if (getStoredWiFiConfig(wifiSSID, wifiPassword)) {
    // Config trouvee, tenter la connexion
    Serial.println("Config WiFi trouvee, connexion...");

    if (!connectWiFi()) {
      // Echec de connexion avec la config stockee
      Serial.println("Echec connexion WiFi, passage en mode BLE...");
      clearWiFiConfig(); // Effacer la config invalide

      // Afficher message sur ecran
      display.clear();
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 20, "WiFi echec");
      display.drawString(64, 35, "Mode config BLE");
      display.display();
      delay(2000);

      // Lancer le mode BLE provisioning
      initBLEProvisioning(deviceId);
      runBLEProvisioningLoop([](const char *msg) {
        display.clear();
        display.setFont(ArialMT_Plain_16);
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.drawString(64, 20, msg);
        display.display();
      });

      // Recharger la config apres provisioning
      getStoredWiFiConfig(wifiSSID, wifiPassword);
    }
  } else {
    // Pas de config, lancer le mode BLE provisioning
    Serial.println("Pas de config WiFi, mode BLE provisioning...");

    // Afficher message sur ecran
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 15, "Mode");
    display.drawString(64, 35, "Configuration");
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 55,
                       "edukit-" + deviceId.substring(deviceId.length() - 6));
    display.display();
    delay(2000);

    // Lancer le mode BLE provisioning
    initBLEProvisioning(deviceId);
    runBLEProvisioningLoop([](const char *msg) {
      display.clear();
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 20, msg);
      display.display();
    });

    // Charger la config apres provisioning
    getStoredWiFiConfig(wifiSSID, wifiPassword);
    connectWiFi();
  }

  // ============================================
  // CHARGEMENT DU REFRESH RATE
  // ============================================

  // Charger le refresh rate depuis NVS (configurable via BLE)
  loadRefreshRateFromNVS();
  dataSendInterval = getRefreshRateMs();
  Serial.printf("Intervalle d'envoi: %lu ms (%d secondes)\n", dataSendInterval,
                getRefreshRateSeconds());

  // ============================================
  // INITIALISATION CAPTEUR CO2
  // ============================================

  // Initialiser le port serie pour le capteur CO2
  // Le SensAir S88 utilise 9600 bauds, 8N1
  Serial.println("Initialisation du capteur CO2...");
  Serial2.begin(9600, SERIAL_8N1, S88_RX, S88_TX);
  co2Sensor.begin(Serial2);

  // Attendre que le capteur soit pret
  delay(1000);

  // Premiere lecture
  Serial.println("Premiere lecture du capteur...");
  readCO2Sensor();

  // ============================================
  // RECUPERATION DU NOM PUBLIC
  // ============================================

  // Charger le nom public depuis NVS si disponible
  loadPublicNameFromNVS();

  // Si WiFi connecte et pas de nom public, essayer de le recuperer
  if (wifiConnected && publicName.length() == 0) {
    Serial.println("Recuperation du nom public depuis le serveur...");
    fetchPublicName();
  }

  // Premier envoi au serveur si WiFi connecte
  if (wifiConnected && sensorReady) {
    Serial.println("Premier envoi des donnees au serveur...");
    sendDataToServer();
    lastDataSend = millis();
  }

  Serial.println();
  Serial.println("Systeme pret!");
  if (publicName.length() > 0) {
    Serial.println("Nom public: " + publicName);
  }
  Serial.println();

  // ============================================
  // DEMARRAGE BLE MODE CONFIG (permanent)
  // ============================================
  // Le BLE reste actif pour permettre la configuration
  // des parametres (refresh rate) via l'app AirCarto
  Serial.println("Demarrage BLE mode configuration...");
  initBLEConfigMode(deviceId);
  Serial.println("BLE actif - pret pour configuration via app");
}

// ============================================
// LOOP
// ============================================

void loop() {
  unsigned long currentTime = millis();

  // Verifier si le refresh rate a ete modifie via BLE
  if (checkAndResetRefreshRateChanged()) {
    dataSendInterval = getRefreshRateMs();
    Serial.printf("[CONFIG] Nouveau refresh rate applique: %lu ms\n",
                  dataSendInterval);
  }

  // Lire le capteur CO2 selon l'intervalle defini
  if (currentTime - lastCO2Read >= CO2_READ_INTERVAL) {
    lastCO2Read = currentTime;
    readCO2Sensor();
  }

  // Envoyer les donnees au serveur selon l'intervalle configure
  if (sensorReady && (currentTime - lastDataSend >= dataSendInterval)) {
    lastDataSend = currentTime;
    sendDataToServer();
    // Les erreurs sont affichees dans les logs serie uniquement
  }

  // Afficher la valeur sur l'ecran
  if (sensorReady) {
    displayCO2(currentCO2);
  } else {
    displayError("Capteur non detecte");
  }

  // Petite pause pour ne pas surcharger
  delay(100);
}
