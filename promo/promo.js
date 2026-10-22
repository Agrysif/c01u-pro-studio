// Промо-ролик C01U Pro Studio: ~31 с, 1920×1080, 60 к/с. Каждый кадр — функция времени трека t,
// склейки стоят на долях Suno-трека (120 BPM, первая доля 0,19 с, дроп 13,19 с, финальный удар 29,19 с).
//
// Сюжет: ретро-телевизор включается — на экране чёрно-белый C01U Pro в ряби и шуме: «годами без
// обновлений». Помехи, щелчки каналов → на экране современный установщик → щелчок ручки каналов,
// картинка становится цветной: «новые настройки». Камера влетает в кинескоп → дроп: микрофон и логотип.
// Дальше настоящий пульт на экране телевизора, студийная цепочка, программы как каналы, табло
// языков, обновление само по себе, финальная плашка — и телевизор гаснет в точку.
import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { Line2 } from 'three/addons/lines/Line2.js';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import { LineGeometry } from 'three/addons/lines/LineGeometry.js';
import { buildMic } from './mic.js';

const W = 1920, H = 1080;
const LANG = window.__lang;
const DEBUG = new URLSearchParams(location.search).has('debug');
const $ = (s) => document.querySelector(s);

// ------------------------------------------------------------------ тексты
const TXT = {
  ru: {
    caps: [
      { at: 'hear', l1: 'Годами без обновлений', l2: 'старый драйвер не ставится на Windows 11' },
      { at: 'noise', words: ['Шум.', 'Гул.', 'Щелчки.'] },
      { at: 'rise', l1: 'Пора обновиться' },
      { at: 'back', l1: 'Новые настройки', l2: 'новый звук вашего C01U Pro' },
      { at: 'ns', l1: 'Шумодав без нейросетей', l2: '0,8% процессора · 12 мс задержки' },
      { at: 'rack', l1: 'Студийная цепочка', l2: 'гейт · де-эссер · компрессор · лимитер' },
      { at: 'eq', l1: 'Эквалайзер на 10 полос', l2: 'поверх живого спектра' },
      { at: 'diag', l1: 'Диагностика', l2: 'исправление в один клик' },
      { at: 'apps', l1: 'Работает везде', l2: 'даже при закрытом пульте' },
      { at: 'upd', l1: 'Обновляется сам', l2: 'бесплатно · с GitHub' },
    ],
    tags: ['ШУМ ВЕНТИЛЯТОРА', 'ЩЕЛЧКИ КЛАВИАТУРЫ', 'ГУЛ 50 ГЦ'],
    install: ['Установка нового драйвера', ['Модуль обработки звука', 'Windows 11 · 25H2', 'Цифровая подпись проверена', 'Улучшения звука включены']],
    channel: 'КАНАЛ',
    update: ['C01U Pro Studio', 'Загружаю обновление…', 'Обновлено до новой версии', 'Само, без окон Windows — пока микрофон свободен'],
    logoTag: 'Чистый голос<br>во всех программах',
    rack: [['ШУМОДАВ', 'СПЕКТРАЛЬНЫЙ'], ['ГЕЙТ', 'АВТОПОРОГ'], ['ДЕ-ЭССЕР', 'С · Ш'], ['КОМПРЕССОР', '3 : 1'], ['ЛИМИТЕР', '−1 дБ']],
    langsFinal: '25 языков',
    lkLine: 'Драйвер для Samson C01U Pro',
    lkSmall: 'Неофициальный проект · Windows',
  },
  en: {
    caps: [
      { at: 'hear', l1: 'Years without updates', l2: 'the old driver won’t install on Windows 11' },
      { at: 'noise', words: ['Noise.', 'Hum.', 'Clicks.'] },
      { at: 'rise', l1: 'Time to update' },
      { at: 'back', l1: 'New settings', l2: 'a new sound for your C01U Pro' },
      { at: 'ns', l1: 'Noise suppression, no AI', l2: '0.8% CPU · 12 ms latency' },
      { at: 'rack', l1: 'A studio chain', l2: 'gate · de-esser · compressor · limiter' },
      { at: 'eq', l1: '10-band equalizer', l2: 'over the live spectrum' },
      { at: 'diag', l1: 'Diagnostics', l2: 'one-click fixes' },
      { at: 'apps', l1: 'Works everywhere', l2: 'even with the panel closed' },
      { at: 'upd', l1: 'Updates itself', l2: 'free · from GitHub' },
    ],
    tags: ['FAN NOISE', 'KEYBOARD CLICKS', '50 HZ HUM'],
    install: ['Installing the new driver', ['Audio processing module', 'Windows 11 · 25H2', 'Digital signature verified', 'Audio enhancements on']],
    channel: 'CHANNEL',
    update: ['C01U Pro Studio', 'Downloading the update…', 'Updated to the new version', 'By itself, no Windows prompts — while the mic is free'],
    logoTag: 'Clean voice<br>in every app',
    rack: [['NOISE', 'SPECTRAL'], ['GATE', 'AUTO'], ['DE-ESSER', 'S · SH'], ['COMPRESSOR', '3 : 1'], ['LIMITER', '−1 dB']],
    langsFinal: '25 languages',
    lkLine: 'Driver for the Samson C01U Pro',
    lkSmall: 'Unofficial project · Windows',
  },
}[LANG];
const APPS = ['Discord', 'OBS', 'Zoom', 'Telegram'];
const WORDS = ['Привет', 'Hello', 'Сәлем', 'Salom', 'Привіт', 'Բարև', 'გამარჯობა', 'Hola', '你好', 'こんにちは', '안녕하세요', 'Merhaba'];

