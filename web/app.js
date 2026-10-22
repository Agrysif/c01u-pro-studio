/* C01U Pro Studio — control panel (no frameworks, no build step). */
'use strict';

// ------------------------------------------------------------------ helpers
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
function el(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  if (attrs) for (const k in attrs) {
    const v = attrs[k];
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'text') e.textContent = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const c of kids.flat()) if (c != null) e.append(c.nodeType ? c : document.createTextNode(c));
  return e;
}
function icon(name, cls) {
  const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  if (cls) s.setAttribute('class', cls);
  const u = document.createElementNS('http://www.w3.org/2000/svg', 'use');
  u.setAttribute('href', '#i-' + name);
  s.append(u);
  return s;
}
const debounce = (fn, ms) => { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const deepCopy = (o) => JSON.parse(JSON.stringify(o));

// ------------------------------------------------------------------ i18n
const LANGS = [
  ['ru', 'Русский'], ['en', 'English'], ['uk', 'Українська'], ['be', 'Беларуская'], ['kk', 'Қазақ тілі'],
  ['uz', 'Oʻzbekcha'], ['ky', 'Кыргызча'], ['tg', 'Тоҷикӣ'], ['hy', 'Հայերեն'], ['az', 'Azərbaycan dili'],
  ['ka', 'ქართული'], ['ro', 'Română'], ['tk', 'Türkmençe'], ['de', 'Deutsch'], ['fr', 'Français'],
  ['es', 'Español'], ['it', 'Italiano'], ['pt', 'Português'], ['pl', 'Polski'], ['tr', 'Türkçe'],
  ['zh', '中文 (简体)'], ['ja', '日本語'], ['ko', '한국어'], ['ar', 'العربية'], ['hi', 'हिन्दी'],
];
const RTL = new Set(['ar']);
let STR = {}, STR_EN = {}, LANG = 'en';

async function loadLang(code) {
  const get = async (c) => { try { const r = await fetch('i18n/' + c + '.json', { cache: 'no-cache' }); return r.ok ? await r.json() : {}; } catch { return {}; } };
  if (!Object.keys(STR_EN).length) STR_EN = await get('en');
  LANG = LANGS.some((l) => l[0] === code) ? code : 'en';
  STR = LANG === 'en' ? STR_EN : await get(LANG);
  document.documentElement.lang = LANG;
  document.documentElement.dir = RTL.has(LANG) ? 'rtl' : 'ltr';
  $('#langName').textContent = (LANGS.find((l) => l[0] === LANG) || LANGS[1])[1];
}
function t(key, vars) {
  let s = STR[key];
  if (s == null) s = STR_EN[key];
  if (s == null) return key;
  if (typeof s === 'string' && vars) s = s.replace(/\{(\w+)\}/g, (m, k) => (vars[k] != null ? vars[k] : m));
  return s;
}
function applyI18n(root = document) {
  for (const e of $$('[data-i18n]', root)) e.textContent = t(e.dataset.i18n);
}
const nf = (v, d = 1) => (v == null || isNaN(v) ? '—' : Number(v).toLocaleString(LANG, { minimumFractionDigits: d, maximumFractionDigits: d }));

// ------------------------------------------------------------------ api
async function api(path, body) {
  const opt = body === undefined ? {} : { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-C01U': '1' }, body: JSON.stringify(body) };
  const r = await fetch('/api/' + path, opt);
  let j = null;
  try { j = await r.json(); } catch { j = null; }
  if (!r.ok) { const e = new Error((j && j.error) || r.statusText); e.status = r.status; e.data = j; throw e; }
  return j;
}

// ------------------------------------------------------------------ state
const S = {
  state: null, params: null, settings: null, status: null, devices: null,
  meters: null, page: 'overview', dragging: false, dirtyParams: {},
};

// keep in sync with DefaultParams() in src/shared/params.h
const EQ_DEFAULT = [
  { on: true, type: 1, freq: 100, gain: 0, q: 0.7 }, { on: true, type: 0, freq: 200, gain: 0, q: 1 },
  { on: true, type: 0, freq: 400, gain: 0, q: 1 }, { on: true, type: 0, freq: 800, gain: 0, q: 1 },
  { on: true, type: 0, freq: 1600, gain: 0, q: 1 }, { on: true, type: 0, freq: 3200, gain: 0, q: 1 },
  { on: true, type: 0, freq: 5000, gain: 0, q: 1 }, { on: true, type: 0, freq: 8000, gain: 0, q: 1 },
  { on: true, type: 2, freq: 12000, gain: 0, q: 0.7 }, { on: false, type: 0, freq: 2500, gain: 0, q: 1 },
];
const DEFAULTS = {
  bypass: false, mute: false, inputGainDb: 0, outputGainDb: 0,
  hpfOn: true, hpfFreq: 80, hpfOrder: 2, humOn: false, humFreq: 50, humHarmonics: 4,
  nsOn: true, nsReductionDb: 18, nsSensitivity: 0.5, popOn: true, popAmount: 0.5,
  gateOn: false, gateAuto: true, gateThresholdDb: -50, gateRangeDb: 18, gateAttackMs: 2, gateHoldMs: 150, gateReleaseMs: 180,
  eqOn: true, warmth: 0, clarity: 0, air: 0,
  deessOn: true, deessFreq: 6500, deessAmount: 0.35,
  agcOn: false, agcTargetDb: -18, agcMaxGainDb: 12, agcSpeed: 0.4,
  compOn: true, compThresholdDb: -22, compRatio: 3, compAttackMs: 5, compReleaseMs: 120, compKneeDb: 6, compMakeupDb: 0, compAutoMakeup: true,
  fxType: 0, fxMix: 0.5, fxParam: 0.5, limOn: true, limCeilingDb: -1, eq: EQ_DEFAULT,
};
const eqWith = (gains) => EQ_DEFAULT.map((b, i) => Object.assign({}, b, gains[i] != null ? (typeof gains[i] === 'number' ? { gain: gains[i] } : gains[i]) : {}));

const PRESETS = [
  { id: 'c01u', p: { hpfFreq: 85, nsReductionDb: 16, deessFreq: 6800, deessAmount: 0.4, compThresholdDb: -22, compRatio: 3, compAttackMs: 6, compReleaseMs: 140, clarity: 0.1,
    eq: eqWith({ 0: 1.5, 2: { gain: -2, q: 1.2 }, 6: { gain: -1.5, q: 1.4 }, 8: 1.5 }) } },
  { id: 'natural', p: { nsReductionDb: 12, compRatio: 2, compThresholdDb: -20, deessAmount: 0.3 } },
  { id: 'podcast', p: { hpfFreq: 90, nsReductionDb: 18, gateOn: true, warmth: 0.25, clarity: 0.25, deessAmount: 0.45, agcOn: true, agcTargetDb: -18, compThresholdDb: -24, compRatio: 3.5 } },
  { id: 'stream', p: { hpfFreq: 100, nsReductionDb: 24, nsSensitivity: 0.6, gateOn: true, gateRangeDb: 30, clarity: 0.35, compThresholdDb: -26, compRatio: 4, compAttackMs: 3, deessAmount: 0.4, agcOn: true, agcTargetDb: -16 } },
  { id: 'chat', p: { hpfFreq: 120, hpfOrder: 4, nsReductionDb: 28, nsSensitivity: 0.7, gateOn: true, clarity: 0.4, air: -0.2, compThresholdDb: -24, compRatio: 4, agcOn: true, agcTargetDb: -18, deessAmount: 0.3 } },
  { id: 'vocal', p: { hpfFreq: 70, nsReductionDb: 10, nsSensitivity: 0.3, popAmount: 0.4, warmth: 0.1, air: 0.3, deessAmount: 0.3, compThresholdDb: -18, compRatio: 2.5, compAttackMs: 10, compReleaseMs: 200 } },
  { id: 'asmr', p: { hpfFreq: 40, nsReductionDb: 20, nsSensitivity: 0.4, popAmount: 0.7, inputGainDb: 12, warmth: 0.2, air: 0.4, deessAmount: 0.1, compThresholdDb: -30, compRatio: 2, limCeilingDb: -2 } },
  { id: 'radio', p: { hpfFreq: 70, nsReductionDb: 16, warmth: 0.5, clarity: 0.4, air: 0.2, deessAmount: 0.5, compThresholdDb: -26, compRatio: 5, compAttackMs: 2, compReleaseMs: 80, compKneeDb: 3, agcOn: true, agcTargetDb: -16 } },
  { id: 'raw', p: { hpfOn: false, nsOn: false, popOn: false, gateOn: false, eqOn: false, deessOn: false, agcOn: false, compOn: false, limOn: false } },
];
function presetParams(p) {
  const out = deepCopy(DEFAULTS);
  Object.assign(out, deepCopy(p));
  out.bypass = S.params ? S.params.bypass : false;
  out.mute = S.params ? S.params.mute : false;
  return out;
}

const EQ_PRESETS = {
  flat: {},
  clarity: { 1: -1, 2: -2, 5: 2.5, 6: 1, 8: 2 },
  warm: { 0: 3, 1: 1.5, 5: -1, 7: -1.5 },
  deboxy: { 2: { gain: -4, q: 1.2 }, 3: -2 },
  bright: { 6: 2, 7: 2.5, 8: 3 },
  deep: { 0: 4, 1: 2 },
  presence: { 4: 1.5, 5: 3 },
  dehiss: { 7: -3, 8: -5 },
};

// ------------------------------------------------------------------ params
const pushParams = debounce(async () => {
  const body = S.dirtyParams;
  S.dirtyParams = {};
  if (!Object.keys(body).length) return;
  try {
    const r = await api('params', body);
    if (!S.dragging) { S.params = r.params; S.settings.preset = r.preset; refreshParams(); }
  } catch (e) { toast(t('err.save'), true); }
}, 70);

function setParam(key, value, opts = {}) {
  S.params[key] = value;
  S.dirtyParams[key] = value;
  if (key === 'eq') S.dirtyParams.eq = S.params.eq;
  if (!opts.keepPreset && key !== 'bypass' && key !== 'mute') { S.dirtyParams.preset = 'custom'; S.settings.preset = 'custom'; }
  pushParams();
  if (!opts.silent) refreshParams(opts.except);
}
function setParams(obj, preset) {
  Object.assign(S.params, obj);
  Object.assign(S.dirtyParams, obj);
  if (preset) { S.dirtyParams.preset = preset; S.settings.preset = preset; }
  pushParams();
  refreshParams();
}
async function saveSettings(obj) {
  Object.assign(S.settings, obj);
  try { const st = await api('settings', obj); applyState(st, true); } catch { toast(t('err.save'), true); }
}

// ------------------------------------------------------------------ formatting
const fmtDb = (v) => (v > 0 ? '+' : '') + nf(v, 1) + ' dB';
const fmtHz = (v) => (v >= 1000 ? nf(v / 1000, v >= 10000 ? 1 : 2) + ' kHz' : nf(v, 0) + ' Hz');
const fmtMs = (v) => (v < 10 ? nf(v, 1) : nf(v, 0)) + ' ms';
const fmtPct = (v) => nf(v * 100, 0) + '%';
const NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
function noteName(f) {
  if (!f) return '';
  const n = Math.round(12 * Math.log2(f / 440)) + 69;
  return NOTES[(n % 12 + 12) % 12] + (Math.floor(n / 12) - 1);
}

// ------------------------------------------------------------------ rotary knob
// spec: {k, l, min, max, step, fmt, log, def, get, set, disabled, small}
// Drag up/down (Shift = fine), mouse wheel, arrow keys; double-click resets.
function makeSlider(host, spec) {
  const out = el('output', { class: 'lcd' });
  const knob = el('div', { class: 'knob' + (spec.small ? ' small' : ''), role: 'slider', tabindex: 0 },
    el('div', { class: 'knob-scale' }), el('div', { class: 'knob-body' }), el('div', { class: 'knob-rot' }), el('div', { class: 'knob-cap' }));
  const lab = el('label');
  const box = el('div', { class: 'knob-wrap' + (spec.small ? ' small' : '') }, lab, knob, out);
  host.append(box);
  const toPos = (v) => clamp(spec.log ? Math.log(v / spec.min) / Math.log(spec.max / spec.min) : (v - spec.min) / (spec.max - spec.min), 0, 1);
  const fromPos = (p) => {
    p = clamp(p, 0, 1);
    let v = spec.log ? spec.min * Math.pow(spec.max / spec.min, p) : spec.min + (spec.max - spec.min) * p;
    const st = spec.step || 0.01;
    v = Math.round(v / st) * st;
    return clamp(+v.toFixed(4), spec.min, spec.max);
  };
  const get = spec.get || (() => S.params[spec.k]);
  const set = spec.set || ((v) => setParam(spec.k, v, { except: knob }));
  const fmt = spec.fmt || ((v) => nf(v, 1));
  let cur = get(), pos = toPos(cur), drag = null;
  const paint = (v) => {
    cur = v;
    pos = toPos(v);
    knob.querySelector('.knob-rot').style.setProperty('--a', (-135 + 270 * pos) + 'deg');
    out.textContent = fmt(v);
    knob.setAttribute('aria-valuetext', fmt(v));
  };
  const disabled = () => (spec.disabled ? spec.disabled() : false);
  const commit = (p) => {
    const v = fromPos(p);
    if (v !== cur) { paint(v); set(v); }
    pos = clamp(p, 0, 1);
  };
  knob.addEventListener('pointerdown', (e) => {
    if (disabled()) return;
    knob.setPointerCapture(e.pointerId);
    drag = { y: e.clientY, x: e.clientX, p: pos };
    S.dragging = true;
    knob.focus();
  });
  knob.addEventListener('pointermove', (e) => {
    if (!drag) return;
    const scale = e.shiftKey ? 900 : 220;
    commit(drag.p + (drag.y - e.clientY) / scale + (e.clientX - drag.x) / (scale * 2));
  });
  const end = () => { if (drag) { drag = null; setTimeout(() => (S.dragging = false), 150); } };
  knob.addEventListener('pointerup', end);
  knob.addEventListener('pointercancel', end);
  knob.addEventListener('wheel', (e) => {
    if (disabled()) return;
    e.preventDefault();
    commit(pos + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.005 : 0.02));
  }, { passive: false });
  knob.addEventListener('keydown', (e) => {
    if (disabled()) return;
    const d = { ArrowUp: 0.02, ArrowRight: 0.02, ArrowDown: -0.02, ArrowLeft: -0.02, PageUp: 0.1, PageDown: -0.1 }[e.key];
    if (d != null) { e.preventDefault(); commit(pos + d); }
    else if (e.key === 'Home') { e.preventDefault(); commit(0); }
    else if (e.key === 'End') { e.preventDefault(); commit(1); }
  });
  knob.addEventListener('dblclick', () => { if (spec.def != null && !disabled()) { paint(spec.def); set(spec.def); } });
  const update = () => {
    lab.textContent = t(spec.l);
    knob.setAttribute('aria-label', t(spec.l));
    knob.classList.toggle('disabled', disabled());
    if (!drag) paint(get());
  };
  update();
  return { box, range: knob, update };
}

function makeSwitch(checked, onchange) {
  const inp = el('input', { type: 'checkbox' });
  inp.checked = !!checked;
  inp.addEventListener('change', () => onchange(inp.checked));
  return { node: el('label', { class: 'switch' }, inp, el('i')), inp };
}

function bindSeg(seg, get, set) {
  const btns = $$('button', seg);
  const paint = () => { const v = String(get()); btns.forEach((b) => b.classList.toggle('on', b.dataset.v === v)); };
  btns.forEach((b) => b.addEventListener('click', () => { set(b.dataset.v); paint(); }));
  paint();
  return paint;
}

// ------------------------------------------------------------------ toast & modal
let toastTimer;
function toast(msg, fail) {
  const tEl = $('#toast');
  tEl.textContent = msg;
  tEl.className = 'toast' + (fail ? ' fail' : '');
  tEl.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (tEl.hidden = true), 3200);
}
function openModal(build, opts = {}) {
  const m = $('#modal'), box = $('#modalBox');
  box.innerHTML = '';
  const close = () => { m.hidden = true; if (opts.onClose) opts.onClose(); };
  build(box, close);
  m.hidden = false;
  m.onclick = (e) => { if (e.target === m && !opts.sticky) close(); };
  return close;
}
function confirmBox(title, text, okLabel, danger) {
  return new Promise((res) => {
    openModal((box, close) => {
      box.append(el('h2', { text: title }), el('p', { class: 'muted', text }),
        el('div', { class: 'modal-actions' },
          el('button', { class: 'key', type: 'button', text: t('common.cancel'), onclick: () => { close(); res(false); } }),
          el('button', { class: 'key ' + (danger ? 'danger' : 'primary'), type: 'button', text: okLabel, onclick: () => { close(); res(true); } })));
    }, { onClose: () => res(false) });
  });
}
function promptBox(title, placeholder) {
  return new Promise((res) => {
    openModal((box, close) => {
      const inp = el('input', { type: 'text', placeholder, maxlength: 40 });
      const ok = () => { const v = inp.value.trim(); close(); res(v || null); };
      inp.addEventListener('keydown', (e) => { if (e.key === 'Enter') ok(); });
      box.append(el('h2', { text: title }), inp,
        el('div', { class: 'modal-actions' },
          el('button', { class: 'key', type: 'button', text: t('common.cancel'), onclick: () => { close(); res(null); } }),
          el('button', { class: 'key primary', type: 'button', text: t('common.save'), onclick: ok })));
      setTimeout(() => inp.focus(), 30);
    }, { onClose: () => res(null) });
  });
}
function langModal() {
  openModal((box, close) => {
    const grid = el('div', { class: 'lang-grid' });
    for (const [code, name] of LANGS)
      grid.append(el('button', { type: 'button', class: code === LANG ? 'on' : '', text: name, onclick: async () => { close(); await switchLang(code); } }));
    box.append(el('h2', { text: t('set.language') }), grid);
  });
}
async function switchLang(code) {
  await loadLang(code);
  saveSettings({ language: code });
  renderAll();
}

