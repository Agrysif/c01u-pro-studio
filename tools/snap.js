// README screenshots and the tour GIF, taken from the real panel (http://127.0.0.1:47811).
// The language is switched in the page only and the GIF animates parameters in the page only,
// so the user's settings stay as they are.
//
// Needs Node.js, Microsoft Edge, ffmpeg and playwright-core (npm install --no-save playwright-core).
//   node tools/snap.js docs/screenshots <frames dir>               PNGs for both languages + GIF frames
//   SHOTS=0 GIF_LANG=ru node tools/snap.js <unused> <frames dir>   only the GIF frames, in Russian
// Frames to GIF:
//   ffmpeg -framerate 12 -i <frames>/f%03d.png -vf "scale=880:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=160:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" docs/screenshots/tour-en.gif
const { chromium } = require('playwright-core');
const path = require('path');
const fs = require('fs');

const [OUT, FRAMES] = process.argv.slice(2);
fs.mkdirSync(OUT, { recursive: true });
fs.mkdirSync(FRAMES, { recursive: true });

(async () => {
  const browser = await chromium.launch({ channel: 'msedge', headless: false, args: ['--headless=new', '--hide-scrollbars'] });
  const ctx = await browser.newContext({ viewport: { width: 1240, height: 860 }, deviceScaleFactor: 1, colorScheme: 'dark' });
  const page = await ctx.newPage();
  page.on('pageerror', (e) => console.log('pageerror', e.message));
  await page.goto('http://127.0.0.1:47811/#overview');
  await page.waitForFunction(() => typeof S !== 'undefined' && S.state && S.params, null, { timeout: 20000 });
  await page.waitForTimeout(1500);
  const setLang = (c) => page.evaluate(async (c) => { await loadLang(c); renderAll(); }, c);
  const goto = (p) => page.evaluate((p) => { go(p); document.querySelector('.main') && document.querySelector('.main').scrollTo(0, 0); window.scrollTo(0, 0); }, p);

  await goto('diag');
  await page.evaluate(() => runDiag());
  await page.waitForFunction(() => typeof lastDiag !== 'undefined' && lastDiag, null, { timeout: 90000 });

  for (const lang of (process.env.SHOTS === '0' ? [] : ['en', 'ru'])) {
    await setLang(lang);
    for (const p of ['overview', 'processing', 'eq', 'fx', 'ab', 'diag', 'settings', 'guide']) {
      await goto(p);
      await page.waitForTimeout(p === 'overview' || p === 'eq' ? 2500 : 900);
      await page.screenshot({ path: path.join(OUT, `${p}-${lang}.png`) });
      console.log('shot', p, lang);
    }
  }

  // GIF frames
  await setLang(process.env.GIF_LANG || 'en');
  let n = 0;
  const frame = async () => page.screenshot({ path: path.join(FRAMES, `f${String(n++).padStart(3, '0')}.png`) });
  const orig = await page.evaluate(() => JSON.stringify(S.params));
  const restore = () => page.evaluate((o) => { S.params = JSON.parse(o); refreshParams(); }, orig);

  await goto('overview');
  await page.waitForTimeout(1500);
  for (let i = 0; i < 16; i++) { await frame(); await page.waitForTimeout(70); }

  await goto('eq');
  await page.waitForTimeout(600);
  for (let i = 0; i < 40; i++) {
    await page.evaluate((i) => {
      const b = S.params.eq;
      let k = b.findIndex((x) => x.on && GAIN_TYPES.has(x.type));
      if (k < 0) { k = 1; b[k].on = true; b[k].type = 0; }
      eqSel = k;
      b[k].gain = Math.round(9 * Math.sin((i / 39) * Math.PI * 2) * 2) / 2;
      refreshParams();
    }, i);
    await frame();
  }
  await restore();

  await goto('processing');
  await page.evaluate(() => { const c = document.getElementById('mod-comp'); if (c) c.scrollIntoView({ block: 'center' }); });
  await page.waitForTimeout(600);
  for (let i = 0; i < 28; i++) {
    await page.evaluate((i) => {
      S.params.compThresholdDb = Math.round((-22 - 16 * Math.sin((i / 27) * Math.PI)) * 2) / 2;
      S.params.compRatio = Math.round((3 + 3 * Math.sin((i / 27) * Math.PI)) * 10) / 10;
      refreshParams();
    }, i);
    await frame();
  }
  await restore();

  await goto('ab');
  await page.waitForTimeout(600);
  for (let i = 0; i < 10; i++) { await frame(); await page.waitForTimeout(70); }
  await goto('diag');
  await page.waitForTimeout(400);
  for (let i = 0; i < 14; i++) await frame();
  await goto('overview');
  await page.waitForTimeout(800);
  for (let i = 0; i < 12; i++) { await frame(); await page.waitForTimeout(70); }

  await restore();
  console.log('frames', n);
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });

