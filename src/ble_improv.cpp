/**
 * ============================================
 *    CONFIGURATION WIFI PAR BLUETOOTH - Implementation
 * ============================================
 *
 * Portage direct de l'Alba (Pico2W_mhz19b_V6/src/ble_improv.cpp), lui-meme
 * portage du ModuleAir mini. Voir ble_improv.h pour les regles du jeu.
 */

#include "ble_improv.h"

#include <BLE.h>
#include <BluetoothLock.h>
#include <WiFi.h>
#include <btstack.h>

// ============================================
// UUIDs Improv WiFi
// ============================================
// A NE JAMAIS TOUCHER : ce sont EXACTEMENT ceux de ModuleAir_V4, que l'app a
// en dur (services/improv-ble.ts). Un seul chiffre qui differe et l'EduKit
// devient invisible.

#define SERVICE_UUID       "00467768-6228-2272-4663-277478268000"
#define CHAR_CURRENT_STATE "00467768-6228-2272-4663-277478268001"
#define CHAR_ERROR_STATE   "00467768-6228-2272-4663-277478268002"
#define CHAR_RPC_COMMAND   "00467768-6228-2272-4663-277478268003"
#define CHAR_RPC_RESULT    "00467768-6228-2272-4663-277478268004"
#define CHAR_CAPABILITIES  "00467768-6228-2272-4663-277478268005"
// Caracteristique MAISON (hors spec Improv) : les reseaux vus par le capteur,
// une notification par reseau ("ssid\trssi\tsecure"), puis "END".
#define CHAR_WIFI_SCAN     "00467768-6228-2272-4663-277478268010"

// Etats Improv
#define STATE_AUTHORIZED   0x02
#define STATE_PROVISIONING 0x03
#define STATE_PROVISIONED  0x04

// Erreurs Improv
#define ERROR_NONE              0x00
#define ERROR_INVALID_RPC       0x01
#define ERROR_UNKNOWN_RPC       0x02
#define ERROR_UNABLE_TO_CONNECT 0x03

// Commandes RPC
#define CMD_WIFI_SETTINGS 0x01
#define CMD_IDENTIFY      0x02

// ============================================
// Rythmes
// ============================================

// 120 ms entre deux lignes de la liste : la valeur mesuree sur ModuleAir.
// En dessous un iPhone perd des lignes ; au-dessus 20 reseaux mettent plus de
// 3 s a arriver.
static const uint32_t NOTIFY_GAP_MS = 120;
// Verdict de connexion : confortablement sous les 30 s d'attente de l'app.
static const uint32_t CONNECT_TIMEOUT_MS = 20000;
// Les premiers status() apres un WiFi.begin() sont du bruit : on ne croit un
// verdict negatif qu'apres ce delai de grace.
static const uint32_t CONNECT_EARLY_FAIL_MS = 2500;
// Combien de temps on reste VISIBLE apres avoir annonce un succes. Le lien BLE
// saute vraiment pendant la bascule WiFi (antenne partagee) et l'app se
// reconnecte alors pour LIRE le verdict. "Plus personne n'est connecte" ne
// veut PAS dire "l'app a fini".
static const uint32_t LINGER_AFTER_SUCCESS_MS = 8000;

static const int MAX_NETS = 20;

// ============================================
// Caracteristique a notifications pilotees a la main
// ============================================
// Cf. note 5 de ble_improv.h. put() stocke la valeur (toujours lisible par
// l'app) et marque une notification en attente ; flush() l'envoie des que
// BTstack a de la place. Les deux tournent sur la boucle principale, sous le
// verrou Bluetooth : le contexte Bluetooth ne voit jamais un tampon a moitie
// ecrit.

class ImprovCharacteristic : public BLECharacteristic {
public:
  ImprovCharacteristic(const char *uuid, uint16_t props)
      : BLECharacteristic(BLEUUID(uuid), props) {}

  void put(const uint8_t *data, size_t len) {
    BluetoothLock lock;
    _charData = realloc(_charData, len ? len : 1);
    memcpy(_charData, data, len);
    _charLen = len;
    _pending = true;
  }

  void put(const char *s) { put((const uint8_t *)s, strlen(s)); }

  // True quand l'app connectee a active les notifications sur cette valeur.
  bool subscribed() const { return _notificationEnabled && con_handle; }

  bool pending() const { return _pending; }