// ------------------------------------------------------------------ navigation
const PAGES = ['overview', 'processing', 'eq', 'fx', 'ab', 'diag', 'guide', 'settings'];
function go(page) {
  if (!PAGES.includes(page)) page = 'overview';
  S.page = page;
  $$('.page').forEach((p) => p.classList.toggle('active', p.dataset.page === page));
  $$('#nav a').forEach((a) => a.classList.toggle('active', a.dataset.page === page));
  $('#pageTitle').textContent = t('nav.' + page);
  $('#side').classList.remove('open');
  if (location.hash !== '#' + page) history.replaceState(null, '', '#' + page);
  requestAnimationFrame(resizeCanvases);
}

// ------------------------------------------------------------------ theme
function applyTheme() {
  const th = (S.settings && S.settings.theme) || 'auto';
  if (th === 'auto') document.documentElement.removeAttribute('data-theme');
  else document.documentElement.setAttribute('data-theme', th);
  const dark = th === 'dark' || (th === 'auto' && matchMedia('(prefers-color-scheme: dark)').matches);
  $('#btnTheme use').setAttribute('href', dark ? '#i-sun' : '#i-moon');
}

// ------------------------------------------------------------------ state application
function applyState(st, keepParams) {
  S.state = st;
  S.settings = st.settings;
  if (!keepParams || !S.params) S.params = st.params;
  S.devices = st.devices;
  if (st.update) S.update = st.update;
  applyStatus(st.status);
  applyTheme();
  refreshSettings();
}

function applyStatus(s) {
  if (!s) return;
  S.status = s;
  const d = s.device || {}, drv = s.driver || {};
  // header toggles
  $('#btnMute').classList.toggle('on', !!s.muted);
  $('#btnMute use').setAttribute('href', s.muted ? '#i-mic-off' : '#i-mic');
  $('#btnMute span').textContent = t(s.muted ? 'top.muted' : 'top.mute');
  $('#btnBypass').classList.toggle('on', !s.bypass);
  $('#btnMonitor').classList.toggle('on', !!(s.monitor && s.monitor.on));
  // sidebar pill
  const pill = $('#drvPill');
  let cls = 'warn', txt = t('pill.preview');
  if (!d.found) { cls = 'fail'; txt = t('pill.noMic'); }
  else if (drv.active) { cls = 'ok'; txt = t('pill.active'); }
  else if (drv.installed && drv.attached) { cls = 'warn'; txt = t('pill.waiting'); }
  pill.className = 'drv-pill ' + cls;
  $('span', pill).textContent = txt;
  // overview header: a row of indicator lamps
  $('#micName').textContent = d.found ? (d.c01u ? 'Samson C01U Pro' : d.name) : t('ov.noMic');
  const chips = $('#micChips');
  chips.innerHTML = '';
  const chip = (text, c) => chips.append(el('span', { class: 'chip' }, el('i', { class: 'lamp ' + (c || '') }), text));
  if (d.found) {
    chip(t('chip.connected'), 'ok');
    if (d.rate) chip(nf(d.rate / 1000, 1) + ' kHz · ' + d.bits + ' bit · ' + (d.channels === 1 ? t('chip.mono') : d.channels + 'ch'));
    if (d.mute) chip(t('chip.winMuted'), 'fail');
    chip(drv.active ? t('chip.driverActive') : drv.installed ? t('chip.driverIdle') : t('chip.preview'), drv.active ? 'ok' : 'warn');
    if (drv.active && drv.load != null) chip(t('chip.cpu', { v: nf(drv.load, 2) }));
  } else chip(t('chip.notFound'), 'fail');
  // banner
  renderBanner();
  // monitor toggles
  if (S.settings) $('#monOn').checked = !!S.settings.monitorOn;
  if (winVol) winVol.update();
  renderDriverStatus();
}

function renderBanner() {
  const s = S.status, b = $('#banner');
  if (!s || !s.device) return;
  const drv = s.driver || {};
  let msg = null, btn = null, action = null, warn = false;
  if (!s.device.found) { msg = t('banner.noMic'); warn = true; btn = t('diag.run'); action = () => { go('diag'); runDiag(); }; }
  else if (!drv.installed) { msg = t('banner.notInstalled'); btn = t('set.install'); action = installDriver; }
  else if (drv.sysfxDisabled) { msg = t('banner.sysfx'); warn = true; btn = t('fix.enableSysfx'); action = () => driverAction('enableSysfx'); }
  else if (!drv.attached) { msg = t('banner.notAttached'); warn = true; btn = t('fix.attach'); action = () => driverAction('attach'); }
  else if (drv.unattached && drv.unattached.length) { msg = t('banner.otherPorts'); btn = t('fix.attach'); action = () => driverAction('attach', { all: true }); }
  else if (s.mode === 'app' && s.device.capturing && !drv.active && drv.lastSeenMs !== 0) { msg = t('banner.notLoaded'); warn = true; btn = t('fix.restartAudio'); action = () => driverAction('restartAudio'); }
  if (!msg) { b.hidden = true; return; }
  b.className = 'banner' + (warn ? ' warn' : '');
  b.innerHTML = '';
  b.append(el('div', { class: 'grow', text: msg }));
  if (btn) b.append(el('button', { class: 'key primary small', type: 'button', text: btn, onclick: action }));
  b.hidden = false;
}

// ------------------------------------------------------------------ overview
let monVol, winVol;
const QUICK = [
  ['nsOn', 'mod.ns'], ['gateOn', 'mod.gate'], ['popOn', 'mod.pop'], ['deessOn', 'mod.deess'], ['compOn', 'mod.comp'],
  ['agcOn', 'mod.agc'], ['limOn', 'mod.lim'], ['humOn', 'mod.hum'], ['hpfOn', 'mod.hpf'], ['eqOn', 'mod.eq'],
];
function renderOverview() {
  const q = $('#quickToggles');
  q.innerHTML = '';
  for (const [k, m] of QUICK) {
    const sw = makeSwitch(S.params[k], (v) => setParam(k, v));
    sw.inp.dataset.param = k;
    q.append(el('div', { class: 'tg' }, sw.node, el('div', null, el('b', { text: t(m) }), el('span', { text: t(m + 'Short') })), el('i', { class: 'lamp' })));
  }
  renderPresets();
  const mv = $('#monVolCtl'); mv.innerHTML = '';
  monVol = makeSlider(mv, { l: 'ov.monitorVolume', min: 0, max: 1.5, step: 0.01, def: 0.8, fmt: fmtPct,
    get: () => (S.settings ? S.settings.monitorVolume : 0.8), set: debounce((v) => saveSettings({ monitorVolume: v }), 80) });
  const wv = $('#winVolCtl'); wv.innerHTML = '';
  winVol = makeSlider(wv, { l: 'ov.winLevelLabel', min: 0, max: 100, step: 1, def: 100, fmt: (v) => nf(v, 0) + '%',
    get: () => (S.status && S.status.device && S.status.device.volume != null ? S.status.device.volume : 0),
    set: debounce((v) => { if (S.status && S.status.device) S.status.device.volume = v; api('settings', { micVolume: v }).catch(() => {}); }, 80) });
  fillDeviceSelect($('#monDev'), S.devices ? S.devices.outputs : [], S.settings.monitorDevice, t('common.defaultDevice'));
}

