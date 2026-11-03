# Versions du firmware EduKit

La version est definie dans `include/version.h` (MAJEUR.MINEUR.CORRECTIF).
Elle s'affiche sur l'ecran de demarrage, avec la commande serie `i`, et part
au serveur avec chaque mesure (`firmware_version`).

## 2.1.0

- La LED du Pico s'allume au demarrage : temoin que le 5V et le GND sont bien
  branches.
- La version du firmware est envoyee au serveur avec chaque mesure et affichee
  sous la barre de progression de l'ecran de demarrage.
- Firmware en anglais : environnement PlatformIO `pico_en` (textes de l'ecran
  dans `include/lang.h`).
- Capteur BME280 optionnel sur un bus I2C separe (GPIO 6 SDA, GPIO 7 SCL) :
  temperature, humidite et pression affichees en alternance avec le CO2 et
  envoyees au serveur (`temperature`, `humidity`, `pressure`).

## 2.0.0

- Configuration WiFi par Bluetooth (Improv WiFi), comme l'Alba.
