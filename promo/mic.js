// 3D-модель Samson C01U Pro по фотографиям: корпус цвета шампань сужается книзу, голова —
// плетёная ромбом сетка со скруглённым верхом и рамкой по контуру, надписи SAMSON / C01UPRO,
// зелёный светодиод, две канавки, значок наушников и гнездо, чёрная резиновая пятка, трипод.
// Единицы: ширина головы = 1. Ноль по высоте — стык головы и корпуса.
import * as THREE from 'three';

const R = 0.5;          // радиус головы и верха корпуса
const HG = 0.4;         // прямая часть стенок головы (над ней скругление радиусом R)
const BODY = 2.55;      // длина корпуса
const radiusAt = (d) => R - 0.1 * Math.pow(d / BODY, 1.6);  // d — расстояние вниз от верха корпуса

function canvas(w, h) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  return [c, c.getContext('2d')];
}

// Корпус: развёртка по окружности (u) и высоте (v), подписи — по центру развёртки (перед микрофона).
function bodyTexture() {
  const W = 4096, H = 2048;
  const [c, g] = canvas(W, H);
  g.fillStyle = '#b5ae9f';
  g.fillRect(0, 0, W, H);
  for (let i = 0; i < 9000; i++) {  // лёгкая сатиновая неровность
    g.fillStyle = `rgba(${i % 2 ? 255 : 0},${i % 2 ? 255 : 0},${i % 2 ? 255 : 0},0.025)`;
    g.fillRect((i * 733) % W, (i * 1291) % H, 3, 1 + (i % 5));
  }
  // координаты в единицах модели: x вдоль окружности у верха корпуса, y вниз от верха
  const px = W / (2 * Math.PI * R), py = H / BODY;
  const X = (u) => W / 2 + u * px, Y = (d) => d * py;
  // две канавки
  for (const d of [0.47, 0.53]) {
    g.fillStyle = 'rgba(40,36,30,0.55)'; g.fillRect(0, Y(d) - 3, W, 5);
    g.fillStyle = 'rgba(255,250,240,0.35)'; g.fillRect(0, Y(d) + 3, W, 3);
  }
  // SAMSON
  g.save();
  g.translate(X(0), Y(0.15));
  g.scale(1, py / px);
  g.fillStyle = '#26231f';
  g.font = `900 ${0.095 * px}px "Arial Black", "Segoe UI Black", sans-serif`;
  g.textAlign = 'center'; g.textBaseline = 'middle';
  g.fillText('SAMSON', 0, 0);
  g.restore();
  // C01UPRO (U — жирная)
  g.save();
  g.translate(X(0), Y(0.355));
  g.scale(1, py / px);
  g.fillStyle = '#2b2824';
  g.textBaseline = 'middle';
  const parts = [['C01', '500'], ['U', '800'], ['PRO', '500']];
  const size = 0.075 * px;
  const widths = parts.map(([s, w]) => { g.font = `${w} ${size}px "Segoe UI", Arial, sans-serif`; return g.measureText(s).width; });
  let x = -widths.reduce((a, b) => a + b, 0) / 2;
  parts.forEach(([s, w], i) => { g.font = `${w} ${size}px "Segoe UI", Arial, sans-serif`; g.fillText(s, x, 0); x += widths[i]; });
  g.restore();
  // значок наушников над гнездом
  g.save();
  g.translate(X(0), Y(1.6));
  g.scale(1, py / px);
  g.strokeStyle = '#2b2824'; g.fillStyle = '#2b2824'; g.lineWidth = 0.011 * px; g.lineCap = 'round';
  const hr = 0.045 * px;
  g.beginPath(); g.arc(0, 0, hr, Math.PI, 0); g.stroke();
  g.fillRect(-hr - 0.012 * px, -0.004 * px, 0.022 * px, 0.04 * px);
  g.fillRect(hr - 0.01 * px, -0.004 * px, 0.022 * px, 0.04 * px);
  g.restore();
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 16;
  return tex;
}

