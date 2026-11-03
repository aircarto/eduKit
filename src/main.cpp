/**
 * ============================================
 *         KIT PEDAGOGIQUE CO2 (EduKit)
 * ============================================
 *
 * Materiel:
 *   - Raspberry Pi Pico 2 W
 *   - Capteur CO2 SensAir S88
 *   - Ecran OLED 0.96" SSD1306 (I2C, bicolore jaune/bleu)
 *
 * Cablage:
 *   OLED:
 *     - GND        -> GND
 *     - VDD        -> 3.3V (3V3 OUT)
 *     - SCK (SCL)  -> GPIO 5 (I2C0 SCL)
 *     - SDA        -> GPIO 4 (I2C0 SDA)
 *
 *   SensAir S88:
 *     - G0       -> GND
 *     - G+       -> VBUS (5V)
 *     - UART_TxD -> GPIO 1 (UART0 RX)
 *     - UART_RxD -> GPIO 0 (UART0 TX)
 *
 *   BME280 (optionnel, bus I2C separe de l'ecran):
 *     - VCC      -> 3.3V (3V3 OUT)
 *     - GND      -> GND
 *     - SDA      -> GPIO 6 (I2C1 SDA)
 *     - SCL      -> GPIO 7 (I2C1 SCL)
 *
 * Note: Le capteur S88 utilise le protocole Modbus RTU a 9600 bauds.
 *
 * OLED bicolore: les 16 premiers pixels (y 0-15) sont jaunes,
 * le reste (y 16-63) est bleu. Le layout est optimise en consequence.
 *
 * Reseau (meme mecanique que l'Alba et le ModuleAir mini):
 *   - En ligne : envoi des mesures, Bluetooth coupe.
 *   - Hors ligne : le CO2 reste affiche (icone reseau barree) et l'EduKit
 *     s'annonce en Bluetooth "EduKit-<deviceId>" (protocole Improv WiFi) :
 *     l'app AirCarto le detecte et lui envoie un reseau. Le reseau enregistre
 *     est retente en arriere-plan toutes les 10 minutes.
 *
 * Commandes serie (115200 bauds):
 *   r : oublier le reseau WiFi enregistre (repasse en annonce Bluetooth)
 *   i : afficher l'identite du capteur
 */

#include "SensAir_S88.h"
#include "ble_improv.h"
#include "lang.h"
#include "pico_compat.h"
#include "version.h"
#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <SSD1306Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <pico/unique_id.h>

// ============================================
// CONFIGURATION SERVEUR
// ============================================

const char *ENDPOINT_URL =
    "https://data.moduleair.fr/eduKit/edukit_endpoint.php";

// Identifiant unique du capteur (ID du Pico, cle cote serveur)
String deviceId = "";

// Nom Bluetooth annonce : "EduKit-<deviceId>". L'app lit le produit dans le
// prefixe et le deviceId dans les hexa de fin (services/improv-names.ts).
String bleName = "";

// ============================================
// CONFIGURATION DES PINS
// ============================================

#define OLED_SDA 4
#define OLED_SCL 5
#define OLED_ADDRESS 0x3C

#define S88_RX 1 // GPIO1 = UART0 RX
#define S88_TX 0 // GPIO0 = UART0 TX

// BME280 sur le second bus I2C : un mauvais branchement ne bloque pas l'ecran
#define BME_SDA 6 // GPIO6 = I2C1 SDA
#define BME_SCL 7 // GPIO7 = I2C1 SCL

// ============================================
// CONFIGURATION
// ============================================

#define CO2_READ_INTERVAL 2000
#define DISPLAY_INTERVAL 500

// Avec un BME280, l'ecran alterne : CO2 pendant 8 s, temperature, humidite et
// pression pendant 4 s.
#define SCREEN_CO2_MS 8000UL
#define SCREEN_ENV_MS 4000UL
// Sans mesure valide depuis ce delai, on cesse d'afficher la derniere valeur
// (capteur debranche en cours de route).
#define CO2_STALE_MS 10000

// Seuils de CO2
#define CO2_GOOD 800
#define CO2_BAD 1500

