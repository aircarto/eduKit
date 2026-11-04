#pragma once

/**
 * Version du firmware EduKit (MAJEUR.MINEUR.CORRECTIF), seule source de la
 * version : affichee au demarrage, par la commande serie 'i', et envoyee au
 * serveur a chaque mesure (champ "firmware_version").
 *
 * A changer a chaque firmware distribue, avec une entree dans CHANGELOG.md :
 *   - MAJEUR    : cablage ou format d'envoi incompatible avec l'ancien
 *   - MINEUR    : nouvelle fonction
 *   - CORRECTIF : correction sans nouvelle fonction
 *
 * 20 caracteres au plus (colonne capteurs.firmware_version).
 */
#define FIRMWARE_VERSION "2.1.1"