// ------------------------------------------------------------------ время
const SPB = 60 / 119.96, B0 = 0.19;
const beat = (n) => B0 + n * SPB;
const T = {
  open: beat(0), pic: beat(1), wide: beat(2), hear: beat(3), stab1: beat(9), stab2: beat(10), stab3: beat(10.5), rise: beat(11),
  knob: beat(15), flip: beat(16), back: beat(17), build: beat(19), hit: beat(22), thru: beat(25), drop: beat(26),
  ns: beat(30), brk: beat(32), rack: beat(34), eq: beat(38), diag: beat(42), stop2: beat(45), apps: beat(46),
  langs: beat(50), stop3: beat(53), upd: beat(54), fin: beat(58),
};
T.off = T.fin + 1.66;
T.noise = T.stab1;
const DURATION = 31.3;  // музыка заканчивается на 30,35 с; дальше тишина, плашка и гаснущий кинескоп
const CAP_END = { hear: T.stab1 - 0.04, noise: T.rise, rise: T.knob, back: T.thru - 0.15, ns: T.rack, rack: T.eq, eq: T.diag, diag: T.apps, apps: T.langs, upd: T.fin };

// ------------------------------------------------------------------ математика
const clamp = (v, a = 0, b = 1) => Math.min(b, Math.max(a, v));
const lerp = (a, b, k) => a + (b - a) * k;
const seg = (t, a, b) => clamp((t - a) / (b - a));
const smooth = (k) => k * k * (3 - 2 * k);
const outCubic = (k) => 1 - Math.pow(1 - k, 3);
const inCubic = (k) => k * k * k;
const inOut = (k) => (k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2);
const outExpo = (k) => (k >= 1 ? 1 : 1 - Math.pow(2, -10 * k));
const outBack = (k, s = 1.70158) => 1 + (s + 1) * Math.pow(k - 1, 3) + s * Math.pow(k - 1, 2);
const pulse = (t, t0, len = 0.3) => (t < t0 ? 0 : Math.exp(-((t - t0) / len) * 3));
const within = (t, a, b) => t >= a && t < b;
function rng(seed) {
  return () => {
    seed |= 0; seed = (seed + 0x6d2b79f5) | 0;
    let r = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    r = (r + Math.imul(r ^ (r >>> 7), 61 | r)) ^ r;
    return ((r ^ (r >>> 14)) >>> 0) / 4294967296;
  };
}
const shake = (t, f) => [
  Math.sin(t * 37.1 * f) * 0.5 + Math.sin(t * 61.7 * f + 1.3) * 0.3 + Math.sin(t * 97.3 * f + 2.1) * 0.2,
  Math.sin(t * 41.3 * f + 0.7) * 0.5 + Math.sin(t * 73.9 * f + 2.9) * 0.3 + Math.sin(t * 89.1 * f + 0.4) * 0.2,
];

// ------------------------------------------------------------------ трек
const track = await fetch('music/track.json').then((r) => r.json());
const fi = (t) => clamp(Math.round(t * track.fps), 0, track.level.length - 1);
const level = (t) => track.level[fi(t)];
const loud = (t) => clamp((level(t) + 30) / 16);
const r1 = (v) => Math.round(v * 10) / 10;

// живые индикаторы пульта во фрейме питаются музыкой
window.__meters = (t) => {
  const l = level(t), k = loud(t), sp = track.spec[fi(t)];
  const top = (sp[54] + sp[56] + sp[58]) / 3;
  return {
    run: true, in: [r1(l - 1.5), r1(l - 7)], out: [r1(l + 0.5), r1(l - 4)], clip: false, floor: -74, voice: k > 0.25,
    gr: { gate: 0, ns: r1(-16 - 8 * (1 - k)), comp: r1(-7 * k), lim: r1(l > -13 ? -1.2 : 0), agc: 2.5, deess: r1(clamp((top + 40) / 8, 0, 1) * -4), pop: 0 },
    lufsM: r1(l - 1), lufsS: r1(l - 2), pitch: r1(128 + 9 * Math.sin(t * 2.3)),
    spec: sp, mode: 'apo', rec: { state: 0, pos: 0, len: 0 },
  };
};

