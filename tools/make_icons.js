// Renders the app and tray icons from tools/icons.js (the emblem: enamel plate, bakelite medallion,
// the C01U Pro microphone) into assets/icons/*.ico and the web UI's favicon and badge.
// Each size is drawn from the SVG at that size, so small icons get their own simplified artwork.
//   node tools/make_icons.js      (needs Microsoft Edge and playwright-core, e.g. from promo/node_modules)
'use strict';

const fs = require('fs');
const path = require('path');
const { svg, traySvg, micSvg } = require('./icons.js');

let chromium;
try { ({ chromium } = require('playwright-core')); } catch { ({ chromium } = require('../promo/node_modules/playwright-core')); }

const ROOT = path.join(__dirname, '..');
const OUT = path.join(ROOT, 'assets', 'icons');
const VARIANT = 'badge';

// PNG-compressed ICO (Windows Vista and newer)
function ico(images) {
  const header = Buffer.alloc(6);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(images.length, 4);
  const entries = [];
  let offset = 6 + 16 * images.length;
  for (const [size, png] of images) {
    const e = Buffer.alloc(16);
    e.writeUInt8(size >= 256 ? 0 : size, 0);
    e.writeUInt8(size >= 256 ? 0 : size, 1);
    e.writeUInt16LE(1, 4);
    e.writeUInt16LE(32, 6);
    e.writeUInt32LE(png.length, 8);
    e.writeUInt32LE(offset, 12);
    entries.push(e);
    offset += png.length;
  }
  return Buffer.concat([header, ...entries, ...images.map(([, png]) => png)]);
}

(async () => {
  const browser = await chromium.launch({ channel: 'msedge', headless: false, args: ['--headless=new'] });
  const page = await browser.newPage({ deviceScaleFactor: 1 });
  const render = async (markup, size) => {
    await page.setViewportSize({ width: size, height: size });
    await page.setContent(`<html><body style="margin:0;background:transparent">${markup}</body></html>`);
    return page.screenshot({ omitBackground: true, clip: { x: 0, y: 0, width: size, height: size } });
  };
  const plans = {
    app: [[16, 20, 24, 32, 40, 48, 64, 128, 256], (s) => svg(VARIANT, s)],
    tray: [[16, 20, 24, 32, 40, 48, 64], (s) => traySvg(s, false)],
    tray_muted: [[16, 20, 24, 32, 40, 48, 64], (s) => traySvg(s, true)],
  };
  fs.mkdirSync(OUT, { recursive: true });
  for (const [name, [sizes, art]] of Object.entries(plans)) {
    const images = [];
    for (const s of sizes) images.push([s, await render(art(s), s)]);
    fs.writeFileSync(path.join(OUT, `${name}.ico`), ico(images));
    if (name === 'app') fs.writeFileSync(path.join(OUT, 'app-256.png'), images[images.length - 1][1]);
    console.log('wrote', `${name}.ico`);
  }
  // the web UI: favicon and the microphone in the sidebar badge
  fs.writeFileSync(path.join(ROOT, 'web', 'favicon.svg'), svg(VARIANT, 256));
  fs.writeFileSync(path.join(ROOT, 'web', 'c01u.svg'), micSvg());
  console.log('wrote web/favicon.svg, web/c01u.svg');
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
