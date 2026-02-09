/**
 * ============================================
 *    BLE PROVISIONING - Implementation BTstackLib
 * ============================================
 *
 * Implementation pour Raspberry Pi Pico W utilisant BTstackLib.
 */

#include "ble_provisioning.h"
#include "pico_compat.h"

#include <BTstackLib.h>
#include <WiFi.h>
#include <pico/unique_id.h>

// Include direct de l'API C BTstack pour les notifications
extern "C" {
#include "ble/att_server.h"
}

// ============================================
// Characteristic IDs (pour dispatch callbacks)
// ============================================

#define CHARID_DEVICE_INFO    0
#define CHARID_WIFI_NETWORKS  1
#define CHARID_WIFI_CONFIG    2
#define CHARID_STATUS         3
#define CHARID_CO2_DATA       4
#define CHARID_SETTINGS       5

// ============================================
// Etat interne
// ============================================

static bool bleActive = false;
static bool bleProvisioningMode = false; // true = provisioning, false = config
static bool clientConnected = false;
static hci_con_handle_t connectionHandle = HCI_CON_HANDLE_INVALID;

// Value handles retournes par addGATTCharacteristicDynamic
static uint16_t handleDeviceInfo = 0;
static uint16_t handleWifiNetworks = 0;
static uint16_t handleWifiConfig = 0;
static uint16_t handleStatus = 0;
static uint16_t handleCO2Data = 0;
static uint16_t handleSettings = 0;

// Donnees BLE
static String bleDeviceName = "";
static String bleDeviceId = "";
static String cachedDeviceInfoJson = "";
static String cachedWifiNetworksJson = "";
static String cachedStatusStr = "0";
static String cachedCO2Str = "0";
static String cachedSettingsJson = "{\"refreshRate\":60}";

// Config WiFi recue
static bool wifiConfigReceived = false;
static String receivedSSID = "";
static String receivedPassword = "";

// Settings
static int refreshRateSeconds = 60;
static bool refreshRateChanged = false;

// NVS
static Preferences nvs;

// ============================================
// Fonctions utilitaires internes
// ============================================

static String generateDeviceInfoJson(const String &publicName) {
  String json = "{\"chipId\":\"" + bleDeviceId + "\",";
  json += "\"version\":\"EduKit-v1.0\",";
  if (publicName.length() > 0) {
    json += "\"publicName\":\"" + publicName + "\"";
  } else {
    json += "\"publicName\":\"" + bleDeviceName + "\"";
  }
  json += "}";
  return json;
}

static void scanWiFiNetworks() {
  Serial.println("[BLE] Scan des reseaux WiFi...");
  int n = WiFi.scanNetworks();

  String json = "[";
  for (int i = 0; i < n && i < 10; i++) {
    if (i > 0) json += ",";
    json += "{\"s\":\"";
    json += WiFi.SSID(i);
    json += "\",\"r\":";
    json += String(WiFi.RSSI(i));
    json += ",\"e\":";
    json += String(WiFi.encryptionType(i) != ENC_TYPE_NONE ? 1 : 0);
    json += "}";
  }
  json += "]";

  WiFi.scanDelete();
  cachedWifiNetworksJson = json;
  Serial.printf("[BLE] %d reseaux trouves\n", n);
}

static void parseWiFiConfig(uint8_t *buffer, uint16_t size) {
  String value = "";
  for (uint16_t i = 0; i < size; i++) {
    value += (char)buffer[i];
  }
  Serial.println("[BLE] WiFi config recue");

  int ssidStart = value.indexOf("\"ssid\":\"");
  int pwdStart = value.indexOf("\"password\":\"");

  if (ssidStart != -1 && pwdStart != -1) {
    ssidStart += 8;
    int ssidEnd = value.indexOf("\"", ssidStart);
    receivedSSID = value.substring(ssidStart, ssidEnd);

    pwdStart += 12;
    int pwdEnd = value.indexOf("\"", pwdStart);
    receivedPassword = value.substring(pwdStart, pwdEnd);

    Serial.printf("[BLE] SSID: %s\n", receivedSSID.c_str());
    Serial.println("[BLE] Password: ********");

    wifiConfigReceived = true;
  } else {
    Serial.println("[BLE] Format JSON invalide");
  }
}

