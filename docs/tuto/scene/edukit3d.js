// Scene 3D de l'EduKit CO2 : la plaque (STL) et les cartes, modelisees en
// volumes simples a partir des cotes de docs/cablage_edukit.svg, de la plaque
// et des captures des cartes (docs/carte_pico, Gestion batterie).
//
// Reperes : millimetres, origine au coin haut-gauche de la plaque vue de
// dessus. x vers la droite, y vers le bas de la plaque (vers l'eleve),
// z vers le haut. En three.js : X = x, Y = z, Z = y.
//
// Les etapes du tuto (STEPS) disent quelles pieces et quels fils montrer.

import * as THREE from 'three';
import { STLLoader } from 'three/addons/loaders/STLLoader.js';

export const V = (x, y, z = 0) => new THREE.Vector3(x, z, y);

// Conversion des coordonnees du SVG de cablage (viewBox calee sur la plaque).
const sx = (px) => (px - 117) * 115 / 740;
const sy = (py) => (py - 55) * 160 / 1030;

const PITCH = 2.54;
const POST_TOP = 4;          // haut des plots de la plaque
const PCB_T = 1.6;
const FEMALE_H = 8.5;        // connecteur femelle 2,54 mm
const MALE_BASE = 2.5;       // plastique d'un connecteur male
const MALE_PIN = 6;          // broche au-dessus du plastique
const DUPONT_L = 14;         // boitier d'un cable Dupont

export const WIRE_COLORS = {
  rouge: '#d7263d', orange: '#f08a24', noir: '#2e3036', bleu: '#2f6fdf',
  jaune: '#f2c81b', vert: '#2e9e54', violet: '#8b5cf6', marron: '#8a5a32',
  blanc: '#f2f2f2',
};

// ---------------------------------------------------------------- materiaux

const matCache = new Map();
function mat(color, opts = {}) {
  const key = color + JSON.stringify(opts);
  if (!matCache.has(key)) {
    matCache.set(key, new THREE.MeshStandardMaterial({ color, roughness: 0.65, metalness: 0, ...opts }));
  }
  return matCache.get(key);
}
const M = {
  plastic: () => mat('#1b1b1e', { roughness: 0.55 }),
  gold: () => mat('#d4a93c', { roughness: 0.3, metalness: 0.9 }),
  silver: () => mat('#c9cdd2', { roughness: 0.3, metalness: 0.85 }),
  chip: () => mat('#202124', { roughness: 0.45 }),
};

// Texture d'un dessus de carte : fond, trous, puis serigraphie.
function canvasTexture(wmm, hmm, draw, pxPerMm = 24) {
  const c = document.createElement('canvas');
  c.width = Math.round(wmm * pxPerMm);
  c.height = Math.round(hmm * pxPerMm);
  const ctx = c.getContext('2d');
  ctx.scale(pxPerMm, pxPerMm);
  draw(ctx);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 8;
  return t;
}

function text(ctx, s, x, y, size, { color = '#f4f4f4', align = 'center', bold = true, rot = 0, font = 'DejaVu Sans' } = {}) {
  ctx.save();
  ctx.translate(x, y);
  if (rot) ctx.rotate(rot);
  ctx.fillStyle = color;
  ctx.font = `${bold ? 'bold ' : ''}${size}px "${font}", sans-serif`;
  ctx.textAlign = align;
  ctx.textBaseline = 'middle';
  ctx.fillText(s, 0, 0);
  ctx.restore();
}

function roundRect(ctx, x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}

// ---------------------------------------------------------------- briques

function box(w, d, h, material) {
  const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), material);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}
// Pose un volume par son centre (x, y) et sa base z0, en coordonnees locales.
function put(parent, mesh, x, y, z0, h) {
  mesh.position.set(x, z0 + h / 2, y);
  parent.add(mesh);
  return mesh;
}

// Une carte : un groupe dont l'origine est le coin haut-gauche de la face du
// dessus. Les enfants se posent en (x, y, z) locaux, z = 0 sur le circuit.
function board({ w, d, color, holes = [], silk, t = PCB_T, radius = 1.2 }) {
  const g = new THREE.Group();
  const top = canvasTexture(w, d, (ctx) => {
    ctx.fillStyle = color;
    roundRect(ctx, 0, 0, w, d, radius);
    ctx.fill();
    for (const [hx, hy] of holes) {
      ctx.fillStyle = '#d8c27a';
      ctx.beginPath(); ctx.arc(hx, hy, 2.6, 0, Math.PI * 2); ctx.fill();
      ctx.fillStyle = '#1a1a1a';
      ctx.beginPath(); ctx.arc(hx, hy, 1.6, 0, Math.PI * 2); ctx.fill();
    }
    if (silk) silk(ctx);
  });
  const side = mat(color, { roughness: 0.7 });
  const topMat = new THREE.MeshStandardMaterial({ map: top, roughness: 0.6 });
  const mesh = box(w, d, t, [side, side, topMat, side, side, side]);
  put(g, mesh, w / 2, d / 2, -t, t);
  g.userData.anchors = {};
  g.userData.guides = holes.map(([x, y]) => V(x, y, -t));
  return g;
}

const holeTex = canvasTexture(2.54, 2.54, (ctx) => {
  ctx.fillStyle = '#1b1b1e'; ctx.fillRect(0, 0, 2.54, 2.54);
  ctx.fillStyle = '#050505'; ctx.fillRect(0.62, 0.62, 1.3, 1.3);
  ctx.fillStyle = '#b08a2e'; ctx.fillRect(1.0, 1.0, 0.54, 0.54);
}, 40);

// Connecteur femelle : un bloc par broche. Chaque broche nommee devient un
// point d'accroche (haut du connecteur) pour un cable.
function femaleHeader(g, pins, h = FEMALE_H) {
  const side = M.plastic();
  const topMat = new THREE.MeshStandardMaterial({ map: holeTex, roughness: 0.55 });
  for (const [name, x, y] of pins) {
    put(g, box(PITCH - 0.04, PITCH - 0.04, h, [side, side, topMat, side, side, side]), x, y, 0, h);
    if (name) g.userData.anchors[name] = { p: V(x, y, h) };
  }
}

// Connecteur male, broches vers le haut. Accroche : haut du plastique.
function maleHeader(g, pins) {
  for (const [name, x, y] of pins) {
    put(g, box(PITCH - 0.04, PITCH - 0.04, MALE_BASE, M.plastic()), x, y, 0, MALE_BASE);
    put(g, box(0.64, 0.64, MALE_PIN, M.gold()), x, y, MALE_BASE, MALE_PIN);
    if (name) g.userData.anchors[name] = { p: V(x, y, MALE_BASE) };
  }
}