// ------------------------------------------------------------------ 3D: рендерер
const glCanvas = $('#gl');
const renderer = new THREE.WebGLRenderer({ canvas: glCanvas, antialias: true, preserveDrawingBuffer: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(1);
renderer.setSize(W, H, false);
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.0;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFShadowMap;
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0d0907);
scene.fog = new THREE.FogExp2(0x0d0907, 0.03);
scene.environment = new THREE.PMREMGenerator(renderer).fromScene(new RoomEnvironment(), 0.04).texture;
scene.environmentIntensity = 0.5;
const camera = new THREE.PerspectiveCamera(34, W / H, 0.05, 200);
// MSAA в буфере постобработки: сглаживает края и нужен сетке микрофона (alpha to coverage)
const composer = new EffectComposer(renderer, new THREE.WebGLRenderTarget(W, H, { samples: 4, type: THREE.HalfFloatType }));
composer.setPixelRatio(1);
composer.setSize(W, H);
composer.addPass(new RenderPass(scene, camera));
composer.addPass(new UnrealBloomPass(new THREE.Vector2(W, H), 0.32, 0.45, 0.92));  // свечение — только у ярких ламп
composer.addPass(new OutputPass());

const hemi = new THREE.HemisphereLight(0x5a4838, 0x080605, 0.7);
const key = new THREE.SpotLight(0xffe2c0, 110, 40, 0.5, 0.6, 1.5);
key.position.set(5.5, 6, 7.5);
key.target.position.set(0, -1.2, 0);
key.castShadow = true;
key.shadow.mapSize.set(2048, 2048);
key.shadow.bias = -0.0004;
const rim = new THREE.PointLight(0xffb070, 55, 25, 1.6);
rim.position.set(-4, 2.4, -3.2);
const rim2 = new THREE.PointLight(0xa0b8ff, 18, 25, 1.6);
rim2.position.set(4.2, 1.6, -3);
scene.add(hemi, key, key.target, rim, rim2);
const floor = new THREE.Mesh(new THREE.PlaneGeometry(90, 90), new THREE.MeshStandardMaterial({ color: 0x16100c, roughness: 0.55, metalness: 0.2 }));
floor.rotation.x = -Math.PI / 2;
floor.position.y = -4.12;
floor.receiveShadow = true;
scene.add(floor);

const mic = buildMic();
mic.rotation.x = 0.06;
scene.add(mic);

// мягкие огни студии вдали
const BN = 70;
const bGeo = new THREE.BufferGeometry();
const brand = rng(21);
const bPos = new Float32Array(BN * 3), bCol = new Float32Array(BN * 3), bSize = new Float32Array(BN), bAlpha = new Float32Array(BN);
for (let i = 0; i < BN; i++) {
  bPos.set([(brand() - 0.5) * 36, -3 + brand() * 10, -9 - brand() * 14], i * 3);
  const w = brand();
  bCol.set([1, 0.6 + w * 0.2, 0.3 + w * 0.15], i * 3);
  bSize[i] = 160 + brand() * 340;
  bAlpha[i] = 0.06 + 0.1 * brand();
}
bGeo.setAttribute('position', new THREE.BufferAttribute(bPos, 3));
bGeo.setAttribute('aColor', new THREE.BufferAttribute(bCol, 3));
bGeo.setAttribute('aSize', new THREE.BufferAttribute(bSize, 1));
bGeo.setAttribute('aAlpha', new THREE.BufferAttribute(bAlpha, 1));
const bokeh = new THREE.Points(bGeo, new THREE.ShaderMaterial({
  transparent: true, depthWrite: false, blending: THREE.AdditiveBlending,
  vertexShader: `attribute float aSize; attribute float aAlpha; attribute vec3 aColor; varying float vA; varying vec3 vC;
    void main(){ vec4 mv = modelViewMatrix * vec4(position, 1.0); gl_Position = projectionMatrix * mv; gl_PointSize = aSize * (6.0 / max(0.2, -mv.z)); vA = aAlpha; vC = aColor; }`,
  fragmentShader: `varying float vA; varying vec3 vC; void main(){ vec2 p = gl_PointCoord * 2.0 - 1.0; float d = dot(p, p); if (d > 1.0) discard; float a = smoothstep(1.0, 0.85, d) * vA; gl_FragColor = vec4(vC * a, a); }`,
}));
bokeh.frustumCulled = false;
scene.add(bokeh);

// чистая волна звука за микрофоном (после щелчка)
const WN = 360;
const wavePos = new Float32Array(WN * 3);
const waveGeo = new LineGeometry();
waveGeo.setPositions(wavePos);
const waveMat = new LineMaterial({ color: 0xffb04a, linewidth: 3.5, transparent: true, opacity: 0.85 });
waveMat.resolution.set(W, H);
const wave = new Line2(waveGeo, waveMat);
wave.frustumCulled = false;
scene.add(wave);
function updateWave(t) {
  const amp = 0.35 + 0.75 * loud(t);
  for (let i = 0; i < WN; i++) {
    const u = i / (WN - 1), x = lerp(-13, 13, u);
    const win = Math.pow(Math.sin(Math.PI * u), 1.5);
    const y = Math.sin(x * 1.1 - t * 5.2) * 0.65 + Math.sin(x * 2.6 - t * 7.9 + 1) * 0.25 + Math.sin(x * 0.5 + t * 1.8) * 0.2;
    wavePos.set([x, -0.9 + y * amp * win, -3.2], i * 3);
  }
  waveGeo.setPositions(wavePos);
  waveMat.color.setRGB(1.25, 0.72, 0.3);
}

// ------------------------------------------------------------------ 3D: рэковые модули студийной цепочки
function canvasTex(w, h, draw) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  draw(c.getContext('2d'), w, h);
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  return tex;
}
function enamel(g, w, h) {
  const gr = g.createLinearGradient(0, 0, 0, h);
  gr.addColorStop(0, '#f2e9d6'); gr.addColorStop(1, '#d6c9ad');
  g.fillStyle = gr; g.fillRect(0, 0, w, h);
  g.fillStyle = 'rgba(0,0,0,0.05)';
  for (let i = 0; i < 1600; i++) g.fillRect((i * 97) % w, (i * 57) % h, 2, 2);
  for (const [x, y] of [[40, 40], [w - 40, 40], [40, h - 40], [w - 40, h - 40]]) {
    g.fillStyle = '#9f927a'; g.beginPath(); g.arc(x, y, 16, 0, 7); g.fill();
    g.strokeStyle = '#5c5242'; g.lineWidth = 4; g.beginPath(); g.moveTo(x - 9, y); g.lineTo(x + 9, y); g.stroke();
  }
}
const rack = new THREE.Group();
const darkMetal = new THREE.MeshStandardMaterial({ color: 0x18181a, metalness: 0.4, roughness: 0.5 });
const rackModules = TXT.rack.map(([name, sub], i) => {
  const m = new THREE.Group();
  const tex = canvasTex(1040, 680, (g, w) => {
    enamel(g, w, 680);
    g.fillStyle = '#2a221a'; g.font = '700 84px "Bahnschrift SemiCondensed", "Bahnschrift"'; g.textAlign = 'left';
    g.fillText(name, 80, 140);
    g.fillStyle = '#6c5f4b'; g.font = '600 40px "Bahnschrift SemiCondensed", "Bahnschrift"';
    g.fillText(sub, 82, 196);
    g.strokeStyle = '#6c5f4b'; g.lineWidth = 5;
    for (const cx of [230, 520]) {
      for (let k = 0; k <= 10; k++) {
        const a = Math.PI * (0.75 + 1.5 * k / 10);
        g.beginPath(); g.moveTo(cx + Math.cos(a) * 118, 440 + Math.sin(a) * 118); g.lineTo(cx + Math.cos(a) * 138, 440 + Math.sin(a) * 138); g.stroke();
      }
    }
  });
  const faceM = new THREE.Mesh(new RoundedBoxGeometry(2.7, 1.76, 0.24, 4, 0.06), [0, 1, 2, 3].map(() => new THREE.MeshStandardMaterial({ color: 0xcfc2a6, roughness: 0.6 }))
    .concat([new THREE.MeshStandardMaterial({ map: tex, color: 0xc9bfa9, roughness: 0.7 }), new THREE.MeshStandardMaterial({ color: 0x8a7d63 })]));
  m.add(faceM);
  const kn = [-0.75, -0.19].map((x) => {
    const g = new THREE.Group();
    const k = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.24, 0.2, 48), new THREE.MeshStandardMaterial({ color: 0x1a1612, roughness: 0.35, metalness: 0.2 }));
    k.rotation.x = Math.PI / 2;
    const ptr = new THREE.Mesh(new THREE.BoxGeometry(0.03, 0.16, 0.02), new THREE.MeshStandardMaterial({ color: 0xf6efe1 }));
    ptr.position.set(0, 0.1, 0.11);
    g.add(k, ptr);
    g.position.set(x, -0.33, 0.2);
    m.add(g);
    return g;
  });
  const vu = new THREE.Mesh(new THREE.PlaneGeometry(0.78, 0.5), new THREE.MeshBasicMaterial({ map: canvasTex(400, 256, (g, w, h) => {
    const gr = g.createRadialGradient(w / 2, h, 20, w / 2, h, 300); gr.addColorStop(0, '#ffe2a8'); gr.addColorStop(1, '#d19a44');
    g.fillStyle = gr; g.fillRect(0, 0, w, h);
    g.strokeStyle = '#3a2a14'; g.lineWidth = 4; g.beginPath(); g.arc(w / 2, h + 40, 200, Math.PI * 1.22, Math.PI * 1.78); g.stroke();
    g.strokeStyle = '#c0281a'; g.beginPath(); g.arc(w / 2, h + 40, 200, Math.PI * 1.62, Math.PI * 1.78); g.stroke();
    g.fillStyle = '#3a2a14'; g.font = '700 46px "Bahnschrift"'; g.textAlign = 'center'; g.fillText('VU', w / 2, h - 50);
  }) }));
  vu.position.set(0.66, 0.28, 0.125);
  const needle = new THREE.Mesh(new THREE.BoxGeometry(0.012, 0.42, 0.005), new THREE.MeshBasicMaterial({ color: 0x1a120a }));
  needle.geometry.translate(0, 0.21, 0);
  needle.position.set(0.66, 0.05, 0.13);
  const lmat = new THREE.MeshStandardMaterial({ color: 0x0c1a06, emissive: new THREE.Color(0x8bd84a), emissiveIntensity: 0, roughness: 0.2 });
  const l = new THREE.Mesh(new THREE.SphereGeometry(0.1, 24, 16), lmat);
  l.position.set(0.98, -0.5, 0.14);
  m.add(vu, needle, l);
  m.position.set(-7.2 + i * 3.6, 0.1 + (i % 2) * 0.12, 0);
  m.rotation.y = 0.12;
  rack.add(m);
  return { knobs: kn, needle, lamp: lmat };
});
const rail = (y) => { const r = new THREE.Mesh(new THREE.BoxGeometry(19, 0.16, 0.18), darkMetal); r.position.set(0, y, -0.05); rack.add(r); };
rail(1.05); rail(-0.85);
rack.visible = false;
scene.add(rack);
function updateRack(t) {
  rackModules.forEach((m, i) => {
    const on = T.rack + 0.12 + i * SPB * 0.75;
    m.lamp.emissiveIntensity = t > on ? 2.2 + 1.5 * pulse(t, on, 0.35) : 0.05;
    m.knobs[0].rotation.z = -1.4 + 2.2 * outBack(seg(t, on - 0.1, on + 0.25)) + 0.15 * Math.sin(t * 3 + i);
    m.knobs[1].rotation.z = 1.2 - 1.6 * outCubic(seg(t, on, on + 0.4));
    m.needle.rotation.z = lerp(0.95, -0.55, clamp(loud(t - i * 0.03) * 1.1)) * (t > on ? 1 : 0.15) + (t > on ? 0 : 0.8);
  });
}