// Rythmes reseau (ceux de l'Alba / ModuleAir_V4)
#define WIFI_BOOT_TIMEOUT_MS 20000UL              // essai au demarrage
#define STA_RECONNECT_WINDOW_MS (3UL * 60 * 1000) // lien perdu : 3 min d'essais
#define STA_RECONNECT_KICK_MS 30000UL             // relance du driver
#define OFFLINE_RETRY_INTERVAL_MS (10UL * 60 * 1000)
#define OFFLINE_RETRY_TIMEOUT_MS 30000UL
#define PROVISION_TIMEOUT_MS 40000UL // filet : ble_improv tranche en 20 s
#define FIRST_SEND_DELAY_MS 15000UL  // 1er envoi peu apres la connexion

// ============================================
// MACHINE A ETATS RESEAU
// ============================================

enum NetState {
  NET_ONLINE,       // connecte au reseau enregistre, envoi des mesures
  NET_RECONNECTING, // lien perdu : on tente de le retrouver pendant 3 min
  NET_OFFLINE,      // annonce Bluetooth, nouvel essai toutes les 10 min
  NET_RETRYING,     // hors ligne, essai en arriere-plan du reseau enregistre
  NET_PROVISIONING, // l'app a envoye un reseau, ble_improv attend le verdict
};

NetState netState = NET_OFFLINE;
unsigned long netStateSince = 0;
unsigned long lastReconnectKick = 0;

// Reseau enregistre
String storedSSID = "";
String storedPassword = "";

// Reseau envoye par l'app, mis de cote tant qu'il n'a pas FAIT SES PREUVES :
// seule une connexion reussie l'enregistre, une faute de frappe n'ecrase donc
// jamais le reseau qui marchait.
String pendingSSID = "";
String pendingPassword = "";

// ============================================
// OBJETS GLOBAUX
// ============================================

SSD1306Wire display(OLED_ADDRESS, OLED_SDA, OLED_SCL);
SensAir_S88 co2Sensor;
Adafruit_BME280 bme;

// BME280 : present si detecte au demarrage, mesure valide si plausible
bool bmePresent = false;
bool bmeOk = false;
float bmeTemperature = 0;
float bmeHumidity = 0;
float bmePressure = 0; // hPa

// Variables
int currentCO2 = 0;
unsigned long lastCO2Ok = 0; // millis() de la derniere mesure valide (0 = jamais)
unsigned long lastCO2Read = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long nextDataSendAt = 0;
unsigned long screenHoldUntil = 0; // ecran transitoire (verdict WiFi) a garder
uint32_t co2Sum = 0;
uint16_t co2SampleCount = 0;
int refreshRateSeconds = 60;

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
    display.drawString(64, 2, TXT_BOOT_TITLE);

    // Zone bleue (16-63): pourcentage, barre de progression, version
    display.drawString(64, 26, String(progress) + "%");
    display.drawRect(progressBarX, progressBarY, progressBarWidth,
                     progressBarHeight);

    int fillWidth = (progress * (progressBarWidth - 4)) / 100;
    display.fillRect(progressBarX + 2, progressBarY + 2, fillWidth,
                     progressBarHeight - 4);

    // Version du firmware sous la barre, pour l'assistance
    display.drawString(64, 52, "v" FIRMWARE_VERSION);

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

void printIdentity() {
  Serial.println("================================");
  Serial.printf("  Firmware   : EduKit %s\n", FIRMWARE_VERSION);
  Serial.printf("  Device ID  : %s\n", deviceId.c_str());
  Serial.printf("  Bluetooth  : %s\n", bleName.c_str());
  Serial.printf("  Reseau     : %s\n",
                storedSSID.length() ? storedSSID.c_str() : "(aucun)");
  Serial.printf("  Envoi      : toutes les %d s\n", refreshRateSeconds);
  Serial.printf("  BME280     : %s\n", bmePresent ? "detecte" : "absent");
  Serial.println("================================");
}

// ============================================
// STOCKAGE PERSISTANT
// ============================================
// Memes cles que le firmware 1.x : un EduKit deja configure reste en ligne
// apres la mise a jour.

bool loadWiFiConfig() {
  Preferences prefs;
  prefs.begin("wifi", true);
  storedSSID = prefs.getString("ssid", "");
  storedPassword = prefs.getString("password", "");
  prefs.end();
  return storedSSID.length() > 0;
}

void saveWiFiConfig(const String &ssid, const String &password) {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();
  storedSSID = ssid;
  storedPassword = password;
  Serial.printf("[NVS] Reseau enregistre: %s\n", ssid.c_str());
}