function row(names, x0, y0, dx, dy) {
  return names.map((n, i) => [n, x0 + i * dx, y0 + i * dy]);
}

// ---------------------------------------------------------------- pieces

// Positions sur la plaque (coin haut-gauche de chaque carte), reprises du SVG
// de cablage et des plots du STL.
export const LAYOUT = {
  s88: { x: sx(150), y: sy(97) },
  bme: { x: sx(233), y: sy(346) },
  carrierPico: { x: 65.4, y: 5.9 },
  battery: { x: 4.5, y: 64.5 },
  oled: { x: sx(550), y: sy(787) },
};

// Broches du Pico, du haut vers le bas (gauche : 1 a 20, droite : 40 a 21).
const PICO_LEFT = ['GP0', 'GP1', 'GND', 'GP2', 'GP3', 'GP4', 'GP5', 'GND', 'GP6', 'GP7',
  'GP8', 'GP9', 'GND', 'GP10', 'GP11', 'GP12', 'GP13', 'GND', 'GP14', 'GP15'];
const PICO_RIGHT = ['VBUS', '5V', 'GND', 'EN', '3V', 'ADC', 'GP28', 'GND', 'GP27', 'GP26',
  'RUN', 'GP22', 'GND', 'GP21', 'GP20', 'GP19', 'GP18', 'GND', 'GP17', 'GP16'];
const PIN1_Y = 13.83;                 // y absolu de la broche 1
const ROW_X = { outL: 73.35, inL: 78.43, inR: 96.21, outR: 101.29 };

function makeCarrierPico() {
  const { x: ox, y: oy } = LAYOUT.carrierPico;
  const w = 43.6, d = 65.5;
  const L = (x, y) => [x - ox, y - oy];
  const holes = [[69.2, 9.3], [106.1, 9.3], [69.2, 68.4], [106.1, 68.4]].map(([x, y]) => L(x, y));
  const g = board({
    w, d, color: '#1d4f9e', holes,
    silk: (ctx) => {
      text(ctx, 'Raspberry Pi Pico', w / 2, 4.2, 2.6, { bold: false });
      for (let r = 0; r < 20; r++) {
        const y = PIN1_Y - oy + r * PITCH;
        text(ctx, PICO_LEFT[r], ROW_X.outL - ox - 1.7, y, 1.5, { align: 'right' });
        text(ctx, PICO_RIGHT[r], ROW_X.outR - ox + 1.7, y, 1.5, { align: 'left' });
      }
      text(ctx, '3V', 79.6 - ox, 66.4 - oy, 2.0);
      text(ctx, '5V', 95.1 - ox, 66.4 - oy, 2.0);
      text(ctx, 'AirCarto', w / 2, 61.6 - oy + 0.4, 2.4);
    },
  });
  const pins = [];
  for (let r = 0; r < 20; r++) {
    const y = PIN1_Y - oy + r * PITCH;
    const nl = `${PICO_LEFT[r]}#${r + 1}`, nr = `${PICO_RIGHT[r]}#${40 - r}`;
    pins.push([nl, ROW_X.outL - ox, y], [null, ROW_X.inL - ox, y]);
    pins.push([null, ROW_X.inR - ox, y], [nr, ROW_X.outR - ox, y]);
  }
  for (let k = 0; k < 4; k++) {
    pins.push([`3V.${k}`, 75.78 + k * PITCH - ox, 69.5 - oy]);
    pins.push([`5V.${k}`, 91.28 + k * PITCH - ox, 69.5 - oy]);
  }
  femaleHeader(g, pins);
  g.position.copy(V(ox, oy, POST_TOP + PCB_T));
  g.userData.label = 'Carte support du Pico';
  return g;
}

function makePico() {
  const w = 21, d = 51, t = 1.0;
  const ox = (ROW_X.inL + ROW_X.inR) / 2 - w / 2;
  const oy = PIN1_Y - 1.37;
  const g = board({
    w, d, t, color: '#1f7a3f', radius: 0.8,
    silk: (ctx) => {
      // demi-trous sur les bords
      ctx.fillStyle = '#d8c27a';
      for (let r = 0; r < 20; r++) {
        const y = 1.37 + r * PITCH;
        ctx.beginPath(); ctx.arc(1.61, y, 0.8, 0, 7); ctx.fill();
        ctx.beginPath(); ctx.arc(w - 1.61, y, 0.8, 0, 7); ctx.fill();
      }
      text(ctx, 'Raspberry Pi', w / 2, 36.5, 1.8);
      text(ctx, 'Pico 2 W', w / 2, 39.0, 1.8);
      text(ctx, 'BOOTSEL', 4.8, 8.6, 0.9, { bold: false });
    },
  });
  // Pico : prise USB en haut, RP2350, memoire, puce WiFi sous capot, antenne.
  put(g, box(7.6, 5.6, 2.7, M.silver()), w / 2, 2.0, 0, 2.7);
  put(g, box(3.2, 3.2, 1.2, mat('#f2f2f2')), 4.8, 6.6, 0, 1.2);
  put(g, box(7, 7, 0.9, M.chip()), w / 2, 18.5, 0, 0.9);
  put(g, box(4.5, 3.4, 0.8, M.chip()), w / 2, 26, 0, 0.8);
  put(g, box(10, 10.5, 1.4, M.silver()), w / 2, 42.5, 0, 1.4);
  put(g, box(14, 2.2, 0.3, mat('#d8c27a', { metalness: 0.6, roughness: 0.4 })), w / 2, 49.4, 0, 0.3);
  // barrettes males sous le Pico, enfichees dans la carte support
  for (const x of [1.61, w - 1.61]) {
    put(g, box(PITCH, 20 * PITCH, MALE_BASE, M.plastic()), x, 1.37 + 9.5 * PITCH, -t - MALE_BASE, MALE_BASE);
  }
  g.position.copy(V(ox, oy, POST_TOP + PCB_T + FEMALE_H + MALE_BASE + t));
  g.userData.label = 'Raspberry Pi Pico 2 W';
  // guides : les deux bouts de chaque barrette, qui vont dans la carte support
  g.userData.guides = [1.37, 1.37 + 19 * PITCH].flatMap((y) => [V(1.61, y, -t - MALE_BASE), V(w - 1.61, y, -t - MALE_BASE)]);
  return g;
}

