// Bridge between the TurboWarp GUI's scratch-vm (editing, serialization, monitors)
// and the Lithium WASM runtime (execution, state). While a "session" is active,
// Lithium runs the project and draws it with the VM's own renderer; the VM keeps
// the editor state and is synced from Lithium on stop and for monitors.
import { loadRuntime } from './lithium.js';
import { StageHost } from './stage.js';

const EMPTY_PROJECT = JSON.stringify({
    targets: [{
        isStage: true, name: 'Stage', variables: {}, lists: {}, broadcasts: {}, blocks: {}, comments: {},
        currentCostume: 0, costumes: [], sounds: [], volume: 100, layerOrder: 0, tempo: 60,
        videoTransparency: 50, videoState: 'on', textToSpeechLanguage: null
    }],
    monitors: [], extensions: [], meta: { semver: '3.0.0', vm: '0.2.0', agent: 'lithium' }
});

const EFFECT_NAMES = ['color', 'fisheye', 'whirl', 'pixelate', 'mosaic', 'brightness', 'ghost'];

function loadScript(src) {
    return new Promise((resolve, reject) => {
        if (window.createLithium) return resolve();
        const s = document.createElement('script');
        s.src = src;
        s.onload = resolve;
        s.onerror = () => reject(new Error('could not load ' + src));
        document.head.appendChild(s);
    });
}