function renderPresets() {
  const host = $('#presets');
  host.innerHTML = '';
  const cur = S.settings.preset;
  for (const p of PRESETS) {
    host.append(el('button', { type: 'button', class: 'key preset' + (cur === p.id ? ' on' : ''), onclick: () => applyPreset(p.id) },
      el('i', { class: 'lamp' }), el('b', { text: t('preset.' + p.id) }), el('span', { text: t('preset.' + p.id + 'Desc') })));
  }
  (S.settings.userPresets || []).forEach((u, i) => {
    host.append(el('button', { type: 'button', class: 'key preset' + (cur === 'user:' + u.id ? ' on' : ''), onclick: () => applyUserPreset(u) },
      el('i', { class: 'lamp' }), el('b', { text: u.name }), el('span', { text: t('preset.user') }),
      el('span', { class: 'del', role: 'button', title: t('common.delete'), onclick: (e) => { e.stopPropagation(); deleteUserPreset(i); } }, icon('trash'))));
  });
  if (cur === 'custom') host.append(el('div', { class: 'key preset on', style: 'cursor:default' }, el('i', { class: 'lamp' }), el('b', { text: t('preset.custom') }), el('span', { text: t('preset.customDesc') })));
}
function applyPreset(id) {
  const p = PRESETS.find((x) => x.id === id);
  if (!p) return;
  setParams(presetParams(p.p), id);
  renderPresets();
  toast(t('toast.preset', { name: t('preset.' + id) }));
}
function applyUserPreset(u) {
  const p = Object.assign(deepCopy(DEFAULTS), u.params);
  p.bypass = S.params.bypass; p.mute = S.params.mute;
  setParams(p, 'user:' + u.id);
  renderPresets();
  toast(t('toast.preset', { name: u.name }));
}
async function saveUserPreset() {
  const name = await promptBox(t('ov.savePreset'), t('ov.presetName'));
  if (!name) return;
  const list = (S.settings.userPresets || []).slice();
  const id = Date.now().toString(36);
  const params = deepCopy(S.params); delete params.mute; delete params.bypass;
  list.push({ id, name, params });
  await saveSettings({ userPresets: list });
  await api('params', { preset: 'user:' + id });
  S.settings.preset = 'user:' + id;
  renderPresets();
  toast(t('toast.saved'));
}
async function deleteUserPreset(i) {
  const list = (S.settings.userPresets || []).slice();
  const [u] = list.splice(i, 1);
  if (!u || !(await confirmBox(t('common.delete'), t('ov.deletePreset', { name: u.name }), t('common.delete'), true))) return;
  await saveSettings({ userPresets: list });
  renderPresets();
}

function fillDeviceSelect(sel, list, current, defLabel) {
  sel.innerHTML = '';
  sel.append(el('option', { value: '', text: defLabel }));
  for (const d of list || []) sel.append(el('option', { value: d.id, text: d.name + (d.default ? ' ★' : '') }));
  sel.value = current || '';
  if (sel.value !== (current || '')) sel.value = '';
}

// VU meters: 0 VU = -18 dBFS, spring-damper needle with a little overshoot like the real thing
const VU_TICKS = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3];
const VU_LO = Math.pow(10, -1), VU_HI = Math.pow(10, 3 / 20);
const vuPosVu = (vu) => (Math.pow(10, clamp(vu, -40, 6) / 20) - VU_LO) / (VU_HI - VU_LO);
const meterState = {
  in: { x: -0.05, vel: 0, pk: -120, pkT: 0, lamp: 0 },
  out: { x: -0.05, vel: 0, pk: -120, pkT: 0, lamp: 0 },
  t: 0,
};
function drawVU(c, st, now) {
  if (!c.offsetParent) return;
  resizeCanvas(c);
  const ctx = c.getContext('2d'), W = c.width, H = c.height, dpr = window.devicePixelRatio || 1;
  const bg = ctx.createRadialGradient(W / 2, H * 0.95, H * 0.05, W / 2, H * 0.55, W * 0.8);
  bg.addColorStop(0, '#fff4d2'); bg.addColorStop(0.5, '#f5d590'); bg.addColorStop(1, '#b8843a');
  ctx.fillStyle = bg; ctx.fillRect(0, 0, W, H);
  const cx = W / 2, cy = H * 1.22, R = H * 1.02, a0 = -0.78, a1 = 0.78;
  const ang = (p) => a0 + (a1 - a0) * p - Math.PI / 2;
  const at = (p, r) => [cx + Math.cos(ang(p)) * r, cy + Math.sin(ang(p)) * r];
  // scale arcs
  ctx.lineWidth = 1.4 * dpr; ctx.strokeStyle = '#2b1d10';
  ctx.beginPath(); ctx.arc(cx, cy, R * 0.8, ang(0), ang(vuPosVu(0))); ctx.stroke();
  ctx.lineWidth = 5 * dpr; ctx.strokeStyle = '#b8261a';
  ctx.beginPath(); ctx.arc(cx, cy, R * 0.8 + 2.5 * dpr, ang(vuPosVu(0)), ang(1)); ctx.stroke();
  ctx.font = '600 ' + Math.round(11 * dpr) + 'px ' + cssVar('--label');
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  for (const v of VU_TICKS) {
    const p = vuPosVu(v);
    const [x1, y1] = at(p, R * 0.8), [x2, y2] = at(p, R * 0.8 + 9 * dpr), [tx, ty] = at(p, R * 0.8 + 19 * dpr);
    ctx.strokeStyle = v > 0 ? '#b8261a' : '#2b1d10'; ctx.lineWidth = 1.6 * dpr;
    ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
    ctx.fillStyle = v > 0 ? '#b8261a' : '#2b1d10';
    ctx.fillText(v > 0 ? '+' + v : String(v).replace('-', '−'), tx, ty);
  }
  for (let v = -20; v < 3; v += 0.5) {
    if (VU_TICKS.includes(v)) continue;
    if (v < -10 && v % 2 !== 0) continue;
    const [x1, y1] = at(vuPosVu(v), R * 0.8), [x2, y2] = at(vuPosVu(v), R * 0.8 + 4 * dpr);
    ctx.strokeStyle = v >= 0 ? '#b8261a' : '#2b1d10'; ctx.lineWidth = 1 * dpr;
    ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
  }
  ctx.fillStyle = '#2b1d10';
  ctx.font = '700 ' + Math.round(26 * dpr) + 'px ' + cssVar('--label');
  ctx.fillText('VU', cx, H * 0.66);
  ctx.font = '600 ' + Math.round(8.5 * dpr) + 'px ' + cssVar('--label');
  ctx.fillText('C01U PRO STUDIO', cx, H * 0.82);
  // peak lamp
  const lit = now < st.lamp;
  ctx.beginPath(); ctx.arc(W - 16 * dpr, 15 * dpr, 5 * dpr, 0, 7);
  ctx.fillStyle = lit ? '#ff3b1f' : '#6d2a1c';
  if (lit) { ctx.shadowColor = '#ff3b1f'; ctx.shadowBlur = 12 * dpr; }
  ctx.fill(); ctx.shadowBlur = 0;
  ctx.font = '600 ' + Math.round(8 * dpr) + 'px ' + cssVar('--label'); ctx.fillStyle = '#2b1d10';
  ctx.fillText('PEAK', W - 16 * dpr, 28 * dpr);
  // needle with soft shadow
  const p = clamp(st.x, -0.06, 1.08);
  const [nx0, ny0] = at(p, R * 0.3), [nx1, ny1] = at(p, R * 0.96);
  ctx.strokeStyle = 'rgba(60,30,0,0.25)'; ctx.lineWidth = 3 * dpr;
  ctx.beginPath(); ctx.moveTo(nx0 + 4 * dpr, ny0 + 3 * dpr); ctx.lineTo(nx1 + 4 * dpr, ny1 + 3 * dpr); ctx.stroke();
  ctx.strokeStyle = '#140c05'; ctx.lineWidth = 1.8 * dpr;
  ctx.beginPath(); ctx.moveTo(nx0, ny0); ctx.lineTo(nx1, ny1); ctx.stroke();
  // bezel shadow and glass
  const sh = ctx.createLinearGradient(0, H * 0.72, 0, H);
  sh.addColorStop(0, 'rgba(0,0,0,0)'); sh.addColorStop(1, 'rgba(40,20,0,0.55)');
  ctx.fillStyle = sh; ctx.fillRect(0, 0, W, H);
  const gl = ctx.createLinearGradient(0, 0, W, H);
  gl.addColorStop(0, 'rgba(255,255,255,0.28)'); gl.addColorStop(0.35, 'rgba(255,255,255,0.04)'); gl.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = gl; ctx.fillRect(0, 0, W, H);
}
const GR = [
  { k: 'ns', l: 'gr.ns', on: 'nsOn', max: 30 }, { k: 'gate', l: 'gr.gate', on: 'gateOn', max: 30 },
  { k: 'comp', l: 'gr.comp', on: 'compOn', max: 18 }, { k: 'agc', l: 'gr.agc', on: 'agcOn', max: 18, signed: true },
  { k: 'deess', l: 'gr.deess', on: 'deessOn', max: 12 }, { k: 'pop', l: 'gr.pop', on: 'popOn', max: 24 },
  { k: 'lim', l: 'gr.lim', on: 'limOn', max: 12 },
];
function renderGr() {
  const host = $('#grRow');
  host.innerHTML = '';
  for (const g of GR) {
    host.append(el('div', { class: 'gr', 'data-k': g.k },
      el('div', { class: 'gr-top' }, el('span', { text: t(g.l) }), el('b', { text: '0' })),
      el('div', { class: 'gr-bar' }, el('i'))));
  }
}
function drawMeters(now) {
  const m = S.meters;
  const dt = Math.min(0.05, meterState.t ? (now - meterState.t) / 1000 : 0.016);
  meterState.t = now;
  const upd = (key, canvas, outSel, pair) => {
    const st = meterState[key];
    const rms = pair ? pair[1] : -120, peak = pair ? pair[0] : -120;
    const target = vuPosVu(rms + 18);
    const K = 170, D = 2 * Math.sqrt(K) * 0.72;
    st.vel += (K * (target - st.x) - D * st.vel) * dt;
    st.x += st.vel * dt;
    if (st.x < -0.06) { st.x = -0.06; st.vel = 0; }
    if (st.x > 1.08) { st.x = 1.08; st.vel = 0; }
    if (peak > -3) st.lamp = now + 700;
    if (peak >= st.pk || now - st.pkT > 1500) { st.pk = peak; st.pkT = now; }
    drawVU($(canvas), st, now);
    $(outSel).textContent = st.pk <= -100 ? '−∞' : nf(st.pk, 1);
  };
  upd('in', '#vuIn', '#vIn', m && m.in);
  upd('out', '#vuOut', '#vOut', m && m.out);
  // gain reduction chips
  for (const g of GR) {
    const box = $('.gr[data-k="' + g.k + '"]');
    if (!box) continue;
    const on = S.params && S.params[g.on] && !S.params.bypass;
    box.classList.toggle('off', !on);
    const v = m && m.gr && m.gr[g.k] != null ? m.gr[g.k] : 0;
    $('b', box).textContent = on ? (g.signed && v > 0 ? '+' : '') + nf(v, 1) : t('common.off');
    $('i', box).style.width = on ? clamp(Math.abs(v) / g.max * 100, 0, 100) + '%' : '0';
  }
  // stats
  if (m) {
    const l = m.lufsS;
    $('#lufs').textContent = l > -69 ? nf(l, 1) : '—';
    $('#lufsMark').style.left = 'calc(' + clamp((l + 40) / 30, 0, 1) * 100 + '% - 2px)';
    $('#floor').textContent = m.floor != null ? nf(m.floor, 0) : '—';
    $('#vadDot').classList.toggle('on', !!m.voice);
    $('#vadText').textContent = t(m.voice ? 'ov.speaking' : 'ov.silence');
    pitchTrack(m.pitch, now);
  }
}
const pitchHist = [];
function pitchTrack(p, now) {
  if (p > 0) pitchHist.push([now, p]);
  while (pitchHist.length && now - pitchHist[0][0] > 2500) pitchHist.shift();
  if (!pitchHist.length) { $('#pitch').textContent = '—'; $('#pitchNote').textContent = ''; $('#voiceType').textContent = t('ov.voiceHint'); return; }
  const arr = pitchHist.map((x) => x[1]).sort((a, b) => a - b);
  const med = arr[arr.length >> 1];
  $('#pitch').textContent = nf(med, 0);
  $('#pitchNote').textContent = 'Hz · ' + noteName(med);
  const types = [[100, 'bass'], [130, 'baritone'], [170, 'tenor'], [220, 'alto'], [280, 'mezzo'], [1e9, 'soprano']];
  $('#voiceType').textContent = t('voice.' + types.find((x) => med < x[0])[1]);
}