// Carte du S88 (docs/carte_s88/carte_s88_3d.png). Construite dans son sens de
// lecture : connecteur des cables en haut (GND, 5V, TX, RX), le capteur enfiche
// dans deux connecteurs lateraux, bouton blanc a gauche. Sur la plaque, elle est
// posee RETOURNEE (demi-tour) : le connecteur se retrouve en bas, dans l'ordre
// RX, TX, 5V, GND, comme dans cablage_edukit.svg.
function makeS88() {
  const { x: ox, y: oy } = LAYOUT.s88;
  const w = sx(415) - ox, d = sy(297) - oy;
  // passage plaque (retournee) -> sens de lecture
  const R = ([x, y]) => [w - x, d - y];
  const holes = [[163, 117], [395, 117], [163, 269], [395, 269]].map(([x, y]) => R([sx(x) - ox, sy(y) - oy]));
  const rot = [['RxD', 256], ['TxD', 272], ['G+', 288], ['G0', 304]].map(([n, x]) => [n, ...R([sx(x) - ox, sy(264) - oy])]);
  const pins = rot.slice().reverse();          // GND, 5V, TX, RX de gauche a droite
  const [, gx, py] = pins[0];
  const g = board({
    w, d, color: '#1d3a7a', holes,
    silk: (ctx) => {
      text(ctx, 'GND', gx - 1.7, py, 2.0, { align: 'right', bold: false });
      ['5V', 'TX', 'RX'].forEach((t, i) => text(ctx, t, pins[i + 1][1], py - 1.8, 2.0, { align: 'left', bold: false, rot: -Math.PI / 2 }));
      ctx.strokeStyle = '#f4f4f4'; ctx.lineWidth = 0.3;
      ctx.strokeRect(9.4, 8.6, 24.4, 17.6);
      text(ctx, 'CO2 Sensor S88', w / 2 + 2, d - 2.6, 2.4, { bold: false });
      text(ctx, 'AirCarto v1', w - 2.0, d / 2 - 2, 1.8, { rot: -Math.PI / 2 });
    },
  });
  femaleHeader(g, pins.map(([n, x, y]) => [n, x, y]));
  // connecteurs ou s'enfiche le capteur : 5 broches a gauche, 4 a droite
  femaleHeader(g, [0, 1, 2, 3, 4].map((i) => [null, 6.1, 13.8 + i * PITCH]));
  femaleHeader(g, [0, 1, 2, 3].map((i) => [null, 35.8, 13.8 + i * PITCH]));
  // bouton poussoir blanc
  put(g, box(3.6, 4.4, 1.6, mat('#f2f2f2')), 2.9, 21.5, 0, 1.6);
  const btn = new THREE.Mesh(new THREE.CylinderGeometry(0.9, 0.9, 0.6, 20), M.chip());
  btn.position.set(2.9, 1.9, 21.5); g.add(btn);
  // le capteur Senseair S88, pose sur les deux connecteurs lateraux
  const bw = 33, bd = 20, bh = 7, bx = 20.95, by = 17.6, bz = FEMALE_H + MALE_BASE;
  const lid = canvasTexture(bw, bd, (ctx) => {
    ctx.fillStyle = '#c9ccd0'; ctx.fillRect(0, 0, bw, bd);
    ctx.fillStyle = '#ffffff'; roundRect(ctx, 5, 2.5, bw - 10, bd - 5, 0.8); ctx.fill();
    text(ctx, 'Senseair', bw / 2, 6.5, 2.4, { color: '#333', bold: false });
    text(ctx, 'S88', bw / 2, 11.5, 4.4, { color: '#111' });
  });
  const sideM = mat('#b9bdc2', { metalness: 0.6, roughness: 0.35 });
  put(g, box(bw, bd, bh, [sideM, sideM, new THREE.MeshStandardMaterial({ map: lid, roughness: 0.7 }), sideM, sideM, sideM]), bx, by, bz, bh);
  for (const x of [6.1, 35.8]) put(g, box(PITCH, 4 * PITCH, MALE_BASE, M.plastic()), x, 13.8 + 1.5 * PITCH, FEMALE_H, MALE_BASE);
  // demi-tour sur la plaque : le coin (0, 0) de la carte va en bas a droite
  g.rotation.y = Math.PI;
  g.position.copy(V(ox + w, oy + d, POST_TOP + PCB_T));
  g.userData.label = 'Capteur CO2 SenseAir S88';
  return g;
}

function makeBME() {
  const { x: ox, y: oy } = LAYOUT.bme;
  const w = 15.2, d = 11.7;
  const names = ['SDO', 'CSB', 'SDA', 'SCL', 'GND', 'VCC'];
  const cx = sx(282) - ox, py = sy(362) - oy;
  const xs = names.map((_, i) => cx + (i - 2.5) * PITCH);
  const holes = [[250, 401], [314, 401]].map(([x, y]) => [sx(x) - ox, sy(y) - oy]);
  const g = board({
    w, d, color: '#5b3f8c', holes,
    silk: (ctx) => {
      names.forEach((s, i) => text(ctx, s, xs[i], py + 2.2, 0.95));
      text(ctx, 'BME280', w / 2, d - 1.6, 1.3);
    },
  });
  put(g, box(2.5, 2.5, 0.9, M.silver()), w / 2, 7.3, 0, 0.9);
  maleHeader(g, names.map((n, i) => [n, xs[i], py]));
  g.position.copy(V(ox, oy, POST_TOP + PCB_T));
  g.userData.label = 'Capteur BME280 (option)';
  return g;
}

