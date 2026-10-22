// Local server for the promo page: /promo/* from this folder, /ui/* is the panel's real web UI
// (../web) with the promo clock and the API shim put in front of the panel's own script.
//   node promo/server.js        -> http://127.0.0.1:47900/promo/index.html (?lang=en)
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const PROMO = __dirname;
const WEB = path.join(__dirname, '..', 'web');
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.mp3': 'audio/mpeg' };

function start(port = 47900) {
  const server = http.createServer((req, res) => {
    const url = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    let file = null;
    if (url === '/ui/index.html' || url === '/ui/') {
      const html = fs.readFileSync(path.join(WEB, 'index.html'), 'utf8')
        .replace('<head>', '<head><script src="/promo/clock.js"></script><script src="/promo/ui-shim.js"></script>');
      res.writeHead(200, { 'Content-Type': TYPES['.html'] });
      return res.end(html);
    }
    if (url.startsWith('/ui/')) file = path.join(WEB, url.slice(4));
    else if (url.startsWith('/promo/')) file = path.join(PROMO, url.slice(7));
    const root = file && (file.startsWith(WEB) ? WEB : PROMO);
    if (!file || !path.resolve(file).startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
      res.writeHead(404);
      return res.end();
    }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise((resolve) => server.listen(port, '127.0.0.1', () => resolve(server)));
}

module.exports = { start };
if (require.main === module) start().then(() => console.log('http://127.0.0.1:47900/promo/index.html'));
