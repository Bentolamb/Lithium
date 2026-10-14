// Browser test of the web shell (headless Chromium via playwright-core).
//   node tests/web/ui_test.js [--real]
// Default: the WASM module is replaced by tests/web/mock_wasm.js.
// --real: use build-wasm/lithium_wasm.js (needs an Emscripten build).
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const http = require('http');
const { spawn } = require('child_process');

function loadPlaywright() {
    for (const p of ['playwright-core', '/opt/npm-tools/node_modules/playwright-core', '/opt/node-tools/node_modules/playwright-core']) {
        try { return require(p); } catch (_) {}
    }
    throw new Error('playwright-core not found (npm i playwright-core)');
}

(async () => {
    const real = process.argv.includes('--real');
    const root = path.resolve(__dirname, '../..');
    const port = 8765;
    const server = spawn(process.execPath, [path.join(root, 'tools/serve.js'), String(port)], { stdio: 'ignore' });
    await new Promise(r => setTimeout(r, 400));
    const { chromium } = loadPlaywright();
    const exe = process.env.CHROMIUM_PATH || (fs.existsSync('/opt/pw-browsers/chromium') ? '/opt/pw-browsers/chromium' : undefined);
    const browser = await chromium.launch(exe ? { executablePath: exe } : {});
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    if (!real) {
        await page.route('**/build-wasm/lithium_wasm.js', route =>
            route.fulfill({ contentType: 'text/javascript', body: fs.readFileSync(path.join(__dirname, 'mock_wasm.js'), 'utf8') }));
    }
    try {
        await page.goto(`http://localhost:${port}/web/`);
        await page.waitForFunction(() => /runtime /.test(document.getElementById('status').textContent));
        assert.ok(await page.$eval('#flag', b => b.disabled), 'controls start disabled');

        // Open a real .sb3 (zip + deflate) through the file input.
        await page.setInputFiles('#file', path.join(root, 'tests/projects/slide.sb3'));
        await page.waitForFunction(() => !document.getElementById('flag').disabled);
        assert.match(await page.textContent('#hint'), /slide\.sb3 loaded/);
        assert.match(await page.textContent('#sprites tbody'), /Sprite1/);
        assert.match(await page.textContent('#vars tbody'), /my variable/);

        // Go: the sprite moves and the status counts frames.
        const x0 = await page.$eval('#sprites tbody tr td:nth-child(2)', e => Number(e.textContent));
        await page.click('#flag');
        await page.waitForTimeout(600);
        const x1 = await page.$eval('#sprites tbody tr td:nth-child(2)', e => Number(e.textContent));
        assert.notStrictEqual(x1, x0, 'sprite should move after Go');
        assert.match(await page.textContent('#status'), /\d+ frames/);

        // Something is drawn on the stage canvas (blue marker).
        const drawn = await page.$eval('#stage', c => {
            const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
            for (let i = 0; i < d.length; i += 4) if (d[i + 2] > 200 && d[i] < 100 && d[i + 3] > 200) return true;
            return false;
        });
        assert.ok(drawn, 'stage canvas shows the sprite');

        // Stop freezes motion (mock) / ends threads (real may finish earlier anyway).
        await page.click('#stop');
        await page.waitForTimeout(200);
        const xs = await page.$eval('#sprites tbody tr td:nth-child(2)', e => e.textContent);
        await page.waitForTimeout(300);
        assert.strictEqual(await page.$eval('#sprites tbody tr td:nth-child(2)', e => e.textContent), xs, 'no motion after Stop');

        // A broken file reports an error and keeps the old project.
        fs.writeFileSync('/tmp/not_a_project.sb3', 'this is not a zip');
        await page.setInputFiles('#file', '/tmp/not_a_project.sb3');
        await page.waitForFunction(() => document.getElementById('log').className === 'error');

        // Demo selector.
        await page.selectOption('#demo', 'slide');
        await page.waitForFunction(() => /demo slide loaded/.test(document.getElementById('hint').textContent));
        assert.deepStrictEqual(errors, [], 'no uncaught page errors');
        console.log('web ui test: PASS' + (real ? ' (real WASM)' : ' (mock WASM)'));
    } finally {
        await browser.close();
        server.kill();
    }
})().catch(e => { console.error('web ui test: FAIL', e); process.exit(1); });
