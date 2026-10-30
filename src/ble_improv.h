/**
 * ============================================
 *    CONFIGURATION WIFI PAR BLUETOOTH (Improv WiFi)
 * ============================================
 *
 * Pour l'utilisateur : il ouvre l'app AirCarto, l'EduKit apparait dans la
 * liste des capteurs a proximite, il choisit son reseau, tape le mot de passe,
 * c'est fini.
 *
 * Le dialecte est EXACTEMENT celui de ModuleAir_V4, du ModuleAir mini et de
 * l'Alba : memes UUID, memes octets, meme caracteristique maison pour la liste
 * des reseaux. L'app ne fait aucune difference entre eux pendant la
 * configuration ; seul le NOM annonce lui dit quel produit elle vient de
 * configurer ("EduKit-<deviceId>", "Alba-<token>", "ModuleAirMini-<token>").
 *
 * Portage de l'Alba (Pico 2 W, bibliotheque BLE d'arduino-pico sur BTstack) :
 *
 *  1. RIEN NE BLOQUE. Tout est une machine a etats reveillee par
 *     bleImprovLoop() a chaque tour de loop().
 *
 *  2. PAS DE SCAN WIFI PENDANT QU'UNE APP EST CONNECTEE. La radio 2,4 GHz du
 *     CYW43 est partagee entre WiFi et Bluetooth. main.cpp scanne quand
 *     personne n'est connecte et depose la liste (bleImprovPushNetworks) ; ce
 *     module se contente de la servir -- instantanement, en prime.
 *
 *  3. PAS D'URL LOCALE DANS LE VERDICT. L'EduKit n'a pas d'API que l'app
 *     pourrait piloter : il repond ["", deviceId] comme le mini et l'Alba.
 *
 *  4. LES CALLBACKS BTstack NE FONT QUE POSER DES DRAPEAUX. Ils tournent dans
 *     le contexte Bluetooth ; y allouer pendant que loop() touche les memes
 *     objets corrompt le tas. Tampons de taille fixe + drapeaux volatile.
 *
 *  5. LES NOTIFICATIONS SONT ENVOYEES A LA MAIN. Le setValue() de la
 *     bibliotheque passe par UN seul emplacement d'enregistrement par
 *     caracteristique : deux mises a jour avant le depart de la premiere
 *     corrompent la liste de BTstack. On stocke la valeur, puis on la pousse
 *     nous-memes (att_server_notify) quand la pile a de la place.
 *
 * L'annonce ne vit que hors ligne : main.cpp appelle bleImprovStop() une fois
 * le reseau rejoint, et l'EduKit devient invisible. La pile, elle, est
 * construite une seule fois et gardee a vie.
 */

#ifndef BLE_IMPROV_H
#define BLE_IMPROV_H

#include <Arduino.h>

// Ce que l'ecran doit montrer. Ce module ne dessine rien : il PUBLIE son etat
// et main.cpp repeint depuis loop().
enum BleImprovUiState {
  BLE_UI_OFF,         // pas d'annonce (EduKit en ligne)
  BLE_UI_ADVERTISING, // annonce en cours, personne en face
  BLE_UI_LINKED,      // l'app est connectee : configuration en cours
  BLE_UI_CONNECTING,  // identifiants recus, le WiFi tente de s'associer
  BLE_UI_SUCCESS,     // reseau rejoint
  BLE_UI_FAILED,      // echec -- l'app peut reessayer sans se reconnecter
};

// Un reseau a proposer a l'app (copie du scan de main.cpp).
struct BleImprovNet {
  const char *ssid;
  int rssi;
  bool secure; // false = reseau ouvert
};

// Identifiants recus. main.cpp lance la connexion ; ce module ne fait ensuite
// que LIRE WiFi.status() pour annoncer le verdict a l'app.
typedef void (*BleImprovCredentialsCallback)(const String &ssid,
                                             const String &password);

// Demarre l'annonce sous `deviceName` ("EduKit-4430353234313938").
// `deviceId` est renvoye a l'app une fois le reseau rejoint : c'est la cle de
// getPublicName.php, qui lui permet de retrouver le capteur sans rien demander
// a l'utilisateur. Idempotent.
void bleImprovStart(const String &deviceName, const String &deviceId);

// Coupe l'annonce (la pile est conservee). Idempotent.
void bleImprovStop();

// A appeler a chaque tour de loop(). Ne bloque jamais. No-op si arrete.
void bleImprovLoop();

bool bleImprovRunning();

// True tant qu'une app est connectee. A consulter avant tout scan WiFi.
bool bleImprovClientConnected();

// True de la reception des identifiants jusqu'au verdict. main.cpp s'en sert
// pour ne pas relancer en parallele une connexion avec l'ANCIEN reseau.
bool bleImprovBusy();

// True une fois qu'on peut couper le BLE sans perdre le verdict. L'etat
// "provisionne" et le deviceId partent au tour de loop() QUI SUIT
// l'association ; et le lien saute souvent pendant la bascule WiFi, l'app se
// reconnecte alors pour LIRE le verdict. Apres un succes on reste donc visible
// quelques secondes quoi qu'il arrive.
bool bleImprovCanStop();

// Depose la liste des reseaux a servir. Ignore pendant qu'une liste part.
void bleImprovPushNetworks(const BleImprovNet *nets, int count);

void bleImprovOnCredentials(BleImprovCredentialsCallback cb);

BleImprovUiState bleImprovUiState();

// SSID de la tentative en cours (vide hors tentative).
const char *bleImprovSsid();

#endif // BLE_IMPROV_H