// spectrum
let specMode = 'bars';
const specSmooth = new Float32Array(64).fill(-120), specPeak = new Float32Array(64).fill(-120);
let waterfall = null;
function cssVar(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }
function resizeCanvas(c) {
  const dpr = window.devicePixelRatio || 1;
  if (!c.dataset.h) c.dataset.h = c.getAttribute('height');
  const w = Math.max(10, c.clientWidth), h = +c.dataset.h;
  c.style.height = h + 'px';
  if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); return true; }
  return false;
}
function resizeCanvases() { for (const c of $$('canvas')) if (c.offsetParent) { if (resizeCanvas(c) && c.id === 'spec') waterfall = null; } drawEq(); }
function specColor(v) {
  // v 0..1 -> black -> deep red -> amber -> pale yellow (phosphor of an old display)
  const stops = [[0, [6, 4, 2]], [0.35, [110, 34, 6]], [0.7, [255, 160, 40]], [1, [255, 245, 200]]];
  for (let i = 1; i < stops.length; i++) if (v <= stops[i][0]) {
    const a = stops[i - 1], b = stops[i], f = (v - a[0]) / (b[0] - a[0]);
    return a[1].map((x, j) => Math.round(x + (b[1][j] - x) * f));
  }
  return [255, 255, 255];
}
function drawSpectrum() {
  const c = $('#spec');
  if (!c.offsetParent) return;
  const ctx = c.getContext('2d'), W = c.width, H = c.height, dpr = window.devicePixelRatio || 1;
  const spec = S.meters && S.meters.spec;
  const lo = -96, hi = -12;
  for (let i = 0; i < 64; i++) {
    const v = spec ? spec[i] + i * 0.09 * 3 : -120;  // gentle tilt so voice looks balanced
    specSmooth[i] = v > specSmooth[i] ? specSmooth[i] + (v - specSmooth[i]) * 0.6 : specSmooth[i] + (v - specSmooth[i]) * 0.15;
    specPeak[i] = Math.max(specSmooth[i], specPeak[i] - 0.4);
  }
  if (specMode === 'wave') {
    if (!waterfall || waterfall.width !== W) { waterfall = document.createElement('canvas'); waterfall.width = W; waterfall.height = H; const wc = waterfall.getContext('2d'); wc.fillStyle = '#060402'; wc.fillRect(0, 0, W, H); }
    const wc = waterfall.getContext('2d');
    wc.drawImage(waterfall, 0, 0, W, H, 0, 2 * dpr, W, H);
    const img = wc.createImageData(W, Math.ceil(2 * dpr));
    for (let x = 0; x < W; x++) {
      const band = Math.min(63, Math.floor(x / W * 64));
      const col = specColor(clamp((specSmooth[band] - lo) / (hi - lo), 0, 1));
      for (let y = 0; y < img.height; y++) { const o = (y * W + x) * 4; img.data[o] = col[0]; img.data[o + 1] = col[1]; img.data[o + 2] = col[2]; img.data[o + 3] = 255; }
    }
    wc.putImageData(img, 0, 0);
    ctx.drawImage(waterfall, 0, 0);
  } else {
    // LED bar graph: 32 columns x 16 segments, green / amber / red, with peak hold
    ctx.fillStyle = '#060402'; ctx.fillRect(0, 0, W, H);
    const cols = 32, segs = 16, labelH = 14 * dpr;
    const cw = W / cols, sh = (H - labelH) / segs;
    for (let c = 0; c < cols; c++) {
      const v = Math.max(specSmooth[c * 2], specSmooth[c * 2 + 1]);
      const pk = Math.max(specPeak[c * 2], specPeak[c * 2 + 1]);
      const lit = Math.round(clamp((v - lo) / (hi - lo), 0, 1) * segs);
      const pkSeg = Math.round(clamp((pk - lo) / (hi - lo), 0, 1) * segs) - 1;
      for (let s = 0; s < segs; s++) {
        const on = s < lit || s === pkSeg;
        const col = s >= segs - 2 ? (on ? '#ff3b1f' : '#2a0c06') : s >= segs - 5 ? (on ? '#ffb23e' : '#2a1b06') : (on ? '#9be04a' : '#14200a');
        ctx.fillStyle = col;
        if (on) { ctx.shadowColor = col; ctx.shadowBlur = 6 * dpr; }
        ctx.fillRect(c * cw + 1.5 * dpr, H - labelH - (s + 1) * sh + 1.2 * dpr, cw - 3 * dpr, sh - 2.4 * dpr);
        ctx.shadowBlur = 0;
      }
    }
    ctx.fillStyle = '#c98a2a';
    ctx.font = '600 ' + Math.round(9.5 * dpr) + 'px ' + cssVar('--label');
    ctx.textAlign = 'center';
    for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) {
      const x = Math.log(f / 30) / Math.log(20000 / 30) * W;
      ctx.fillText(f >= 1000 ? f / 1000 + 'k' : String(f), x, H - 3 * dpr);
    }
    ctx.textAlign = 'start';
  }
}
function roundRect(ctx, x, y, w, h, r) {
  if (h <= 0) return;
  r = Math.min(r, h / 2, w / 2);
  ctx.beginPath();
  ctx.moveTo(x + r, y); ctx.lineTo(x + w - r, y); ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h); ctx.lineTo(x, y + h); ctx.lineTo(x, y + r); ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.fill();
}

// ------------------------------------------------------------------ processing page
const fmtRatio = (v) => nf(v, 1) + ':1';
const MODULES = [
  { id: 'gain', ctls: [{ k: 'inputGainDb', l: 'ctl.inputGain', min: -20, max: 30, step: 0.5, fmt: fmtDb }, { k: 'outputGainDb', l: 'ctl.outputGain', min: -20, max: 20, step: 0.5, fmt: fmtDb }] },
  { id: 'hpf', on: 'hpfOn', ctls: [{ k: 'hpfFreq', l: 'ctl.cutoff', min: 20, max: 300, step: 1, log: true, fmt: fmtHz }], seg: { k: 'hpfOrder', opts: [[2, '12 dB/oct'], [4, '24 dB/oct']] } },
  { id: 'hum', on: 'humOn', ctls: [{ k: 'humHarmonics', l: 'ctl.harmonics', min: 1, max: 8, step: 1, fmt: (v) => nf(v, 0) }], seg: { k: 'humFreq', opts: [[50, '50 Hz'], [60, '60 Hz']] } },
  { id: 'ns', on: 'nsOn', ctls: [{ k: 'nsReductionDb', l: 'ctl.reduction', min: 3, max: 40, step: 1, fmt: (v) => nf(v, 0) + ' dB' }, { k: 'nsSensitivity', l: 'ctl.sensitivity', min: 0, max: 1, step: 0.01, fmt: fmtPct }], relearn: true },
  { id: 'pop', on: 'popOn', ctls: [{ k: 'popAmount', l: 'ctl.strength', min: 0, max: 1, step: 0.01, fmt: fmtPct }] },
  { id: 'gate', on: 'gateOn', auto: 'gateAuto', ctls: [
    { k: 'gateThresholdDb', l: 'ctl.threshold', min: -80, max: -10, step: 1, fmt: (v) => nf(v, 0) + ' dB', disabled: () => S.params.gateAuto },
    { k: 'gateRangeDb', l: 'ctl.range', min: 3, max: 80, step: 1, fmt: (v) => nf(v, 0) + ' dB' },
    { k: 'gateAttackMs', l: 'ctl.attack', min: 0.1, max: 50, step: 0.1, log: true, fmt: fmtMs },
    { k: 'gateHoldMs', l: 'ctl.hold', min: 0, max: 1000, step: 5, fmt: fmtMs },
    { k: 'gateReleaseMs', l: 'ctl.release', min: 10, max: 2000, step: 1, log: true, fmt: fmtMs }] },
  { id: 'deess', on: 'deessOn', ctls: [{ k: 'deessFreq', l: 'ctl.frequency', min: 3000, max: 10000, step: 50, log: true, fmt: fmtHz }, { k: 'deessAmount', l: 'ctl.strength', min: 0, max: 1, step: 0.01, fmt: fmtPct }] },
  { id: 'agc', on: 'agcOn', ctls: [
    { k: 'agcTargetDb', l: 'ctl.target', min: -30, max: -10, step: 0.5, fmt: (v) => nf(v, 1) + ' LUFS' },
    { k: 'agcMaxGainDb', l: 'ctl.maxGain', min: 0, max: 30, step: 0.5, fmt: fmtDb },
    { k: 'agcSpeed', l: 'ctl.speed', min: 0, max: 1, step: 0.01, fmt: fmtPct }] },
  { id: 'comp', on: 'compOn', graph: true, auto: 'compAutoMakeup', ctls: [
    { k: 'compThresholdDb', l: 'ctl.threshold', min: -60, max: 0, step: 0.5, fmt: (v) => nf(v, 1) + ' dB' },
    { k: 'compRatio', l: 'ctl.ratio', min: 1, max: 20, step: 0.1, log: true, fmt: fmtRatio },
    { k: 'compAttackMs', l: 'ctl.attack', min: 0.1, max: 200, step: 0.1, log: true, fmt: fmtMs },
    { k: 'compReleaseMs', l: 'ctl.release', min: 10, max: 2000, step: 1, log: true, fmt: fmtMs },
    { k: 'compKneeDb', l: 'ctl.knee', min: 0, max: 24, step: 0.5, fmt: (v) => nf(v, 1) + ' dB' },
    { k: 'compMakeupDb', l: 'ctl.makeup', min: -12, max: 24, step: 0.5, fmt: fmtDb }] },
  { id: 'lim', on: 'limOn', ctls: [{ k: 'limCeilingDb', l: 'ctl.ceiling', min: -12, max: 0, step: 0.1, fmt: (v) => nf(v, 1) + ' dBFS' }] },
];
const CHAIN = ['gain', 'hpf', 'hum', 'ns', 'pop', 'gate', 'eq', 'deess', 'agc', 'comp', 'fx', 'lim'];
let sliders = [];
function renderProcessing() {
  const chain = $('#chain');
  chain.innerHTML = '';
  CHAIN.forEach((id, i) => {
    if (i) chain.append(el('span', { class: 'arrow', text: document.documentElement.dir === 'rtl' ? '←' : '→' }));
    const onKey = id === 'eq' ? 'eqOn' : id === 'fx' ? null : (MODULES.find((m) => m.id === id) || {}).on;
    const on = id === 'fx' ? S.params.fxType > 0 : id === 'gain' ? true : !!S.params[onKey];
    chain.append(el('button', { type: 'button', class: on ? 'on' : '', text: t('mod.' + id), onclick: () => {
      if (id === 'eq') go('eq'); else if (id === 'fx') go('fx'); else { const c = $('#mod-' + id); if (c) c.scrollIntoView({ behavior: 'smooth', block: 'center' }); }
    } }));
  });
  const host = $('#modules');
  host.innerHTML = '';
  sliders = sliders.filter((s) => !host.contains(s.box));
  for (const m of MODULES) {
    const card = el('div', { class: 'card module', id: 'mod-' + m.id });
    const head = el('div', { class: 'card-head' }, el('h3', { text: t('mod.' + m.id) }));
    if (m.on) { const sw = makeSwitch(S.params[m.on], (v) => setParam(m.on, v)); sw.inp.dataset.param = m.on; head.append(sw.node); }
    card.append(head, el('div', { class: 'desc', text: t('mod.' + m.id + 'Desc') }));
    if (m.graph) card.append(el('div', { class: 'glass ctl' }, el('canvas', { class: 'mini-graph', id: 'compGraph', height: 150 })));
    if (m.seg) {
      const seg = el('div', { class: 'seg wide' }, m.seg.opts.map(([v, l]) => el('button', { type: 'button', 'data-v': v, text: l })));
      card.append(el('div', { class: 'ctl' }, seg));
      bindSeg(seg, () => S.params[m.seg.k], (v) => setParam(m.seg.k, +v));
    }
    const knobs = el('div', { class: 'knobs' });
    card.append(knobs);
    for (const c of m.ctls) sliders.push(makeSlider(knobs, Object.assign({ def: DEFAULTS[c.k] }, c)));
    const extra = el('div', { class: 'mod-extra' });
    if (m.auto) {
      const sw = makeSwitch(S.params[m.auto], (v) => setParam(m.auto, v));
      sw.inp.dataset.param = m.auto;
      extra.append(el('div', { class: 'row gap' }, sw.node, el('span', { class: 'small', text: t('ctl.' + m.auto) })));
    }
    if (m.relearn) extra.append(el('button', { class: 'key small', type: 'button', onclick: async () => { await api('relearn', {}); toast(t('toast.relearn')); } }, icon('refresh'), t('ctl.relearn')));
    if (extra.childNodes.length) card.append(extra);
    card.classList.toggle('off', m.on ? !S.params[m.on] : false);
    host.append(card);
  }
  drawCompGraph();
}
function drawCompGraph() {
  const c = $('#compGraph');
  if (!c || !c.offsetParent) return;
  resizeCanvas(c);
  const ctx = c.getContext('2d'), W = c.width, H = c.height, dpr = window.devicePixelRatio || 1;
  const p = S.params, lo = -60;
  const map = (db) => [(db - lo) / -lo * W, H - (db - lo) / -lo * H];
  ctx.fillStyle = '#040703'; ctx.fillRect(0, 0, W, H);
  ctx.strokeStyle = 'rgba(120,255,140,0.13)'; ctx.lineWidth = 1;
  for (let db = -48; db < 0; db += 12) { const [x] = map(db); const [, y] = map(db); ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke(); }
  ctx.setLineDash([4 * dpr, 4 * dpr]);
  ctx.beginPath(); ctx.moveTo(0, H); ctx.lineTo(W, 0); ctx.stroke(); ctx.setLineDash([]);
  const T = p.compThresholdDb, R = p.compRatio, K = p.compKneeDb;
  const gc = (x) => { const o = x - T; if (2 * o < -K) return x; if (K > 0 && 2 * Math.abs(o) <= K) return x + (1 / R - 1) * (o + K / 2) ** 2 / (2 * K); return T + o / R; };
  ctx.strokeStyle = '#8dff9c'; ctx.shadowColor = '#5dff7a'; ctx.shadowBlur = 8 * dpr; ctx.lineWidth = 2 * dpr; ctx.beginPath();
  const pt = (x) => [map(x)[0], map(gc(x))[1]];
  for (let i = 0; i <= 120; i++) { const [px, py] = pt(lo + i * (-lo) / 120); if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py); }
  ctx.stroke();
  ctx.shadowBlur = 0;
  const m = S.meters;
  if (m && m.in && p.compOn) { const [px, py] = pt(clamp(m.in[1], lo, 0)); ctx.fillStyle = '#ffb23e'; ctx.shadowColor = '#ffb23e'; ctx.shadowBlur = 10 * dpr; ctx.beginPath(); ctx.arc(px, py, 4 * dpr, 0, 7); ctx.fill(); ctx.shadowBlur = 0; }
}

