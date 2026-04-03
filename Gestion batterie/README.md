# Edukit – V3

Cette V3 est une évolution du module d’alimentation conçu pour un kit éducatif.  

La carte est organisée en plusieurs blocs fonctionnels : une **entrée USB**, un **chargeur Li-ion TP4056**, une **protection batterie DW01A + FS8205A**, un **étage de load sharing**, puis un **convertisseur boost MT3608** qui génère le **5V final** pour alimenter le système éducatif.

Le principal problème de la **V2** était le suivant :  
le système fonctionnait lorsque l’USB était branché, mais **la batterie seule n’alimentait pas correctement le MT3608**, donc le kit ne pouvait pas fonctionner normalement en mode autonome.

La **V3** corrige ce comportement en mettant en place un vrai chemin d’alimentation cohérent entre :
- l’entrée USB,
- la batterie Li-ion,
- le bloc de partage de charge,
- et le convertisseur boost 5V.

### 1. USB input
L’alimentation arrive d’abord par le **connecteur micro-USB**.  
Quand un câble USB est branché, la ligne **VIN** fournit l’énergie au reste du système de gestion d’alimentation.

### 2. Battery charging
Le **TP4056** gère la recharge d’une **batterie Li-ion 1 cellule**.  
Quand l’USB est présent, il récupère le 5V d’entrée et recharge la batterie via sa broche **BAT**.  
Le courant de charge est défini par la résistance reliée à la broche **PROG**, et les deux LEDs associées au TP4056 permettent d’indiquer l’état de charge et la fin de charge.

### 3. Battery protection
La batterie ne part pas directement vers la sortie : elle passe d’abord par un **circuit de protection** composé du **DW01A** et du **FS8205A**.  
Ce bloc sert à sécuriser l’utilisation de la cellule Li-ion en protégeant contre :
- la surcharge,
- la décharge trop profonde,
- et les défauts de type surintensité ou court-circuit.

### 4. Load sharing
Le bloc de **load sharing** est la partie clé de cette V3.  
Son rôle est de gérer intelligemment la source d’énergie utilisée par le système.

En pratique :
- **si l’USB est branché**, le système peut être alimenté pendant que la batterie se recharge ;
- **si l’USB est retiré**, la batterie reprend automatiquement le relais.

Autrement dit, le kit peut être **utilisé et rechargé en même temps**, sans dépendre uniquement de la batterie lorsque l’USB est présent.

### 5. Step-up conversion to 5V
Après le bloc de partage de charge, l’alimentation attaque le **MT3608**.  
Ce composant est un **convertisseur boost** : il prend la tension disponible en entrée et l’élève pour produire une **sortie 5V stable**.

Autour du MT3608, on retrouve :
- l’inductance,
- la diode Schottky du boost,
- les condensateurs de filtrage,
- et le pont de résistances de feedback qui fixe la tension de sortie.

Le **5V obtenu** est ensuite envoyé vers le connecteur de sortie destiné à alimenter le système éducatif.

Cette architecture est intéressante pour un kit éducatif parce qu’elle montre, dans une seule carte, plusieurs fonctions essentielles de l’électronique embarquée :

- recharger une batterie Li-ion correctement,
- protéger la batterie,
- alimenter un système même pendant la charge,
- basculer automatiquement entre USB et batterie,
- et générer un 5V exploitable par un module externe.

La V3 n’est donc pas seulement une correction de la V2 : c’est une version plus aboutie, plus robuste et plus pédagogique.
