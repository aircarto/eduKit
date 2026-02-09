/**
 * SensAir S88 CO2 Sensor Library
 * ==============================
 * Communication via Modbus RTU protocol over UART
 *
 * Ce capteur utilise le protocole Modbus RTU pour communiquer.
 * Il fonctionne en 9600 bauds, 8N1.
 */

#ifndef SENSAIR_S88_H
#define SENSAIR_S88_H

#include <Arduino.h>

class SensAir_S88 {
public:
  SensAir_S88();

  /**
   * Initialise le capteur avec un port série
   * @param serial Port série hardware (Serial1 sur Pico)
   */
  void begin(Stream &serial);

  /**
   * Lit la valeur de CO2 en ppm
   * @return Valeur CO2 en ppm, ou -1 en cas d'erreur
   */
  int getCO2();

  /**
   * Récupère le code de la dernière erreur
   * @return 0 = OK, 1 = Serial non initialisé, 2 = Timeout, 3 = Réponse
   * invalide
   */
  uint8_t getLastError();

private:
  Stream *_serial;
  uint8_t _lastError;

  // Commande Modbus pour lire le CO2
  static const uint8_t READ_CO2_CMD[];
  static const uint8_t CMD_LENGTH = 8;
  static const uint8_t RESPONSE_LENGTH = 7;

  // Buffer pour la réponse
  uint8_t _response[10];

  // Fonctions internes
  bool readResponse();
  bool isResponseValid();
};

#endif // SENSAIR_S88_H
