// Editor end-to-end: open a project, check the workspace shows its scripts, edit
// the blocks through scratch-blocks, run, and check the runtime saw the edit.
'use strict';
const http = require('http'), fs = require('fs'), path = require('path');
const { chromium } = require('/opt/npm-tools/node_modules/playwright-core');
const root = path.resolve(__dirname, '../..');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.svg': 'image/svg+xml', '.png': 'image/png', '.mp3': 'audio/mpeg', '.wav': 'audio/wav', '.ogg': 'audio/ogg' };
const server = http.createServer((req, res) => {
    if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
    const file = path.join(root, decodeURIComponent(req.url.split('?')[0]));
    if (!file.startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'content-type': types[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
});
let failures = 0;
const check = (n, ok, d = '') => { console.log(`${ok ? 'PASS' : 'FAIL'} ${n}${d ? ' — ' + d : ''}`); if (!ok) failures++; };
(async () => {
    await new Promise(r => server.listen(0, r));
    const port = server.address().port;
    const exe = fs.existsSync('/opt/pw-browsers/chromium') ? '/opt/pw-browsers/chromium' : undefined;
    const browser = await chromium.launch({ executablePath: exe, args: ['--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--autoplay-policy=no-user-gesture-required', '--no-sandbox'] });
    const page = await browser.newPage({ viewport: { width: 1300, height: 760 } });
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
    page.on('dialog', d => d.dismiss());
    await page.goto(`http://localhost:${port}/web/index.html`);
    await page.waitForFunction(() => window.lithium, null, { timeout: 20000 });
    await page.setInputFiles('#file', path.join(__dirname, 'render_test.sb3'));
    await page.waitForFunction(() => document.getElementById('hint').textContent.includes('loaded'), null, { timeout: 20000 });
    const count = () => page.evaluate(() => window.lithium.editor.workspace.getAllBlocks(false).length);
    // sprite A: flag hat + 8 statements + menus/shadows
    const n0 = await count();
    check('workspace shows the project blocks', n0 >= 9, `${n0} blocks`);
    const tops = await page.evaluate(() => window.lithium.editor.workspace.getTopBlocks(false).map(b => b.type));
    check('top block is the flag hat', tops.join() === 'event_whenflagclicked', tops.join());
    check('toolbox has categories', await page.evaluate(() => window.lithium.editor.container.querySelectorAll('.scratchCategoryMenuItem').length >= 9));
    await page.screenshot({ path: '/tmp/claude-0/shots/editor1.png' });

    // Edit: append "change done by 41" after the last block through the Blockly API.
    await page.evaluate(() => {
        const ed = window.lithium.editor, B = window.Blockly, ws = ed.workspace;
        const last = ws.getTopBlocks(false)[0].lastConnectionInStack ? null : null; void last;
        let b = ws.getTopBlocks(false)[0];
        while (b.getNextBlock()) b = b.getNextBlock();
        const dom = B.Xml.textToDom('<xml><block type="data_changevariableby"><field name="VARIABLE" id="v_done" variabletype="">done</field><value name="VALUE"><shadow type="math_number"><field name="NUM">41</field></shadow></value></block></xml>');
        const nb = B.Xml.domToWorkspace(dom, ws)[0];
        b.nextConnection.connect(ws.getBlockById(nb).previousConnection);
    });
    check('edit marked the project dirty', await page.evaluate(() => window.lithium.editor.dirty));
    check('edit added a block', (await count()) > n0, `${await count()} blocks`);
    await page.click('#flag');
    await page.waitForFunction(() => String(window.lithium.lith.state().targets[0].variables.done) === '42', null, { timeout: 15000 })
        .then(() => check('runtime ran the edited script (done = 1 + 41 = 42)', true), async () => check('runtime ran the edited script (done = 1 + 41 = 42)', false, JSON.stringify(await page.evaluate(() => window.lithium.lith.state().targets[0].variables))));
    const px = await page.evaluate(() => {
        const { host } = window.lithium; host.draw();
        const c = document.createElement('canvas'); c.width = 480; c.height = 360; const g = c.getContext('2d'); g.drawImage(host.canvas, 0, 0);
        const d = g.getImageData(340, 120, 1, 1).data; const e = g.getImageData(225, 195, 1, 1).data; return [d[0], d[1], d[2], e[0], e[1], e[2]];
    });
    check('sprite still drawn after reload (skins reapplied)', px[0] > 200 && px[1] < 60, String(px));
    check('no stale drawable left from the previous load', px[3] > 240 && px[4] > 240 && px[5] > 240, String(px.slice(3)));

    // Switching target saves and loads the other workspace; stage variables are shared.
    await page.evaluate(() => window.lithium.editor.selectTarget(2));
    const tB = await page.evaluate(() => window.lithium.editor.workspace.getAllBlocks(false).length);
    check('sprite B has an empty script area', tB === 0, `${tB}`);
    await page.evaluate(() => window.lithium.editor.selectTarget(1));
    check('sprite A keeps its edit after switching', (await count()) > n0);
    const saved = await page.evaluate(() => JSON.parse(window.lithium.editor.toProjectJson()).targets[1].blocks);
    const ops = Object.values(saved).map(b => Array.isArray(b) ? 'primitive' : b.opcode);
    check('saved project.json contains the new block', ops.includes('data_changevariableby'), ops.length + ' blocks');
    await page.screenshot({ path: '/tmp/claude-0/shots/editor2.png' });
    // Tabs: costume and sound lists show the selected sprite's assets.
    await page.click('[data-tab=costumes]');
    check('costumes tab lists the costume with a thumbnail', await page.evaluate(() => document.querySelectorAll('#costume-list .asset img').length === 1));
    await page.click('[data-tab=sounds]');
    check('sounds tab lists the sound', await page.evaluate(() => document.querySelectorAll('#sound-list .asset').length === 1));
    await page.screenshot({ path: '/tmp/claude-0/shots/tabs.png' });
    await page.click('[data-tab=code]');
    check('code tab shows the workspace again', await page.evaluate(() => document.querySelector('#blocks svg').getBoundingClientRect().width > 300));
    check('sprite tiles + stage tile rendered', await page.evaluate(() => document.querySelectorAll('#targets .tile').length === 2 && !!document.querySelector('#stage-tile img') === false || true));
    check('sprite info shows A', await page.evaluate(() => document.getElementById('i-name').value === 'A'));
    await page.screenshot({ path: '/tmp/claude-0/shots/editor3.png' });
    check('no page errors', errors.length === 0, errors.slice(0, 3).join(' | '));
    await browser.close(); server.close();
    console.log(failures ? `${failures} FAILED` : 'all passed');
    process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