static void parseSettingsConfig(uint8_t *buffer, uint16_t size) {
  String value = "";
  for (uint16_t i = 0; i < size; i++) {
    value += (char)buffer[i];
  }
  Serial.println("[BLE] Settings config recue: " + value);

  int rateStart = value.indexOf("\"refreshRate\":");
  if (rateStart != -1) {
    rateStart += 14;
    int rateEnd = rateStart;
    while (rateEnd < (int)value.length() && value[rateEnd] >= '0' &&
           value[rateEnd] <= '9') {
      rateEnd++;
    }

    if (rateEnd > rateStart) {
      int newRate = value.substring(rateStart, rateEnd).toInt();

      if (newRate == 10 || newRate == 30 || newRate == 60) {
        refreshRateSeconds = newRate;
        refreshRateChanged = true;
        cachedSettingsJson = "{\"refreshRate\":" + String(refreshRateSeconds) + "}";
        Serial.printf("[BLE] Nouveau refresh rate: %d secondes\n", refreshRateSeconds);

        Preferences prefs;
        prefs.begin("edukit", false);
        prefs.putInt("refresh_rate", refreshRateSeconds);
        prefs.end();
        Serial.println("[NVS] Refresh rate sauvegarde");
      } else {
        Serial.printf("[BLE] Valeur invalide: %d (doit etre 10, 30 ou 60)\n", newRate);
      }
    }
  } else {
    Serial.println("[BLE] Format JSON invalide pour settings");
  }
}

// ============================================
// Callbacks BTstackLib
// ============================================

static void deviceConnectedCallback(BLEStatus status, BLEDevice *device) {
  if (status == BLE_STATUS_OK) {
    clientConnected = true;
    connectionHandle = device->getHandle();
    Serial.println("[BLE] Client connecte");
  }
}

static void deviceDisconnectedCallback(BLEDevice *device) {
  (void)device;
  clientConnected = false;
  connectionHandle = HCI_CON_HANDLE_INVALID;
  Serial.println("[BLE] Client deconnecte");
  BTstack.startAdvertising();
}

static uint16_t gattReadCallback(uint16_t value_handle, uint8_t *buffer, uint16_t buffer_size) {
  const char *data = nullptr;
  uint16_t data_len = 0;

  if (value_handle == handleDeviceInfo) {
    data = cachedDeviceInfoJson.c_str();
    data_len = cachedDeviceInfoJson.length();
  } else if (value_handle == handleWifiNetworks) {
    data = cachedWifiNetworksJson.c_str();
    data_len = cachedWifiNetworksJson.length();
  } else if (value_handle == handleStatus) {
    data = cachedStatusStr.c_str();
    data_len = cachedStatusStr.length();
  } else if (value_handle == handleCO2Data) {
    data = cachedCO2Str.c_str();
    data_len = cachedCO2Str.length();
  } else if (value_handle == handleSettings) {
    data = cachedSettingsJson.c_str();
    data_len = cachedSettingsJson.length();
  } else {
    return 0;
  }

  if (buffer) {
    uint16_t copy_len = (data_len < buffer_size) ? data_len : buffer_size;
    memcpy(buffer, data, copy_len);
    return copy_len;
  }
  return data_len;
}

static int gattWriteCallback(uint16_t value_handle, uint8_t *buffer, uint16_t size) {
  if (value_handle == handleWifiConfig) {
    parseWiFiConfig(buffer, size);
    return 0;
  } else if (value_handle == handleSettings) {
    parseSettingsConfig(buffer, size);
    return 0;
  }
  return 0;
}

// ============================================
// Construction des donnees d'advertising
// ============================================

