/**
 * SensAir S88 CO2 Sensor Library
 * ==============================
 * Implémentation de la communication Modbus RTU
 */

#include "SensAir_S88.h"

// Commande Modbus pour lire le CO2:
// 0xFE = adresse du capteur
// 0x04 = fonction "read input register"
// 0x00 0x03 = adresse du registre (0x0003 = CO2)
// 0x00 0x01 = nombre de registres à lire (1)
// 0xD5 0xC5 = CRC
const uint8_t SensAir_S88::READ_CO2_CMD[] = {0xFE, 0x04, 0x00, 0x03,
                                             0x00, 0x01, 0xD5, 0xC5};

SensAir_S88::SensAir_S88() : _serial(nullptr), _lastError(0) {
  memset(_response, 0, sizeof(_response));
}

void SensAir_S88::begin(HardwareSerial &serial) {
  _serial = &serial;
  _lastError = 0;
}

int SensAir_S88::getCO2() {
  if (_serial == nullptr) {
    _lastError = 1; // Serial non initialisé
    return -1;
  }

  // Vider le buffer (supprimer les anciennes données)
  while (_serial->available() > 0) {
    _serial->read();
  }

  // Envoyer la commande au capteur
  _serial->write(READ_CO2_CMD, CMD_LENGTH);
  _serial->flush(); // Attendre la fin de la transmission

  // Attendre la réponse du capteur (200ms)
  delay(200);

  // Lire la réponse
  if (!readResponse()) {
    _lastError = 2; // Timeout ou réponse incomplète
    return -1;
  }

  // Vérifier la validité de la réponse
  if (!isResponseValid()) {
    _lastError = 3; // Réponse invalide
    return -1;
  }

  // Extraire la valeur CO2 des octets 3 et 4
  // Exemple: 0x02 0x4D = (2 × 256) + 77 = 589 ppm
  int co2 = (_response[3] << 8) | _response[4];

  _lastError = 0; // Succès
  return co2;
}

bool SensAir_S88::readResponse() {
  int bytesRead = 0;
  unsigned long startTime = millis();

  // Lire jusqu'à RESPONSE_LENGTH octets avec timeout de 1 seconde
  while (bytesRead < RESPONSE_LENGTH && (millis() - startTime) < 1000) {
    if (_serial->available() > 0) {
      _response[bytesRead] = _serial->read();
      bytesRead++;
    }
  }

  // Vérifier qu'on a reçu le nombre d'octets attendu
  return (bytesRead >= RESPONSE_LENGTH);
}

bool SensAir_S88::isResponseValid() {
  // Vérifier les octets d'en-tête
  // Attendu: 0xFE (adresse) et 0x04 (code fonction)
  if (_response[0] != 0xFE || _response[1] != 0x04) {
    return false;
  }

  // Vérifier l'octet de longueur des données
  // Attendu: 0x02 (2 octets de données)
  if (_response[2] != 0x02) {
    return false;
  }

  return true;
}

uint8_t SensAir_S88::getLastError() { return _lastError; }
