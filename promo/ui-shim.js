// Runs inside the panel UI frame of the promo, right after clock.js. The panel's HTTP API is
// answered from the recorded state (window.parent.__uiData) and the live meters come from the
// music (window.parent.__meters), so the real interface moves in time with the track.
(function () {
  'use strict';
  const P = window.parent;
  const reply = (obj) => new Response(JSON.stringify(obj), { headers: { 'Content-Type': 'application/json' } });
  const realFetch = window.fetch.bind(window);

  window.fetch = async (url, opts) => {
    const u = new URL(String(url), location.href);
    if (!u.pathname.startsWith('/api/')) return realFetch(url, opts);
    const d = await P.__uiData;
    switch (u.pathname.slice(5)) {
      case 'state': return reply(d.state);
      case 'status': return reply(d.state.status);
      case 'devices': return reply(d.state.devices);
      case 'update': return reply(d.state.update);
      case 'diagnostics': return reply(d.diag);
      default: return reply({ ok: true });  // settings and params: accepted, nothing is stored
    }
  };

  const sources = [];
  class PromoEventSource {
    constructor() {
      this.listeners = {};
      this.onopen = this.onerror = null;
      sources.push(this);
    }
    addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); }
    removeEventListener() {}
    close() {}
    emit(type, data) { for (const fn of this.listeners[type] || []) fn({ data: JSON.stringify(data) }); }
  }
  window.EventSource = PromoEventSource;

  // ~25 updates per second like the real panel, on the virtual clock
  setInterval(() => {
    if (!P.__meters) return;
    const m = P.__meters(performance.now() / 1000);
    for (const s of sources) s.emit('m', m);
  }, 40);
})();