// Carte batterie V4, construite a plat (paysage, comme 3D_view.png) puis
// tournee d'un quart de tour : sur la plaque, elle est debout, H1 en haut.
function makeBatteryBoard() {
  const w = 90.9, d = 49.8;
  const holes = [[3.5, 3.5], [87.4, 3.5], [3.5, 46.3], [87.4, 46.3]];
  const h1u = 75.2, h1v = [30.09, 32.63, 35.17, 37.71];
  const g = board({
    w, d, color: '#173a7a', holes,
    silk: (ctx) => {
      ['5V', '5V', 'GND', 'GND'].forEach((s, i) => text(ctx, s, h1u - 2.4, h1v[i], 1.7, { align: 'right' }));
      text(ctx, 'H1', h1u, h1v[3] + 3, 1.3);
      text(ctx, 'BATTERY 18650', 42, 46.0, 2.6, { color: '#5f86d6' });
      text(ctx, 'V4', 82.5, 45.3, 2.6, { color: '#5f86d6' });
      text(ctx, '+', 14, 12, 4, { bold: false });
      text(ctx, '−', 80, 12, 4, { bold: false });
      text(ctx, 'USB', 8.6, 36.6, 1.2, { rot: -Math.PI / 2 });
      ctx.strokeStyle = '#f4f4f4'; ctx.lineWidth = 0.25;
      ctx.strokeRect(81.6, 23.6, 13, 18.6);
    },
  });
  // support 18650 : fond, deux flancs, deux butees avec lamelles
  const hu = 46.7, hv = 11.75, hl = 78, hw = 21;
  put(g, box(hl - 12, hw, 1.4, M.plastic()), hu, hv, 0, 1.4);
  put(g, box(hl - 12, 1.6, 9, M.plastic()), hu, hv - hw / 2 + 0.8, 0, 9);
  put(g, box(hl - 12, 1.6, 9, M.plastic()), hu, hv + hw / 2 - 0.8, 0, 9);
  for (const s of [-1, 1]) {
    put(g, box(6, hw, 15, M.plastic()), hu + s * (hl / 2 - 3), hv, 0, 15);
    put(g, box(0.6, 7, 9, M.gold()), hu + s * (hl / 2 - 6.3), hv, 3, 9);
  }
  // electronique de charge
  put(g, box(5.6, 7.6, 2.6, M.silver()), 2.4, 36.6, 0, 2.6);
  put(g, box(5, 4, 1.4, M.chip()), 13.4, 36.5, 0, 1.4);
  put(g, box(6.8, 6.8, 3.2, mat('#d9d9d6')), 51.5, 34, 0, 3.2);
  put(g, box(5, 2.6, 2, M.chip()), 40.5, 34, 0, 2);
  put(g, box(5, 2.6, 2, M.chip()), 55, 26, 0, 2);
  for (const [u, v, c] of [[2.3, 27, '#ff3b30'], [6.9, 27, '#3b82f6'], [75.4, 24.2, '#34c759']]) {
    put(g, box(1.6, 0.9, 0.6, mat(c, { emissive: c, emissiveIntensity: 0.15 })), u, v, 0, 0.6);
  }
  // interrupteur a bascule, deborde du bord comme sur la carte
  put(g, box(11, 16, 8, M.plastic()), 88.5, 33, 0, 8);
  const rocker = box(8, 12.5, 4, mat('#e53935', { roughness: 0.45 }));
  rocker.rotation.z = 0.18;
  put(g, rocker, 88.5, 33, 8, 4);
  // connecteur de sortie H1 (femelle 1x4)
  femaleHeader(g, ['5V.a', '5V.b', 'GND.a', 'GND.b'].map((n, i) => [n, h1u, h1v[i]]));
  g.rotation.y = Math.PI / 2;
  g.position.copy(V(LAYOUT.battery.x, LAYOUT.battery.y + w, POST_TOP + PCB_T));
  g.userData.label = 'Carte batterie V4';
  g.userData.cellSlot = { u: hu, v: hv, len: hl - 13 };
  return g;
}

function makeCell() {
  const g = new THREE.Group();
  const len = 65, r = 9;
  const body = new THREE.Mesh(new THREE.CylinderGeometry(r, r, len - 1.5, 48), mat('#2f6db5', { roughness: 0.35 }));
  body.rotation.z = Math.PI / 2;
  body.castShadow = body.receiveShadow = true;
  g.add(body);
  const ring = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.98, r * 0.98, 1.2, 48), mat('#e6e6e6'));
  ring.rotation.z = Math.PI / 2; ring.position.x = -len / 2 + 6; g.add(ring);
  const nub = new THREE.Mesh(new THREE.CylinderGeometry(3, 3, 1.2, 32), M.silver());
  nub.rotation.z = Math.PI / 2; nub.position.x = -len / 2 + 0.2; g.add(nub);
  const cap = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.9, r * 0.9, 0.8, 48), M.silver());
  cap.rotation.z = Math.PI / 2; cap.position.x = len / 2 - 0.4; g.add(cap);
  g.userData.label = 'Batterie 18650';
  return g;
}

function makeOledCarrier() {
  const { x: ox, y: oy } = LAYOUT.oled;
  const w = sx(820) - ox, d = sy(1057) - oy;
  const holes = [[570, 815], [800, 815], [570, 1038], [800, 1038]].map(([x, y]) => [sx(x) - ox, sy(y) - oy]);
  const names = ['GND', 'VCC', 'SCL', 'SDA'];
  const cx = sx(679) - ox;
  const xs = names.map((_, i) => cx + (i - 1.5) * PITCH);
  const yDup = sy(826) - oy, yMod = yDup + 3.7;
  const g = board({
    w, d, color: '#1d4f9e', holes,
    silk: (ctx) => {
      // comme sur la carte : GND a gauche, 3V3 / SCK / SDA debout au-dessus
      text(ctx, 'GND', xs[0] - 1.7, yDup, 1.8, { align: 'right', bold: false });
      ['3V3', 'SCK', 'SDA'].forEach((s, i) => text(ctx, s, xs[i + 1], yDup - 1.8, 1.8, { align: 'left', bold: false, rot: -Math.PI / 2 }));
      text(ctx, 'OLED screen 128x64', w / 2, d - 2.4, 2.0, { bold: false });
      text(ctx, 'AirCarto v1', w - 2.2, d / 2, 1.6, { rot: -Math.PI / 2 });
    },
  });
  // deux connecteurs femelles : en haut les cables, en bas l'ecran
  femaleHeader(g, names.map((n, i) => [n, xs[i], yDup]));
  femaleHeader(g, names.map((n, i) => [null, xs[i], yMod]));
  g.position.copy(V(ox, oy, POST_TOP + PCB_T));
  g.userData.label = 'Support de l’écran';
  g.userData.moduleAt = { x: ox + cx, y: oy + yMod };
  return g;
}

