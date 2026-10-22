// Покадровый рендер промо-ролика в MP4 (H.264 1080p60 + AAC) через Microsoft Edge без окна.
//   node promo/render.js                         → promo/out/C01U-Pro-Studio-promo-ru.mp4
//   node promo/render.js --lang en               → английские подписи
//   node promo/render.js --stills 1,8.2,13.3     → PNG-кадры в promo/out/stills
//   --blur 3   размытие движения из N подкадров (по умолчанию 3; 1 — без него, быстрее)
//   --dbg      время и номер доли в кадре
// Нужны ffmpeg в PATH, Microsoft Edge, `npm install` в папке promo и трек promo/music/c01u-suno.mp3
// (с данными для визуала: python promo/analyze.py promo/music/c01u-suno.mp3 promo/music/track.json).
'use strict';

const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const { chromium } = require('playwright-core');
const { start } = require('./server');

const argv = process.argv;
const arg = (name, def) => {
  const i = argv.indexOf(name);
  return i > 0 && argv[i + 1] ? argv[i + 1] : def;
};
const LANG = arg('--lang', 'ru');
const OUT_DIR = path.join(__dirname, 'out');
const OUT = arg('--out', path.join(OUT_DIR, `C01U-Pro-Studio-promo-${LANG}.mp4`));
const MUSIC = arg('--music', path.join(__dirname, 'music', 'c01u-suno.mp3'));
const STILLS = arg('--stills', null);
const SUB = Math.max(1, parseInt(arg('--blur', '3'), 10) || 1);
const PORT = 47900;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  fs.mkdirSync(OUT_DIR, { recursive: true });
  const server = await start(PORT);
  const browser = await chromium.launch({
    channel: 'msedge', headless: false,
    args: ['--headless=new', '--hide-scrollbars', '--force-device-scale-factor=1', '--force-color-profile=srgb', '--disable-background-timer-throttling'],
  });
  const page = await browser.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1 });
  page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') console.log('PAGE:', m.text()); });
  page.on('pageerror', (e) => console.log('PAGE ERROR:', e.message));
  await page.goto(`http://127.0.0.1:${PORT}/promo/index.html?lang=${LANG}${argv.includes('--dbg') ? '&debug=1' : ''}`);
  // страница и настоящий пульт во фрейме (его таймеры виртуальные, поэтому опрашиваем отсюда)
  for (let i = 0; ; i++) {
    const ok = await page.evaluate(() => window.__ready === true && window.__uiReady && window.__uiReady()).catch(() => false);
    if (ok) break;
    if (i > 300) throw new Error('страница не загрузилась');
    await sleep(100);
  }
  const meta = await page.evaluate(() => window.__meta);
  const frames = Math.round(meta.duration * meta.fps);
  const started = Date.now();
  const step = (ms) => page.evaluate((v) => window.__step(v, false), ms);

  if (STILLS) {
    const dir = path.join(OUT_DIR, 'stills');
    fs.mkdirSync(dir, { recursive: true });
    const want = new Map(STILLS.split(',').map((s) => [Math.round(parseFloat(s) * meta.fps), s]));
    const last = Math.max(...want.keys());
    for (let i = 0; i <= last; i++) {
      await step((i * 1000) / meta.fps);
      if (!want.has(i)) continue;
      const name = `${LANG}-t${String(want.get(i)).replace('.', '_')}.png`;
      await page.screenshot({ path: path.join(dir, name) });
      console.log('кадр', name);
    }
    console.log(`готово за ${((Date.now() - started) / 1000).toFixed(1)} с`);
  } else {
    // размытие движения: каждый кадр видео — среднее из SUB подкадров
    const subFps = meta.fps * SUB;
    const vf = SUB > 1 ? `tmix=frames=${SUB},fps=${meta.fps}` : 'null';
    const ff = spawn('ffmpeg', [
      '-y', '-loglevel', 'error',
      '-f', 'image2pipe', '-c:v', 'mjpeg', '-framerate', String(subFps), '-i', '-',
      '-ss', String(meta.musicStart), '-t', String(meta.duration), '-i', MUSIC,
      // ролик длиннее трека (финальная плашка в тишине) — звук добивается тишиной до конца видео
      '-filter_complex', `[0:v]${vf}[v];[1:a]afade=t=in:st=0:d=0.02,apad[a]`,
      '-map', '[v]', '-map', '[a]',
      '-c:v', 'libx264', '-preset', 'slow', '-crf', '16', '-pix_fmt', 'yuv420p', '-r', String(meta.fps),
      '-c:a', 'aac', '-b:a', '256k', '-movflags', '+faststart', '-shortest', OUT,
    ], { stdio: ['pipe', 'inherit', 'inherit'] });
    const ffDone = new Promise((r) => ff.on('close', r));
    for (let i = 0; i < frames * SUB; i++) {
      await step((i * 1000) / subFps);
      const jpg = await page.screenshot({ type: 'jpeg', quality: 94 });
      if (!ff.stdin.write(jpg)) await new Promise((r) => ff.stdin.once('drain', r));
      if (i % (60 * SUB) === 0) console.log(`кадр ${i / SUB}/${frames}  ${((Date.now() - started) / 1000).toFixed(0)} с`);
    }
    ff.stdin.end();
    const code = await ffDone;
    console.log(code === 0 ? `готово: ${OUT} за ${((Date.now() - started) / 1000).toFixed(0)} с` : `ffmpeg завершился с кодом ${code}`);
    process.exitCode = code === 0 ? 0 : 1;
  }
  await browser.close();
  server.close();
})().catch((e) => { console.error(e); process.exit(1); });