void forgetWiFiConfig() {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.remove("ssid");
  prefs.remove("password");
  prefs.end();
  storedSSID = "";
  storedPassword = "";
  Serial.println("[NVS] Reseau oublie");
}

// Cadence d'envoi reglee par l'ancienne app (10, 30 ou 60 s), conservee si
// elle existe. 60 s sinon.
void loadRefreshRate() {
  Preferences prefs;
  prefs.begin("edukit", true);
  int rate = prefs.getInt("refresh_rate", 60);
  prefs.end();

  if (rate != 10 && rate != 30 && rate != 60) {
    rate = 60;
  }
  refreshRateSeconds = rate;
}

// ============================================
// ENVOI DES DONNEES AU SERVEUR
// ============================================

int sendDataToServer(int co2Value) {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  Serial.println("[HTTP] ================================");
  Serial.printf("[HTTP] Envoi des donnees a %s\n", ENDPOINT_URL);

  String jsonPayload = "{";
  jsonPayload += "\"device_id\":\"" + deviceId + "\",";
  jsonPayload += "\"signal_quality\":" + String(WiFi.RSSI()) + ",";
  jsonPayload += "\"co2\":" + String(co2Value) + ",";
  if (bmeOk) {
    jsonPayload += "\"temperature\":" + String(bmeTemperature, 1) + ",";
    jsonPayload += "\"humidity\":" + String(bmeHumidity, 1) + ",";
    jsonPayload += "\"pressure\":" + String(bmePressure, 1) + ",";
  }
  jsonPayload += "\"firmware_version\":\"" FIRMWARE_VERSION "\"";
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
// AFFICHAGE
// ============================================

bool isOnline() { return netState == NET_ONLINE && WiFi.status() == WL_CONNECTED; }

// Garde l'ecran courant (verdict WiFi) pendant `ms` avant de reprendre le CO2.
void holdScreen(unsigned long ms) { screenHoldUntil = millis() + ms; }

const char *getCO2Message(int co2) {
  if (co2 > -1 && co2 < CO2_GOOD)
    return TXT_CO2_GOOD;
  if (co2 >= CO2_GOOD && co2 < CO2_BAD)
    return TXT_CO2_VENTILATE;
  if (co2 >= CO2_BAD)
    return TXT_CO2_VENTILATE;
  return TXT_CO2_ERROR;
}

void displayWiFiConnecting(const String &ssid) {
  display.clear();

  // Zone jaune (0-15): titre
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 2, TXT_WIFI_CONNECTING);

  // Zone bleue (16-63): SSID
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 24, ssid);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 48, TXT_PLEASE_WAIT);
  display.display();
}

void displayWiFiResult(bool connected) {
  display.clear();

  // Zone jaune (0-15): titre
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 2, TXT_WIFI_RESULT_TITLE);

  // Zone bleue (16-63): statut
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 22, TXT_CONNECTION);
  display.drawString(64, 40, connected ? TXT_CONNECTION_OK : TXT_CONNECTION_FAILED);
  display.display();
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

  // Zone jaune (y 0-15): "CO2", etat de l'app et indicateur WiFi
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 2, "CO2");

  if (bleImprovUiState() == BLE_UI_LINKED) {
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 2, TXT_APP_LINKED);
  }

  if (isOnline()) {
    drawWiFiBars(WiFi.RSSI());
  } else {
    drawWiFiBarsDisconnected();
  }

  // Zone bleue (y 16-63): valeur CO2 + message
  const int valueY = 18;
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  if (co2 > 0) {
    display.drawString(64, valueY, String(co2));

    // "ppm" aligne verticalement sur les chiffres
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
    display.drawString(64, 20, TXT_WIRING_1);
    display.drawString(64, 40, TXT_WIRING_2);
  }

  display.display();
}

void displayEnvironment() {
  display.clear();

  // Zone jaune (y 0-15): titre et indicateur WiFi
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 2, TXT_ENV_TITLE);
  if (isOnline()) {
    drawWiFiBars(WiFi.RSSI());
  } else {
    drawWiFiBarsDisconnected();
  }

  // Zone bleue (y 16-63): temperature et humidite, puis pression
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  if (bmeOk) {
    display.drawString(64, 20,
                       String(bmeTemperature, 1) + " C   " +
                           String((int)(bmeHumidity + 0.5f)) + " %");
    display.drawString(64, 42, String((int)(bmePressure + 0.5f)) + " hPa");
  } else {
    display.drawString(64, 20, TXT_WIRING_1);
    display.drawString(64, 40, TXT_WIRING_2);
  }

  display.display();
}