  // Envoie la notification en attente. True une fois qu'il ne reste rien.
  bool flush() {
    if (!_pending)
      return true;
    BluetoothLock lock;
    if (!_notificationEnabled || !con_handle) {
      _pending = false; // personne n'ecoute : la valeur reste lisible
      return true;
    }
    if (!att_server_can_send_packet_now(con_handle))
      return false; // on reessaie au tour suivant
    att_server_notify(con_handle, _valueHandle, (uint8_t *)_charData,
                      _charLen);
    _pending = false;
    return true;
  }

private:
  bool _pending = false;
};

// ============================================
// Etat du module
// ============================================

static BLEService *improvService = nullptr;
static ImprovCharacteristic *charState = nullptr;
static ImprovCharacteristic *charError = nullptr;
static ImprovCharacteristic *charRpcCmd = nullptr;
static ImprovCharacteristic *charRpcResult = nullptr;
static ImprovCharacteristic *charCapabilities = nullptr;
static ImprovCharacteristic *charWifiScan = nullptr;

static BleImprovCredentialsCallback credentialsCb = nullptr;
static bool stackBuilt = false;
static bool bleRunning = false;
static String myDeviceId;

// Liste des reseaux, recopiee depuis main.cpp puis emise ligne par ligne.
struct NetEntry {
  char ssid[33];
  int rssi;
  bool secure;
};
static NetEntry netList[MAX_NETS];
static int netCount = 0;
static int netCursor = -1; // >= 0 : emission en cours, index de la suivante
static uint32_t nextNotifyAt = 0;
static bool sendingNets = false;
// Liste deja servie a CE client ; re-armee a la deconnexion pour que le
// suivant ait la sienne. On sonde l'abonnement plutot que de dependre d'un
// callback (la mecanique eprouvee du ModuleAir).
static bool scanServed = false;

// ============================================
// Ce qui traverse la frontiere contexte Bluetooth / loop()
// ============================================
// REGLE ABSOLUE : les callbacks ne posent que des drapeaux scalaires et
// copient des octets dans des tampons fixes. Pas de String, pas de malloc,
// pas de Serial, pas d'appel a la pile BLE.

static volatile bool evtLinked = false;
static volatile bool evtUnlinked = false;
static volatile bool evtCreds = false;
static volatile bool evtBadRpc = false;
static volatile bool evtUnknownRpc = false;
static volatile bool evtIdentify = false;
static volatile bool clientLinked = false;

// Ecrits par le callback, lus par loop(). evtCreds est pose EN DERNIER : c'est
// lui qui publie les tampons.
static char credSsid[33] = "";
static char credPass[65] = "";
static bool pendingConnect = false;

static bool connecting = false;
static uint32_t connectStart = 0;

static BleImprovUiState uiState = BLE_UI_OFF;
static char uiSsid[33] = "";
static uint32_t verdictAt = 0;

// ============================================
// Utilitaires
// ============================================

static void setState(uint8_t state) { charState->put(&state, 1); }

static void setError(uint8_t error) { charError->put(&error, 1); }

// Resultat RPC : [longueur totale, commande, (longueur, octets)*] -- le format
// Improv que l'app decode tel quel (parseRpcStrings).
static void sendRpcResult(uint8_t command, const String *strings, int count) {
  uint8_t data[64];
  size_t n = 2;
  for (int i = 0; i < count; i++) {
    size_t len = strings[i].length();
    if (n + 1 + len > sizeof(data))
      break;
    data[n++] = (uint8_t)len;
    memcpy(data + n, strings[i].c_str(), len);
    n += len;
  }
  data[0] = (uint8_t)(n - 1); // octet de commande + chaines
  data[1] = command;
  charRpcResult->put(data, n);
}

// Retour a "pret", sans toucher a l'annonce ni au serveur GATT.
static void resetSession() {
  sendingNets = false;
  scanServed = false;
  netCursor = -1;
  connecting = false;
  pendingConnect = false;
  credSsid[0] = 0;
  credPass[0] = 0;
  uiSsid[0] = 0;
}

// ============================================
// Callbacks BTstack (contexte Bluetooth : drapeaux uniquement)
// ============================================

class RpcCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    const uint8_t *val = (const uint8_t *)c->valueData();
    const size_t len = c->valueLen();
    if (!val || len < 2) {
      evtBadRpc = true;
      return;
    }

    const uint8_t command = val[1];
    if (command == CMD_WIFI_SETTINGS) {
      // Trame : [longueur, CMD, lenSSID, ssid..., lenPw, pw...]
      size_t pos = 2;
      if (pos >= len) {
        evtBadRpc = true;
        return;
      }
      const uint8_t ssidLen = val[pos++];
      if (pos + ssidLen > len) {
        evtBadRpc = true;
        return;
      }
      char ssid[sizeof(credSsid)] = "";
      size_t n = 0;
      for (uint8_t i = 0; i < ssidLen; i++) {
        const char ch = (char)val[pos++];
        if (n < sizeof(ssid) - 1)
          ssid[n++] = ch; // un SSID fait 32 octets au plus
      }
      ssid[n] = 0;

      const uint8_t pwLen = (pos < len) ? val[pos++] : 0;
      char pass[sizeof(credPass)] = "";
      n = 0;
      for (uint8_t i = 0; i < pwLen && pos < len; i++) {
        const char ch = (char)val[pos++];
        if (n < sizeof(pass) - 1)
          pass[n++] = ch;
      }
      pass[n] = 0;

      memcpy(credSsid, ssid, sizeof(credSsid));
      memcpy(credPass, pass, sizeof(credPass));
      evtCreds = true; // EN DERNIER : publie les tampons
    } else if (command == CMD_IDENTIFY) {
      evtIdentify = true;
    } else {
      evtUnknownRpc = true;
    }
  }
};
static RpcCallbacks rpcCallbacks;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    clientLinked = true;
    evtLinked = true;
  }
  void onDisconnect(BLEServer *) override {
    clientLinked = false;
    evtUnlinked = true;
    // Pas d'appel a l'annonce ici : BTstack la reprend tout seul une fois le
    // lien ferme (hci.c la reactive pour le role peripherique).
  }
};
static ServerCallbacks serverCallbacks;

// ============================================
// Pile / annonce
// ============================================

// Reponse de scan : l'UUID 128 bits du service. Il ne tient pas dans les 31
// octets de l'annonce a cote du nom complet, et c'est le NOM qui porte le
// deviceId (l'identite du capteur cote serveur) -- il ne doit jamais etre
// tronque. BTstack garde un pointeur sur ce tampon, d'ou le static.
static uint8_t scanResponse[18];

static void buildStack(const String &deviceName) {
  Serial.printf("[BLE] Construction de la pile sous '%s' (tas libre %u)\n",
                deviceName.c_str(), (unsigned)rp2040.getFreeHeap());
  BLE.begin(deviceName);

  improvService = new BLEService(BLEUUID(SERVICE_UUID));

  charState = new ImprovCharacteristic(CHAR_CURRENT_STATE, BLERead | BLENotify);
  charError = new ImprovCharacteristic(CHAR_ERROR_STATE, BLERead | BLENotify);
  charRpcCmd = new ImprovCharacteristic(CHAR_RPC_COMMAND, BLEWrite);
  charRpcResult = new ImprovCharacteristic(CHAR_RPC_RESULT, BLERead | BLENotify);
  charCapabilities = new ImprovCharacteristic(CHAR_CAPABILITIES, BLERead);
  charWifiScan = new ImprovCharacteristic(CHAR_WIFI_SCAN, BLERead | BLENotify);

  charRpcCmd->setCallbacks(&rpcCallbacks);

  uint8_t initState = STATE_AUTHORIZED;
  charState->put(&initState, 1);
  uint8_t initError = ERROR_NONE;
  charError->put(&initError, 1);
  uint8_t caps = 0x01; // bit 0 = commande "identify" supportee
  charCapabilities->put(&caps, 1);
  charWifiScan->put("pending");

  improvService->addCharacteristic(charState);
  improvService->addCharacteristic(charError);
  improvService->addCharacteristic(charRpcCmd);
  improvService->addCharacteristic(charRpcResult);
  improvService->addCharacteristic(charCapabilities);
  improvService->addCharacteristic(charWifiScan);

  BLE.server()->setCallbacks(&serverCallbacks);
  BLE.server()->addService(improvService);

  // L'annonce est en little-endian ; BLEUUID stocke en big-endian.
  BLEUUID uuid(SERVICE_UUID);
  scanResponse[0] = 17;
  scanResponse[1] =
      BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS;
  for (int i = 0; i < 16; i++)
    scanResponse[2 + i] = uuid.uuid128[15 - i];

  stackBuilt = true;
  Serial.printf("[BLE] Pile construite (tas libre %u)\n",
                (unsigned)rp2040.getFreeHeap());
}

static void startAdvertising() {
  BluetoothLock lock;
  // Flags + nom complet ("EduKit-4430353234313938", 23 octets) : ca tient.
  BLE.startAdvertising(false);
  gap_scan_response_set_data(sizeof(scanResponse), scanResponse);
}