// ------------------------------------------------------------------ 3D: камера и свет по сценам
const V = (x, y, z) => new THREE.Vector3(x, y, z);
const orbit = (r, a, y, look) => [V(Math.sin(a) * r, y, Math.cos(a) * r), look];
function shot3d(t) {
  let pos, look, fov = 34;
  if (t < T.knob) {                                  // на экране телевизора, чёрно-белое
    const k = seg(t, T.pic, T.rise);
    [pos, look] = orbit(lerp(9.8, 9.2, k), lerp(-0.55, -0.3, k), lerp(0.2, 0.0, k), V(0, -1.6, 0));
    const [sx, sy] = shake(t, 1);
    const j = 0.15 * (pulse(t, T.stab1, 0.25) + pulse(t, T.stab2, 0.25));
    pos.x += sx * j; pos.y += sy * j;
  } else if (t < T.drop) {                           // на экране, цветное: медленный наезд
    const k = seg(t, T.flip, T.drop);
    [pos, look] = orbit(lerp(9.2, 7.4, inOut(k)), lerp(0.42, 0.62, k), lerp(0.1, -0.2, k), V(0, lerp(-1.6, -1.2, k), 0));
  } else if (t < T.ns) {                             // дроп: микрофон слева, логотип справа
    const k = outCubic(seg(t, T.drop, T.ns));
    pos = V(lerp(3.0, 3.6, k), lerp(0.2, 0.5, k), lerp(8.6, 9.6, k));
    look = V(lerp(2.0, 2.2, k), -1.0, 0);
    fov = 34;
  } else {                                           // проезд вдоль рэка
    const k = inOut(seg(t, T.rack, T.eq));
    pos = V(lerp(-5.6, 5.2, k), 0.35 + 0.15 * Math.sin(k * 3), 6.2);
    look = V(lerp(-4.4, 6.4, k), 0.1, 0);
    fov = 40;
  }
  camera.position.copy(pos);
  camera.fov = fov;
  camera.updateProjectionMatrix();
  camera.up.set(0, 1, 0);
  camera.lookAt(look);
  const color = t >= T.flip;
  key.color.setHex(color ? 0xffd9b0 : 0xffffff);
  key.intensity = color ? 120 : 95;
  rim.intensity = color ? 60 : 40;
  rim2.intensity = color ? 14 : 30;
  scene.environmentIntensity = color ? 0.5 : 0.3;
  mic.userData.led.emissiveIntensity = color ? 1.8 : 0.6;
  const isRack = within(t, T.rack, T.eq);
  rack.visible = isRack;
  mic.visible = !isRack;
  floor.visible = !isRack;
  wave.visible = color && !isRack;
  bokeh.visible = color;
  if (wave.visible) updateWave(t);
  if (isRack) updateRack(t);
}

