import { loadRuntime, readSb3 } from './lithium.js';
import { StageHost } from './stage.js';

const $ = id => document.getElementById(id);
const stage = $('stage');
let lith = null;
let host = null;
let steps = 0;
let lastStatus = 0;
let lastTable = 0;
let stepsInWindow = 0;
let windowStart = performance.now();
let stepsPerSecond = 0;

function setLog(text, isError) {
    $('log').textContent = text.trim() || 'ok';
    $('log').className = isError ? 'error' : '';
}

function enableControls(on) {
    $('flag').disabled = $('stop').disabled = !on;
}

// ---- loading ------------------------------------------------------------

async function openFile(file) {
    if (!lith || !file) return;
    try {
        const { json, buffer } = await readSb3(file);
        const r = lith.loadProjectJson(json);
        setLog(r.log, !r.ok);
        if (r.ok) {
            await host.loadAssets(buffer);
            afterLoad(file.name);
        }
    } catch (e) {
        setLog(String(e.message || e), true);
    }
}

function afterLoad(name) {
    enableControls(true);
    $('hint').textContent = name + ' loaded. Press Go.';
    refresh(true);
}

$('open').onclick = () => $('file').click();
$('file').onchange = e => { openFile(e.target.files[0]); e.target.value = ''; };
$('demo').onchange = e => {
    const name = e.target.value;
    if (name && lith.loadBuiltin(name)) { setLog('demo: ' + name, false); afterLoad('demo ' + name); }
    e.target.value = '';
};
const drop = $('drop');
['dragenter', 'dragover'].forEach(t => drop.addEventListener(t, e => { e.preventDefault(); drop.classList.add('dragging'); }));
['dragleave', 'drop'].forEach(t => drop.addEventListener(t, e => { e.preventDefault(); drop.classList.remove('dragging'); }));
drop.addEventListener('drop', e => openFile(e.dataTransfer.files[0]));

// ---- controls -----------------------------------------------------------

$('flag').onclick = () => lith.greenFlag();
$('stop').onclick = () => { lith.stopAll(); refresh(true); };
$('turbo').onchange = e => lith.setTurbo(e.target.checked);
$('compat').onchange = e => lith.setCompat(e.target.checked);
// ---- input to the project -------------------------------------------------

const typing = e => e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA';
document.addEventListener('keydown', e => {
    if (typing(e) || !lith) return;
    if ([' ', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(e.key)) e.preventDefault();
    lith.postKey(e.key, true);
});
document.addEventListener('keyup', e => { if (!typing(e) && lith) lith.postKey(e.key, false); });
function mouseData(e, flags) {
    const r = stage.getBoundingClientRect();
    lith.postMouse(flags | 1 | 2, e.clientX - r.left, e.clientY - r.top, r.width, r.height);
}
stage.addEventListener('mousemove', e => lith && mouseData(e, 0));
stage.addEventListener('mousedown', e => lith && mouseData(e, 4 | 8));
window.addEventListener('mouseup', e => { if (lith) mouseData(e, 4); });

document.addEventListener('keydown', e => {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
    if (e.key === 'Enter' && !$('flag').disabled) lith.greenFlag();
    if (e.key === 'Escape' && !$('stop').disabled) lith.stopAll();
});

// ---- drawing ------------------------------------------------------------

function row(cells) {
    const tr = document.createElement('tr');
    for (const c of cells) { const td = document.createElement('td'); td.textContent = c; tr.appendChild(td); }
    return tr;
}

function fmt(v) { return typeof v === 'number' ? String(Math.round(v * 1e6) / 1e6) : String(v); }

function drawTables(state) {
    const vars = document.querySelector('#vars tbody');
    const sprites = document.querySelector('#sprites tbody');
    vars.replaceChildren(); sprites.replaceChildren();
    for (const t of state.targets) {
        for (const [name, value] of Object.entries(t.variables)) {
            vars.appendChild(row([t.isStage ? name : t.name + ': ' + name, value]));
        }
        if (!t.isStage) sprites.appendChild(row([t.name, fmt(t.x), fmt(t.y), fmt(t.direction)]));
    }
    if (!vars.children.length) vars.appendChild(row(['–', '']));
}

function refresh(force) {
    if (!lith) return;
    const now = performance.now();
    const state = lith.state();
    if (force || now - lastTable > 100) { drawTables(state); lastTable = now; }
    if (force || now - lastStatus > 250) {
        $('status').textContent = `${steps} frames · ${state.threads} running · ${stepsPerSecond} steps/s`;
        lastStatus = now;
    }
}

// ---- main loop ----------------------------------------------------------
// Scratch steps at a fixed 60 Hz (30 in compatibility mode), independent of
// the display's refresh rate.

let nextStep = performance.now();
function frame(now) {
    const interval = $('compat').checked ? 1000 / 30 : 1000 / 60;
    if (lith && now >= nextStep) {
        lith.step();
        steps++; stepsInWindow++;
        nextStep = Math.max(nextStep + interval, now - interval);  // don't spiral after a stall
        if (now - windowStart >= 1000) {
            stepsPerSecond = Math.round(stepsInWindow * 1000 / (now - windowStart));
            stepsInWindow = 0; windowStart = now;
        }
        refresh(false);
    }
    if (host) host.draw();
    requestAnimationFrame(frame);
}

(async function main() {
    try {
        lith = await loadRuntime(window.createLithium);
        $('status').textContent = 'runtime ' + lith.version;
        host = new StageHost(stage, lith);
        lith.setHost((op, a, b, c, d, p) => host.handle(op, a, b, c, d, p));
        window.lithium = { lith, host };  // for tests and the console
        requestAnimationFrame(frame);
    } catch (e) {
        $('status').textContent = 'runtime failed to load';
        setLog('Could not load build-wasm/lithium_wasm.js. Build the WASM target and start tools/serve.js.\n' + (e.message || e), true);
    }
})();
