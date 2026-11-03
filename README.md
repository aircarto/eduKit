# Kit Pedagogique CO2 (EduKit)

Mesure et affiche le CO2 en temps réel avec un Raspberry Pi Pico 2 W, et envoie
les mesures au serveur AirCarto pour les consulter dans l'app.

## Materiel Necessaire

| Composant | Quantité |
|-----------|----------|
| Raspberry Pi Pico 2 W | 1 |
| Capteur CO2 SensAir S88 | 1 |
| Ecran OLED 0.96" SSD1306 I2C (bicolore jaune/bleu) | 1 |
| Cables dupont | ~8 |

## Cablage

#### Ecran OLED SSD1306 (I2C, adresse 0x3C)
| OLED | Pico 2 W | Broche physique |
|------|----------|-----------------|
| GND  | GND | 38 |
| VDD  | 3V3 (OUT) | 36 |
| SCK (= SCL) | GPIO 5 (I2C0 SCL) | 7 |
| SDA  | GPIO 4 (I2C0 SDA) | 6 |

#### Capteur SensAir S88 (UART, Modbus RTU 9600 bauds)
| SensAir | Pico 2 W | Broche physique |
|---------|----------|-----------------|
| G0       | GND | 38 |
| G+       | VBUS (5V) | 40 |
| UART_TxD | GPIO 1 (UART0 RX) | 2 |
| UART_RxD | GPIO 0 (UART0 TX) | 1 |

> ⚠️ Le SensAir S88 s'alimente en **5V**. `VBUS` n'a du 5V que lorsque le Pico
> est branché en USB : sur batterie, il faut alimenter `G+` par la sortie 5V
> de l'alimentation. Ses lignes UART sont en 3.3V, compatibles avec le Pico.

#### Capteur BME280 (optionnel, I2C1, adresse 0x76 ou 0x77)
Température, humidité et pression, sur un bus I2C séparé de l'écran : un
mauvais branchement du BME280 ne bloque pas l'écran. Sans BME280, l'EduKit
fonctionne comme avant ; avec, l'écran alterne CO2 (8 s) et air ambiant (4 s).

| BME280 | Pico 2 W | Broche physique |
|--------|----------|-----------------|
| VCC / VIN | 3V3 (OUT) | 36 |
| GND  | GND | 8 |
| SDA  | GPIO 6 (I2C1 SDA) | 9 |
| SCL  | GPIO 7 (I2C1 SCL) | 10 |

> ⚠️ Prendre un module **BME280** en 3.3V (le BMP280, très ressemblant, ne
> mesure pas l'humidité).

## Configuration WiFi depuis l'app AirCarto

L'EduKit parle le même protocole Bluetooth (**Improv WiFi**) que le ModuleAir,
le ModuleAir mini et l'Alba :

1. **Hors ligne** (premier démarrage, réseau introuvable), l'EduKit continue
   d'afficher le CO2 avec l'icône réseau barrée, et s'annonce en Bluetooth
   sous le nom `EduKit-<deviceId>`.
2. Dans l'app, il apparaît dans la liste des capteurs à proximité : on choisit
   le réseau, on tape le mot de passe. L'écran affiche « App connectee », puis
   « Connexion WiFi », puis le verdict.
3. **En ligne**, le Bluetooth s'arrête et la moyenne du CO2 est envoyée toutes
   les 60 s à `data.moduleair.fr/eduKit/edukit_endpoint.php`.

Si le réseau tombe, l'EduKit le cherche pendant 3 min, puis repasse en annonce
Bluetooth et retente le réseau enregistré toutes les 10 min.

Le `deviceId` est l'identifiant unique du Pico (16 caractères hexa), celui
qu'utilise le serveur. Un EduKit déjà configuré avec un firmware 1.x garde son
réseau après la mise à jour.

## Commandes

```bash
# Compiler le projet
pio run

# Compiler et uploader
pio run -t upload

# Voir les logs série
pio device monitor
```

Sur le moniteur série (115200 bauds) :

| Touche | Action |
|--------|--------|
| `i` | Affiche l'identité (version, deviceId, nom Bluetooth, réseau) |
| `r` | Oublie le réseau WiFi enregistré et repasse en annonce Bluetooth |

## Affichage

- **< 800 ppm** : « BIEN »
- **≥ 800 ppm** : « AERER SVP »
- En haut à droite : barres de signal WiFi, ou icône barrée hors ligne.
- « Branchements incorrects » : aucune mesure valide du capteur depuis 10 s.

## Structure du Projet

```
kit-pedagogique-co2/
├── platformio.ini          # Configuration PlatformIO (plateforme et coeur épinglés)
├── include/
│   ├── lang.h              # Textes de l'écran, français / anglais
│   ├── pico_compat.h       # Preferences (ESP32) émulé sur LittleFS
│   └── version.h           # Version du firmware (voir CHANGELOG.md)
├── src/
│   ├── main.cpp            # Capteur, écran, machine à états réseau, envoi
│   ├── ble_improv.h        # Configuration WiFi par Bluetooth (Improv)
│   └── ble_improv.cpp
├── lib/
│   └── SensAir_S88/        # Bibliothèque capteur CO2
└── server/                 # Endpoints PHP (réception, historique, nom public)
```

## Langue de l'écran

Le firmware existe en français (par défaut) et en anglais, pour les kits livrés à des étudiants anglophones. Les textes de l'écran sont dans `include/lang.h` ; les logs du port série restent en français.

```
pio run -e pico -t upload      # français
pio run -e pico_en -t upload   # anglais
```

## Versions

La version du firmware est définie dans `include/version.h` (MAJEUR.MINEUR.CORRECTIF). Elle s'affiche sur l'écran de démarrage et avec la commande `i`, et part au serveur avec chaque mesure (champ `firmware_version`, enregistré dans `capteurs.firmware_version`). À chaque firmware distribué : changer la version et ajouter une entrée dans `CHANGELOG.md`.

## Erreurs Communes

| Erreur | Cause probable |
|--------|----------------|
| "Timeout" | Capteur non connecté ou TX/RX inversés |
| "Réponse invalide" | Interférence ou capteur défectueux |
| Valeurs aberrantes | Capteur en préchauffage (attendre 1-2 min) |
| EduKit absent de l'app | Il est en ligne (Bluetooth coupé) : `r` sur le port série pour le reconfigurer |
