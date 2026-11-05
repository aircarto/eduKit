# Tutoriel de montage : images 3D

Les images du tutoriel [aircarto.fr/eduKit_co2/tuto.html](https://aircarto.fr/eduKit_co2/tuto.html)
sont des rendus 3D : faute de photos des étapes, la plaque réelle
(`docs/plaque/plaque_edukit.stl`) est chargée telle quelle et les cartes sont
modélisées en volumes simples, à la cote, d'après `docs/cablage_edukit.svg`
(dessiné à l'échelle de la plaque), `docs/carte_pico/` et `Gestion batterie/`.

| Fichier | Rôle |
|---|---|
| `scene/edukit3d.js` | les pièces, les fils (`WIRES`) et les étapes (`STEPS`) |
| `scene/index.html` | la scène : visionneuse 3D, ou capture d'une étape (`?step=…&render=1`) |
| `render.mjs` | rend toutes les images dans `img/` (liste `JOBS`) |
| `publier.sh` | copie `img/`, le SVG et la visionneuse dans le site (`eduKit_co2/img/tuto/` et `eduKit_co2/3d/`) |
| `img/` | les images générées (étapes, gros plans, pièces), non versionnées : `npm run render` les recrée |

## Régénérer les images

```bash
cd docs/tuto
npm install                                        # playwright-core
npx playwright-core install chromium-headless-shell # une fois
npm run render                                     # toutes les images
npm run render -- etape08                          # seulement celles qui contiennent « etape08 »
./publier.sh                                       # copie vers le site
```

Pour tourner autour du montage : `npm run viewer`, puis ouvrir l'adresse
affichée.

## Modifier

- **Un fil** : `WIRES` dans `edukit3d.js`. Chaque fil donne ses deux broches
  (`piece:broche`), sa couleur et les coins de son tracé, en coordonnées du SVG.
- **Une étape** : `STEPS` (pièces visibles, pièce en cours de pose, fils).
- **Une pièce** : les fonctions `make…`. Repère en millimètres, origine au coin
  haut-gauche de la plaque, `y` vers le bas de la plaque.