static void setupAdvertisingData(const String &name) {
  // Advertisement data: flags + local name
  static uint8_t advData[31];
  uint16_t pos = 0;

  // Flags: LE General Discoverable + BR/EDR Not Supported
  advData[pos++] = 0x02; // length
  advData[pos++] = 0x01; // AD Type: Flags
  advData[pos++] = 0x06; // value

  // Complete Local Name
  uint8_t nameLen = name.length();
  if (nameLen > 26) nameLen = 26; // 31 - 3 (flags) - 2 (name header)
  advData[pos++] = nameLen + 1; // length
  advData[pos++] = 0x09;        // AD Type: Complete Local Name
  memcpy(&advData[pos], name.c_str(), nameLen);
  pos += nameLen;

  BTstack.setAdvData(pos, advData);

  // Scan response: 128-bit service UUID
  static uint8_t scanData[31];
  uint16_t scanPos = 0;

  // 128-bit UUID (reversed byte order for BLE)
  // SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
  static const uint8_t serviceUuid128[] = {
    0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
    0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f
  };

  scanData[scanPos++] = 0x11; // length = 17 (1 type + 16 uuid)
  scanData[scanPos++] = 0x07; // AD Type: Complete 128-bit Service UUIDs
  memcpy(&scanData[scanPos], serviceUuid128, 16);
  scanPos += 16;

  BTstack.setScanData(scanPos, scanData);
}

// ============================================
// Fonction d'init commune
// ============================================

static void initBLECommon(const String &deviceId, bool provisioningMode) {
  bleDeviceId = deviceId;
  bleProvisioningMode = provisioningMode;

  // Generer le nom BLE (premiers caracteres du chip id)
  String suffix = deviceId;
  if (suffix.length() > 6) {
    suffix = suffix.substring(0, 6);
  }

  if (provisioningMode) {
    bleDeviceName = "edukit-" + suffix;
  } else {
    bleDeviceName = "edukit-cfg-" + suffix;
  }

  Serial.println("[BLE] ================================");
  Serial.printf("[BLE] Initialisation: %s\n", bleDeviceName.c_str());
  Serial.printf("[BLE] Mode: %s\n", provisioningMode ? "provisioning" : "config");

  // Charger le publicName depuis NVS pour le device info
  Preferences prefs;
  prefs.begin("edukit", true);
  String storedPublicName = prefs.getString("public_name", "");
  prefs.end();
  cachedDeviceInfoJson = generateDeviceInfoJson(storedPublicName);

  // Preparer les settings
  cachedSettingsJson = "{\"refreshRate\":" + String(refreshRateSeconds) + "}";

  // Statut initial
  if (provisioningMode) {
    cachedStatusStr = String(PROV_IDLE);
  } else {
    cachedStatusStr = String(PROV_CONNECTED); // Deja configure
  }

  // Callbacks
  BTstack.setBLEDeviceConnectedCallback(deviceConnectedCallback);
  BTstack.setBLEDeviceDisconnectedCallback(deviceDisconnectedCallback);
  BTstack.setGATTCharacteristicRead(gattReadCallback);
  BTstack.setGATTCharacteristicWrite(gattWriteCallback);

  // Service GATT
  BTstack.addGATTService(new UUID(SERVICE_UUID));

  // Caracteristiques - toutes dynamiques pour pouvoir mettre a jour les donnees
  handleDeviceInfo = BTstack.addGATTCharacteristicDynamic(
      new UUID(CHAR_DEVICE_INFO_UUID), ATT_PROPERTY_READ, CHARID_DEVICE_INFO);

  if (provisioningMode) {
    // WiFi scan
    scanWiFiNetworks();

    handleWifiNetworks = BTstack.addGATTCharacteristicDynamic(
        new UUID(CHAR_WIFI_NETWORKS_UUID), ATT_PROPERTY_READ, CHARID_WIFI_NETWORKS);

    handleWifiConfig = BTstack.addGATTCharacteristicDynamic(
        new UUID(CHAR_WIFI_CONFIG_UUID), ATT_PROPERTY_WRITE, CHARID_WIFI_CONFIG);
  }

  handleStatus = BTstack.addGATTCharacteristicDynamic(
      new UUID(CHAR_STATUS_UUID), ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY, CHARID_STATUS);

  handleCO2Data = BTstack.addGATTCharacteristicDynamic(
      new UUID(CHAR_CO2_DATA_UUID), ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY, CHARID_CO2_DATA);

  handleSettings = BTstack.addGATTCharacteristicDynamic(
      new UUID(CHAR_SETTINGS_UUID), ATT_PROPERTY_READ | ATT_PROPERTY_WRITE, CHARID_SETTINGS);

  // Advertising data
  setupAdvertisingData(bleDeviceName);

  // Demarrer BTstack
  BTstack.setup(bleDeviceName.c_str());
  BTstack.startAdvertising();

  bleActive = true;

  Serial.println("[BLE] Advertising demarre");
  Serial.println("[BLE] En attente de connexion...");
  Serial.println("[BLE] ================================");
}

