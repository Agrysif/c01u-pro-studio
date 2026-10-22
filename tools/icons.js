// Icon artwork as SVG, in the panel's 1970s studio palette: cream enamel, bakelite, walnut,
// amber lamps. The microphone is the Samson C01U Pro silhouette (woven mesh head with a rounded
// top, champagne body, green LED, black foot) without the Samson logo.
// Used by tools/make_icons.js; `detail` drops fine parts for 16–24 px.
'use strict';

const DEFS = `
  <linearGradient id="body" x1="0" x2="1"><stop offset="0" stop-color="#7b7468"/><stop offset=".32" stop-color="#e6e0d3"/><stop offset=".55" stop-color="#c8c1b2"/><stop offset="1" stop-color="#7f786c"/></linearGradient>
  <linearGradient id="head" x1="0" x2="1"><stop offset="0" stop-color="#000" stop-opacity=".55"/><stop offset=".35" stop-color="#000" stop-opacity="0"/><stop offset=".65" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".55"/></linearGradient>
  <pattern id="mesh" width="6.5" height="6.5" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
    <rect width="6.5" height="6.5" fill="#2b2824"/><path d="M0 0H6.5M0 0V6.5" stroke="#e2ddd3" stroke-width="1.9"/>
  </pattern>
  <linearGradient id="enamel" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f4ecda"/><stop offset="1" stop-color="#d4c6a8"/></linearGradient>
  <radialGradient id="bakelite" cx=".5" cy=".38" r=".7"><stop offset="0" stop-color="#3a2f25"/><stop offset="1" stop-color="#120d09"/></radialGradient>
  <radialGradient id="vu" cx=".5" cy="1" r="1"><stop offset="0" stop-color="#ffe7b0"/><stop offset=".75" stop-color="#e8aa4c"/><stop offset="1" stop-color="#b8782a"/></radialGradient>
  <linearGradient id="walnut" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#7a4f2e"/><stop offset=".5" stop-color="#5a3820"/><stop offset="1" stop-color="#3e2614"/></linearGradient>
  <radialGradient id="ledGlow"><stop offset="0" stop-color="#9dff7a" stop-opacity=".9"/><stop offset="1" stop-color="#58e03a" stop-opacity="0"/></radialGradient>`;

// C01U Pro, front view, in a 100 × 170 box (head 8–64, body 64–160, foot 160–170)
function mic(detail) {
  const head = 'M20 64V25Q20 8 37 8H63Q80 8 80 25V64Z';
  return `
    <path d="${head}" fill="${detail ? 'url(#mesh)' : '#c9c3b6'}"/>
    ${detail ? `<path d="${head}" fill="url(#head)"/>` : '<path d="M33 18L67 52M33 52L67 18" stroke="#6f695e" stroke-width="5"/>'}
    <path d="${head}" fill="none" stroke="#efe9dd" stroke-width="${detail ? 3.2 : 5}"/>
    <path d="M20 64H80L76.5 154Q76 160 70 160H30Q24 160 23.5 154Z" fill="url(#body)"/>
    <path d="M20.5 65H79.5" stroke="#fffaf0" stroke-width="1.6" opacity=".8"/>
    ${detail ? `
    <rect x="37" y="71" width="26" height="3.4" rx="1" fill="#2d2a25"/>
    <rect x="40" y="85.5" width="20" height="2.4" rx="1" fill="#3b3731"/>
    <path d="M21.6 95H78.4M21.7 99.5H78.3" stroke="#5f594e" stroke-width="1.3"/>
    <path d="M21.6 96.3H78.4M21.7 100.8H78.3" stroke="#fff7ea" stroke-width=".9" opacity=".7"/>
    <circle cx="50" cy="143" r="4.2" fill="#2a2724" stroke="#e8e2d6" stroke-width="1.6"/>` : ''}
    <circle cx="50" cy="${detail ? 79 : 80}" r="${detail ? 7 : 9}" fill="url(#ledGlow)"/>
    <circle cx="50" cy="${detail ? 79 : 80}" r="${detail ? 2.6 : 4.5}" fill="#8dff66"/>
    <rect x="27" y="159" width="46" height="11" rx="2.5" fill="#141210"/>`;
}

// placed so the microphone of height h has its top at (cx, top); wide > 1 makes it chunkier for tiny sizes
const place = (cx, top, h, detail, wide = 1) => {
  const s = h / 170;
  return `<g transform="translate(${cx - 50 * s * wide} ${top}) scale(${s * wide} ${s})">${mic(detail)}</g>`;
};