// ------------------------------------------------------------------ EQ
const FS = 48000;
const BAND_COLORS = ['#ff6b4a', '#ff9a3c', '#ffc53d', '#d7e04a', '#9be04a', '#5fd38a', '#4fc7c0', '#6aa8e8', '#b08be8', '#e886b8'];
const GAIN_TYPES = new Set([0, 1, 2]);
function biquad(type, f0, gain, q) {
  f0 = clamp(f0, 5, FS * 0.49); q = Math.max(q, 0.05);
  const A = Math.pow(10, gain / 40), w0 = 2 * Math.PI * f0 / FS, cw = Math.cos(w0), sw = Math.sin(w0), al = sw / (2 * q), sA = Math.sqrt(A);
  let b0, b1, b2, a0, a1, a2;
  switch (type) {
    case 0: b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
    case 1: b0 = A * ((A + 1) - (A - 1) * cw + 2 * sA * al); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - 2 * sA * al); a0 = (A + 1) + (A - 1) * cw + 2 * sA * al; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - 2 * sA * al; break;
    case 2: b0 = A * ((A + 1) + (A - 1) * cw + 2 * sA * al); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - 2 * sA * al); a0 = (A + 1) - (A - 1) * cw + 2 * sA * al; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - 2 * sA * al; break;
    case 3: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    case 4: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    case 5: b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    case 6: b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    default: return null;
  }
  return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
}
function magDb(c, f) {
  if (!c) return 0;
  const w = 2 * Math.PI * f / FS, cw = Math.cos(w), c2 = Math.cos(2 * w), sw = Math.sin(w), s2 = Math.sin(2 * w);
  const nr = c[0] + c[1] * cw + c[2] * c2, ni = -(c[1] * sw + c[2] * s2);
  const dr = 1 + c[3] * cw + c[4] * c2, di = -(c[3] * sw + c[4] * s2);
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-30);
}
function eqFilters() {
  const p = S.params, list = [];
  p.eq.forEach((b, i) => { if (b.on) list.push({ i, c: biquad(b.type, b.freq, b.gain, b.q) }); });
  if (Math.abs(p.warmth) > 0.01) list.push({ c: biquad(1, 220, 6 * p.warmth, 0.7) });
  if (Math.abs(p.clarity) > 0.01) list.push({ c: biquad(0, 3200, 5 * p.clarity, 0.8) });
  if (Math.abs(p.air) > 0.01) list.push({ c: biquad(2, 10000, 6 * p.air, 0.7) });
  return list;
}
let eqSel = -1, eqDrag = null;
const EQ_RANGE = 24;
const fx2x = (f, W) => Math.log(f / 20) / Math.log(1000) * W;
const x2f = (x, W) => 20 * Math.pow(1000, x / W);
const g2y = (g, H) => H / 2 - g / EQ_RANGE * (H / 2 - 12);
const y2g = (y, H) => (H / 2 - y) / (H / 2 - 12) * EQ_RANGE;
function drawEq() {
  const c = $('#eqCanvas');
  if (!c || !c.offsetParent || !S.params) return;
  resizeCanvas(c);
  const ctx = c.getContext('2d'), W = c.width, H = c.height, dpr = window.devicePixelRatio || 1;
  // oscilloscope screen: dark green glass, phosphor grid
  const scr = ctx.createRadialGradient(W / 2, H / 2, H * 0.1, W / 2, H / 2, W * 0.7);
  scr.addColorStop(0, '#0b1a0d'); scr.addColorStop(1, '#030603');
  ctx.fillStyle = scr; ctx.fillRect(0, 0, W, H);
  ctx.font = '600 ' + Math.round(10 * dpr) + 'px ' + cssVar('--label');
  ctx.fillStyle = 'rgba(140,255,150,0.45)';
  ctx.strokeStyle = 'rgba(120,255,140,0.12)'; ctx.lineWidth = 1;
  for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
    const x = fx2x(f, W); ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
    if (f > 20 && f < 20000) ctx.fillText(f >= 1000 ? f / 1000 + 'k' : f, x + 3 * dpr, H - 6 * dpr);
  }
  for (const g of [-18, -12, -6, 0, 6, 12, 18]) {
    const y = g2y(g, H); ctx.strokeStyle = g === 0 ? 'rgba(120,255,140,0.35)' : 'rgba(120,255,140,0.12)';
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
    ctx.fillText((g > 0 ? '+' : '') + g, 4 * dpr, y - 3 * dpr);
  }
  // live spectrum behind
  if (S.meters && S.meters.spec) {
    ctx.beginPath(); ctx.moveTo(0, H);
    for (let i = 0; i < 64; i++) {
      const f = 30 * Math.pow(20000 / 30, (i + 0.5) / 64);
      const y = H - clamp((specSmooth[i] + 100) / 90, 0, 1) * H * 0.9;
      ctx.lineTo(fx2x(f, W), y);
    }
    ctx.lineTo(W, H); ctx.closePath();
    ctx.fillStyle = 'rgba(255,170,60,0.09)'; ctx.fill();
  }
  const filters = eqFilters(), on = S.params.eqOn;
  const N = 240, pts = [];
  for (let i = 0; i <= N; i++) { const f = 20 * Math.pow(1000, i / N); let g = 0; if (on) for (const fl of filters) g += magDb(fl.c, f); pts.push([fx2x(f, W), g2y(clamp(g, -EQ_RANGE - 6, EQ_RANGE + 6), H)]); }
  // selected band alone
  if (eqSel >= 0 && S.params.eq[eqSel].on) {
    const b = S.params.eq[eqSel], c1 = biquad(b.type, b.freq, b.gain, b.q);
    ctx.beginPath();
    for (let i = 0; i <= N; i++) { const f = 20 * Math.pow(1000, i / N); const y = g2y(magDb(c1, f), H); if (i) ctx.lineTo(fx2x(f, W), y); else ctx.moveTo(fx2x(f, W), y); }
    ctx.strokeStyle = BAND_COLORS[eqSel]; ctx.globalAlpha = 0.6; ctx.setLineDash([5 * dpr, 4 * dpr]); ctx.lineWidth = 1.5 * dpr; ctx.stroke(); ctx.setLineDash([]); ctx.globalAlpha = 1;
  }
  ctx.beginPath(); pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
  ctx.lineTo(W, g2y(0, H)); ctx.lineTo(0, g2y(0, H)); ctx.closePath();
  ctx.fillStyle = on ? 'rgba(120,255,140,0.07)' : 'transparent'; ctx.fill();
  ctx.beginPath(); pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
  ctx.strokeStyle = on ? '#9dffaa' : 'rgba(140,255,150,0.35)'; ctx.shadowColor = '#4dff6a'; ctx.shadowBlur = on ? 10 * dpr : 0;
  ctx.lineWidth = 2.2 * dpr; ctx.stroke(); ctx.shadowBlur = 0;
  // nodes
  S.params.eq.forEach((b, i) => {
    if (!b.on) return;
    const x = fx2x(b.freq, W), y = g2y(GAIN_TYPES.has(b.type) ? b.gain : 0, H);
    ctx.beginPath(); ctx.arc(x, y, (i === eqSel ? 9 : 7) * dpr, 0, 7);
    ctx.fillStyle = BAND_COLORS[i]; ctx.globalAlpha = on ? 1 : 0.5;
    ctx.shadowColor = BAND_COLORS[i]; ctx.shadowBlur = i === eqSel ? 14 * dpr : 6 * dpr; ctx.fill(); ctx.shadowBlur = 0;
    ctx.lineWidth = 1.5 * dpr; ctx.strokeStyle = '#030603'; ctx.stroke(); ctx.globalAlpha = 1;
    ctx.fillStyle = '#120c04'; ctx.font = '700 ' + Math.round(9 * dpr) + 'px ' + cssVar('--label'); ctx.textAlign = 'center'; ctx.fillText(i + 1, x, y + 3.2 * dpr); ctx.textAlign = 'start';
  });
}
function eqHit(ev) {
  const c = $('#eqCanvas'), r = c.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  const x = (ev.clientX - r.left) * dpr, y = (ev.clientY - r.top) * dpr;
  let best = -1, bd = 18 * dpr;
  S.params.eq.forEach((b, i) => {
    if (!b.on) return;
    const d = Math.hypot(fx2x(b.freq, c.width) - x, g2y(GAIN_TYPES.has(b.type) ? b.gain : 0, c.height) - y);
    if (d < bd) { bd = d; best = i; }
  });
  return { best, x, y, W: c.width, H: c.height };
}
function setupEqCanvas() {
  const c = $('#eqCanvas');
  c.addEventListener('pointerdown', (ev) => {
    const h = eqHit(ev);
    if (h.best < 0) return;
    eqSel = h.best; eqDrag = { id: ev.pointerId }; S.dragging = true;
    c.setPointerCapture(ev.pointerId);
    renderEqBands(); drawEq();
  });
  c.addEventListener('pointermove', (ev) => {
    if (!eqDrag) { c.style.cursor = eqHit(ev).best >= 0 ? 'grab' : 'crosshair'; return; }
    const h = eqHit(ev), b = S.params.eq[eqSel];
    b.freq = Math.round(clamp(x2f(h.x, h.W), 20, 20000));
    if (GAIN_TYPES.has(b.type)) b.gain = Math.round(clamp(y2g(h.y, h.H), -EQ_RANGE, EQ_RANGE) * 2) / 2;
    setParam('eq', S.params.eq, { silent: true });
    drawEq(); updateEqBandValues();
  });
  const end = () => { if (eqDrag) { eqDrag = null; setTimeout(() => (S.dragging = false), 150); } };
  c.addEventListener('pointerup', end); c.addEventListener('pointercancel', end);
  c.addEventListener('wheel', (ev) => {
    const h = eqHit(ev);
    const i = h.best >= 0 ? h.best : eqSel;
    if (i < 0) return;
    ev.preventDefault();
    const b = S.params.eq[i];
    b.q = Math.round(clamp(b.q * (ev.deltaY < 0 ? 1.12 : 1 / 1.12), 0.1, 18) * 100) / 100;
    eqSel = i;
    setParam('eq', S.params.eq, { silent: true });
    drawEq(); renderEqBands();
  }, { passive: false });
  c.addEventListener('dblclick', (ev) => {
    const h = eqHit(ev);
    if (h.best >= 0) { S.params.eq[h.best].gain = 0; }
    else {
      const free = S.params.eq.findIndex((b) => !b.on);
      if (free < 0) return;
      Object.assign(S.params.eq[free], { on: true, type: 0, freq: Math.round(x2f(h.x, h.W)), gain: Math.round(y2g(h.y, h.H) * 2) / 2, q: 1 });
      eqSel = free;
    }
    setParam('eq', S.params.eq, { silent: true });
    drawEq(); renderEqBands();
  });
}
const TYPE_KEYS = ['peak', 'lowShelf', 'highShelf', 'lowPass', 'highPass', 'notch', 'bandPass'];
let bandKnobs = [];
function renderEqBands() {
  const host = $('#eqBands');
  host.innerHTML = '';
  bandKnobs = [];
  S.params.eq.forEach((b, i) => {
    const sw = makeSwitch(b.on, (v) => { b.on = v; setParam('eq', S.params.eq, { silent: true }); renderEqBands(); drawEq(); });
    const type = el('select', null, TYPE_KEYS.map((k, ti) => el('option', { value: ti, text: t('eq.' + k) })));
    type.value = b.type;
    type.addEventListener('change', () => { b.type = +type.value; setParam('eq', S.params.eq, { silent: true }); drawEq(); renderEqBands(); });
    const knobs = el('div', { class: 'knobs' });
    const card = el('div', { class: 'band' + (i === eqSel ? ' sel' : '') + (b.on ? '' : ' off'), 'data-i': i, onpointerdown: () => { if (eqSel !== i) { eqSel = i; $$('.band', host).forEach((x) => x.classList.toggle('sel', +x.dataset.i === i)); drawEq(); } } },
      el('div', { class: 'band-top' }, el('b', null, el('i', { style: 'background:' + BAND_COLORS[i] + ';color:' + BAND_COLORS[i] }), String(i + 1)), sw.node),
      type, knobs);
    const knob = (key, l, min, max, step, log, fmt, dis) => makeSlider(knobs, {
      l, min, max, step, log, fmt, small: true, def: EQ_DEFAULT[i][key],
      disabled: () => !b.on || (dis ? dis() : false),
      get: () => b[key],
      set: (v) => { b[key] = v; setParam('eq', S.params.eq, { silent: true }); drawEq(); },
    });
    bandKnobs.push(knob('freq', 'eq.freq', 20, 20000, 1, true, fmtHz),
      knob('gain', 'eq.gain', -24, 24, 0.5, false, fmtDb, () => !GAIN_TYPES.has(b.type)),
      knob('q', 'Q', 0.1, 18, 0.01, true, (v) => nf(v, 2)));
    host.append(card);
  });
}
function updateEqBandValues() { for (const k of bandKnobs) k.update(); }
let toneSliders = [];
function renderEq() {
  const sel = $('#eqPreset');
  sel.innerHTML = '';
  sel.append(el('option', { value: '', text: t('eq.presets') }));
  for (const k in EQ_PRESETS) sel.append(el('option', { value: k, text: t('eqp.' + k) }));
  sel.onchange = () => {
    const k = sel.value; sel.value = '';
    if (!k) return;
    S.params.eq = eqWith(EQ_PRESETS[k]);
    setParam('eq', S.params.eq);
    renderEqBands(); drawEq();
  };
  $('#eqOn').checked = !!S.params.eqOn;
  $('#eqOn').onchange = (e) => { setParam('eqOn', e.target.checked); drawEq(); };
  renderEqBands();
  const tone = $('#tone'); tone.innerHTML = '';
  toneSliders = ['warmth', 'clarity', 'air'].map((k) => makeSlider(tone, { k, l: 'tone.' + k, min: -1, max: 1, step: 0.01, def: 0, fmt: (v) => (v > 0 ? '+' : '') + nf(v * 100, 0) + '%', set: (v) => { setParam(k, v, { silent: true }); drawEq(); } }));
  drawEq();
}