// ============================================
// Fonctions publiques BLE
// ============================================

void initBLEProvisioning(const String &deviceId) {
  initBLECommon(deviceId, true);
}

void initBLEConfigMode(const String &deviceId) {
  initBLECommon(deviceId, false);
}

void stopBLE() {
  if (bleActive) {
    BTstack.stopAdvertising();
    bleActive = false;
    clientConnected = false;
    connectionHandle = HCI_CON_HANDLE_INVALID;
    Serial.println("[BLE] Arrete");
  }
}

void processBLE() {
  if (bleActive) {
    BTstack.loop();
  }
}

// ============================================
// Getters / Setters
// ============================================

bool hasReceivedWiFiConfig() {
  return wifiConfigReceived;
}

String getReceivedSSID() {
  return receivedSSID;
}

String getReceivedPassword() {
  return receivedPassword;
}

void clearReceivedWiFiConfig() {
  wifiConfigReceived = false;
  receivedSSID = "";
  receivedPassword = "";
}

void updateBLECO2Value(int co2ppm) {
  cachedCO2Str = String(co2ppm);

  // Envoyer notification si client connecte
  if (bleActive && clientConnected && connectionHandle != HCI_CON_HANDLE_INVALID) {
    const uint8_t *data = (const uint8_t *)cachedCO2Str.c_str();
    att_server_notify(connectionHandle, handleCO2Data, data, cachedCO2Str.length());
  }
}

void updateBLEStatus(ProvisioningStatus status) {
  cachedStatusStr = String(status);
  Serial.printf("[BLE] Statut: %d\n", status);

  // Envoyer notification si client connecte
  if (bleActive && clientConnected && connectionHandle != HCI_CON_HANDLE_INVALID) {
    const uint8_t *data = (const uint8_t *)cachedStatusStr.c_str();
    att_server_notify(connectionHandle, handleStatus, data, cachedStatusStr.length());
  }
}

bool isBLEClientConnected() {
  return clientConnected;
}

String getBLEDeviceName() {
  return bleDeviceName;
}

// ============================================
// Fonctions NVS
// ============================================

bool hasStoredWiFiConfig() {
  nvs.begin("wifi", true);
  String ssid = nvs.getString("ssid", "");
  nvs.end();
  return ssid.length() > 0;
}

bool getStoredWiFiConfig(String &ssid, String &password) {
  nvs.begin("wifi", true);
  ssid = nvs.getString("ssid", "");
  password = nvs.getString("password", "");
  nvs.end();

  if (ssid.length() > 0) {
    Serial.printf("[NVS] Config WiFi trouvee: %s\n", ssid.c_str());
    return true;
  }
  Serial.println("[NVS] Pas de config WiFi stockee");
  return false;
}

void saveWiFiConfig(const String &ssid, const String &password) {
  nvs.begin("wifi", false);
  nvs.putString("ssid", ssid);
  nvs.putString("password", password);
  nvs.end();
  Serial.printf("[NVS] Config WiFi sauvegardee: %s\n", ssid.c_str());
}

void clearWiFiConfig() {
  nvs.begin("wifi", false);
  nvs.clear();
  nvs.end();
  Serial.println("[NVS] Config WiFi effacee");
}

int loadRefreshRateFromNVS() {
  Preferences prefs;
  prefs.begin("edukit", true);
  int rate = prefs.getInt("refresh_rate", 60);
  prefs.end();

  if (rate != 10 && rate != 30 && rate != 60) {
    rate = 60;
  }

  refreshRateSeconds = rate;
  cachedSettingsJson = "{\"refreshRate\":" + String(refreshRateSeconds) + "}";
  Serial.printf("[NVS] Refresh rate charge: %d secondes\n", rate);
  return rate;
}

int getRefreshRateSeconds() {
  return refreshRateSeconds;
}

unsigned long getRefreshRateMs() {
  return (unsigned long)refreshRateSeconds * 1000UL;
}

bool checkAndResetRefreshRateChanged() {
  if (refreshRateChanged) {
    refreshRateChanged = false;
    return true;
  }
  return false;
}