// Сетка: две системы проволок по диагоналям, «через одну» сверху — как плетение.
function meshTextures() {
  const S = 1024, step = 64, wire = 12;  // 1024 / 64 — целое число ячеек, текстура без шва
  const [ca, ga] = canvas(S, S);
  const [cc, gc] = canvas(S, S);
  ga.fillStyle = '#000'; ga.fillRect(0, 0, S, S);
  gc.fillStyle = '#6d6a64'; gc.fillRect(0, 0, S, S);
  const line = (g, x0, y0, x1, y1, w, style) => { g.strokeStyle = style; g.lineWidth = w; g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.stroke(); };
  for (let k = -S; k <= 2 * S; k += step) {
    line(ga, k, 0, k + S, S, wire, '#fff');
    line(ga, k, S, k + S, 0, wire, '#fff');
    line(gc, k, 0, k + S, S, wire, '#d9d6cf');
    line(gc, k, S, k + S, 0, wire, '#bdb9b1');
    line(gc, k, 0, k + S, S, 2, 'rgba(255,255,255,0.6)');
  }
  // тени у перекрёстков: проволока уходит «под» соседнюю
  gc.fillStyle = 'rgba(0,0,0,0.35)';
  for (let y = 0; y < S; y += step / 2) {
    for (let x = ((y / (step / 2)) % 2) * (step / 2); x < S; x += step) gc.fillRect(x - 4, y - 4, 8, 8);
  }
  const alpha = new THREE.CanvasTexture(ca), color = new THREE.CanvasTexture(cc);
  for (const t of [alpha, color]) { t.wrapS = t.wrapT = THREE.RepeatWrapping; t.anisotropy = 16; }
  color.colorSpace = THREE.SRGBColorSpace;
  return { alpha, color };
}