// ------------------------------------------------------------------ телевизор: дерево, ручки, рябь
function woodGrain(w, h, seed) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  const g = c.getContext('2d'), r = rng(seed);
  const gr = g.createLinearGradient(0, 0, 0, h);
  gr.addColorStop(0, '#6a4428'); gr.addColorStop(0.5, '#5a3820'); gr.addColorStop(1, '#4a2d19');
  g.fillStyle = gr; g.fillRect(0, 0, w, h);
  for (let i = 0; i < 260; i++) {
    const y0 = r() * h, amp = 4 + r() * 18, f = 0.002 + r() * 0.006, ph = r() * 10;
    g.strokeStyle = r() < 0.5 ? `rgba(30,16,8,${0.08 + r() * 0.18})` : `rgba(140,96,60,${0.05 + r() * 0.1})`;
    g.lineWidth = 0.6 + r() * 2.4;
    g.beginPath();
    for (let x = 0; x <= w; x += 8) g.lineTo(x, y0 + Math.sin(x * f + ph) * amp + Math.sin(x * f * 3.1 + ph) * amp * 0.3);
    g.stroke();
  }
  return c.toDataURL('image/jpeg', 0.9);
}
$('#cabinet').style.backgroundImage = `linear-gradient(180deg, rgba(255,220,180,0.08), rgba(0,0,0,0.25)), url(${woodGrain(1300, 820, 3)})`;
for (const l of document.querySelectorAll('.leg')) l.style.backgroundImage = `linear-gradient(90deg, rgba(0,0,0,0.35), rgba(255,220,180,0.08) 45%, rgba(0,0,0,0.4)), url(${woodGrain(60, 130, 9)})`;
$('#console').style.backgroundImage = `linear-gradient(180deg, rgba(255,220,180,0.1) 0 6px, rgba(0,0,0,0) 6px, rgba(0,0,0,0.55) 40px), url(${woodGrain(2400, 160, 5)})`;
{
  const ring = $('#dialRing');
  for (let i = 0; i < 12; i++) {
    const a = (-150 + i * 27) * Math.PI / 180;
    const s = document.createElement('span');
    s.textContent = String(i + 1);
    s.style.transform = `translate(${Math.cos(a - Math.PI / 2) * 96 - 6}px, ${Math.sin(a - Math.PI / 2) * 96 - 8}px)`;
    ring.append(s);
  }
}
const SCR = { x: 310 + 60 + 34 + 420, y: 90 + 60 + 34 + 315 };   // центр кинескопа в координатах комнаты
const DIAL = { x: 310 + 1000 + 120, y: 90 + 60 + 91 + 80 };
const VU = { x: 310 + 60 + 34 + 300, y: 90 + 60 + 34 + 190 };

const staticCtx = $('#scrStatic').getContext('2d');
const staticFrames = Array.from({ length: 6 }, (_, f) => {
  const img = staticCtx.createImageData(420, 316), r = rng(500 + f);
  for (let i = 0; i < img.data.length; i += 4) { const v = r() * 255; img.data[i] = img.data[i + 1] = img.data[i + 2] = v; img.data[i + 3] = 255; }
  return img;
});
const scrGlCtx = $('#scrGl').getContext('2d');
const scopeCtx = $('#scope').getContext('2d');
const wrand = rng(99);
const wnoise = Array.from({ length: 4000 }, () => wrand() * 2 - 1);

// экранные метки шума
const stags = TXT.tags.map((text, i) => {
  const n = document.createElement('div');
  n.className = 'stag';
  n.textContent = text;
  $('#scrTags').append(n);
  return { n, t0: T.hear + 0.3 + i * SPB, x: [56, 470, 90][i], y: [96, 250, 420][i] };
});

// карточка установки
$('#icTitle').textContent = TXT.install[0];
const icSteps = TXT.install[1].map((s) => {
  const li = document.createElement('li');
  li.innerHTML = `<i></i><span>${s}</span>`;
  $('#icSteps').append(li);
  return li;
});
$('#upTitle').textContent = TXT.update[0];
$('#upNote').textContent = TXT.update[3];

