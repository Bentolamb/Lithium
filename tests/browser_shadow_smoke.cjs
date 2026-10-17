#!/usr/bin/env node
// Hosted-browser smoke test. Run AFTER building the TurboWarp+Lithium site.
// npm install --prefix /tmp/lithium-playwright --no-save playwright@1.56.1
// NODE_PATH=/tmp/lithium-playwright/node_modules node tests/browser_shadow_smoke.cjs <static-site-root>
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('playwright');

const root = path.resolve(process.argv[2] || '.');
const types = {
    '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8', '.wasm': 'application/wasm',
    '.json': 'application/json', '.svg': 'image/svg+xml',
    '.png': 'image/png', '.woff2': 'font/woff2', '.webmanifest': 'application/manifest+json'
};

const server = http.createServer((req, res) => {
    let url;
    try { url = new URL(req.url, 'http://localhost'); }
    catch (error) { res.writeHead(400); res.end(); return; }
    const filepath = path.resolve(root, '.' + decodeURIComponent(url.pathname));
    if (!filepath.startsWith(root + path.sep)) {
        res.writeHead(403);
        res.end();
        return;
    }
    fs.readFile(filepath, (err, bytes) => {
        if (err) { res.writeHead(404); res.end('not found'); return; }
        res.writeHead(200, {'Content-Type': types[path.extname(filepath)] || 'application/octet-stream',
            'Cache-Control': 'no-store'});
        res.end(bytes);
    });
});

async function main() {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const address = server.address();
    const url = 'http://127.0.0.1:' + address.port + '/editor.html';
    console.log('Testing editor at', url);
    let browser;
    let page;
    const issues = [];
    try {
        browser = await chromium.launch({
            headless: true,
            args: ['--no-sandbox', '--disable-dev-shm-usage',
                '--use-gl=angle', '--use-angle=swiftshader', '--enable-webgl']
        });
        page = await browser.newPage({viewport: {width: 1365, height: 850}});
        page.on('pageerror', error => issues.push('Page exception: ' + error.message));
        const response = await page.goto(url, {waitUntil: 'domcontentloaded', timeout: 90000});
        assert.equal(response.status(), 200);
        await page.waitForFunction(() => {
            const shadow = window.LithiumShadow;
            return shadow && ['ready', 'ready-no-step-hook', 'error', 'no-vm'].includes(shadow.mode);
        }, null, {timeout: 120000});

        const result = await page.evaluate(() => ({
            mode: window.LithiumShadow.mode,
            bridge: typeof window.LithiumShadow.compare,
            comparator: typeof window.LithiumCompare,
            hasPanel: !!document.querySelector('#lithium-diagnostics'),
            scripts: [...document.querySelectorAll('script[src*="/lithium/"]')]
                .map(script => script.src),
            comparison: window.LithiumShadow.compare && window.LithiumShadow.compare(),
            lastError: window.LithiumShadow.lastError
        }));

        console.log('Browser diagnostics:', JSON.stringify(result, null, 2));
        assert.equal(result.mode, 'ready', 'Shadow engine was not attached to TurboWarp');
        assert.equal(result.bridge, 'function');
        assert.equal(result.comparator, 'object');
        assert.equal(result.hasPanel, true);
        assert.equal(result.comparison.status, 'not-run');
        assert.equal(result.comparison.match, false);
        for (const asset of ['lithium_wasm.js', 'compare.js', 'shadow.js', 'panel.js']) {
            assert.ok(result.scripts.some(src => src.endsWith('/lithium/' + asset)),
                'Missing integration script: ' + asset);
        }

        await page.getByRole('button', {name: /Lithium shadow:/}).click();
        assert.equal(await page.locator('#lithium-diagnostics-panel').isVisible(), true);

        console.log('PASS: TurboWarp editor initializes with the Lithium Wasm shadow and diagnostic UI');
        if (issues.length) console.warn('Nonfatal upstream page errors:', issues.slice(0, 10));
    } finally {
        if (page) {
            try { await page.screenshot({path: 'lithium-browser-smoke.png'}); }
            catch (error) { console.warn('Screenshot failed:', error.message); }
        }
        if (browser) await browser.close();
        await new Promise(resolve => server.close(resolve));
    }
}
main().catch(error => {
    console.error('Browser smoke failed:', error);
    process.exitCode = 1;
    // The finally block in main() already closes the browser and server.
});
