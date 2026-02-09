/**
 * ============================================
 *         KIT PEDAGOGIQUE CO2
 * ============================================
 *
 * Materiel:
 *   - Raspberry Pi Pico 2 W
 *   - Capteur CO2 SensAir S88
 *   - Ecran OLED 0.96" SSD1306 (I2C, bicolore jaune/bleu)
 *
 * Cablage:
 *   OLED:
 *     - GND  -> GND
 *     - VDD  -> 3.3V
 *     - SCL  -> GPIO 5 (I2C0 SCL)
 *     - SDA  -> GPIO 4 (I2C0 SDA)
 *
 *   SensAir S88:
 *     - G0       -> GND
 *     - G+       -> VBUS (5V)
 *     - UART_TxD -> GPIO 1 (UART0 RX)
 *     - UART_RxD -> GPIO 0 (UART0 TX)
 *
 * Note: Le capteur S88 utilise le protocole Modbus RTU a 9600 bauds.
 *
 * OLED bicolore: les 16 premiers pixels (y 0-15) sont jaunes,
 * le reste (y 16-63) est bleu. Le layout est optimise en consequence.
 */

#include "SensAir_S88.h"
#include "ble_provisioning.h"
#include "pico_compat.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <SSD1306Wire.h>
#include <WiFi.h>
#include <Wire.h>
#include <pico/unique_id.h>

// ============================================
// CONFIGURATION SERVEUR
// ============================================

const char *ENDPOINT_URL =
    "https://data.moduleair.fr/eduKit/edukit_endpoint.php";

const char *GET_PUBLIC_NAME_URL =
    "https://api.aircarto.fr/capteurs/getPublicName.php";

// Identifiant unique du capteur
String deviceId = "";

// Nom public du capteur (recupere depuis le serveur)
String publicName = "";

// Configuration WiFi (chargee depuis NVS)
String wifiSSID = "airlabo";
String wifiPassword = "xxxxxx";

// ============================================
// CONFIGURATION DES PINS
// ============================================

#define OLED_SDA 4
#define OLED_SCL 5
#define OLED_ADDRESS 0x3C

#define S88_RX 1 // GPIO1 = UART0 RX
#define S88_TX 0 // GPIO0 = UART0 TX

// ============================================
// CONFIGURATION
// ============================================

#define CO2_READ_INTERVAL 2000

// Seuils de CO2
#define CO2_GOOD 800
#define CO2_BAD 1500

// ============================================
// MACHINE A ETATS
// ============================================

enum AppState { STATE_BLE_PROVISIONING, STATE_NORMAL };

AppState currentState = STATE_BLE_PROVISIONING;

// ============================================
// OBJETS GLOBAUX
// ============================================

SSD1306Wire display(OLED_ADDRESS, OLED_SDA, OLED_SCL);
SensAir_S88 co2Sensor;

// Variables
int currentCO2 = 0;
unsigned long lastCO2Read = 0;
unsigned long lastDataSend = 0;
unsigned long lastDisplayUpdate = 0;
uint32_t co2Sum = 0;
uint16_t co2SampleCount = 0;
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

    // Zone jaune (0-15): titre
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 2, "ATELIER PEDAGOGIQUE");

    // Zone bleue (16-63): barre de progression
    display.drawRect(progressBarX, progressBarY, progressBarWidth,
                     progressBarHeight);

    int fillWidth = (progress * (progressBarWidth - 4)) / 100;
    display.fillRect(progressBarX + 2, progressBarY + 2, fillWidth,
                     progressBarHeight - 4);

    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 52, String(progress) + "%");

    display.display();
    delay(100);
  }
}

// ============================================
// GENERATION DEVICE ID
// ============================================

void generateDeviceId() {
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);

  deviceId = "";
  for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
    char buf[3];
    sprintf(buf, "%02X", id.id[i]);
    deviceId += buf;
  }
  Serial.printf("Device ID (Pico ID): %s\n", deviceId.c_str());
}

// ============================================
// CONNEXION WIFI
// ============================================

void displayWiFiConnecting(const String &ssid) {
  display.clear();

  // Zone jaune (0-15): titre
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 2, "Connexion WiFi");

  // Zone bleue (16-63): SSID
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 24, ssid);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 48, "Veuillez patienter...");
  display.display();
}

void displayWiFiStatus(bool connected) {
  display.clear();

  // Zone jaune (0-15): titre
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 2, "WiFi");

  // Zone bleue (16-63): statut
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  if (connected) {
    display.drawString(64, 22, "Connexion");
    display.drawString(64, 40, "etablie");
  } else {
    display.drawString(64, 22, "Connexion");
    display.drawString(64, 40, "echouee");
  }
  display.display();
  delay(connected ? 800 : 2000);
}