let screenCanvas = null;
function makeOledModule(at, { lit = false, withBme = false } = {}) {
  const w = 27.3, d = 27.8;
  const pinY = 1.6;
  const g = board({
    w, d, color: '#14467a', radius: 0.6,
    holes: [[2, 2], [w - 2, 2], [2, d - 2], [w - 2, d - 2]].map(([x, y]) => [x, y]),
    silk: (ctx) => {
      ['GND', 'VCC', 'SCL', 'SDA'].forEach((s, i) => text(ctx, s, w / 2 + (i - 1.5) * PITCH, pinY + 2.3, 0.8));
    },
  });
  // verre de l'ecran et zone active
  const gw = 26.6, gd = 19.2, gy = 5.6 + gd / 2;
  put(g, box(gw, gd, 1.4, mat('#0b0d12', { roughness: 0.15, metalness: 0.2 })), w / 2, gy, 0, 1.4);
  const scr = box(21.7, 10.9, 0.05, new THREE.MeshStandardMaterial({ color: '#000' }));
  if (lit && screenCanvas) {
    const c = document.createElement('canvas');
    c.width = screenCanvas.width; c.height = screenCanvas.height;
    const ctx = c.getContext('2d');
    ctx.drawImage(screenCanvas, 0, 0);
    if (!withBme) { ctx.fillStyle = '#000'; ctx.fillRect(200, 30, 420, 85); }
    const t = new THREE.CanvasTexture(c);
    t.colorSpace = THREE.SRGBColorSpace;
    const sm = new THREE.MeshBasicMaterial({ map: t });
    scr.material = [sm, sm, sm, sm, sm, sm];
  }
  put(g, scr, w / 2, gy - 0.6, 1.4, 0.05);
  // ses broches, enfichees dans le support
  for (let i = 0; i < 4; i++) {
    put(g, box(PITCH - 0.04, PITCH - 0.04, MALE_BASE, M.plastic()), w / 2 + (i - 1.5) * PITCH, pinY, -PCB_T - MALE_BASE, MALE_BASE);
  }
  g.position.copy(V(at.x - w / 2, at.y - pinY, POST_TOP + PCB_T + FEMALE_H + MALE_BASE + PCB_T));
  g.userData.label = 'Écran OLED 0,96"';
  g.userData.guides = [];
  return g;
}

// ---------------------------------------------------------------- cablage

// Chaque fil : [de, vers, couleur, coins du trace (x, y en mm sur la plaque),
// hauteur du trace en mm]. Les traces suivent des couloirs espaces d'au moins
// 4 mm ; la ou deux fils se croisent, leurs hauteurs different d'au moins 3 mm.
// Broches de la carte support : colonne de gauche x = 73,35, ligne r a
// y = 13,83 + (r - 1) * 2,54 ; rails a y = 69,5.
export const WIRES = {
  ecran: [
    ['oled:GND', 'support:GND#23', 'noir', [[83.52, 104], [111, 104], [111, 57.01]], 52],
    ['oled:VCC', 'support:3V.2', 'orange', [[86.06, 80], [80.86, 80]], 35],
    ['oled:SCL', 'support:GP5#7', 'jaune', [[88.6, 96], [61, 96], [61, 29.07]], 45],
    ['oled:SDA', 'support:GP4#6', 'bleu', [[91.14, 100], [57, 100], [57, 26.53]], 48],
  ],
  s88: [
    ['s88:G0', 'support:GND#3', 'noir', [[29.1, 16], [58, 16], [58, 18.91]], 38],
    ['s88:G+', 'support:5V.2', 'rouge', [[26.6, 25], [53, 25], [53, 64.5], [96.36, 64.5]], 41],
    ['s88:TxD', 'support:GP1#2', 'blanc', [[24.1, 12], [62, 12], [62, 16.37]], 35],
    ['s88:RxD', 'support:GP0#1', 'marron', [[21.6, 8], [66, 8], [66, 13.83]], 32],
  ],
  alim: [
    ['batt:5V.a', 'support:5V.1', 'rouge', [[34.7, 86], [93.82, 86]], 32],
    ['batt:GND.b', 'support:GND#18', 'noir', [[42.1, 57.01]], 35],
  ],
  bme: [
    ['bme:VCC', 'support:3V.1', 'orange', [[31.95, 66], [78.32, 66]], 32],
    ['bme:GND', 'support:GND#13', 'noir', [[29.41, 44.31]], 38],
    ['bme:SCL', 'support:GP7#10', 'violet', [[26.87, 36.69]], 35],
    ['bme:SDA', 'support:GP6#9', 'vert', [[24.33, 34.15]], 32],
  ],
};

function anchorWorld(parts, ref) {
  const [part, name] = ref.split(':');
  const g = parts[part];
  if (!g) throw new Error(`piece inconnue : ${part}`);
  const a = g.userData.anchors[name];
  if (!a) throw new Error(`broche inconnue : ${ref}`);
  g.updateMatrixWorld(true);
  return a.p.clone().applyMatrix4(g.matrixWorld);
}

// Trace a angles arrondis, comme le SVG : monte du connecteur, suit les coins
// a la hauteur h, redescend sur l'autre connecteur.
function routedCurve(pts, r = 3.5) {
  const path = new THREE.CurvePath();
  let prev = pts[0];
  for (let i = 1; i < pts.length - 1; i++) {
    const a = pts[i - 1], b = pts[i], c = pts[i + 1];
    const ab = b.clone().sub(a), bc = c.clone().sub(b);
    const rr = Math.min(r, ab.length() / 2, bc.length() / 2);
    const p1 = b.clone().sub(ab.normalize().multiplyScalar(rr));
    const p2 = b.clone().add(bc.normalize().multiplyScalar(rr));
    if (prev.distanceTo(p1) > 1e-3) path.add(new THREE.LineCurve3(prev.clone(), p1));
    path.add(new THREE.QuadraticBezierCurve3(p1, b.clone(), p2));
    prev = p2;
  }
  path.add(new THREE.LineCurve3(prev.clone(), pts[pts.length - 1].clone()));
  return path;
}

// ---------------------------------------------------------------- explications

// Langue des textes de la visionneuse : 'fr' (defaut) ou 'en'.
let LANG = 'fr';
export function setLang(l) { LANG = l === 'en' ? 'en' : 'fr'; }
export function getLang() { return LANG; }