const VARIANTS = {
  // A: the panel's badge — cream enamel plate with a dark bakelite medallion and an amber ring
  badge: (detail) => `
    <rect x="10" y="10" width="236" height="236" rx="54" fill="url(#enamel)"/>
    <rect x="10" y="10" width="236" height="236" rx="54" fill="none" stroke="#9c8c6e" stroke-width="${detail ? 5 : 9}"/>
    <circle cx="128" cy="128" r="${detail ? 98 : 104}" fill="url(#bakelite)"/>
    <circle cx="128" cy="128" r="${detail ? 98 : 104}" fill="none" stroke="#d8641c" stroke-width="${detail ? 7 : 12}"/>
    ${detail ? '<circle cx="128" cy="128" r="88" fill="none" stroke="#ffb23e" stroke-width="1.5" opacity=".5"/>' : ''}
    ${place(128, detail ? 46 : 38, detail ? 164 : 180, detail, detail ? 1 : 1.4)}`,

  // B: an amber VU meter behind the microphone, on bakelite
  vu: (detail) => `
    <rect x="10" y="10" width="236" height="236" rx="54" fill="url(#bakelite)"/>
    <rect x="10" y="10" width="236" height="236" rx="54" fill="none" stroke="#4a3b2c" stroke-width="${detail ? 5 : 9}"/>
    <rect x="30" y="30" width="196" height="${detail ? 132 : 120}" rx="16" fill="url(#vu)"/>
    ${detail ? `
    <path d="M58 132A82 82 0 0 1 198 132" fill="none" stroke="#3a2a14" stroke-width="4"/>
    <path d="M168 86A82 82 0 0 1 198 132" fill="none" stroke="#c0281a" stroke-width="6"/>
    ${Array.from({ length: 9 }, (_, i) => { const a = Math.PI * (1.17 + 0.66 * i / 8), r1 = 82, r2 = i % 2 ? 92 : 98; return `<path d="M${128 + Math.cos(a) * r1} ${132 + Math.sin(a) * r1}L${128 + Math.cos(a) * r2} ${132 + Math.sin(a) * r2}" stroke="#3a2a14" stroke-width="3"/>`; }).join('')}
    <path d="M128 150L182 64" stroke="#1d1309" stroke-width="3.5" stroke-linecap="round"/>` : '<path d="M128 140L184 56" stroke="#1d1309" stroke-width="9" stroke-linecap="round"/>'}
    ${place(128, detail ? 92 : 86, detail ? 150 : 160, detail)}`,

  // C: walnut like the panel's side cheeks, with a cream nameplate C01U PRO
  walnut: (detail) => `
    <rect x="10" y="10" width="236" height="236" rx="54" fill="url(#walnut)"/>
    ${detail ? [30, 58, 92, 121, 150, 182, 210].map((y, i) => `<path d="M10 ${y}C80 ${y - 8 + (i % 2) * 14} 150 ${y + 10 - (i % 3) * 6} 246 ${y - 4}" fill="none" stroke="${i % 2 ? '#2a170b' : '#8a5e3a'}" stroke-width="${1.2 + (i % 3)}" opacity=".45"/>`).join('') : ''}
    <rect x="10" y="10" width="236" height="236" rx="54" fill="none" stroke="#2a170b" stroke-width="${detail ? 5 : 9}"/>
    ${detail ? `
    <rect x="46" y="190" width="164" height="38" rx="7" fill="url(#enamel)" stroke="#9c8c6e" stroke-width="2.5"/>
    <text x="128" y="217" text-anchor="middle" font-family="Bahnschrift SemiCondensed, Bahnschrift, Arial Narrow, sans-serif" font-weight="700" font-size="27" letter-spacing="2.5" fill="#2a221a">C01U PRO</text>
    ${place(128, 22, 164, detail)}` : place(128, 30, 196, detail)}`,
};

function svg(variant, size) {
  const detail = size >= 32;
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="${size}" height="${size}"><defs>${DEFS}</defs>${VARIANTS[variant](detail)}</svg>`;
}

// tray: the emblem's medallion (bakelite, amber ring, the microphone) without the enamel plate;
// muted: the microphone greys out and a red slash crosses it
function traySvg(size, muted) {
  const detail = size >= 40;
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="${size}" height="${size}"><defs>${DEFS}
    <filter id="grey"><feColorMatrix type="saturate" values="0"/><feComponentTransfer><feFuncR type="linear" slope=".7"/><feFuncG type="linear" slope=".7"/><feFuncB type="linear" slope=".7"/></feComponentTransfer></filter></defs>
    <circle cx="128" cy="128" r="120" fill="url(#bakelite)"/>
    <circle cx="128" cy="128" r="112" fill="none" stroke="${muted ? '#7d7469' : '#d8641c'}" stroke-width="${detail ? 14 : 20}"/>
    <g ${muted ? 'filter="url(#grey)"' : ''}>${place(128, 30, 196, detail, detail ? 1.05 : 1.45)}</g>
    ${muted ? '<path d="M50 50L206 206" stroke="#120d09" stroke-width="44" stroke-linecap="round"/><path d="M50 50L206 206" stroke="#ff4b2e" stroke-width="26" stroke-linecap="round"/>' : ''}</svg>`;
}
// the microphone alone (the panel's sidebar badge)
function micSvg() {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="14 2 72 172"><defs>${DEFS}</defs>${mic(true)}</svg>`;
}

module.exports = { svg, traySvg, micSvg, VARIANTS: Object.keys(VARIANTS) };
