#!/bin/sh
# Copie les images du tuto et la visionneuse 3D dans le site aircarto.fr.
#   ./publier.sh [dossier eduKit_co2 du site]
# Les images se regenerent avant avec : npm run render
set -eu
ici=$(cd "$(dirname "$0")" && pwd)
docs=$(dirname "$ici")
site=${1:-/home/airlab_server5/server/sites/aircarto.fr/eduKit_co2}

mkdir -p "$site/img/tuto" "$site/3d"
cp "$ici"/img/*.png "$site/img/tuto/"
cp "$docs/cablage_edukit.svg" "$site/img/tuto/"
# schema en anglais : memes dessins, textes traduits
sed -e 's/Schema de cablage de l.EduKit CO2/EduKit CO2 wiring diagram/' \
    -e 's/>CAPTEUR CO2</>CO2 SENSOR</' -e 's/>INTERRUPTEUR</>SWITCH</' \
    -e 's/>BATTERIE 18650</>18650 BATTERY</' -e 's/>ECRAN OLED</>OLED SCREEN</' \
    "$docs/cablage_edukit.svg" > "$site/img/tuto/cablage_edukit_en.svg"
cp "$ici/scene/edukit3d.js" "$docs/plaque/plaque_edukit.stl" "$docs/ecran_edukit_fr.png" "$docs/ecran_edukit_en.png" "$site/3d/"
sed 's|<body>|<body data-stl="plaque_edukit.stl" data-screen="ecran_edukit_fr.png" data-screen-en="ecran_edukit_en.png">|' \
    "$ici/scene/index.html" > "$site/3d/index.html"
echo "Publie dans $site (img/tuto/ et 3d/)"