// ------------------------------------------------------------------ FX
const FX = [
  { id: 'none', type: 0, emo: '🎙️' }, { id: 'robot', type: 1, emo: '🤖', param: 0.5, mix: 1 }, { id: 'radio', type: 2, emo: '📻', param: 0.4, mix: 1 },
  { id: 'phone', type: 3, emo: '☎️', param: 0.4, mix: 1 }, { id: 'deep', type: 4, emo: '🐻', param: 0.2917, mix: 1 }, { id: 'high', type: 4, emo: '🐿️', param: 0.75, mix: 1 },
  { id: 'echo', type: 5, emo: '🏔️', param: 0.4, mix: 0.35 }, { id: 'reverb', type: 6, emo: '⛪', param: 0.5, mix: 0.3 },
];
let fxMix, fxParam;
function currentFx() {
  const p = S.params;
  if (p.fxType === 4) return p.fxParam < 0.5 ? 'deep' : 'high';
  return (FX.find((f) => f.type === p.fxType) || FX[0]).id;
}
function renderFx() {
  const grid = $('#fxGrid'); grid.innerHTML = '';
  const cur = currentFx();
  for (const f of FX) {
    grid.append(el('button', { type: 'button', class: 'key fx' + (f.id === cur ? ' on' : ''), onclick: () => {
      const o = { fxType: f.type };
      if (f.param != null) o.fxParam = f.param;
      if (f.mix != null) o.fxMix = f.mix;
      setParams(o, 'custom'); renderFx();
    } }, el('i', { class: 'lamp' }), el('b', { text: t('fx.' + f.id) }), el('span', { text: t('fx.' + f.id + 'Desc') })));
  }
  const pm = $('#fxMixCtl'); pm.innerHTML = '';
  fxMix = makeSlider(pm, { k: 'fxMix', l: 'fx.mix', min: 0, max: 1, step: 0.01, def: 0.5, fmt: fmtPct, disabled: () => !S.params.fxType });
  const pp = $('#fxParamCtl'); pp.innerHTML = '';
  const type = S.params.fxType;
  const fmt = type === 4 ? (v) => { const s = Math.round((v - 0.5) * 24); return (s > 0 ? '+' : '') + s + ' ' + t('fx.semitones'); }
    : type === 5 ? (v) => nf(120 + 480 * v, 0) + ' ms' : fmtPct;
  fxParam = makeSlider(pp, { k: 'fxParam', l: 'fx.param' + type, min: 0, max: 1, step: type === 4 ? 1 / 24 : 0.01, def: 0.5, fmt, disabled: () => !S.params.fxType });
}