const BOARD_NAMES = {
  fr: { oled: 'Support de l\u2019écran', s88: 'Capteur CO2 S88', batt: 'Carte batterie (connecteur H1)', bme: 'Capteur BME280', support: 'Carte support du Pico' },
  en: { oled: 'Screen board', s88: 'CO2 sensor S88', batt: 'Battery board (H1 connector)', bme: 'BME280 sensor', support: 'Pico carrier board' },
};
// noms serigraphies, quand ils different du nom interne de la broche
const PIN_NAMES = { 'oled:VCC': '3V3', 'oled:SCL': 'SCK', 's88:G0': 'GND', 's88:G+': '5V', 's88:TxD': 'TX', 's88:RxD': 'RX', 'batt:5V.a': '5V', 'batt:GND.b': 'GND' };
const ROLES = {
  fr: {
    'oled:GND': 'Masse (le « moins ») de l\u2019écran.',
    'oled:VCC': 'Alimentation de l\u2019écran en 3,3 V, prise sur le rail 3V.',
    'oled:SCL': 'Horloge du bus I2C de l\u2019écran : elle cadence l\u2019échange avec le Pico.',
    'oled:SDA': 'Données du bus I2C : le Pico envoie ce qu\u2019il faut afficher.',
    's88:G0': 'Masse (le « moins ») du capteur.',
    's88:G+': 'Alimentation du capteur en 5 V, prise sur le rail 5V. Sur le 3V, il ne mesurerait rien.',
    's88:TxD': 'Le capteur émet ses mesures (TX) : elles arrivent sur l\u2019entrée série du Pico, GP1. Les fils se croisent avec RX.',
    's88:RxD': 'Le Pico envoie ses demandes de mesure par GP0 : elles arrivent sur l\u2019entrée du capteur (RX).',
    'batt:5V.a': 'Le 5 V de la batterie arrive sur le rail 5V : il alimente le Pico (VSYS) et le capteur S88.',
    'batt:GND.b': 'Masse de la batterie, commune à tout le montage.',
    'bme:VCC': 'Alimentation du BME280 en 3,3 V, prise sur le rail 3V.',
    'bme:GND': 'Masse (le « moins ») du BME280.',
    'bme:SCL': 'Horloge du second bus I2C, réservé au BME280.',
    'bme:SDA': 'Données du second bus I2C : température, humidité et pression.',
  },
  en: {
    'oled:GND': 'Ground (the “minus”) of the screen.',
    'oled:VCC': '3.3 V power for the screen, taken from the 3V rail.',
    'oled:SCL': 'Clock of the screen\u2019s I2C bus: it paces the exchange with the Pico.',
    'oled:SDA': 'Data of the I2C bus: the Pico sends what to display.',
    's88:G0': 'Ground (the “minus”) of the sensor.',
    's88:G+': '5 V power for the sensor, taken from the 5V rail. On the 3V rail it would not measure anything.',
    's88:TxD': 'The sensor sends its readings (TX): they reach the Pico\u2019s serial input, GP1. This wire crosses the RX one.',
    's88:RxD': 'The Pico sends its measurement requests on GP0: they reach the sensor\u2019s input (RX).',
    'batt:5V.a': 'The battery\u2019s 5 V goes to the 5V rail: it powers the Pico (VSYS) and the S88 sensor.',
    'batt:GND.b': 'Battery ground, shared by the whole kit.',
    'bme:VCC': '3.3 V power for the BME280, taken from the 3V rail.',
    'bme:GND': 'Ground (the “minus”) of the BME280.',
    'bme:SCL': 'Clock of the second I2C bus, used only by the BME280.',
    'bme:SDA': 'Data of the second I2C bus: temperature, humidity and pressure.',
  },
};
const COLOR_NAMES = {
  fr: { rouge: 'rouge', orange: 'orange', noir: 'noir', bleu: 'bleu', jaune: 'jaune', vert: 'vert', violet: 'violet', marron: 'marron', blanc: 'blanc' },
  en: { rouge: 'red', orange: 'orange', noir: 'black', bleu: 'blue', jaune: 'yellow', vert: 'green', violet: 'purple', marron: 'brown', blanc: 'white' },
};

function ordinal(n) {
  if (LANG === 'en') return n + (n % 10 === 1 && n !== 11 ? 'st' : n % 10 === 2 && n !== 12 ? 'nd' : n % 10 === 3 && n !== 13 ? 'rd' : 'th');
  return n === 1 ? '1re' : n + 'e';
}

// Ce qu'il faut savoir d'un fil, pour le panneau de la visionneuse.
export function describeWire({ from, to, color }) {
  const [pf] = from.split(':');
  const name = to.split(':')[1];
  const en = LANG === 'en';
  let broche, ou;
  const rail = name.match(/^(3V|5V)\./);
  if (rail) {
    broche = `rail ${rail[1]}`;
    ou = en ? `At the bottom of the carrier board, any of the 4 pins of the ${rail[1]} rail.`
      : `En bas de la carte support, n\u2019importe laquelle des 4 broches du rail ${rail[1]}.`;
  } else {
    const [label, num] = name.split('#');
    const n = +num, left = n <= 20, rang = left ? n : 41 - n;
    broche = label;
    ou = en ? `Pin ${n}, ${left ? 'left' : 'right'} column, ${ordinal(rang)} from the top (USB side).`
      : `Broche ${n}, colonne ${left ? 'de gauche' : 'de droite'}, ${ordinal(rang)} en partant du haut (côté prise USB).`;
  }
  return {
    couleur: COLOR_NAMES[LANG][color] || color,
    hex: WIRE_COLORS[color],
    de: { carte: BOARD_NAMES[LANG][pf], broche: PIN_NAMES[from] || from.split(':')[1] },
    vers: { carte: BOARD_NAMES[LANG].support, broche, ou },
    role: ROLES[LANG][from] || '',
  };
}