function updateTv(t) {
  // какое изображение на экране
  const bw = within(t, T.pic, T.rise);
  const installer = within(t, T.rise, T.flip);
  const color = within(t, T.flip, T.drop + 0.2);
  const uiOn = within(t, T.ns, T.rack) || within(t, T.eq, T.apps) || within(t, T.langs, T.upd);
  const channel = within(t, T.apps, T.langs);
  const update = t >= T.upd;
  $('#scrContent').style.display = bw || color ? '' : 'none';
  $('#scrContent').style.filter = bw ? 'grayscale(1) contrast(1.35) brightness(0.92) sepia(0.12)' : 'saturate(1.05)';
  // вертикальная «прокрутка» кадра на помехах
  const roll = (pulse(t, T.stab1, 0.18) + pulse(t, T.stab2, 0.18) + pulse(t, T.stab3, 0.18)) * 120;
  $('#scrContent').style.transform = `translateY(${-roll}px)`;
  $('#scrUi').style.display = uiOn ? '' : 'none';
  $('#cardInstall').style.opacity = installer ? '1' : '0';
  $('#cardChannel').style.opacity = channel ? '1' : '0';
  $('#cardUpdate').style.opacity = update ? '1' : '0';

  // картинка с 3D-камеры (центр 4:3) и осциллограф
  if (bw || color) scrGlCtx.drawImage(glCanvas, 240, 0, 1440, 1080, 0, 0, 1440, 1080);
  scopeCtx.clearRect(0, 0, 840, 630);
  if (bw) {
    const f = Math.floor(t * 30), amp = 26 + 40 * loud(t);
    scopeCtx.strokeStyle = 'rgba(255,255,255,0.85)';
    scopeCtx.lineWidth = 2.2;
    scopeCtx.beginPath();
    for (let x = 0; x <= 840; x += 4) {
      const n = wnoise[(x * 3 + f * 17) % wnoise.length] * 0.7 + Math.sin(x * 0.09 + t * 40) * 0.3;
      scopeCtx.lineTo(x, 545 + n * amp * Math.sin(Math.PI * x / 840));
    }
    scopeCtx.stroke();
  }
  stags.forEach((s) => {
    const vis = bw && t >= s.t0;
    s.n.style.display = vis ? '' : 'none';
    if (!vis) return;
    const k = outBack(seg(t, s.t0, s.t0 + 0.2), 2);
    const j = (pulse(t, T.stab1, 0.2) + pulse(t, T.stab2, 0.2)) * 18;
    s.n.style.transform = `translate(${s.x + Math.sin(t * 77 + s.x) * j}px, ${s.y}px) scale(${k})`;
    s.n.style.opacity = String(clamp(k) * (0.85 + 0.15 * Math.sin(t * 31 + s.y)));
  });

  // установка: прогресс и шаги
  if (installer) {
    const p = smooth(seg(t, T.rise + 0.15, T.knob - 0.05));
    $('#icBar').style.width = `${p * 100}%`;
    $('#icPct').textContent = `${Math.round(p * 100)}%`;
    icSteps.forEach((li, i) => {
      const done = p >= (i + 1) / icSteps.length - 0.001;
      li.classList.toggle('done', done);
      li.querySelector('i').textContent = done ? '✓' : '';
    });
  }
  // каналы: программы, по одной на долю
  if (channel) {
    const n = clamp(Math.floor((t - T.apps) / SPB), 0, APPS.length - 1);
    $('#chNo').textContent = `${TXT.channel} ${n + 1}`;
    $('#chName').textContent = APPS[n];
  }
  // обновление
  if (update) {
    const p = smooth(seg(t, T.upd + 0.2, T.upd + 1.1));
    $('#upBar').style.width = `${p * 100}%`;
    $('#upSub').textContent = p < 1 ? TXT.update[1] : `✓ ${TXT.update[2]}`;
  }

  // рябь: включение, помехи, переключения каналов, влёт в кинескоп
  const switches = [T.rise, T.flip, T.ns, T.eq, T.diag, T.upd, ...APPS.map((_, i) => T.apps + i * SPB)];
  let st = 0;
  if (t < T.pic) st = t >= T.open + 0.12 ? 1 : 0;
  else if (bw) st = 0.22 + 0.08 * Math.sin(t * 23) + 0.75 * Math.max(pulse(t, T.stab1, 0.2), pulse(t, T.stab2, 0.2), pulse(t, T.stab3, 0.2));
  for (const s of switches) if (t >= s && t < s + 0.12) st = Math.max(st, 0.9 - (t - s) * 5);
  if (t < T.drop) st = Math.max(st, 0.55 * inCubic(seg(t, T.thru, T.drop)));
  $('#scrStatic').style.opacity = String(clamp(st));
  if (st > 0.01) staticCtx.putImageData(staticFrames[Math.floor(t * 60) % staticFrames.length], 0, 0);
  // катящаяся полоса и маска пикселей
  $('#rollbar').style.opacity = bw ? '1' : '0';
  $('#rollbar').style.transform = `translateY(${((t * 0.55) % 1.3 - 0.2) * 630}px)`;
  $('#crtMask').style.opacity = String(0.06 + 0.6 * inCubic(seg(t, T.build + 1.2, T.drop)) * (t < T.drop ? 1 : 0));
  // включение кинескопа: горизонтальная линия раскрывается в кадр
  const pw = $('#pwr');
  if (t < T.pic) {
    const k1 = seg(t, T.open, T.open + 0.07), k2 = seg(t, T.open + 0.07, T.open + 0.22);
    const hx = 50 - 50 * k1, hy = 50 - 50 * k2;
    pw.style.clipPath = `inset(calc(${hy}% - 1px) ${hx}% calc(${hy}% - 1px) ${hx}%)`;
    pw.style.opacity = t >= T.open ? String(1 - seg(t, T.open + 0.18, T.open + 0.45)) : '0';
  } else pw.style.opacity = '0';
  // экранное меню
  const osd = [[T.pic, 'CH 03'], [T.stab1, 'CH 04'], [T.stab2, 'CH 05'], [T.rise, 'AV'], [T.flip, 'CH 01 · STUDIO'], [T.ns, 'CH 02'],
    ...APPS.map((_, i) => [T.apps + i * SPB, `CH 0${i + 1}`])];
  let o = '';
  for (const [t0, text] of osd) if (t >= t0 && t < t0 + 1.0) o = text;
  $('#osd').textContent = o;
  // ручка каналов и лампа
  const turn = outBack(seg(t, T.flip - 0.02, T.flip + 0.16), 2.2);
  $('#dial').style.transform = `rotate(${lerp(-54, 0, turn)}deg)`;
  $('#tvLamp').classList.toggle('on', t >= T.flip);
  // отсвет экрана на стене
  $('#tvGlow').style.background = `radial-gradient(ellipse at center, ${t < T.flip ? 'rgba(170,200,230,0.18)' : 'rgba(255,170,90,0.2)'}, rgba(0,0,0,0) 65%)`;
}