// Avec un BME280, l'ecran CO2 laisse regulierement la place aux mesures
// d'ambiance.
bool showEnvironmentNow() {
  if (!bmePresent)
    return false;
  return millis() % (SCREEN_CO2_MS + SCREEN_ENV_MS) >= SCREEN_CO2_MS;
}

// CO2 a afficher : 0 si le capteur n'a jamais repondu ou s'est tu.
int displayedCO2() {
  if (lastCO2Ok == 0 || millis() - lastCO2Ok > CO2_STALE_MS)
    return 0;
  return currentCO2;
}

void updateDisplay() {
  if ((long)(millis() - screenHoldUntil) < 0)
    return; // un verdict WiFi est encore a l'ecran
  if (bleImprovUiState() == BLE_UI_CONNECTING) {
    displayWiFiConnecting(String(bleImprovSsid()));
    return;
  }
  if (showEnvironmentNow()) {
    displayEnvironment();
    return;
  }
  displayCO2(displayedCO2());
}

// Reflete a l'ecran une configuration faite depuis l'app.
void bleUiLoop() {
  static BleImprovUiState shown = BLE_UI_OFF;
  const BleImprovUiState s = bleImprovUiState();
  if (s == shown)
    return;
  shown = s;

  if (s == BLE_UI_SUCCESS) {
    displayWiFiResult(true);
    holdScreen(2000);
  } else if (s == BLE_UI_FAILED) {
    displayWiFiResult(false);
    holdScreen(2500);
  } else {
    // Connexion de l'app, tentative en cours, depart : on repeint tout de
    // suite plutot qu'au prochain rafraichissement.
    updateDisplay();
  }
}

// ============================================
// LECTURE DU CAPTEUR
// ============================================