// Nom lisible d'une broche de la carte support : 'GP4#6' -> 'GP4', '3V.2' -> '3V'.
export function pinLabel(ref) {
  const name = ref.split(':')[1];
  return name.replace(/#.*$/, '').replace(/\..*$/, '');
}

// Etiquette posee a plat au-dessus d'une broche, pour les vues de dessus.
function tag(textStr, color, at, side) {
  const wmm = 1.6 + textStr.length * 1.55, hmm = 2.6;
  const t = canvasTexture(wmm, hmm, (ctx) => {
    ctx.fillStyle = '#ffffff';
    roundRect(ctx, 0.1, 0.1, wmm - 0.2, hmm - 0.2, 0.7); ctx.fill();
    // bordure de la couleur du cable ; un cable blanc garde une bordure visible
    ctx.strokeStyle = color === WIRE_COLORS.blanc ? '#555' : color; ctx.lineWidth = 0.35; ctx.stroke();
    text(ctx, textStr, wmm / 2, hmm / 2 + 0.1, 1.8, { color: '#111' });
  }, 60);
  const m = new THREE.Mesh(new THREE.PlaneGeometry(wmm, hmm), new THREE.MeshBasicMaterial({ map: t, transparent: true, depthTest: false }));
  m.rotation.x = -Math.PI / 2;
  m.renderOrder = 10;
  m.position.set(at.x + side * (wmm / 2 + 1.9), 60, at.z);
  return m;
}

function makeWire(parts, [from, to, color, corners, height], { faded = false, tags = false, rank = 0 } = {}) {
  const g = new THREE.Group();
  const a = anchorWorld(parts, from), b = anchorWorld(parts, to);
  const col = new THREE.Color(WIRE_COLORS[color]);
  if (faded) col.lerp(new THREE.Color('#b9b6ae'), 0.65);
  const wireMat = new THREE.MeshStandardMaterial({ color: col, roughness: 0.5 });
  // materiaux propres a ce fil, pour pouvoir le mettre en evidence seul
  const housing = new THREE.MeshStandardMaterial({ color: faded ? '#55555a' : '#1b1b1e', roughness: 0.55 });
  for (const p of [a, b]) {
    const hsg = box(PITCH, PITCH, DUPONT_L, housing);
    hsg.position.set(p.x, p.y + DUPONT_L / 2, p.z);
    g.add(hsg);
  }
  const ta = a.clone().add(new THREE.Vector3(0, DUPONT_L, 0));
  const tb = b.clone().add(new THREE.Vector3(0, DUPONT_L, 0));
  const mid = corners.map(([x, y]) => V(x, y, height));
  const pts = [ta, new THREE.Vector3(ta.x, height, ta.z), ...mid, new THREE.Vector3(tb.x, height, tb.z), tb];
  // supprime les points confondus (coin pile au-dessus d'un connecteur)
  const clean = pts.filter((p, i) => i === 0 || p.distanceTo(pts[i - 1]) > 0.3);
  const curve = routedCurve(clean);
  const tube = new THREE.Mesh(new THREE.TubeGeometry(curve, 400, 0.75, 10, false), wireMat);
  tube.castShadow = true;
  g.add(tube);
  // gaine invisible plus grosse : un fil de 1,5 mm est dur a viser a la souris
  const hit = new THREE.Mesh(new THREE.TubeGeometry(curve, 200, 2.6, 8, false),
    new THREE.MeshBasicMaterial({ colorWrite: false, depthWrite: false }));
  g.add(hit);
  const materials = { wire: wireMat, housing };
  // un cable noir disparait sur la plaque noire : liseré clair autour (coque
  // un peu plus grosse, vue de l'interieur)
  // Meme chose, en sombre, pour un cable blanc sur le fond clair.
  const OUTLINE = { noir: ['#e4e7eb', '#8c8f95'], blanc: ['#2e3036', '#77797f'] };
  if (OUTLINE[color]) {
    materials.outlineColors = OUTLINE[color];
    materials.outline = new THREE.MeshBasicMaterial({ color: OUTLINE[color][faded ? 1 : 0], side: THREE.BackSide });
    g.add(new THREE.Mesh(new THREE.TubeGeometry(curve, 400, 1.15, 10, false), materials.outline));
  }
  g.userData.wire = { from, to, color, faded, ends: [ta, tb], materials };
  if (tags) {
    // cote composant : etiquettes sous les broches, en escalier de gauche a
    // droite (les broches sont a 2,54 mm, une etiquette est plus large)
    g.add(tag(PIN_NAMES[from] || from.split(':')[1], WIRE_COLORS[color], new THREE.Vector3(a.x, 0, a.z + 3.4 + rank * 3.0), 0));
  }
  if (tags && to.startsWith('support:')) {
    const name = to.split(':')[1];
    const rail = /^(3V|5V)\./.test(name);
    // colonne de gauche : etiquette sur le Pico, a droite de la broche ; colonne de droite : a gauche ;
    // rails : sous la carte.
    const side = rail ? 0 : (b.x < 87 ? 1 : -1);
    const at = rail ? new THREE.Vector3(b.x, 0, b.z + 4.6) : b;
    g.add(tag(pinLabel(to), WIRE_COLORS[color], at, side));
  }
  return g;
}

// ---------------------------------------------------------------- etapes

// show : pieces visibles ; lift : piece qu'on est en train de poser (la batterie) (montree
// au-dessus de sa place avec une fleche) ; wires : groupes de fils, le dernier
// est celui de l'etape (les autres sont grises).
export const STEPS = {
  // Le kit est livre monte : cartes vissees sur la plaque, batterie en place
  // (coupee par l'interrupteur rouge).
  '00_kit': { titre: 'Kit livré', title: 'Kit as delivered', show: ['batt', 'support', 'pico', 's88', 'oled', 'oledModule', 'cell'] },
  '01_fils_ecran': { titre: '1. Câbles écran', title: '1. Screen wires', show: ['batt', 'support', 'pico', 's88', 'oled', 'oledModule', 'cell'], wires: ['ecran'] },
  '02_fils_s88': { titre: '2. Câbles S88', title: '2. S88 wires', show: ['batt', 'support', 'pico', 's88', 'oled', 'oledModule', 'cell'], wires: ['ecran', 's88'] },
  '03_fils_alim': { titre: '3. Câbles alim.', title: '3. Power wires', show: ['batt', 'support', 'pico', 's88', 'oled', 'oledModule', 'cell'], wires: ['ecran', 's88', 'alim'] },
  '04_allume': { titre: '4. Allumé', title: '4. Switched on', show: ['batt', 'support', 'pico', 's88', 'oled', 'oledModule', 'cell'], wires: ['ecran', 's88', 'alim'], allWires: true, lit: true },
  'option_bme280': { titre: 'Option BME280', title: 'BME280 option', show: [...['batt', 'support', 'pico', 's88', 'oled', 'oledModule', 'cell'], 'bme'], wires: ['ecran', 's88', 'alim', 'bme'], lit: true, withBme: true },
};

export async function loadScreen(url) {
  const img = new Image();
  img.src = url;
  await img.decode();
  const c = document.createElement('canvas');
  c.width = img.width; c.height = img.height;
  c.getContext('2d').drawImage(img, 0, 0);
  screenCanvas = c;
}

export async function loadPlate(url) {
  const geo = await new STLLoader().loadAsync(url);
  // STL : x, y (vers le haut de la plaque), z. Origine ramenee au coin.
  geo.applyMatrix4(new THREE.Matrix4().set(
    1, 0, 0, -15.46,
    0, 0, 1, 0,
    0, -1, 0, -28.92,
    0, 0, 0, 1));
  geo.computeVertexNormals();
  // plaque noire, logo AirCarto en relief imprime en blanc (comme le kit reel)
  const pos = geo.attributes.position;
  const col = new Float32Array(pos.count * 3);
  const black = new THREE.Color('#2b2b2e'), white = new THREE.Color('#f2f2ef');
  for (let i = 0; i < pos.count; i += 3) {
    let x = 0, y = 0, z = 0;
    for (let k = 0; k < 3; k++) { x += pos.getX(i + k) / 3; y += pos.getY(i + k) / 3; z += pos.getZ(i + k) / 3; }
    const logo = y > 0.05 && x > 52 && x < 113 && z > 84 && z < 104;
    const c = logo ? white : black;
    for (let k = 0; k < 3; k++) c.toArray(col, (i + k) * 3);
  }
  geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
  const m = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.8 }));
  m.castShadow = m.receiveShadow = true;
  m.userData.label = 'Plaque de montage';
  return m;
}

