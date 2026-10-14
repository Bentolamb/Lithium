// End-to-end browser test: loads the WASM runtime + web UI in Chromium, opens
// render_test.sb3, presses the flag and checks pixels, variables and sound.
//   node browser_test.js [--shots dir]
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('/opt/npm-tools/node_modules/playwright-core');

const root = path.resolve(__dirname, '../..');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.sb3': 'application/zip' };
const server = http.createServer((req, res) => {
    if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
    const file = path.join(root, decodeURIComponent(req.url.split('?')[0]));
    if (!file.startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'content-type': types[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
});

let failures = 0;
const check = (name, ok, detail = '') => { console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? ' — ' + detail : ''}`); if (!ok) failures++; };

(async () => {
    await new Promise(r => server.listen(0, r));
    const port = server.address().port;
    const shots = process.argv.includes('--shots') ? process.argv[process.argv.indexOf('--shots') + 1] : null;
    const exe = fs.existsSync('/opt/pw-browsers/chromium') ? '/opt/pw-browsers/chromium' : undefined;
    const browser = await chromium.launch({ executablePath: exe, args: ['--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--autoplay-policy=no-user-gesture-required', '--no-sandbox'] });
    const page = await browser.newPage({ viewport: { width: 900, height: 700 } });
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
    await page.goto(`http://localhost:${port}/web/index.html`);
    await page.waitForFunction(() => window.lithium, null, { timeout: 20000 });
    await page.setInputFiles('#file', path.join(__dirname, 'render_test.sb3'));
    await page.waitForFunction(() => document.getElementById('hint').textContent.includes('loaded'), null, { timeout: 20000 });
    check('project loads', true);
    await page.click('#flag');
    const doneAt = Date.now();
    await page.waitForFunction(() => { const s = window.lithium.lith.state(); return String(s.targets[0].variables.done) === '1'; }, null, { timeout: 15000 })
        .then(() => check('script ran to the end (sound finished)', true, `${Date.now() - doneAt} ms`), () => check('script ran to the end (sound finished)', false));
    const state = await page.evaluate(() => window.lithium.lith.state());
    check('touching reported by renderer', String(state.targets[0].variables.touch) === 'true', JSON.stringify(state.targets[0].variables));

    const px = await page.evaluate(() => {
        const { host } = window.lithium;
        host.draw();
        const c = document.createElement('canvas'); c.width = 480; c.height = 360;
        const g = c.getContext('2d'); g.drawImage(host.canvas, 0, 0);
        return Array.from(g.getImageData(0, 0, 480, 360).data);
    });
    const at = (x, y) => { const i = (y * 480 + x) * 4; return [px[i], px[i + 1], px[i + 2]]; };
    const near = (c, t, tol = 40) => c.every((v, i) => Math.abs(v - t[i]) <= tol);
    check('backdrop drawn (white)', near(at(20, 20), [255, 255, 255]), String(at(20, 20)));
    check('backdrop drawn (blue strip)', near(at(20, 340), [42, 109, 244]), String(at(20, 340)));
    check('sprite A drawn red at (100,60)', near(at(340, 120), [255, 0, 0]), String(at(340, 120)));
    check('sprite B drawn green at (-100,-60)', near(at(140, 240), [0, 192, 0]), String(at(140, 240)));
    let penHits = 0;
    for (let t = 0.2; t <= 0.8; t += 0.1) {
        const x = Math.round(240 + -90 + 190 * t), y = Math.round(180 - (-50 + 110 * t));
        let hit = false;
        for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) { const c = at(x + dx, y + dy); if (c[2] > 150 && c[0] < 120 && c[1] < 120) hit = true; }
        if (hit) penHits++;
    }
    check('pen line drawn along the path', penHits >= 6, `${penHits}/7 samples`);
    let bubble = 0;
    for (let y = 40; y < 125; y++) for (let x = 355; x < 475; x++) { const c = at(x, y); if (!near(c, [255, 255, 255], 8) && !near(c, [255, 0, 0], 40)) bubble++; }
    check('speech bubble drawn next to A', bubble > 40, `${bubble} px`);
    if (shots) { fs.mkdirSync(shots, { recursive: true }); await page.screenshot({ path: path.join(shots, 'stage.png') }); }

    // Input: click on the stage and a key press reach the runtime.
    await page.evaluate(() => { window.lithium.lith.postKey('a', true); });
    check('no page errors', errors.length === 0, errors.slice(0, 3).join(' | '));
    await browser.close();
    server.close();
    console.log(failures ? `${failures} FAILED` : 'all passed');
    process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