void readCO2Sensor() {
  int co2Value = co2Sensor.getCO2();

  if (co2Value > 0) {
    currentCO2 = co2Value;
    lastCO2Ok = millis();
    Serial.printf("CO2: %d ppm\n", currentCO2);

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

void readBME280() {
  if (!bmePresent)
    return;
  float t = bme.readTemperature();
  float h = bme.readHumidity();
  float p = bme.readPressure() / 100.0f;

  // Un capteur debranche en cours de route rend des valeurs hors limites
  bmeOk = !isnan(t) && !isnan(h) && !isnan(p) && t > -40 && t < 85 &&
          h >= 0 && h <= 100 && p > 300 && p < 1100;
  if (bmeOk) {
    bmeTemperature = t;
    bmeHumidity = h;
    bmePressure = p;
    Serial.printf("BME280: %.1f C, %.1f %%, %.1f hPa\n", t, h, p);
  } else {
    Serial.println("Erreur lecture BME280");
  }
}

// ============================================
// RESEAU
// ============================================

void setNetState(NetState s) {
  netState = s;
  netStateSince = millis();
}

// Scanne les reseaux et les depose dans ble_improv, qui les servira a l'app.
// Bloquant (~2-4 s). JAMAIS avec une app connectee : la radio 2,4 GHz est
// partagee entre WiFi et Bluetooth (cf. ble_improv.h).
void scanNetworksForApp() {
  int n = WiFi.scanNetworks();
  if (n < 0)
    n = 0;
  if (n > 20)
    n = 20;

  String ssids[20];
  BleImprovNet nets[20];
  for (int i = 0; i < n; i++) {
    ssids[i] = WiFi.SSID(i);
    nets[i] = {ssids[i].c_str(), (int)WiFi.RSSI(i),
               WiFi.encryptionType(i) != ENC_TYPE_NONE};
  }
  bleImprovPushNetworks(nets, n); // copie : ssids[] sort de portee
  WiFi.scanDelete();
}

// Premier essai au demarrage, bloquant (le Bluetooth n'est pas encore lance).
bool connectWiFiAtBoot() {
  Serial.printf("[WiFi] Connexion a %s...\n", storedSSID.c_str());
  displayWiFiConnecting(storedSSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < WIFI_BOOT_TIMEOUT_MS) {
    delay(250);
  }

  bool ok = WiFi.status() == WL_CONNECTED;
  displayWiFiResult(ok);
  holdScreen(ok ? 1000 : 2000);
  return ok;
}

void goOnline() {
  Serial.printf("[WiFi] En ligne sur %s (IP %s, %d dBm)\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                (int)WiFi.RSSI());
  // Nouvelle fenetre de moyenne, premier envoi peu apres : l'app voit vite
  // arriver les mesures d'un EduKit qu'on vient de configurer.
  co2Sum = 0;
  co2SampleCount = 0;
  nextDataSendAt = millis() + FIRST_SEND_DELAY_MS;
  setNetState(NET_ONLINE);
  // Le Bluetooth s'arrete dans loop(), une fois que l'app a eu son verdict
  // (bleImprovCanStop).
}

// Hors ligne : on s'annonce en Bluetooth pour que l'app puisse configurer.
void goOffline() {
  Serial.println("[WiFi] Hors ligne -> annonce Bluetooth pour l'app");
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  // Avant l'annonce : la liste est prete des la premiere connexion de l'app.
  if (!bleImprovClientConnected())
    scanNetworksForApp();
  bleImprovStart(bleName, deviceId);
  setNetState(NET_OFFLINE);
}

// L'app a envoye un reseau. ble_improv lit WiFi.status() pour lui donner son
// verdict ; netLoop() suit ce verdict.
void onBleCredentials(const String &ssid, const String &password) {
  Serial.printf("[WiFi] Connexion a '%s' (envoye par l'app)...\n",
                ssid.c_str());
  pendingSSID = ssid;
  pendingPassword = password;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  setNetState(NET_PROVISIONING);
}

// Machine a etats reseau, 1 fois par seconde.
void netLoop() {
  static unsigned long lastTick = 0;
  unsigned long now = millis();
  if (now - lastTick < 1000)
    return;
  lastTick = now;

  switch (netState) {
  case NET_ONLINE:
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Lien perdu -> reconnexion");
      WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
      lastReconnectKick = now;
      setNetState(NET_RECONNECTING);
    }
    break;

  case NET_RECONNECTING:
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WiFi] Reconnecte (%d dBm)\n", (int)WiFi.RSSI());
      setNetState(NET_ONLINE);
    } else if (now - netStateSince > STA_RECONNECT_WINDOW_MS) {
      Serial.println("[WiFi] 3 min sans reseau");
      goOffline();
    } else if (now - lastReconnectKick > STA_RECONNECT_KICK_MS) {
      WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
      lastReconnectKick = now;
    }
    break;

  case NET_OFFLINE:
    if (now - netStateSince > OFFLINE_RETRY_INTERVAL_MS) {
      // Jamais sous les pieds de quelqu'un qui configure depuis l'app.
      if (bleImprovClientConnected() || bleImprovBusy()) {
        setNetState(NET_OFFLINE); // re-arme, essai dans 10 min
        break;
      }
      scanNetworksForApp(); // liste fraiche pour la prochaine visite de l'app
      if (storedSSID.length() == 0) {
        setNetState(NET_OFFLINE);
        break;
      }
      Serial.printf("[WiFi] Nouvel essai de %s en arriere-plan\n",
                    storedSSID.c_str());
      WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
      setNetState(NET_RETRYING);
    }
    break;

  case NET_RETRYING:
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] Reseau enregistre retrouve");
      goOnline();
    } else if (now - netStateSince > OFFLINE_RETRY_TIMEOUT_MS) {
      Serial.println("[WiFi] Toujours rien, prochain essai dans 10 min");
      WiFi.disconnect();
      setNetState(NET_OFFLINE);
    }
    break;

  case NET_PROVISIONING: {
    // ble_improv tranche (et le dit a l'app) ; on suit son verdict.
    const BleImprovUiState ble = bleImprovUiState();
    if (ble == BLE_UI_SUCCESS) {
      saveWiFiConfig(pendingSSID, pendingPassword);
      pendingPassword = "";
      goOnline();
    } else if (!bleImprovBusy() ||
               now - netStateSince > PROVISION_TIMEOUT_MS) {
      // Mauvais mot de passe / reseau absent : on reste visible pour que
      // l'utilisateur reessaie. Le reseau enregistre n'est pas touche.
      Serial.println("[WiFi] Reseau de l'app non rejoint");
      pendingPassword = "";
      WiFi.disconnect();
      setNetState(NET_OFFLINE);
    }
    break;
  }
  }
}