// ============================================
// API publique
// ============================================

void bleImprovOnCredentials(BleImprovCredentialsCallback cb) {
  credentialsCb = cb;
}

bool bleImprovRunning() { return bleRunning; }

BleImprovUiState bleImprovUiState() { return uiState; }

const char *bleImprovSsid() { return uiSsid; }

bool bleImprovClientConnected() { return bleRunning && clientLinked; }

bool bleImprovBusy() {
  return bleRunning && (pendingConnect || connecting || evtCreds);
}

bool bleImprovCanStop() {
  if (!bleRunning)
    return true;
  // Verdict pas encore parti : couper maintenant, c'est un capteur qui rejoint
  // le reseau pendant que l'app annonce un echec.
  if (connecting || pendingConnect)
    return false;
  if (charState->pending() || charRpcResult->pending())
    return false;
  if (uiState == BLE_UI_SUCCESS)
    return millis() - verdictAt > LINGER_AFTER_SUCCESS_MS;
  return true;
}

void bleImprovPushNetworks(const BleImprovNet *nets, int count) {
  // Jamais pendant l'emission : le curseur pointe dans la liste.
  if (sendingNets)
    return;
  netCount = 0;
  for (int i = 0; i < count && netCount < MAX_NETS; i++) {
    if (!nets[i].ssid || !nets[i].ssid[0])
      continue; // reseau masque
    strncpy(netList[netCount].ssid, nets[i].ssid, 32);
    netList[netCount].ssid[32] = 0;
    netList[netCount].rssi = nets[i].rssi;
    netList[netCount].secure = nets[i].secure;
    netCount++;
  }
  Serial.printf("[BLE] %d reseaux en reserve\n", netCount);
}

void bleImprovStart(const String &deviceName, const String &deviceId) {
  if (bleRunning)
    return;
  myDeviceId = deviceId;

  // Construite une fois, gardee a vie : demonter puis remonter une pile BLE
  // est la source classique de plantage a la toute fin d'une configuration
  // reussie (c'est ce qui faisait redemarrer le mini sous NimBLE).
  if (!stackBuilt)
    buildStack(deviceName);

  resetSession();
  evtLinked = evtUnlinked = evtCreds = false;
  evtBadRpc = evtUnknownRpc = evtIdentify = false;
  clientLinked = false;

  uint8_t s = STATE_AUTHORIZED;
  charState->put(&s, 1);
  uint8_t e = ERROR_NONE;
  charError->put(&e, 1);

  startAdvertising();
  uiState = BLE_UI_ADVERTISING;
  bleRunning = true;
  Serial.printf("[BLE] Annonce en cours sous '%s' (tas libre %u)\n",
                deviceName.c_str(), (unsigned)rp2040.getFreeHeap());
}

void bleImprovStop() {
  if (!bleRunning)
    return;
  bleRunning = false;
  resetSession();
  uiState = BLE_UI_OFF;
  {
    BluetoothLock lock;
    BLE.stopAdvertising();
  }
  Serial.println("[BLE] Annonce arretee");
}

