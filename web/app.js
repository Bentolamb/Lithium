import { loadRuntime, readSb3 } from './lithium.js';
import { StageHost } from './stage.js';
import { Editor } from './editor.js';

const $ = id => document.getElementById(id);
const stage = $('stage');
let lith = null;
let host = null;
let editor = null;
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
            editor.loadProject(JSON.parse(json));
            renderTargets();
            afterLoad(file.name);
        }
    } catch (e) {
        setLog(String(e.message || e), true);
    }
}

function afterLoad(name) {
    enableControls(true);
    $('hint').textContent = name + ' loaded. Press the green flag.';
    refresh(true);
}

const closeMenus = () => document.querySelectorAll('details.menu').forEach(d => { d.open = false; });
document.addEventListener('click', e => { if (!e.target.closest('details.menu')) closeMenus(); });
$('open').onclick = () => { closeMenus(); $('file').click(); };
$('file').onchange = e => { openFile(e.target.files[0]); e.target.value = ''; };
document.querySelectorAll('[data-demo]').forEach(b => {
    b.onclick = () => {
        closeMenus();
        const name = b.dataset.demo;
        if (lith.loadBuiltin(name)) { setLog('demo: ' + name, false); afterLoad('demo ' + name); }
    };
});

// ---- tabs ---------------------------------------------------------------
function showTab(name) {
    document.querySelectorAll('.tab').forEach(t => t.classList.toggle('on', t.dataset.tab === name));
    for (const p of ['code', 'costumes', 'sounds']) $('panel-' + p).classList.toggle('hidden', p !== name);
    if (name === 'code') editor.resize();
    if (name === 'costumes') renderCostumes();
    if (name === 'sounds') renderSounds();
}
document.querySelectorAll('.tab').forEach(t => { t.onclick = () => showTab(t.dataset.tab); });
const thumbOf = md5 => host && host.thumbs.get(md5);
function renderCostumes() {
    const box = $('costume-list');
    box.replaceChildren();
    const t = editor.project && editor.target;
    const list = (t && t.costumes) || [];
    if (!list.length) { box.innerHTML = '<div class="empty">No costumes.</div>'; return; }
    list.forEach((c, i) => {
        const d = document.createElement('div');
        d.className = 'asset';
        d.innerHTML = '<div class="thumb"></div><div class="nm"></div><div class="sub"></div>';
        const url = thumbOf(c.md5ext);
        if (url) { const img = document.createElement('img'); img.src = url; d.querySelector('.thumb').appendChild(img); }
        d.querySelector('.nm').textContent = (i + 1) + '. ' + c.name;
        d.querySelector('.sub').textContent = c.dataFormat;
        box.appendChild(d);
    });
}
function renderSounds() {
    const box = $('sound-list');
    box.replaceChildren();
    const t = editor.project && editor.target;
    const list = (t && t.sounds) || [];
    if (!list.length) { box.innerHTML = '<div class="empty">No sounds.</div>'; return; }
    list.forEach((s, i) => {
        const d = document.createElement('div');
        d.className = 'asset';
        d.innerHTML = '<div class="thumb">&#128266;</div><div class="nm"></div><div class="sub"></div><button>Play</button>';
        d.querySelector('.nm').textContent = (i + 1) + '. ' + s.name;
        d.querySelector('.sub').textContent = Math.round((s.sampleCount / s.rate) * 100) / 100 + ' s';
        d.querySelector('button').onclick = () => host.previewSound(editor.current, i);
        box.appendChild(d);
    });
}
const drop = $('drop');
['dragenter', 'dragover'].forEach(t => drop.addEventListener(t, e => { e.preventDefault(); drop.classList.add('dragging'); }));
['dragleave', 'drop'].forEach(t => drop.addEventListener(t, e => { e.preventDefault(); drop.classList.remove('dragging'); }));
drop.addEventListener('drop', e => openFile(e.dataTransfer.files[0]));

// ---- controls -----------------------------------------------------------

function selectTarget(i) {
    editor.selectTarget(i);
    renderTargets();
    if (!$('panel-costumes').classList.contains('hidden')) renderCostumes();
    if (!$('panel-sounds').classList.contains('hidden')) renderSounds();
}
function renderTargets() {
    const box = $('targets');
    box.replaceChildren();
    const names = editor.targetNames();
    const tileFor = (name, i, big) => {
        const t = editor.project.targets[i];
        const d = document.createElement('div');
        d.className = 'tile' + (i === editor.current ? ' on' : '');
        d.innerHTML = '<div class="thumb"></div><span></span>';
        d.querySelector('span').textContent = name;
        const url = thumbOf(((t.costumes || [])[t.currentCostume || 0] || {}).md5ext);
        if (url) { const img = document.createElement('img'); img.src = url; d.querySelector('.thumb').appendChild(img); }
        d.onclick = () => selectTarget(i);
        return d;
    };
    names.forEach((name, i) => { if (!editor.project.targets[i].isStage) box.appendChild(tileFor(name, i)); });
    const st = $('stage-tile');
    const si = editor.project.targets.findIndex(t => t.isStage);
    const fresh = tileFor('Stage', si);
    fresh.id = 'stage-tile';
    fresh.classList.add('stage-tile');
    st.replaceWith(fresh);
}

// Runs the project; if the blocks were edited, the runtime reloads the edited project first.
function runProject() {
    if (editor && editor.project && editor.dirty) {
        const r = lith.loadProjectJson(editor.toProjectJson());
        setLog(r.log, !r.ok);
        if (!r.ok) return;
        host.reapplySkins();
        editor.dirty = false;
    }
    lith.greenFlag();
}
$('flag').onclick = runProject;
$('stop').onclick = () => { lith.stopAll(); refresh(true); };
$('turbo').onchange = e => lith.setTurbo(e.target.checked);
$('compat').onchange = e => lith.setCompat(e.target.checked);
// ---- input to the project -------------------------------------------------

const typing = e => e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA' || e.target.closest('.blocklyWidgetDiv, .blocklyDropDownDiv');
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
    if (e.key === 'Enter' && !$('flag').disabled) runProject();
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
    vars.replaceChildren();
    for (const t of state.targets) {
        for (const [name, value] of Object.entries(t.variables)) {
            vars.appendChild(row([t.isStage ? name : t.name + ': ' + name, value]));
        }
    }
    const sel = editor && editor.project ? state.targets[editor.current] : null;
    if (sel) {
        $('i-name').value = sel.isStage ? 'Stage' : sel.name;
        $('i-x').value = sel.isStage ? '' : fmt(Math.round(sel.x));
        $('i-y').value = sel.isStage ? '' : fmt(Math.round(sel.y));
        $('i-size').value = sel.isStage ? '' : fmt(Math.round(sel.size));
        $('i-dir').value = sel.isStage ? '' : fmt(Math.round(sel.direction));
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
        editor = new Editor($('blocks'), {});
        window.lithium = { lith, host, editor, runProject };  // for tests and the console
        requestAnimationFrame(frame);
    } catch (e) {
        $('status').textContent = 'runtime failed to load';
        setLog('Could not load build-wasm/lithium_wasm.js. Build the WASM target and start tools/serve.js.\n' + (e.message || e), true);
    }
})();

window.addEventListener('resize', () => editor && editor.resize());
