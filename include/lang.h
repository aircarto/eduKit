#pragma once

/**
 * Textes affiches sur l'ecran OLED, en francais (par defaut) ou en anglais.
 *
 * La langue se choisit a la compilation : l'environnement PlatformIO "pico"
 * donne le firmware francais, "pico_en" (-DLANG_EN) le firmware anglais.
 *   pio run -e pico_en -t upload
 *
 * Textes sans accents, comme le reste du firmware.
 * Les logs du port serie restent en francais.
 */

#if defined(LANG_EN)

#define TXT_BOOT_TITLE "EDUCATIONAL KIT"
#define TXT_WIFI_CONNECTING "WiFi connection"
#define TXT_PLEASE_WAIT "Please wait..."
#define TXT_WIFI_RESULT_TITLE "WiFi"
#define TXT_CONNECTION "Connection"
#define TXT_CONNECTION_OK "successful"
#define TXT_CONNECTION_FAILED "failed"
#define TXT_APP_LINKED "App connected"
#define TXT_WIRING_1 "Wiring"
#define TXT_WIRING_2 "incorrect"
#define TXT_CO2_GOOD "GOOD"
#define TXT_CO2_VENTILATE "VENTILATE"
#define TXT_CO2_ERROR "ERROR"
#define TXT_DECIMAL_SEP "."

#else

#define TXT_BOOT_TITLE "ATELIER PEDAGOGIQUE"
#define TXT_WIFI_CONNECTING "Connexion WiFi"
#define TXT_PLEASE_WAIT "Veuillez patienter..."
#define TXT_WIFI_RESULT_TITLE "WiFi"
#define TXT_CONNECTION "Connexion"
#define TXT_CONNECTION_OK "etablie"
#define TXT_CONNECTION_FAILED "echouee"
#define TXT_APP_LINKED "App connectee"
#define TXT_WIRING_1 "Branchements"
#define TXT_WIRING_2 "incorrects"
#define TXT_CO2_GOOD "BIEN"
#define TXT_CO2_VENTILATE "AERER SVP"
#define TXT_CO2_ERROR "ERREUR"
#define TXT_DECIMAL_SEP ","

#endif