bool connectWiFi() {
  Serial.printf("Connexion au WiFi: %s\n", wifiSSID.c_str());
  displayWiFiConnecting(wifiSSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 120) {
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

bool fetchPublicName() {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] WiFi non connecte");
    return false;
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  String url = String(GET_PUBLIC_NAME_URL) + "?device_id=" + deviceId;

  Serial.println("[API] Recuperation du nom public...");
  Serial.println("[API] URL: " + url);

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.GET();

  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("[API] Reponse: " + response);

    int startIdx = response.indexOf("\"public_name\":");
    if (startIdx != -1) {
      startIdx += 14;
      while (startIdx < (int)response.length() &&
             (response[startIdx] == ' ' || response[startIdx] == '"')) {
        startIdx++;
      }
      int endIdx = response.indexOf("\"", startIdx);
      if (endIdx != -1) {
        publicName = response.substring(startIdx, endIdx);
        Serial.println("[API] Nom public recupere: " + publicName);

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
  }

  http.end();
  return false;
}

void loadPublicNameFromNVS() {
  Preferences prefs;
  prefs.begin("edukit", true);
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

String lastHttpError = "";

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
  default:
    if (code < 0)
      return "Erreur reseau";
    if (code >= 500)
      return "Erreur serveur";
    if (code >= 400)
      return "Erreur requete";
    return "Erreur inconnue";
  }
}

int sendDataToServer(int co2Value) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] WiFi non connecte, tentative de reconnexion...");
    if (!connectWiFi()) {
      Serial.println("[HTTP] ERREUR: Impossible de se reconnecter au WiFi");
      return -6;
    }
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  Serial.println("[HTTP] ================================");
  Serial.printf("[HTTP] Envoi des donnees a %s\n", ENDPOINT_URL);

  String jsonPayload = "{";
  jsonPayload += "\"device_id\":\"" + deviceId + "\",";
  jsonPayload += "\"signal_quality\":" + String(WiFi.RSSI()) + ",";
  jsonPayload += "\"co2\":" + String(co2Value);
  jsonPayload += "}";

  Serial.printf("[HTTP] Payload: %s\n", jsonPayload.c_str());

  http.begin(client, ENDPOINT_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("[HTTP] Code reponse: %d\n", httpResponseCode);
    Serial.println("[HTTP] === REPONSE SERVEUR ===");
    Serial.println(response);
    Serial.println("[HTTP] === FIN REPONSE ===");

    if (httpResponseCode == 200) {
      Serial.println("[HTTP] SUCCESS: Donnees enregistrees!");
    } else if (httpResponseCode == 400) {
      Serial.println("[HTTP] ERREUR 400: Capteur inconnu");
    } else if (httpResponseCode == 401) {
      Serial.println("[HTTP] ERREUR 401: Requete invalide");
    } else if (httpResponseCode == 500) {
      Serial.println("[HTTP] ERREUR 500: Erreur serveur");
    }
  } else {
    Serial.printf("[HTTP] ERREUR CONNEXION (code %d)\n", httpResponseCode);
  }

  Serial.println("[HTTP] ================================");
  http.end();
  return httpResponseCode;
}

// ============================================
// AFFICHAGE DU CO2
// ============================================

const char *getCO2Message(int co2) {
  if (co2 > -1 && co2 < CO2_GOOD)
    return "BIEN";
  if (co2 >= CO2_GOOD && co2 < CO2_BAD)
    return "AERER SVP";
  if (co2 >= CO2_BAD)
    return "AERER SVP";
  return "ERREUR";
}

void drawWiFiBarsWithLevel(int level, bool crossed) {
  const int barCount = 3;
  const int barWidth = 3;
  const int barSpacing = 1;
  const int barHeights[barCount] = {4, 8, 12};
  const int bottomY = 14;
  const int totalWidth = barCount * barWidth + (barCount - 1) * barSpacing;
  const int startX = 128 - 1 - totalWidth;
  const int maxHeight = barHeights[barCount - 1];

  for (int i = 0; i < barCount; i++) {
    int h = barHeights[i];
    int x = startX + i * (barWidth + barSpacing);
    int y = bottomY - h;
    if (level > i) {
      display.fillRect(x, y, barWidth, h);
    } else {
      display.drawRect(x, y, barWidth, h);
    }
  }

  if (crossed) {
    const int extend = 1;
    int x0 = startX - extend;
    int y0 = bottomY;
    int x1 = startX + totalWidth - 1 + extend;
    int y1 = bottomY - maxHeight;

    // Thicker, longer single slash over the bars
    display.drawLine(x0, y1, x1, y0);
    display.drawLine(x0 + 1, y1, x1 + 1, y0);
  }
}