// ------------------------------------------------------------------ камера комнаты (2D-сцена с телевизором)
const world = $('#world');
function worldCam(t) {
  let fx = SCR.x, fy = SCR.y, s = 1, sh = 0;
  if (t < T.wide) { fx = 960; fy = 560; s = lerp(0.84, 0.88, seg(t, 0, T.wide)); }
  else if (t < T.stab1) { const k = seg(t, T.wide, T.stab1); fx = lerp(930, 880, k); fy = lerp(545, 520, k); s = lerp(0.95, 1.05, k); }
  else if (t < T.rise) { fx = 860; fy = 515; s = t < T.stab2 ? 1.14 : 1.24; sh = 14 * (pulse(t, T.stab1, 0.3) + pulse(t, T.stab2, 0.3) + pulse(t, T.stab3, 0.3)); }
  else if (t < T.knob) { s = lerp(1.36, 1.48, seg(t, T.rise, T.knob)); }
  else if (t < T.back) { fx = DIAL.x; fy = DIAL.y + 40; s = lerp(2.9, 3.1, seg(t, T.knob, T.back)); sh = 10 * pulse(t, T.flip, 0.25); }
  else if (t < T.build) { s = lerp(1.3, 1.36, seg(t, T.back, T.build)); }
  else if (t < T.thru) { s = lerp(1.36, 2.35, inOut(seg(t, T.build, T.thru))); sh = 4 * pulse(t, T.hit, 0.3); }
  else if (t < T.drop) { s = lerp(2.35, 5.2, inCubic(seg(t, T.thru, T.drop))); }
  else if (within(t, T.ns, T.rack)) {
    const z = inOut(seg(t, T.brk, T.rack));
    fx = lerp(SCR.x, VU.x, z); fy = lerp(SCR.y, VU.y, z); s = lerp(1.55, 3.1, z) + 0.03 * seg(t, T.ns, T.brk);
  }
  else if (within(t, T.eq, T.diag)) { fy = SCR.y - 20; s = lerp(1.56, 1.64, seg(t, T.eq, T.diag)); }
  else if (within(t, T.diag, T.apps)) { s = 1.55 - 0.2 * inOut(seg(t, T.stop2, T.apps)); }
  else if (within(t, T.apps, T.langs)) {
    s = 1.42 + 0.05 * APPS.reduce((a, _, i) => a + pulse(t, T.apps + i * SPB, 0.25), 0);
  }
  else if (within(t, T.langs, T.upd)) { s = lerp(1.0, 1.05, seg(t, T.langs, T.upd)); }
  else if (within(t, T.upd, T.fin)) { s = lerp(1.46, 1.54, seg(t, T.upd, T.fin)); }
  const [sx, sy] = shake(t, 1);
  world.style.transformOrigin = `${fx}px ${fy}px`;
  world.style.transform = `translate(${960 - fx + sx * sh}px, ${540 - fy + sy * sh}px) scale(${s})`;
  world.style.filter = within(t, T.langs, T.upd) ? 'brightness(0.38) blur(3px)' : 'none';
}

// ------------------------------------------------------------------ титры
const caps = TXT.caps.map((c) => {
  const node = document.createElement('div');
  node.className = 'cap';
  const words = (s) => s.split(' ').map((w) => `<span class="w"><span>${w}</span></span>`).join(' ');
  const l1 = c.words ? c.words.map((w) => `<span class="w"><span>${w}</span></span>`).join(' ') : words(c.l1);
  node.innerHTML = `<div class="cp"><div class="l1">${l1}</div>${c.l2 ? `<div class="l2">${words(c.l2)}</div>` : ''}</div>`;
  $('#captions').append(node);
  return {
    node, plate: node.querySelector('.cp'), t0: T[c.at], t1: CAP_END[c.at], timed: c.words ? [T.stab1, T.stab2, T.stab3] : null,
    w1: [...node.querySelectorAll('.l1 .w > span')], w2: [...node.querySelectorAll('.l2 .w > span')],
  };
});
function updateCaptions(t) {
  for (const c of caps) {
    const vis = t >= c.t0 - 0.02 && t < c.t1;
    c.node.style.display = vis ? '' : 'none';
    if (!vis) continue;
    const out = seg(t, c.t1 - 0.14, c.t1);
    c.node.style.opacity = String(1 - out);
    const wipe = outCubic(seg(t, c.t0, c.t0 + 0.28));
    c.plate.style.clipPath = `inset(0 ${(1 - wipe) * 100}% 0 0)`;
    c.w1.forEach((s, i) => {
      const st = c.timed ? c.timed[i] : c.t0 + 0.08 + i * 0.05;
      const k = outCubic(seg(t, st, st + 0.22));
      s.style.transform = `translateY(${(1 - k) * 105}%)`;
    });
    c.w2.forEach((s, i) => {
      const st = c.t0 + 0.3 + i * 0.035;
      const k = outCubic(seg(t, st, st + 0.3));
      s.style.transform = `translateY(${(1 - k) * 105}%)`;
    });
  }
}

// ------------------------------------------------------------------ пульт во фрейме
const ui = $('#ui');
const UI = (code) => { try { return ui.contentWindow.eval(code); } catch (e) { return null; } };
let uiPage = '';
function uiGo(page) {
  if (uiPage === page || !UI('typeof go === "function"')) return;
  uiPage = page;
  UI(`go('${page}'); document.querySelector('.main') && document.querySelector('.main').scrollTo(0, 0); window.scrollTo(0, 0);`);
  if (page === 'diag') UI('runDiag()');
}
function updateUi(t) {
  if (t < T.rack) uiGo('overview');
  else if (t < T.diag) uiGo('eq');
  else if (t < T.langs) uiGo('diag');
  else uiGo('overview');
  if (within(t, T.eq, T.diag)) {
    const base = [3, -3, -1.5, 2.5, 4];
    const k = seg(t, T.eq, T.diag);
    const vals = base.map((b, i) => r1(Math.round((b + Math.sin(t * 2.4 + i * 1.3) * 3.2 + pulse(t, beat(Math.floor((t - B0) / SPB)), 0.35) * (i === 3 ? 2.5 : 0) + (i === 0 ? 4 * Math.sin(k * Math.PI) : 0)) * 2) / 2));
    UI(`(function(v){ v.forEach(function(g, i){ S.params.eq[i].gain = g; }); eqSel = 3; refreshParams(); })(${JSON.stringify(vals)})`);
  }
  if (within(t, T.diag, T.apps) && ui.contentDocument) {
    ui.contentDocument.querySelectorAll('#diagList .dg').forEach((r, i) => {
      const rk = outCubic(seg(t, T.diag + 0.2 + i * 0.07, T.diag + 0.4 + i * 0.07));
      r.style.opacity = String(rk);
      r.style.transform = `translateX(${(1 - rk) * 50}px)`;
    });
  }
  if (ui.contentWindow && ui.contentWindow.__step) ui.contentWindow.__step(t * 1000, false);
}