// Pointilles orange d'un trou de la piece soulevee jusqu'a sa place, avec une
// pointe de fleche en bas.
function guide(top, bottom, color = '#ff7a00') {
  const g = new THREE.Group();
  const m = mat(color, { roughness: 0.4, emissive: color, emissiveIntensity: 0.35 });
  const len = top.y - bottom.y;
  const dash = 2.2, gap = 1.6;
  for (let y = 3.5; y + dash < len; y += dash + gap) {
    const c = new THREE.Mesh(new THREE.CylinderGeometry(0.42, 0.42, dash, 10), m);
    c.position.set(bottom.x, bottom.y + y + dash / 2, bottom.z);
    g.add(c);
  }
  const head = new THREE.Mesh(new THREE.ConeGeometry(1.3, 3.2, 20), m);
  head.rotation.x = Math.PI;
  head.position.set(bottom.x, bottom.y + 1.6, bottom.z);
  g.add(head);
  return g;
}

// Construit la scene d'une etape. Rend { root, focus } : focus = boite a cadrer.
export function buildStep(plate, key, { tags = false } = {}) {
  const step = STEPS[key];
  if (!step) throw new Error(`etape inconnue : ${key}`);
  const root = new THREE.Group();
  root.add(plate);
  const parts = {
    batt: makeBatteryBoard(),
    support: makeCarrierPico(),
    pico: makePico(),
    s88: makeS88(),
    bme: makeBME(),
    oled: makeOledCarrier(),
  };
  parts.oledModule = makeOledModule(parts.oled.userData.moduleAt, { lit: !!step.lit, withBme: !!step.withBme });
  // pile dans son support (coordonnees de la carte batterie)
  const cell = makeCell();
  const slot = parts.batt.userData.cellSlot;
  cell.position.set(slot.u, 1.4 + 9, slot.v);
  parts.batt.add(cell);
  cell.visible = step.show.includes('cell');
  parts.cell = cell;

  const lifts = [].concat(step.lift || []);
  const LIFT = 34;
  for (const [name, g] of Object.entries(parts)) {
    if (name === 'cell') continue;
    if (!step.show.includes(name)) continue;
    root.add(g);
  }
  // les fils se calculent avant de soulever quoi que ce soit
  const wires = step.wires || [];
  wires.forEach((grp, gi) => {
    const current = step.allWires || gi === wires.length - 1;
    // rang de chaque broche de gauche a droite, pour etager les etiquettes
    const xs = WIRES[grp].map((w) => anchorWorld(parts, w[0]).x).sort((p, q) => p - q);
    WIRES[grp].forEach((w) => {
      const rank = xs.indexOf(anchorWorld(parts, w[0]).x);
      root.add(makeWire(parts, w, { faded: !current, tags: tags && current, rank }));
    });
  });

  const focus = new THREE.Box3();
  for (const name of lifts) {
    const g = parts[name];
    g.updateMatrixWorld(true);
    const from = (g.userData.guides || []).map((p) => p.clone().applyMatrix4(g.matrixWorld));
    focus.union(new THREE.Box3().setFromObject(g));
    g.position.y += LIFT;
    focus.union(new THREE.Box3().setFromObject(g));
    for (const p of from) root.add(guide(p.clone().add(new THREE.Vector3(0, LIFT, 0)), p));
  }
  return { root, parts, focus: lifts.length ? focus : null };
}

// Une piece seule, pour la liste du materiel.
export function buildPart(name) {
  const parts = {
    batt: () => {
      // carte batterie avec sa batterie, comme dans le kit livre
      const g = makeBatteryBoard();
      const cell = makeCell(), slot = g.userData.cellSlot;
      cell.position.set(slot.u, 1.4 + 9, slot.v);
      g.add(cell);
      return g;
    },
    support: makeCarrierPico,
    pico: makePico,
    s88: makeS88,
    bme: makeBME,
    oled: () => {
      const c = makeOledCarrier();
      const grp = new THREE.Group();
      grp.add(c, makeOledModule(c.userData.moduleAt, { lit: true, withBme: true }));
      return grp;
    },
    cell: makeCell,
  };
  if (!parts[name]) throw new Error(`piece inconnue : ${name}`);
  return parts[name]();
}

export function dupontSample() {
  const g = new THREE.Group();
  const colors = ['rouge', 'noir', 'orange', 'bleu', 'jaune', 'blanc', 'marron', 'vert'];
  colors.forEach((c, i) => {
    const x = i * 5;
    const curve = new THREE.CatmullRomCurve3([
      new THREE.Vector3(x, 0, 0), new THREE.Vector3(x + 6, 6, 40), new THREE.Vector3(x - 4, 3, 85), new THREE.Vector3(x, 0, 120)]);
    const t = new THREE.Mesh(new THREE.TubeGeometry(curve, 120, 0.8, 10), new THREE.MeshStandardMaterial({ color: WIRE_COLORS[c], roughness: 0.5 }));
    t.castShadow = true;
    g.add(t);
    for (const p of [curve.getPoint(0), curve.getPoint(1)]) {
      const h = box(PITCH, DUPONT_L, PITCH, M.plastic());
      h.position.set(p.x, p.y + PITCH / 2, p.z + (p.z > 60 ? DUPONT_L / 2 : -DUPONT_L / 2));
      g.add(h);
    }
  });
  return g;
}