// Голова: вертикальные стенки до y = HG + R·|sinθ| и верх — горизонтальный полуцилиндр (ось X).
// Спереди силуэт плоский со скруглёнными углами, сбоку — полукруг, как у настоящего C01U Pro.
function headGeometry(radius, uvTile) {
  const pos = [], uv = [], idx = [];
  const add = (x, y, z, u, v) => { pos.push(x, y, z); uv.push(u, v); return pos.length / 3 - 1; };
  const NT = 160, NV = 24;
  const base = 0;
  for (let i = 0; i <= NT; i++) {
    const th = (i / NT) * Math.PI * 2;
    const top = HG + radius * Math.abs(Math.sin(th));
    for (let j = 0; j <= NV; j++) {
      const y = (j / NV) * top;
      add(Math.sin(th) * radius, y, Math.cos(th) * radius, (th * radius) / uvTile, y / uvTile);
    }
  }
  for (let i = 0; i < NT; i++) for (let j = 0; j < NV; j++) {
    const a = base + i * (NV + 1) + j, b = a + NV + 1;
    idx.push(a, b, a + 1, b, b + 1, a + 1);
  }
  const top0 = pos.length / 3, NA = 96, NU = 48;
  for (let i = 0; i <= NA; i++) {
    const al = (i / NA) * Math.PI;
    const s = Math.sin(al);
    for (let j = 0; j <= NU; j++) {
      const u = (j / NU) * 2 - 1;
      const x = u * radius * s;
      add(x, HG + radius * s, radius * Math.cos(al), x / uvTile, (al * radius) / uvTile);
    }
  }
  for (let i = 0; i < NA; i++) for (let j = 0; j < NU; j++) {
    const a = top0 + i * (NU + 1) + j, b = a + NU + 1;
    idx.push(a, a + 1, b, b, a + 1, b + 1);
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  geo.setIndex(idx);
  geo.computeVertexNormals();
  return geo;
}

export function buildMic() {
  const mic = new THREE.Group();
  const satin = new THREE.MeshStandardMaterial({ color: 0xffffff, map: bodyTexture(), metalness: 0.78, roughness: 0.36 });
  const trim = new THREE.MeshStandardMaterial({ color: 0xb8b1a2, metalness: 0.85, roughness: 0.3 });
  const rubber = new THREE.MeshStandardMaterial({ color: 0x141414, roughness: 0.85 });
  const blackStand = new THREE.MeshStandardMaterial({ color: 0x1b1b1c, metalness: 0.35, roughness: 0.5 });

  // корпус: точки снизу вверх и равномерно по высоте — тогда v текстуры линейна по высоте и
  // надписи ложатся на свои места (v = 1 — верх корпуса = верх холста); u = 0,5 — перед (+z)
  const prof = [];
  for (let i = 50; i >= 0; i--) {
    const d = (i / 50) * BODY;
    prof.push(new THREE.Vector2(radiusAt(d), -d));
  }
  const body = new THREE.Mesh(new THREE.LatheGeometry(prof, 160, -Math.PI, Math.PI * 2), satin);
  const foot = new THREE.Mesh(new THREE.CylinderGeometry(0.385, 0.37, 0.17, 96), rubber);
  foot.position.y = -BODY - 0.07;

  // голова
  const tile = (2 * Math.PI * R) / 3;  // целое число ячеек по кругу — без шва
  const { alpha, color } = meshTextures();
  const meshMat = new THREE.MeshStandardMaterial({ map: color, alphaMap: alpha, alphaToCoverage: true, metalness: 0.9, roughness: 0.35, side: THREE.DoubleSide });
  const head = new THREE.Mesh(headGeometry(R, tile), meshMat);
  const foam = new THREE.Mesh(headGeometry(R - 0.035, tile), new THREE.MeshStandardMaterial({ color: 0x1a1918, roughness: 1, side: THREE.DoubleSide }));
  const rc = 0.12;  // рамка по контуру головы в плоскости z = 0
  const fr = R + 0.012;
  const path = new THREE.CurvePath();
  path.add(new THREE.LineCurve3(new THREE.Vector3(-fr, -0.02, 0), new THREE.Vector3(-fr, HG + R - rc, 0)));
  path.add(new THREE.QuadraticBezierCurve3(new THREE.Vector3(-fr, HG + R - rc, 0), new THREE.Vector3(-fr, HG + R + 0.012, 0), new THREE.Vector3(-fr + rc, HG + R + 0.012, 0)));
  path.add(new THREE.LineCurve3(new THREE.Vector3(-fr + rc, HG + R + 0.012, 0), new THREE.Vector3(fr - rc, HG + R + 0.012, 0)));
  path.add(new THREE.QuadraticBezierCurve3(new THREE.Vector3(fr - rc, HG + R + 0.012, 0), new THREE.Vector3(fr, HG + R + 0.012, 0), new THREE.Vector3(fr, HG + R - rc, 0)));
  path.add(new THREE.LineCurve3(new THREE.Vector3(fr, HG + R - rc, 0), new THREE.Vector3(fr, -0.02, 0)));
  const frame = new THREE.Mesh(new THREE.TubeGeometry(path, 200, 0.03, 16, false), trim);
  const lip = new THREE.Mesh(new THREE.TorusGeometry(R - 0.004, 0.014, 12, 160), trim);
  lip.rotation.x = Math.PI / 2;

  // светодиод и гнездо
  const ledMat = new THREE.MeshStandardMaterial({ color: 0x0b2a08, emissive: new THREE.Color(0x58e03a), emissiveIntensity: 1.6, roughness: 0.2 });
  const led = new THREE.Mesh(new THREE.SphereGeometry(0.022, 24, 16, 0, Math.PI * 2, 0, Math.PI / 2), ledMat);
  led.rotation.x = Math.PI / 2;
  led.position.set(0, -0.255, radiusAt(0.255) - 0.004);
  const jackR = new THREE.Mesh(new THREE.TorusGeometry(0.038, 0.012, 12, 40), trim);
  jackR.position.set(0, -1.71, radiusAt(1.71) + 0.002);
  const jackH = new THREE.Mesh(new THREE.CircleGeometry(0.03, 32), new THREE.MeshBasicMaterial({ color: 0x0a0a0a }));
  jackH.position.set(0, -1.71, radiusAt(1.71) + 0.004);

  mic.add(body, foot, foam, head, frame, lip, led, jackR, jackH);

  // настольный трипод: держатель-чашка, стойка, три ноги с резиновыми наконечниками
  const stand = new THREE.Group();
  const cup = new THREE.Mesh(new THREE.CylinderGeometry(0.46, 0.43, 0.32, 80, 1, true), blackStand);
  cup.position.y = -BODY + 0.05;
  const cupRing = new THREE.Mesh(new THREE.TorusGeometry(0.455, 0.025, 12, 80), blackStand);
  cupRing.rotation.x = Math.PI / 2;
  cupRing.position.y = -BODY + 0.21;
  const stem = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.13, 0.5, 32), blackStand);
  stem.position.y = -BODY - 0.32;
  const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.16, 0.19, 0.22, 32), blackStand);
  hub.position.y = -BODY - 0.62;
  stand.add(cup, cupRing, stem, hub);
  for (let i = 0; i < 3; i++) {
    const a = (i / 3) * Math.PI * 2 + 0.5;
    const leg = new THREE.Group();
    const rod = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.045, 1.55, 16), blackStand);
    rod.position.y = -0.775;
    const tip = new THREE.Mesh(new THREE.CapsuleGeometry(0.06, 0.12, 6, 16), rubber);
    tip.position.y = -1.6;
    leg.add(rod, tip);
    leg.position.set(0, -BODY - 0.66, 0);
    leg.rotation.set(0, a, 0.95);
    stand.add(leg);
  }
  mic.add(stand);
  mic.traverse((o) => { if (o.isMesh) { o.castShadow = true; o.receiveShadow = true; } });
  mic.userData = { led: ledMat, head: HG + R, bottom: -BODY - 1.75 };
  return mic;
}
