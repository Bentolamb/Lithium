// Serves the bundled Lithium editor on localhost (WebAssembly needs http://, not file://).
const http = require('http'), fs = require('fs'), path = require('path');
const root = path.join(__dirname, 'gui');
const port = Number(process.argv[2]) || 47653;
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css',
    '.svg': 'image/svg+xml', '.png': 'image/png', '.wasm': 'application/wasm', '.json': 'application/json',
    '.webmanifest': 'application/manifest+json', '.ico': 'image/x-icon', '.mp3': 'audio/mpeg', '.wav': 'audio/wav' };
http.createServer((req, res) => {
    let rel = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    if (rel === '/') rel = '/editor.html';
    const file = path.normalize(path.join(root, rel));
    if (!file.startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); return res.end('not found'); }
    res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
}).listen(port, '127.0.0.1', () => console.log('Lithium on http://localhost:' + port))
  .on('error', e => { if (e.code === 'EADDRINUSE') process.exit(0); throw e; }); // already running: reuse it