// ============================================
// COMMANDES SERIE
// ============================================

void checkSerialCommands() {
  if (Serial.available() <= 0)
    return;
  char cmd = Serial.read();

  if (cmd == 'r' || cmd == 'R') {
    forgetWiFiConfig();
    if (!bleImprovRunning()) {
      goOffline();
    } else if (!bleImprovBusy()) {
      WiFi.disconnect();
      setNetState(NET_OFFLINE);
    }
  } else if (cmd == 'i' || cmd == 'I') {
    printIdentity();
  }
}

// ============================================
// SETUP
// ============================================

void setup() {
  // Temoin d'alimentation : la LED du Pico s'allume des le demarrage, pour que
  // les eleves voient tout de suite que le 5V et le GND sont bien branches.
  // Sur le Pico 2 W elle est reliee a la puce CYW43, LED_BUILTIN la pilote.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  // Laisse le temps a l'USB de s'enumerer, sans bloquer un EduKit sur batterie.
  unsigned long serialStart = millis();
  while (!Serial && millis() - serialStart < 3000) {
    delay(10);
  }
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

  // 3. Identite
  generateDeviceId();
  bleName = "EduKit-" + deviceId;
  bleImprovOnCredentials(onBleCredentials);
  loadRefreshRate();
  loadWiFiConfig();
  printIdentity();

  // 4. Init capteur CO2
  Serial.println("Initialisation du capteur CO2...");
  Serial1.setRX(S88_RX);
  Serial1.setTX(S88_TX);
  Serial1.begin(9600);
  co2Sensor.begin(Serial1);
  delay(1000);

  Serial.println("Premiere lecture du capteur...");
  readCO2Sensor();

  // 4b. BME280 optionnel, sur son propre bus I2C (adresse 0x76 ou 0x77)
  Wire1.setSDA(BME_SDA);
  Wire1.setSCL(BME_SCL);
  Wire1.begin();
  bmePresent = bme.begin(0x76, &Wire1) || bme.begin(0x77, &Wire1);
  Serial.printf("BME280 %s\n", bmePresent ? "detecte" : "absent");
  readBME280();

  // 5. Reseau : le reseau enregistre, sinon l'annonce Bluetooth
  if (storedSSID.length() > 0 && connectWiFiAtBoot()) {
    goOnline();
  } else {
    if (storedSSID.length() == 0) {
      Serial.println("[WiFi] Aucun reseau enregistre");
    }
    goOffline();
  }

  Serial.println();
  Serial.println("Systeme pret!");
  Serial.println();
}

// ============================================
// LOOP
// ============================================

void loop() {
  unsigned long currentTime = millis();

  // Bluetooth et reseau
  bleImprovLoop();
  netLoop();
  // De retour en ligne : on cesse de s'annoncer, une fois le verdict lu.
  if (bleImprovRunning() && netState == NET_ONLINE && bleImprovCanStop()) {
    bleImprovStop();
  }
  bleUiLoop();

  // Lecture CO2 periodique
  if (currentTime - lastCO2Read >= CO2_READ_INTERVAL) {
    lastCO2Read = currentTime;
    readCO2Sensor();
    readBME280();
  }

  // Envoi periodique de la moyenne. L'envoi HTTPS bloque quelques secondes :
  // jamais tant que le Bluetooth doit encore son verdict a l'app.
  if (netState == NET_ONLINE && WiFi.status() == WL_CONNECTED &&
      !bleImprovRunning() && (long)(currentTime - nextDataSendAt) >= 0) {
    nextDataSendAt = currentTime + (unsigned long)refreshRateSeconds * 1000UL;
    if (co2SampleCount > 0) {
      int co2Avg = (int)(co2Sum / co2SampleCount);
      co2Sum = 0;
      co2SampleCount = 0;
      sendDataToServer(co2Avg);
    } else {
      Serial.println("[HTTP] Aucune mesure valide, rien a envoyer");
    }
  }

  // Affichage
  if (currentTime - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = currentTime;
    updateDisplay();
  }

  checkSerialCommands();

  delay(10);
}