// ------------------------------------------------------------------ логотип, табло, финал
const logo = $('#logo'), logoPlate = $('#logo .plate'), logoTag = $('#logoTag');
logoTag.innerHTML = TXT.logoTag;
function updateLogo(t) {
  const vis = within(t, T.drop, T.ns);
  logo.style.opacity = vis ? '1' : '0';
  if (!vis) return;
  const k = outBack(seg(t, T.drop + 0.05, T.drop + 0.45), 1.4);
  const out = inCubic(seg(t, T.ns - 0.25, T.ns));
  logoPlate.style.transform = `translateY(${(1 - clamp(k)) * 40}px) scale(${lerp(1.25, 1, k) - out * 0.06})`;
  logoPlate.style.opacity = String(clamp(k * 2.5) * (1 - out));
  const tk = outCubic(seg(t, T.drop + 0.5, T.drop + 0.9));
  logoTag.style.opacity = String(tk * (1 - out));
  logoTag.style.transform = `translateY(${(1 - tk) * 24}px)`;
}
const board = $('#board'), boardA = $('#boardA');
function updateBoard(t) {
  const vis = within(t, T.langs, T.upd);
  board.style.opacity = vis ? '1' : '0';
  if (!vis) return;
  const q = SPB / 2;
  let word, tc;
  if (t < T.stop3) {
    const n = Math.floor((t - T.langs) / q);
    word = WORDS[n % WORDS.length];
    tc = T.langs + n * q;
  } else {
    word = TXT.langsFinal.toUpperCase();
    tc = T.stop3;
  }
  if (boardA.textContent !== word) boardA.textContent = word;
  const k = outCubic(seg(t, tc, tc + 0.11));
  const out = inCubic(seg(t, T.upd - 0.12, T.upd));
  boardA.style.transform = `translateX(-50%) rotateX(${(1 - k) * 88}deg) scale(${(t >= T.stop3 ? lerp(1.2, 1, outBack(seg(t, T.stop3, T.stop3 + 0.3))) : 1) * (1 - out * 0.3)})`;
  boardA.style.opacity = String(1 - out);
}
const lockup = $('#lockup'), lkPlate = $('#lockup .plate'), lkLine = $('#lkLine'), lkUrl = $('#lockup .lk-url'), lkSmall = $('#lkSmall');
lkLine.textContent = TXT.lkLine;
lkSmall.textContent = TXT.lkSmall;
function updateLockup(t) {
  const vis = t >= T.fin;
  lockup.style.opacity = vis ? '1' : '0';
  if (!vis) return;
  const k = outExpo(seg(t, T.fin, T.fin + 0.5));
  lkPlate.style.transform = `scale(${lerp(1.5, 1.15, k)})`;
  lkPlate.style.opacity = String(clamp(k * 1.5));
  [lkLine, lkUrl, lkSmall].forEach((n, i) => {
    const kk = outCubic(seg(t, T.fin + 0.15 + i * 0.12, T.fin + 0.45 + i * 0.12));
    n.style.opacity = String(kk);
    n.style.transform = `translateY(${(1 - kk) * 20}px)`;
  });
}
// кинескоп гаснет: кадр схлопывается в линию, потом в точку
function updatePowerOff(t) {
  const k1 = seg(t, T.off, T.off + 0.12), k2 = seg(t, T.off + 0.12, T.off + 0.22);
  const stage = $('#stage');
  stage.style.transform = t >= T.off ? `scale(${lerp(1, 0.003, inCubic(k2))}, ${lerp(1, 0.004, inCubic(k1))})` : 'none';
  stage.style.filter = t >= T.off ? `brightness(${1 + 3 * k1})` : 'none';
  const line = $('#offLine');
  line.style.opacity = k1 >= 1 && k2 < 1 ? '1' : '0';
  line.style.transform = `scaleX(${1 - inCubic(k2)})`;
  $('#black').style.opacity = t < T.open || t >= T.off + 0.24 ? '1' : '0';
}

// зерно плёнки
const grainCtx = $('#grain').getContext('2d');
const grainFrames = Array.from({ length: 8 }, (_, f) => {
  const img = grainCtx.createImageData(960, 540), r = rng(1000 + f);
  for (let i = 0; i < img.data.length; i += 4) { const v = r() * 255; img.data[i] = img.data[i + 1] = img.data[i + 2] = v; img.data[i + 3] = 255; }
  return img;
});

// ------------------------------------------------------------------ кадр
window.__frame = (t) => {
  const tvShot = within(t, T.pic, T.rise) || within(t, T.flip, T.drop + 0.2);
  const full3d = within(t, T.drop, T.ns) || within(t, T.rack, T.eq);
  if (tvShot || full3d) {
    shot3d(t);
    composer.render();
  }
  glCanvas.style.opacity = full3d ? '1' : '0';
  world.style.opacity = full3d || t >= T.fin ? '0' : '1';
  worldCam(t);
  updateTv(t);
  updateUi(t);
  updateCaptions(t);
  updateLogo(t);
  updateBoard(t);
  updateLockup(t);
  updatePowerOff(t);
  // короткая вспышка только на дропе и финальном ударе
  $('#flash').style.opacity = String(clamp(0.55 * pulse(t, T.drop, 0.2) + 0.35 * pulse(t, T.fin, 0.25)));
  grainCtx.putImageData(grainFrames[Math.floor(t * 60) % grainFrames.length], 0, 0);
  if (DEBUG) {
    const d = $('#dbg');
    d.style.display = 'block';
    d.textContent = `${t.toFixed(2)} s · beat ${((t - B0) / SPB).toFixed(1)}`;
  }
};

// ------------------------------------------------------------------ готовность
// Таймеры страницы подменены виртуальными часами, поэтому загрузку пульта во фрейме
// дожидается рендерер (опросом __uiReady), а не сама страница.
window.__meta = { duration: DURATION, fps: 60, musicStart: 0 };
window.__uiReady = () => !!UI('typeof S !== "undefined" && !!S.state && !!S.params && typeof go === "function"');
await document.fonts.ready;
window.__ready = true;