void bleImprovLoop() {
  if (!bleRunning)
    return;

  const uint32_t now = millis();

  // --- 0. Evenements poses par le contexte Bluetooth ----------------------
  if (evtBadRpc) {
    evtBadRpc = false;
    setError(ERROR_INVALID_RPC);
  }
  if (evtUnknownRpc) {
    evtUnknownRpc = false;
    setError(ERROR_UNKNOWN_RPC);
  }
  if (evtIdentify) {
    evtIdentify = false;
    // L'utilisateur a l'EduKit sous les yeux, avec son ecran : accuser
    // reception suffit, il n'y a pas d'ambiguite a lever.
    Serial.println("[BLE] Identification demandee");
    setError(ERROR_NONE);
  }

  if (evtLinked) {
    evtLinked = false;
    Serial.println("[BLE] App connectee");
    if (uiState == BLE_UI_ADVERTISING || uiState == BLE_UI_FAILED)
      uiState = BLE_UI_LINKED;
  }

  if (evtUnlinked) {
    evtUnlinked = false;
    Serial.println("[BLE] App deconnectee");
    if (!connecting && !pendingConnect) {
      resetSession();
      // Un succes SURVIT au depart de l'app : partir, c'est justement sa
      // facon de dire que tout s'est bien passe.
      if (uiState != BLE_UI_SUCCESS)
        uiState = BLE_UI_ADVERTISING;
    } else {
      // Une tentative survit a la coupure du lien (antenne partagee pendant
      // la bascule WiFi) : l'app se reconnecte pour lire le verdict.
      scanServed = false;
      sendingNets = false;
    }
  }

  if (evtCreds) {
    evtCreds = false;
    Serial.printf("[BLE] Identifiants recus - SSID : %s\n", credSsid);
    setError(ERROR_NONE);
    setState(STATE_PROVISIONING);
    pendingConnect = true;
  }

  // --- 1. Une notification en attente par caracteristique ------------------
  charState->flush();
  charError->flush();
  charRpcResult->flush();

  // --- 2. Un client vient de s'abonner a la liste des reseaux ? -----------
  // L'ABONNEMENT EST LA DEMANDE. On ne scanne jamais ici (cf. ble_improv.h).
  if (!sendingNets && !scanServed && charWifiScan->subscribed()) {
    scanServed = true;
    Serial.printf("[BLE] Liste des reseaux demandee (%d en reserve)\n",
                  netCount);
    netCursor = 0;
    nextNotifyAt = now;
    sendingNets = true; // netCount peut valoir 0 : "END" part seul
  }

  // --- 3. Emission de la liste, une ligne par tour ------------------------
  if (sendingNets && charWifiScan->flush() &&
      (int32_t)(now - nextNotifyAt) >= 0) {
    if (netCursor < netCount) {
      const NetEntry &n = netList[netCursor];
      char line[48];
      int len = snprintf(line, sizeof(line), "%s\t%d\t%d", n.ssid, n.rssi,
                         n.secure ? 1 : 0);
      charWifiScan->put((const uint8_t *)line, len);
      netCursor++;
      nextNotifyAt = now + NOTIFY_GAP_MS;
    } else {
      // Marqueur de fin : sans lui l'app attend d'autres reseaux.
      charWifiScan->put("END");
      Serial.println("[BLE] Liste des reseaux transmise");
      sendingNets = false;
      netCursor = -1;
    }
    charWifiScan->flush();
  }

  // --- 4. Identifiants recus : on les passe a main.cpp --------------------
  if (pendingConnect) {
    pendingConnect = false;
    Serial.printf("[BLE] Connexion a '%s'...\n", credSsid);
    connecting = true;
    connectStart = now;
    uiState = BLE_UI_CONNECTING;
    strncpy(uiSsid, credSsid, sizeof(uiSsid) - 1);
    uiSsid[sizeof(uiSsid) - 1] = 0;
    // main.cpp lance WiFi.begin(). Les String sont construites ICI, sur la
    // boucle principale -- jamais dans un callback.
    if (credentialsCb)
      credentialsCb(String(credSsid), String(credPass));
    credPass[0] = 0; // on ne garde pas le mot de passe
    return;          // le verdict attendra le tour suivant
  }

  // --- 5. Le succes est un EVENEMENT, pas un etat ou l'on s'installe ------
  if (uiState == BLE_UI_SUCCESS && (now - verdictAt) > LINGER_AFTER_SUCCESS_MS)
    uiState = BLE_UI_ADVERTISING;

  // --- 6. Verdict de la connexion -----------------------------------------
  if (!connecting)
    return;

  const uint32_t elapsed = now - connectStart;
  const int st = WiFi.status();

  if (st == WL_CONNECTED) {
    Serial.printf("[BLE] Connecte ! IP %s\n",
                  WiFi.localIP().toString().c_str());
    connecting = false;
    uiState = BLE_UI_SUCCESS;
    verdictAt = now;
    setState(STATE_PROVISIONED);
    // Premiere chaine VIDE expres : c'est l'emplacement de l'URL locale du
    // ModuleAir, et l'EduKit n'a pas d'API pour l'app. Deuxieme : le deviceId,
    // cle de getPublicName.php cote serveur.
    const String result[2] = {String(""), myDeviceId};
    sendRpcResult(CMD_WIFI_SETTINGS, result, 2);
    charState->flush();
    charRpcResult->flush();
    return;
  }

  const bool hardFail = (elapsed > CONNECT_EARLY_FAIL_MS) &&
                        (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL);
  if (hardFail || elapsed > CONNECT_TIMEOUT_MS) {
    Serial.printf("[BLE] Echec de connexion (status %d apres %lu ms)\n", st,
                  (unsigned long)elapsed);
    connecting = false;
    uiState = BLE_UI_FAILED;
    setError(ERROR_UNABLE_TO_CONNECT);
    setState(STATE_AUTHORIZED); // pret pour un nouvel essai
  }
}