void drawWiFiBars(int rssi) {
  int level = 0;
  if (rssi > -55)
    level = 3;
  else if (rssi > -70)
    level = 2;
  else if (rssi > -85)
    level = 1;

  drawWiFiBarsWithLevel(level, false);
}

void drawWiFiBarsDisconnected() { drawWiFiBarsWithLevel(0, true); }

void displayCO2(int co2) {
  display.clear();

  // Zone jaune (y 0-15): header avec "CO2 (ppm)" et indicateur WiFi
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 2, "CO2");

  // Indicateur WiFi en haut a droite (zone jaune)
  if (wifiConnected && WiFi.status() == WL_CONNECTED) {
    drawWiFiBars(WiFi.RSSI());
  } else {
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 2, "OFFLINE");
  }

  // Zone bleue (y 16-63): valeur CO2 + message
  const int valueY = 18;
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  if (co2 > 0) {
    display.drawString(64, valueY, String(co2));

    // "ppm" aligned with the digits like in offline mode
    const int ppmY = valueY + (24 - 10) / 2;
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(96, ppmY, "ppm");

    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 46, getCO2Message(co2));
  } else {
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 20, "Branchements");
    display.drawString(64, 40, "incorrects");
  }

  display.display();
}

void displayBLEProvisioning(int co2) {
  display.clear();

  // Zone jaune (y 0-15): titre
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 2, "CO2");
  drawWiFiBarsDisconnected();

  // Zone bleue (y 16-63): CO2 en grand + info configuration
  const int valueY = 18;
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  if (co2 > 0) {
    display.drawString(64, valueY, String(co2));

    // Align "ppm" vertically with the digits by centering it on the value.
    const int ppmY = valueY + (24 - 10) / 2;
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(96, ppmY, "ppm");

    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 54, getCO2Message(co2));
  } else {
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 20, "Branchements");
    display.drawString(64, 40, "incorrects");
  }

  display.display();
}

void displayError(const char *message) {
  display.clear();

  // Zone jaune (y 0-15): titre erreur
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 2, "ERREUR");

  // Zone bleue (y 16-63): message
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 30, message);
  display.display();
}

// ============================================
// LECTURE DU CAPTEUR
// ============================================

