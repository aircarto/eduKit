// Rend les images du tutoriel a partir de scene/index.html.
//
//   npm run render                  toutes les images -> img/
//   npm run render -- 07_fils_ecran seulement les images dont le nom contient ce texte
//   PORT=8080 npm run viewer        sert docs/ et affiche l'adresse de la visionneuse
//
// Le navigateur est le Chromium de Playwright :
//   npx playwright-core install chromium-headless-shell

import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';

const here = path.dirname(fileURLToPath(import.meta.url));
const docsRoot = path.resolve(here, '..');
const outDir = path.join(here, 'img');
const PORT = +(process.env.PORT || 0);   // 0 : port libre choisi par le systeme

// nom du fichier -> parametres de scene/index.html
export const JOBS = {
  // kit tel qu'il est livre, puis etapes : vue d'ensemble
  kit_monte: 'step=00_kit',
  etape01_fils_ecran: 'step=01_fils_ecran',
  etape02_fils_s88: 'step=02_fils_s88',
  etape03_fils_alim: 'step=03_fils_alim',
  etape04_allume: 'step=04_allume',
  option_bme280: 'step=option_bme280',
  // gros plans de dessus sur la carte support, pour lire les broches
  etape01_fils_ecran_zoom: 'step=01_fils_ecran&view=top&zoom=support',
  etape02_fils_s88_zoom: 'step=02_fils_s88&view=top&zoom=support',
  etape03_fils_alim_zoom: 'step=03_fils_alim&view=top&zoom=support',
  option_bme280_zoom: 'step=option_bme280&view=top&zoom=support',
  // gros plans de dessus sur l'element a brancher (zone de la plaque en mm)
  etape01_fils_ecran_zoom_ecran: 'step=01_fils_ecran&view=top&zone=60,100,114,154',
  etape02_fils_s88_zoom_capteur: 'step=02_fils_s88&view=top&zone=2,4,52,52',
  etape03_fils_alim_zoom_batterie: 'step=03_fils_alim&view=top&zone=20,62,58,94',
  option_bme280_zoom_capteur: 'step=option_bme280&view=top&zone=12,38,44,66',
  // vues de dessus : kit livre, montage termine
  kit_monte_dessus: 'step=00_kit&view=top',
  schema_dessus: 'step=option_bme280&view=top',
  // versions anglaises des images ou l'ecran est allume (firmware anglais)
  etape04_allume_en: 'step=04_allume&screen=../../ecran_edukit_en.png',
  option_bme280_en: 'step=option_bme280&screen=../../ecran_edukit_en.png',
  schema_dessus_en: 'step=option_bme280&view=top&screen=../../ecran_edukit_en.png',
  piece_ecran_en: 'part=oled&w=1000&h=800&screen=../../ecran_edukit_en.png',
  // pieces, fond transparent
  piece_carte_batterie: 'part=batt&w=1000&h=800',
  piece_carte_support: 'part=support&w=1000&h=800',
  piece_pico: 'part=pico&w=1000&h=800',
  piece_capteur_s88: 'part=s88&w=1000&h=800',
  piece_ecran: 'part=oled&w=1000&h=800',
  piece_bme280: 'part=bme&w=1000&h=800',
  piece_cables_dupont: 'part=dupont&w=1000&h=800',
};

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.png': 'image/png', '.stl': 'model/stl', '.svg': 'image/svg+xml' };

function serve() {
  const server = http.createServer((req, res) => {
    const url = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    const file = path.join(docsRoot, url);
    if (!file.startsWith(docsRoot) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
      const index = path.join(file, 'index.html');
      if (fs.existsSync(index)) {
        res.writeHead(200, { 'content-type': 'text/html' });
        return fs.createReadStream(index).pipe(res);
      }
      res.writeHead(404); return res.end();
    }
    res.writeHead(200, { 'content-type': TYPES[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise((ok) => server.listen(PORT, '127.0.0.1', () => ok(server)));
}

const args = process.argv.slice(2);
const server = await serve();
const base = `http://127.0.0.1:${server.address().port}`;
if (args.includes('--serve')) {
  console.log(`${base}/tuto/scene/`);
} else {
  fs.mkdirSync(outDir, { recursive: true });
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage();
  page.on('console', (m) => { if (m.type() === 'error') console.error('  [page]', m.text()); });
  page.on('pageerror', (e) => console.error('  [page]', e.message));
  const jobs = Object.entries(JOBS).filter(([name]) => !args.length || args.some((a) => name.includes(a)));
  for (const [name, params] of jobs) {
    const p = new URLSearchParams(params);
    const w = +(p.get('w') || 1600), h = +(p.get('h') || 1200);
    await page.setViewportSize({ width: w, height: h });
    const t0 = Date.now();
    await page.goto(`${base}/tuto/scene/index.html?render=1&${params}`);
    await page.waitForFunction(() => window.__ready === true, null, { timeout: 120000 });
    const out = path.join(outDir, `${name}.png`);
    await page.locator('canvas').screenshot({ path: out, omitBackground: p.has('part') });
    console.log(`${name}.png  (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
  }
  await browser.close();
  server.close();
}
