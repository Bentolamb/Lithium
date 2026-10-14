// Tiny static file server for the web shell (WASM needs http://, not file://).
//   node tools/serve.js [port]
// Serves the repository root; open http://localhost:8080/web/
const http = require('http');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const port = Number(process.argv[2]) || 8080;
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
                '.css': 'text/css; charset=utf-8', '.wasm': 'application/wasm',
                '.json': 'application/json', '.sb3': 'application/octet-stream' };

http.createServer((req, res) => {
    let rel = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    if (rel === '/') { res.writeHead(302, { Location: '/web/' }); return res.end(); }
    if (rel.endsWith('/')) rel += 'index.html';
    const file = path.resolve(root, '.' + rel);
    if (file !== root && !file.startsWith(root + path.sep)) { res.writeHead(403); return res.end('forbidden'); }
    fs.readFile(file, (err, data) => {
        if (err) { res.writeHead(404); return res.end('not found: ' + rel); }
        res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream',
                             'Cache-Control': 'no-cache' });
        res.end(data);
    });
}).listen(port, '127.0.0.1', () => console.log(`Lithium web shell: http://localhost:${port}/web/`));