void readCO2Sensor() {
  int co2Value = co2Sensor.getCO2();

  if (co2Value > 0) {
    currentCO2 = co2Value;
    sensorReady = true;
    Serial.printf("CO2: %d ppm\n", currentCO2);

    // Mettre a jour la valeur BLE
    updateBLECO2Value(currentCO2);

    // Accumuler pour la moyenne d'envoi
    co2Sum += (uint32_t)co2Value;
    co2SampleCount++;
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
// GESTION WiFi PROVISIONING
// ============================================

void handleWiFiConfigReceived() {
  String ssid = getReceivedSSID();
  String password = getReceivedPassword();
  clearReceivedWiFiConfig();

  Serial.printf("[PROV] Tentative de connexion a: %s\n", ssid.c_str());
  updateBLEStatus(PROV_CONNECTING);

  // Afficher sur l'ecran
  displayWiFiConnecting(ssid);

  // Stocker temporairement
  wifiSSID = ssid;
  wifiPassword = password;

  if (connectWiFi()) {
    // Succes: sauvegarder et passer en mode normal
    updateBLEStatus(PROV_CONNECTED);
    saveWiFiConfig(ssid, password);

    // Recuperer le nom public
    loadPublicNameFromNVS();
    if (publicName.length() == 0) {
      fetchPublicName();
    }

    // Attendre que l'app lise le statut mis a jour
    delay(3000);

    // Passer en mode normal
    stopBLE();
    delay(500);

    // Redemarrer BLE en mode config
    initBLEConfigMode(deviceId);

    currentState = STATE_NORMAL;
    Serial.println("[PROV] -> STATE_NORMAL");

    // Demarrer le timer d'envoi (pas d'envoi immediat)
    lastDataSend = millis();
    co2Sum = 0;
    co2SampleCount = 0;
  } else {
    // Echec: rester en provisioning
    if (WiFi.status() == WL_CONNECT_FAILED) {
      updateBLEStatus(PROV_WRONG_PASSWORD);
    } else {
      updateBLEStatus(PROV_FAILED);
    }

    Serial.println("[PROV] Echec WiFi, retour au provisioning");
    delay(2000);
    updateBLEStatus(PROV_IDLE);
  }
}

// ============================================
// SETUP
// ============================================

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("================================");
  Serial.println("   KIT PEDAGOGIQUE CO2");
  Serial.println("   (Raspberry Pi Pico 2 W)");
  Serial.println("================================");
  Serial.println();

  // 1. Init I2C et OLED
  Wire.setSDA(OLED_SDA);
  Wire.setSCL(OLED_SCL);
  Wire.begin();

  Serial.println("Initialisation de l'ecran OLED...");
  display.init();
  display.flipScreenVertically();
  display.setContrast(255);

  // 2. Ecran de chargement
  showLoadingScreen();

  // 3. Generer deviceId
  generateDeviceId();

  // 4. Charger refreshRate depuis NVS
  int rate = loadRefreshRateFromNVS();
  Serial.printf("Refresh rate: %d secondes\n", rate);

  // 5. Init capteur CO2
  Serial.println("Initialisation du capteur CO2...");
  Serial1.setRX(S88_RX);
  Serial1.setTX(S88_TX);
  Serial1.begin(9600);
  co2Sensor.begin(Serial1);
  delay(1000);

  Serial.println("Premiere lecture du capteur...");
  readCO2Sensor();

  // 6. Verifier NVS pour credentials WiFi
  if (getStoredWiFiConfig(wifiSSID, wifiPassword)) {
    // Credentials trouvees, tenter connexion
    Serial.println("Credentials WiFi trouvees, tentative de connexion...");

    if (connectWiFi()) {
      // Connecte -> mode NORMAL
      currentState = STATE_NORMAL;
      Serial.println("-> STATE_NORMAL");

      // Recuperer/charger le nom public
      loadPublicNameFromNVS();
      if (publicName.length() == 0) {
        fetchPublicName();
      }

      // Demarrer le timer d'envoi (pas d'envoi immediat)
      lastDataSend = millis();
      co2Sum = 0;
      co2SampleCount = 0;

      // Demarrer BLE en mode config (arriere-plan)
      initBLEConfigMode(deviceId);
    } else {
      // Echec WiFi -> mode provisioning
      currentState = STATE_BLE_PROVISIONING;
      Serial.println("Echec WiFi -> STATE_BLE_PROVISIONING");
      initBLEProvisioning(deviceId);
    }
  } else {
    // Pas de credentials -> mode provisioning
    currentState = STATE_BLE_PROVISIONING;
    Serial.println("Pas de config WiFi -> STATE_BLE_PROVISIONING");
    initBLEProvisioning(deviceId);
  }

  Serial.println();
  Serial.println("Systeme pret!");
  if (publicName.length() > 0) {
    Serial.println("Nom public: " + publicName);
  }
  Serial.println();
}

// ============================================
// LOOP
// ============================================

void loop() {
  unsigned long currentTime = millis();

  // Traitement BLE (toujours actif)
  processBLE();

  // Lecture CO2 periodique (dans tous les modes)
  if (currentTime - lastCO2Read >= CO2_READ_INTERVAL) {
    lastCO2Read = currentTime;
    readCO2Sensor();
  }

  switch (currentState) {

  case STATE_BLE_PROVISIONING:
    // Verifier si config WiFi recue via BLE
    if (hasReceivedWiFiConfig()) {
      handleWiFiConfigReceived();
    }

    // Affichage BLE provisioning
    if (currentTime - lastDisplayUpdate >= 500) {
      lastDisplayUpdate = currentTime;
      displayBLEProvisioning(currentCO2);
    }
    break;

  case STATE_NORMAL:
    // Verifier changement de refresh rate via BLE
    if (checkAndResetRefreshRateChanged()) {
      Serial.printf("[MAIN] Refresh rate mis a jour: %d s\n",
                    getRefreshRateSeconds());
    }

    // Verifier si config WiFi recue via BLE (reconfig)
    if (hasReceivedWiFiConfig()) {
      handleWiFiConfigReceived();
    }

    // Envoi periodique au serveur
    if (sensorReady && wifiConnected &&
        (currentTime - lastDataSend >= getRefreshRateMs())) {
      lastDataSend = currentTime;
      int co2ToSend =
          (co2SampleCount > 0) ? (int)(co2Sum / co2SampleCount) : currentCO2;
      sendDataToServer(co2ToSend);
      co2Sum = 0;
      co2SampleCount = 0;
    }

    // Affichage CO2
    if (currentTime - lastDisplayUpdate >= 500) {
      lastDisplayUpdate = currentTime;
      if (sensorReady) {
        displayCO2(currentCO2);
      } else {
        displayError("Capteur non detecte");
      }
    }
    break;
  }

  delay(10);
}
