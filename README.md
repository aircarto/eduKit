# Kit Pedagogique CO2

Projet simple pour mesurer et afficher le CO2 en temps réel avec un ESP32.

## Materiel Necessaire

| Composant | Quantité |
|-----------|----------|
| ESP32 DevKit V1 | 1 |
| Capteur CO2 SensAir S88 | 1 |
| Ecran OLED 0.96" SSD1306 I2C | 1 |
| Cables dupont | ~8 |
| Breadboard (optionnel) | 1 |

## Schema de Cablage

```
                    ESP32 DevKit
                  ┌──────────────┐
                  │              │
    OLED GND ─────┤ GND      VIN ├───── SensAir G+ (5V)
    OLED VDD ─────┤ 3V3      GND ├───── SensAir G0
                  │              │
    OLED SCK ─────┤ GPIO22       │
    OLED SDA ─────┤ GPIO21       │
                  │              │
SensAir TxD ──────┤ GPIO16 (RX2) │
SensAir RxD ──────┤ GPIO17 (TX2) │
                  │              │
                  └──────────────┘
```

### Details du Cablage

#### Ecran OLED SSD1306 (I2C)
| OLED | ESP32 |
|------|-------|
| GND  | GND   |
| VDD  | 3.3V  |
| SCK  | GPIO 22 (horloge I2C) |
| SDA  | GPIO 21 |

#### Capteur SensAir S88 (UART)
| SensAir | ESP32 |
|---------|-------|
| G0      | GND   |
| G+      | 5V (VIN) |
| UART_TxD | GPIO 16 (RX2) |
| UART_RxD | GPIO 17 (TX2) |

> ⚠️ **ATTENTION**: Le capteur SensAir S88 fonctionne en **5V**, pas en 3.3V!
> Utilise la pin `VIN` de l'ESP32 (qui sort du 5V quand branché en USB).
> Les autres pins (Alarm_OC, PWM 1kHz, DVCC_out, etc.) ne sont pas utilisées.

## Commandes

```bash
# Compiler le projet
pio run

# Compiler et uploader
pio run -t upload

# Voir les logs série
pio device monitor
```

## Fonctionnement

1. **Au démarrage**: L'écran affiche "ATELIER PEDAGOGIQUE" avec une barre de progression
2. **Ensuite**: La valeur de CO2 s'affiche en gros avec un message selon le niveau:
   - **< 800 ppm**: "Air excellent" (bien aéré)
   - **800-1200 ppm**: "Correct" (acceptable)
   - **1200-1500 ppm**: "Aérer SVP!" (à surveiller)
   - **> 1500 ppm**: "AÉRER VITE!" (urgent)

## Structure du Projet

```
kit-pedagogique-co2/
├── platformio.ini          # Configuration PlatformIO
├── src/
│   └── main.cpp            # Code principal
├── lib/
│   └── SensAir_S88/        # Bibliothèque capteur CO2
│       ├── SensAir_S88.h
│       └── SensAir_S88.cpp
└── README.md               # Ce fichier
```

## Debug

Les logs série sont disponibles à 115200 bauds. Tu verras:
- Les valeurs CO2 toutes les 2 secondes
- Les erreurs de communication si le capteur n'est pas détecté

### Erreurs Communes

| Erreur | Cause probable |
|--------|----------------|
| "Timeout" | Capteur non connecté ou mauvais câblage TX/RX |
| "Réponse invalide" | Interférence ou capteur défectueux |
| Valeurs aberrantes | Capteur en préchauffage (attendre 1-2 min) |