export function installLithium(vm, base = 'lithium/') {
    if (vm.lithium) return vm.lithium;
    const runtime = vm.runtime;
    const orig = {
        greenFlag: runtime.greenFlag.bind(runtime),
        stopAll: runtime.stopAll.bind(runtime),
        step: runtime._step.bind(runtime),
        postIOData: vm.postIOData.bind(vm),
        startDrag: vm.startDrag.bind(vm),
    };
    const L = {
        enabled: true, active: false, lith: null, host: null, ready: null, frame: 0, error: null,
        lastDown: false,
    };
    vm.lithium = L;

    L.init = () => {
        if (!L.ready) {
            L.ready = (async () => {
                await loadScript(base + 'lithium_wasm.js');
                L.lith = await loadRuntime(opts => window.createLithium(Object.assign({ locateFile: p => base + p }, opts)));
                return L.lith;
            })().catch(e => { L.error = e; console.error('Lithium failed to load', e); throw e; });
        }
        return L.ready;
    };

    const targetByName = (name, isStage) =>
        runtime.targets.find(t => !t.isClone && (isStage ? t.isStage : (!t.isStage && t.getName() === name)));

    const hideVmDrawables = hidden => {
        const r = vm.renderer;
        if (!r) return;
        for (const t of runtime.targets) {
            if (t.drawableID === null || t.drawableID === undefined) continue;
            r.updateDrawableVisible(t.drawableID, hidden ? false : !!t.visible);
        }
    };

    // Give Lithium's costumes the skins the VM already created.
    const mapAssets = () => {
        const { lith, host } = L;
        const manifest = lith.assetManifest();
        host.skinList = [];
        const sounds = new Map();
        for (let ti = 0; ti < manifest.length; ti++) {
            const m = manifest[ti];
            const t = targetByName(m.name, m.name === 'Stage');
            if (!t) continue;
            const costumes = t.sprite.costumes;
            for (let ci = 0; ci < m.costumes.length; ci++) {
                const c = costumes[ci];
                if (!c || c.skinId === undefined || c.skinId < 0) continue;
                lith.setCostumeSkin(ti, ci, c.skinId);
                host.skinList.push([ti, ci, c.skinId]);
                // thumbnails for asset lists are not needed: the GUI draws its own
            }
            for (const s of t.sprite.sounds) if (s.asset && s.asset.data) sounds.set(s.md5, s.asset.data);
        }
        lith.syncRenderer();
        const read = async name => sounds.get(name) || null;
        host.buffers = manifest.map(m => m.sounds.map(s => host.decodeSound(s, read)));
        host.manifest = manifest;
    };

    L.begin = async () => {
        const lith = await L.init();
        if (!vm.renderer) throw new Error('renderer not attached');
        const json = vm.toJSON();
        const res = lith.loadProjectJson(json);
        if (!res.ok) { console.warn('Lithium could not load the project', res.log); }
        if (!L.host) {
            L.host = new StageHost(null, lith, vm.renderer);
            lith.setHost((op, a, b, c, d, p) => L.host.handle(op, a, b, c, d, p));
        }
        L.host.reset();
        mapAssets();
        hideVmDrawables(true);
        lith.setTurbo(runtime.turboMode);
        lith.setCompat(runtime.frameLoop.framerate <= 30);
        // answer prompts through Lithium's host UI is not available in the GUI yet
        runtime._step = L.step;
        L.active = true;
        L.frame = 0;
        L.lastDown = false;
    };

    // Copy Lithium's state into the VM targets so the editor, monitors and the
    // sprite info pane reflect what ran.
    L.syncToVm = (full) => {
        const st = L.lith.state();
        for (const s of st.targets) {
            if (!s.isOriginal) continue;
            const t = targetByName(s.name, s.isStage);
            if (!t) continue;
            for (const [name, v] of Object.entries(s.variables)) {
                const variable = Object.values(t.variables).find(x => x.name === name && x.type === '');
                if (variable) variable.value = v;
            }
            for (const [name, items] of Object.entries(s.lists)) {
                const variable = Object.values(t.variables).find(x => x.name === name && x.type === 'list');
                if (variable) variable.value = items;
            }
            if (!full || t.isStage) continue;
            t.setXY(s.x, s.y, true);
            t.setDirection(s.direction);
            t.setSize(s.size);
            t.visible = !!s.visible;
            t.setRotationStyle(s.rotationStyle);
            if (s.costume !== t.currentCostume && s.costume < t.sprite.costumes.length) t.setCostume(s.costume);
            t.volume = s.volume;
            EFFECT_NAMES.forEach((e, i) => t.setEffect(e, s.effects[i]));
        }
        if (full) {
            const stage = runtime.getTargetForStage();
            const ss = st.targets.find(x => x.isStage && x.isOriginal);
            if (stage && ss && ss.costume < stage.sprite.costumes.length) stage.setCostume(ss.costume);
        }
    };

    L.end = () => {
        if (!L.active) return;
        L.active = false;
        runtime._step = orig.step;
        try {
            L.syncToVm(true);
            L.lith.stopAll();
            L.host.reset();
            L.lith.loadProjectJson(EMPTY_PROJECT); // releases Lithium's drawables
        } catch (e) { console.error('Lithium stop failed', e); }
        hideVmDrawables(false);
        runtime._emitProjectRunStatus(0);
        runtime.requestTargetsUpdate && runtime.requestTargetsUpdate(runtime.getEditingTarget());
        if (vm.renderer) vm.renderer.draw();
    };

    const monitorsVisible = () => {
        for (const m of runtime._monitorState.map.values()) if (m.visible) return true;
        return false;
    };

    L.step = () => {
        const lith = L.lith;
        lith.step();
        L.host.draw();
        runtime._emitProjectRunStatus(lith.threadCount());
        if (++L.frame % 6 === 0 && monitorsVisible()) {
            L.syncToVm(true);
            runtime._pushMonitors();
            runtime.sequencer.stepThreads();
            if (runtime._monitorState.dirty) {
                runtime.emit('MONITORS_UPDATE', runtime._monitorState.shallowClone());
                runtime._monitorState.dirty = false;
            }
        }
    };

    // ---- overrides ----------------------------------------------------------
    runtime.greenFlag = function () {
        if (!L.enabled) return orig.greenFlag();
        const go = async () => {
            try {
                if (!L.active) await L.begin();
                else { L.lith.stopAll(); }
                L.lith.greenFlag();
            } catch (e) {
                console.error('Lithium failed, falling back to the built-in runtime', e);
                L.active = false;
                runtime._step = orig.step;
                hideVmDrawables(false);
                orig.greenFlag();
            }
        };
        go();
    };
    runtime.stopAll = function () {
        if (L.active) { L.end(); return; }
        return orig.stopAll();
    };
    vm.startDrag = function (...a) { if (!L.active) return orig.startDrag(...a); };
    vm.postIOData = function (device, data) {
        if (L.active) {
            const lith = L.lith;
            if (device === 'keyboard') {
                lith.postKey(data.key, !!data.isDown);
            } else if (device === 'mouse') {
                let flags = 1 | 2;
                if (data.isDown !== undefined && !!data.isDown !== L.lastDown) { flags |= 4; L.lastDown = !!data.isDown; }
                if (L.lastDown) flags |= 8;
                if (data.wasDragged) flags |= 16;
                lith.postMouse(flags, data.x, data.y, data.canvasWidth, data.canvasHeight);
            }
        }
        return orig.postIOData(device, data);
    };

    return L;
}