// ------------------------------------------------------------------ A/B test
let abLen = 10, abBuffers = null, abCtx = null, abSrc = [], abGain = [], abSide = 'b', abPlaying = false, abStart = 0, abOffset = 0;
function renderAb() {
  $('#abPhrase').textContent = t('ab.phrase');
  bindSeg($('#abLen'), () => abLen, (v) => (abLen = +v));
  $('#abRecord').onclick = startAb;
  $$('.ab-side').forEach((b) => (b.onclick = () => setAbSide(b.dataset.v)));
  $('#abPlay').onclick = toggleAbPlay;
  setAbSide(abSide);
}
async function startAb() {
  stopAb();
  try {
    await api('record', { action: 'start', seconds: abLen });
    $('#abRecord').disabled = true;
    $('#abPlayer').hidden = true;
  } catch (e) { toast(t('ab.failed'), true); }
}
const fmtTime = (s) => { s = Math.max(0, Math.floor(s)); return String(Math.floor(s / 60)).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0'); };
function updateAbProgress(rec) {
  if (!rec) return;
  const st = rec.state;
  const bar = $('#abBar'), status = $('#abStatus'), recBtn = $('#abRecord');
  $('#reels').classList.toggle('spin', st === 1 || st === 2 || abPlaying);
  recBtn.classList.toggle('on', st === 1 || st === 2);
  if (st === 1) { status.textContent = t('ab.getReady'); bar.style.width = '0'; $('#abCounter').textContent = fmtTime(0); }
  else if (st === 2) { status.textContent = t('ab.recording', { s: nf(Math.max(0, rec.len - rec.pos), 0) }); bar.style.width = (rec.pos / rec.len * 100) + '%'; $('#abCounter').textContent = fmtTime(rec.pos); }
  else if (st === 3 && recBtn.disabled) { bar.style.width = '100%'; status.textContent = t('ab.done'); recBtn.disabled = false; loadAb(); }
  else if (st === 0 && recBtn.disabled) { recBtn.disabled = false; status.textContent = ''; }
}
async function loadAb() {
  try {
    const [a, b] = await Promise.all(['raw', 'processed'].map((k) => fetch('/api/record/' + k + '.wav?' + Date.now()).then((r) => r.arrayBuffer())));
    $('#abDlA').href = '/api/record/raw.wav'; $('#abDlB').href = '/api/record/processed.wav';
    if (!abCtx) abCtx = new (window.AudioContext || window.webkitAudioContext)();
    abBuffers = await Promise.all([abCtx.decodeAudioData(a.slice(0)), abCtx.decodeAudioData(b.slice(0))]);
    abOffset = 0;
    $('#abPlayer').hidden = false;
    $('#abPlay').disabled = false;
    drawAbWave();
  } catch (e) { toast(t('ab.failed'), true); }
}
function drawAbWave() {
  const c = $('#abWave');
  if (!c.offsetParent || !abBuffers) return;
  resizeCanvas(c);
  const ctx = c.getContext('2d'), W = c.width, H = c.height;
  ctx.fillStyle = '#060402'; ctx.fillRect(0, 0, W, H);
  abBuffers.forEach((buf, k) => {
    const d = buf.getChannelData(0), step = Math.max(1, Math.floor(d.length / W));
    ctx.fillStyle = k === 0 ? '#8a7a60' : '#ffb23e';
    ctx.globalAlpha = k === 0 ? 0.55 : (abSide === 'b' ? 0.9 : 0.35);
    for (let x = 0; x < W; x++) {
      let mx = 0;
      for (let i = x * step; i < (x + 1) * step && i < d.length; i++) mx = Math.max(mx, Math.abs(d[i]));
      const h = Math.max(1, mx * H * 0.95);
      ctx.fillRect(x, (H - h) / 2, 1, h);
    }
  });
  ctx.globalAlpha = 1;
  if (abPlaying || abOffset > 0) {
    const pos = abPos() / abBuffers[0].duration;
    ctx.fillStyle = '#fff3d0'; ctx.fillRect(pos * W, 0, 2, H);
    $('#abCounter').textContent = fmtTime(abPos());
  }
}
const abPos = () => (abPlaying ? (abCtx.currentTime - abStart) % abBuffers[0].duration : abOffset);
function setAbSide(side) {
  abSide = side;
  $$('.ab-side').forEach((b) => b.classList.toggle('on', b.dataset.v === side));
  if (abGain.length) {
    const now = abCtx.currentTime;
    abGain[0].gain.setTargetAtTime(side === 'a' ? 1 : 0, now, 0.015);
    abGain[1].gain.setTargetAtTime(side === 'b' ? 1 : 0, now, 0.015);
  }
  drawAbWave();
}
function toggleAbPlay() {
  if (!abBuffers) return;
  if (abPlaying) { abOffset = abPos(); stopAb(); return; }
  abCtx.resume();
  abSrc = []; abGain = [];
  abBuffers.forEach((buf, k) => {
    const s = abCtx.createBufferSource(); s.buffer = buf; s.loop = true;
    const g = abCtx.createGain(); g.gain.value = (k === 0) === (abSide === 'a') ? 1 : 0;
    s.connect(g).connect(abCtx.destination);
    s.start(0, abOffset);
    abSrc.push(s); abGain.push(g);
  });
  abStart = abCtx.currentTime - abOffset;
  abPlaying = true;
  $('#abPlay use').setAttribute('href', '#i-pause');
  $('#abPlay').classList.add('on');
  $('#reels').classList.add('spin');
}
function stopAb() {
  abSrc.forEach((s) => { try { s.stop(); } catch {} });
  abSrc = []; abGain = [];
  abPlaying = false;
  const u = $('#abPlay use'); if (u) u.setAttribute('href', '#i-play');
  $('#abPlay').classList.remove('on');
  $('#reels').classList.remove('spin');
}

// ------------------------------------------------------------------ diagnostics
const DIAG_MSG = {
  device: (c) => c.status === 'ok' ? 'ok' : c.data.problem ? 'problem' : c.data.disabled ? 'disabled' : c.status === 'warn' ? 'noEndpoint' : 'missing',
  driver: (c) => c.status === 'ok' ? 'ok' : c.data.version ? 'outdated' : 'missing',
  autostart: (c) => (c.data.on ? 'on' : 'off'),
};
let lastDiag = null;
async function runDiag() {
  const list = $('#diagList'), btn = $('#diagRun');
  btn.disabled = true;
  list.innerHTML = '';
  for (let i = 0; i < 5; i++) list.append(el('div', { class: 'skeleton' }));
  const sum = $('#diagSummary');
  sum.className = 'diag-summary';
  $('#diagLamp').className = 'lamp big warn';
  $('h3', sum).textContent = t('diag.running');
  try {
    lastDiag = await api('diagnostics', {});
    renderDiag();
  } catch (e) {
    list.innerHTML = '';
    toast(t('diag.failed'), true);
  }
  btn.disabled = false;
}
function diagText(c) {
  const variant = DIAG_MSG[c.id] ? DIAG_MSG[c.id](c) : c.status;
  const data = Object.assign({}, c.data);
  for (const k in data) if (typeof data[k] === 'number') data[k] = nf(data[k], Number.isInteger(data[k]) ? 0 : 1);
  if (c.id === 'privacy' && c.data.scope) data.scope = t('privacy.' + c.data.scope);
  let s = t('diag.' + c.id + '.' + variant, data);
  if (s === 'diag.' + c.id + '.' + variant) s = t('diag.' + c.id + '.' + c.status, data);
  return s;
}
function renderDiag() {
  const d = lastDiag, list = $('#diagList');
  if (!d) return;
  list.innerHTML = '';
  const order = { fail: 0, warn: 1, ok: 2, info: 3 };
  const checks = d.checks.slice().sort((a, b) => order[a.status] - order[b.status]);
  for (const c of checks) {
    const row = el('div', { class: 'dg ' + c.status },
      el('i', { class: 'lamp ' + c.status }),
      el('div', null, el('b', { text: t('diag.' + c.id + '.title') }), el('p', { text: diagText(c) })));
    if (c.fix) {
      const b = el('button', { class: 'key small' + (c.status === 'fail' ? ' primary' : ''), type: 'button', text: t('fix.' + c.fix, { arg: c.fixArg }) });
      b.onclick = async () => {
        b.disabled = true;
        if (['install', 'attach', 'restartAudio', 'enableSysfx'].includes(c.fix)) await driverAction(c.fix);
        else { try { const r = await api('fix', { id: c.fix, arg: c.fixArg }); toast(r.ok ? t('toast.fixed') : t('toast.fixFailed'), !r.ok); } catch { toast(t('toast.fixFailed'), true); } }
        await sleep(600);
        runDiag();
      };
      row.append(b);
    } else row.append(el('span'));
    list.append(row);
  }
  const sum = $('#diagSummary');
  const st = d.fails ? 'fail' : d.warns ? 'warn' : 'ok';
  sum.className = 'diag-summary ' + st;
  $('#diagLamp').className = 'lamp big ' + st;
  $('h3', sum).textContent = st === 'ok' ? t('diag.allGood') : t('diag.found', { fails: d.fails, warns: d.warns });
  $('#diagBadge').hidden = !d.fails;
}
function diagReport() {
  if (!lastDiag) return '';
  return ['C01U Pro Studio ' + (S.state ? S.state.version : '') + ' — ' + (S.state ? S.state.os : ''), '']
    .concat(lastDiag.checks.map((c) => '[' + c.status.toUpperCase() + '] ' + t('diag.' + c.id + '.title') + ': ' + diagText(c))).join('\n');
}

// ------------------------------------------------------------------ driver actions
async function driverAction(action, extra) {
  const msg = { install: 'drv.installing', attach: 'drv.attaching', restartAudio: 'drv.restarting', enableSysfx: 'drv.enabling', uninstall: 'drv.removing' }[action];
  const close = openModal((box) => {
    box.append(el('h2', { text: t(msg) }), action === 'enableSysfx' ? null : el('p', { class: 'muted', text: t('drv.uac') }),
      el('div', { class: 'wiz-big' }, ringSvg()));
  }, { sticky: true });
  spinRing();
  let r = null;
  try { r = await api('driver', Object.assign({ action }, extra || {})); } catch (e) { r = e.data || { ok: false, code: -1 }; }
  close();
  if (r && r.code === -2) { toast(t('drv.declined'), true); return r; }
  if (r && r.ok) {
    toast(t('drv.ok.' + action));
    if (r.restart || r.removed) { waitForRestart(r.removed); return r; }
  } else {
    toast(t('drv.fail', { code: r ? r.code : '?' }), true);
  }
  await refreshState();
  return r;
}
async function installDriver() {
  if (!(await confirmBox(t('drv.installTitle'), t('drv.installText'), t('set.install')))) return;
  const r = await driverAction('install');
  if (r && r.ok && !r.restart) setTimeout(() => go('diag'), 300);
}
async function waitForRestart(removed) {
  openModal((box) => box.append(el('h2', { text: t(removed ? 'drv.removedTitle' : 'drv.restartTitle') }), el('p', { class: 'muted', text: t(removed ? 'drv.removedText' : 'drv.restartText') })), { sticky: true });
  if (removed) return;
  for (let i = 0; i < 40; i++) {
    await sleep(1000);
    try { await api('status'); location.reload(); return; } catch {}
  }
}
// progress shown on a row of amber LEDs
let ringTimer;
function ringSvg() { return el('div', { class: 'led-bar' }, Array.from({ length: 16 }, () => el('i'))); }
function setRing(frac) {
  const segs = $$('.led-bar i');
  const n = Math.round(clamp(frac, 0, 1) * segs.length);
  segs.forEach((s, i) => s.classList.toggle('on', i < n));
}
function spinRing() {
  clearInterval(ringTimer);
  let k = 0;
  ringTimer = setInterval(() => {
    const segs = $$('.led-bar i');
    if (!segs.length) { clearInterval(ringTimer); return; }
    k = (k + 1) % (segs.length * 2 - 2);
    const pos = k < segs.length ? k : segs.length * 2 - 2 - k;
    segs.forEach((s, i) => s.classList.toggle('on', Math.abs(i - pos) <= 1));
  }, 70);
}

// ------------------------------------------------------------------ smart setup wizard
async function measure(seconds, onTick, relearn) {
  const start = performance.now();
  const timer = setInterval(() => onTick(clamp((performance.now() - start) / (seconds * 1000), 0, 1)), 50);
  try { return await api('measure', { seconds, relearn }); } finally { clearInterval(timer); onTick(1); }
}
function recommend(noise, voice) {
  const p = deepCopy(S.params), notes = [];
  const loud = voice.loudness > -69 ? voice.loudness : voice.speech;
  let gain = clamp(Math.round((-20 - loud) * 2) / 2, -10, 24);
  if (voice.peak + gain > -3) gain = Math.round((-3 - voice.peak) * 2) / 2;
  p.inputGainDb = clamp(gain, -20, 30);
  notes.push(t('wiz.r.gain', { v: fmtDb(p.inputGainDb) }));
  const n = noise.floor + p.inputGainDb;
  p.nsOn = true;
  p.nsReductionDb = n <= -75 ? 10 : n <= -65 ? 14 : n <= -55 ? 20 : n <= -45 ? 26 : 32;
  p.nsSensitivity = voice.speech - noise.floor < 25 ? 0.65 : 0.5;
  notes.push(t('wiz.r.ns', { v: p.nsReductionDb, floor: nf(noise.floor, 0) }));
  const snr = voice.speech - noise.floor;
  p.gateOn = snr < 30; p.gateAuto = true;
  if (p.gateOn) notes.push(t('wiz.r.gate'));
  const hum = Math.max(noise.hum50, noise.hum60);
  if (hum > 12) { p.humOn = true; p.humFreq = noise.hum60 > noise.hum50 ? 60 : 50; notes.push(t('wiz.r.hum', { hz: p.humFreq })); }
  p.hpfOn = true; p.hpfFreq = voice.low > -6 ? 100 : voice.low > -12 ? 90 : 80;
  p.popOn = true; p.popAmount = voice.low > -6 ? 0.7 : 0.5;
  notes.push(t('wiz.r.hpf', { v: p.hpfFreq }));
  p.deessOn = true; p.deessAmount = voice.sibilance > -12 ? 0.6 : voice.sibilance > -18 ? 0.4 : 0.25;
  notes.push(t('wiz.r.deess', { v: fmtPct(p.deessAmount) }));
  const b = voice.bands || [];
  const tilt = ((b[7] + b[8]) - (b[4] + b[5])) / 2;
  p.warmth = 0; p.clarity = 0.1; p.air = 0;
  if (tilt < -22) { p.clarity = 0.35; p.air = 0.3; notes.push(t('wiz.r.dark')); }
  else if (tilt > -10) { p.warmth = 0.2; p.air = -0.2; notes.push(t('wiz.r.bright')); }
  else notes.push(t('wiz.r.balanced'));
  p.compOn = true; p.compRatio = 3; p.compAutoMakeup = true;
  p.compThresholdDb = clamp(Math.round(loud + p.inputGainDb - 4), -40, -10);
  notes.push(t('wiz.r.comp', { v: p.compThresholdDb }));
  p.agcOn = loud + p.inputGainDb < -26; if (p.agcOn) notes.push(t('wiz.r.agc'));
  p.limOn = true; p.limCeilingDb = -1;
  if (voice.clipped > 0) notes.push(t('wiz.r.clip'));
  if (voice.pitch > 0) notes.push(t('wiz.r.pitch', { v: nf(voice.pitch, 0), note: noteName(voice.pitch) }));
  return { p, notes };
}
function wizard(first) {
  let step = 0, noise = null, voice = null, rec = null;
  const steps = 4;
  openModal((box, close) => {
    const draw = () => {
      box.innerHTML = '';
      const bar = el('div', { class: 'wiz-steps' });
      for (let i = 0; i < steps; i++) bar.append(el('i', { class: i <= (step === 5 ? 0 : step) ? 'done' : '' }));
      box.append(bar);
      if (step === 0) {
        box.append(el('h2', { text: t(first ? 'wiz.welcome' : 'wiz.title') }), el('p', { class: 'muted', text: t('wiz.intro') }));
        if (first) {
          const langSel = el('select', null, LANGS.map(([c, n]) => el('option', { value: c, text: n })));
          langSel.value = LANG;
          langSel.onchange = async () => { await switchLang(langSel.value); draw(); };
          box.append(el('div', { class: 'field' }, el('label', { text: t('set.language') }), langSel));
        }
        box.append(el('div', { class: 'modal-actions' },
          el('button', { class: 'key', type: 'button', text: t(first ? 'wiz.skip' : 'common.cancel'), onclick: () => { close(); } }),
          el('button', { class: 'key primary', type: 'button', text: t('wiz.start'), onclick: () => {
            if (first && S.status && S.status.driver && !S.status.driver.installed) { step = 5; draw(); }
            else { step = 1; draw(); runNoise(); }
          } })));
      } else if (step === 5) {
        box.append(el('h2', { text: t('wiz.driverTitle') }), el('p', { class: 'muted', text: t('wiz.driverText') }),
          el('div', { class: 'modal-actions' },
            el('button', { class: 'key', type: 'button', text: t('wiz.later'), onclick: () => { step = 1; draw(); runNoise(); } }),
            el('button', { class: 'key primary', type: 'button', text: t('set.install'), onclick: () => { close(); installDriver(); } })));
      } else if (step === 1) {
        box.append(el('h2', { text: t('wiz.silenceTitle') }), el('p', { class: 'muted', text: t('wiz.silenceText') }), el('div', { class: 'wiz-big' }, ringSvg()));
      } else if (step === 2) {
        box.append(el('h2', { text: t('wiz.speakTitle') }), el('p', { class: 'muted', text: t('wiz.speakText') }),
          el('div', { class: 'paper-strip', text: t('ab.phrase') }), el('div', { class: 'wiz-big' }, ringSvg()));
      } else if (step === 3) {
        rec = recommend(noise, voice);
        const ul = el('ul', { class: 'wiz-list' }, rec.notes.map((n) => el('li', null, icon('check'), el('span', { text: n }))));
        box.append(el('h2', { text: t('wiz.doneTitle') }), el('p', { class: 'muted', text: t('wiz.doneText') }), ul,
          el('div', { class: 'modal-actions' },
            el('button', { class: 'key', type: 'button', text: t('common.cancel'), onclick: () => close() }),
            el('button', { class: 'key primary', type: 'button', text: t('wiz.apply'), onclick: () => { setParams(rec.p, 'custom'); close(); toast(t('wiz.applied')); renderAll(); } })));
      } else if (step === 9) {
        box.append(el('h2', { text: t('wiz.errorTitle') }), el('p', { class: 'muted', text: t('wiz.errorText') }),
          el('div', { class: 'modal-actions' }, el('button', { class: 'key primary', type: 'button', text: t('common.close'), onclick: () => close() })));
      }
    };
    const runNoise = async () => {
      try {
        await sleep(600);
        noise = await measure(4, setRing, true);
        step = 2; draw();
        await sleep(1200);
        voice = await measure(7, setRing);
        step = 3; draw();
      } catch (e) { step = 9; draw(); }
    };
    draw();
  }, { sticky: true, onClose: () => { if (first) saveSettings({ firstRun: false }); } });
}

// ------------------------------------------------------------------ guide
const GUIDE = ['start', 'position', 'monitor', 'apps', 'features', 'hotkeys', 'trouble', 'tech', 'remove'];
function renderGuide() {
  const host = $('#guide');
  host.innerHTML = '';
  host.append(el('div', { class: 'manual-title' }, el('b', { text: 'C01U Pro Studio' }), el('span', { text: t('nav.guide') + ' · Samson C01U Pro' })));
  GUIDE.forEach((k, i) => {
    const items = t('guide.' + k + '.items');
    const list = el('ul', null, (Array.isArray(items) ? items : [items]).map((s) => {
      const li = el('li');
      // **bold** fragments
      String(s).split(/(\*\*[^*]+\*\*)/).forEach((part) => li.append(part.startsWith('**') ? el('b', { text: part.slice(2, -2) }) : part));
      return li;
    }));
    host.append(el('section', null, el('h3', null, el('span', { class: 'num', text: String(i + 1) }), t('guide.' + k + '.title')), list));
  });
}

// ------------------------------------------------------------------ settings page
function keyName(vk) {
  if (vk >= 0x30 && vk <= 0x5a) return String.fromCharCode(vk);
  if (vk >= 0x70 && vk <= 0x87) return 'F' + (vk - 0x6f);
  const names = { 0x20: 'Space', 0x13: 'Pause', 0x91: 'ScrollLock', 0x2d: 'Insert', 0x24: 'Home', 0x23: 'End', 0x21: 'PageUp', 0x22: 'PageDown' };
  return names[vk] || '#' + vk;
}
function hotkeyText(mods, vk) {
  const p = [];
  if (mods & 2) p.push('Ctrl'); if (mods & 1) p.push('Alt'); if (mods & 4) p.push('Shift'); if (mods & 8) p.push('Win');
  p.push(keyName(vk));
  return p.join(' + ');
}
let listening = false;
function refreshSettings() {
  if (!S.settings) return;
  const s = S.settings;
  const langSel = $('#setLang');
  if (!langSel.options.length) {
    for (const [c, n] of LANGS) langSel.append(el('option', { value: c, text: n }));
    langSel.onchange = () => switchLang(langSel.value);
  }
  langSel.value = LANG;
  if (!$('#setTheme').dataset.bound) { $('#setTheme').dataset.bound = 1; bindSeg($('#setTheme'), () => S.settings.theme, (v) => { saveSettings({ theme: v }); S.settings.theme = v; applyTheme(); }); }
  $$('#setTheme button').forEach((b) => b.classList.toggle('on', b.dataset.v === s.theme));
  $('#setAutostart').checked = !!(S.status && S.status.autostart);
  $('#setBeep').checked = !!s.muteBeep;
  $('#setHotkeyOn').checked = !!s.hotkeyOn;
  if (!listening) $('#setHotkey').textContent = hotkeyText(s.hotkeyMods, s.hotkeyVk);
  if (S.devices && document.activeElement !== $('#setMic')) {
    fillDeviceSelect($('#setMic'), S.devices.inputs, s.micId, t('set.micAuto'));
    fillDeviceSelect($('#setRouteDev'), S.devices.outputs, s.routeDevice, t('set.routeNone'));
    if (document.activeElement !== $('#monDev')) fillDeviceSelect($('#monDev'), S.devices.outputs, s.monitorDevice, t('common.defaultDevice'));
  }
  $('#setRouteOn').checked = !!s.routeOn;
  $$('#monSrc button').forEach((b) => b.classList.toggle('on', b.dataset.v === (s.monitorRaw ? '1' : '0')));
  const a = $('#about');
  a.innerHTML = '';
  if (S.state) {
    a.append(el('div', null, el('b', { text: 'C01U Pro Studio ' + S.state.version })), el('div', { text: S.state.os }),
      el('div', { text: t('set.unofficial') }),
      el('a', { href: '#', text: t('set.openLog'), onclick: (e) => { e.preventDefault(); api('fix', { id: 'openLog' }); } }));
  }
}
function renderDriverStatus() {
  const host = $('#drvStatus');
  if (!host || !S.status) return;
  const d = S.status.driver || {}, dev = S.status.device || {};
  host.innerHTML = '';
  const line = (k, v, cls) => host.append(el('div', { class: 'drv-line' }, el('span', { text: t(k) }), el('span', { class: 'chip ' + (cls || ''), text: v })));
  line('drv.state', d.active ? t('drv.stActive') : d.installed ? (d.attached ? t('drv.stWaiting') : t('drv.stNotAttached')) : t('drv.stNotInstalled'), d.active ? 'ok' : d.installed ? 'warn' : '');
  if (d.version) line('drv.version', d.version);
  line('drv.mic', dev.found ? dev.name : '—');
  if (d.latencyMs) line('drv.latency', nf(d.latencyMs, 1) + ' ms');
  if (d.load != null && d.active) line('drv.cpu', nf(d.load, 2) + '%');
  $('#drvInstall span').textContent = t(d.installed ? 'set.reinstall' : 'set.install');
  $('#drvAttach').disabled = !d.installed;
  $('#drvRemove').disabled = !d.installed;
}

// ------------------------------------------------------------------ updates
const UPD_BUSY = ['checking', 'downloading', 'installing'];
function updateText(u) {
  const v = { v: u.latest || '', e: u.error || '?' };
  const map = { current: ['upd.st.current', 'ok'], pending: ['upd.st.pending', 'warn'], checking: ['upd.st.checking', 'warn'],
    downloading: ['upd.st.downloading', 'warn'], installing: ['upd.st.installing', 'warn'], updated: ['upd.st.updated', 'ok'],
    error: ['upd.st.error', 'fail'] };
  const m = map[u.status];
  return m ? [t(m[0], v), m[1]] : [t('upd.never'), ''];
}
function renderUpdate() {
  const u = S.update, host = $('#updStatus');
  if (!u || !host) return;
  host.innerHTML = '';
  const line = (k, v, cls) => host.append(el('div', { class: 'drv-line' }, el('span', { text: t(k) }), el('span', { class: 'chip ' + (cls || ''), text: v })));
  line('drv.version', u.current);
  const [text, cls] = updateText(u);
  line('upd.state', text, cls);
  if (u.checked) line('upd.checked', new Date(u.checked * 1000).toLocaleString(LANG, { dateStyle: 'medium', timeStyle: 'short' }));
  $('#updAuto').checked = !!u.auto;
  $('#updCheck').disabled = UPD_BUSY.includes(u.status);
  const notes = $('#updNotes');
  notes.hidden = !u.page;
  if (u.page) notes.href = u.page;
}
async function checkUpdates() {
  const before = S.update ? S.update.checked : 0;
  let r;
  try { r = await api('update', { check: true }); } catch { toast(t('upd.failed'), true); return; }
  toast(t(r.elevated ? 'upd.uac' : 'upd.started'));
  S.update = Object.assign(S.update || {}, { status: 'checking' });
  renderUpdate();
  // follow the task; once it installs a new version the app restarts and the page reloads
  for (let i = 0; i < 300; i++) {
    await sleep(1500);
    try { S.update = await api('update'); } catch { continue; }
    renderUpdate();
    if (!UPD_BUSY.includes(S.update.status) && S.update.checked !== before) return;
  }
}
function announceUpdate() {
  const u = S.update, v = S.state && S.state.version;
  if (!u || u.status !== 'updated' || u.latest !== v) return;
  try {
    if (localStorage.getItem('c01u.updateSeen') === v) return;
    localStorage.setItem('c01u.updateSeen', v);
  } catch { return; }
  toast(t('upd.toast', { v }));
}
function setupSettings() {
  $('#setAutostart').onchange = (e) => saveSettings({ autostart: e.target.checked });
  $('#setBeep').onchange = (e) => saveSettings({ muteBeep: e.target.checked });
  $('#setHotkeyOn').onchange = (e) => saveSettings({ hotkeyOn: e.target.checked });
  $('#setMic').onchange = (e) => saveSettings({ micId: e.target.value });
  $('#setRouteOn').onchange = (e) => saveSettings({ routeOn: e.target.checked });
  $('#setRouteDev').onchange = (e) => saveSettings({ routeDevice: e.target.value });
  $('#setHotkey').onclick = () => {
    const b = $('#setHotkey');
    listening = true; b.classList.add('listening'); b.textContent = t('set.pressKeys');
    const onKey = (e) => {
      e.preventDefault();
      if (['Control', 'Alt', 'Shift', 'Meta'].includes(e.key)) return;
      document.removeEventListener('keydown', onKey, true);
      listening = false; b.classList.remove('listening');
      if (e.key === 'Escape') { refreshSettings(); return; }
      const mods = (e.altKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.shiftKey ? 4 : 0) | (e.metaKey ? 8 : 0);
      let vk = e.keyCode;
      if (!mods) { toast(t('set.hotkeyNeedsMod'), true); refreshSettings(); return; }
      saveSettings({ hotkeyMods: mods, hotkeyVk: vk, hotkeyOn: true });
      b.textContent = hotkeyText(mods, vk);
    };
    document.addEventListener('keydown', onKey, true);
  };
  $('#drvInstall').onclick = installDriver;
  $('#drvAttach').onclick = () => driverAction('attach', { all: true });
  $('#drvRestart').onclick = () => driverAction('restartAudio');
  $('#drvRemove').onclick = async () => { if (await confirmBox(t('drv.removeTitle'), t('drv.removeText'), t('set.uninstall'), true)) driverAction('uninstall'); };
  $('#updAuto').onchange = async (e) => { try { S.update = await api('update', { auto: e.target.checked }); renderUpdate(); } catch {} };
  $('#updCheck').onclick = checkUpdates;
  $('#setImport').onchange = async (e) => {
    const f = e.target.files[0]; e.target.value = '';
    if (!f) return;
    try { const st = await api('import', JSON.parse(await f.text())); applyState(st); renderAll(); toast(t('toast.imported')); }
    catch { toast(t('toast.importFailed'), true); }
  };
  $('#setReset').onclick = async () => { if (await confirmBox(t('set.reset'), t('set.resetText'), t('set.reset'), true)) { applyPreset('c01u'); } };
  $('#monOn').onchange = (e) => { saveSettings({ monitorOn: e.target.checked }); if (e.target.checked) toast(t('ov.monitorWarn')); };
  $('#monDev').onchange = (e) => saveSettings({ monitorDevice: e.target.value });
  bindSeg($('#monSrc'), () => (S.settings && S.settings.monitorRaw ? '1' : '0'), (v) => saveSettings({ monitorRaw: v === '1' }));
}

// ------------------------------------------------------------------ refresh after param change
function refreshParams(except) {
  if (!S.params) return;
  for (const s of sliders.concat(toneSliders, [fxMix, fxParam].filter(Boolean))) if (s.range !== except) s.update();
  for (const inp of $$('input[type=checkbox][data-param]')) inp.checked = !!S.params[inp.dataset.param];
  for (const m of MODULES) { const c = $('#mod-' + m.id); if (c && m.on) c.classList.toggle('off', !S.params[m.on]); }
  $$('#chain button').forEach((b, i) => {
    const id = CHAIN[i];
    const onKey = id === 'eq' ? 'eqOn' : (MODULES.find((m) => m.id === id) || {}).on;
    b.classList.toggle('on', id === 'gain' || (id === 'fx' ? S.params.fxType > 0 : !!S.params[onKey]));
  });
  $('#btnBypass').classList.toggle('on', !S.params.bypass);
  $('#eqOn').checked = !!S.params.eqOn;
  drawCompGraph();
  drawEq();
}

function renderAll() {
  applyI18n();
  $('#pageTitle').textContent = t('nav.' + S.page);
  renderOverview();
  renderGr();
  renderProcessing();
  renderEq();
  renderFx();
  renderAb();
  renderGuide();
  refreshSettings();
  renderUpdate();
  applyStatus(S.status);
  if (lastDiag) renderDiag();
}

// ------------------------------------------------------------------ live data
function connectEvents() {
  const es = new EventSource('/api/events');
  es.addEventListener('m', (e) => {
    try {
      S.meters = JSON.parse(e.data);
      updateAbProgress(S.meters.rec);
    } catch {}
  });
  es.addEventListener('s', (e) => { try { applyStatus(JSON.parse(e.data)); } catch {} });
  es.onerror = () => { S.meters = null; };
  // back after a restart: a different version (an update) brings a different page
  let opened = false;
  es.onopen = () => {
    if (opened) api('state').then((st) => { if (st.version !== S.state.version) location.reload(); }).catch(() => {});
    opened = true;
  };
}
let lastCompDraw = 0;
function frame(now) {
  if (S.page === 'overview') { drawMeters(now); drawSpectrum(); }
  else if (S.page === 'eq') { drawSpectrum(); if (!eqDrag) drawEq(); }
  else if (S.page === 'processing' && now - lastCompDraw > 60) { lastCompDraw = now; drawCompGraph(); }
  else if (S.page === 'ab' && abPlaying) drawAbWave();
  requestAnimationFrame(frame);
}
async function refreshState() {
  try { const st = await api('state'); applyState(st, true); S.params = st.params; refreshParams(); } catch {}
}

// ------------------------------------------------------------------ boot
async function boot() {
  let st;
  for (;;) { try { st = await api('state'); break; } catch { await sleep(800); } }
  await loadLang(st.settings.language || st.sysLang || 'en');
  applyState(st);
  setupSettings();
  setupEqCanvas();
  $$('#nav a').forEach((a) => a.addEventListener('click', () => go(a.dataset.page)));
  $('#menuBtn').onclick = () => $('#side').classList.toggle('open');
  $('#langBtn').onclick = langModal;
  $('#drvPill').onclick = () => go('settings');
  $('#btnMute').onclick = () => { setParam('mute', !S.params.mute, { keepPreset: true }); S.status.muted = S.params.mute; applyStatus(S.status); };
  $('#btnBypass').onclick = () => { setParam('bypass', !S.params.bypass, { keepPreset: true }); S.status.bypass = S.params.bypass; applyStatus(S.status); toast(t(S.params.bypass ? 'toast.bypassOn' : 'toast.bypassOff')); };
  $('#btnMonitor').onclick = () => { const v = !S.settings.monitorOn; saveSettings({ monitorOn: v }); if (v) toast(t('ov.monitorWarn')); };
  $('#btnTheme').onclick = () => {
    const dark = document.documentElement.getAttribute('data-theme') === 'dark' || (!document.documentElement.getAttribute('data-theme') && matchMedia('(prefers-color-scheme: dark)').matches);
    S.settings.theme = dark ? 'light' : 'dark'; applyTheme(); saveSettings({ theme: S.settings.theme });
  };
  $('#btnWizard').onclick = () => wizard(false);
  $('#btnSavePreset').onclick = saveUserPreset;
  $('#diagRun').onclick = runDiag;
  $('#diagCopy').onclick = async () => { if (!lastDiag) await runDiag(); try { await navigator.clipboard.writeText(diagReport()); toast(t('toast.copied')); } catch { toast(t('toast.copyFailed'), true); } };
  bindSeg($('#specMode'), () => specMode, (v) => { specMode = v; waterfall = null; });
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', applyTheme);
  window.addEventListener('resize', debounce(resizeCanvases, 100));
  renderAll();
  go((location.hash || '#overview').slice(1));
  connectEvents();
  requestAnimationFrame(frame);
  setInterval(() => { if (!S.dragging && document.visibilityState === 'visible') api('devices').then((d) => { S.devices = d; refreshSettings(); }).catch(() => {}); }, 5000);
  if (st.settings.firstRun) setTimeout(() => wizard(true), 400);
  else announceUpdate();
}
boot();
