// Thin JS binding over the Lithium WASM module's C API, plus a browser-side
// reader for .sb3 files. The UI only talks to the runtime through this file.

export async function loadRuntime(factory) {
    const m = await factory();
    const c = (name, ret, args) => m.cwrap('lithium_' + name, ret, args);
    const f = {
        create: c('runtime_create', 'number', []),
        destroy: c('runtime_destroy', null, ['number']),
        version: c('version', 'string', []),
        loadSb3: c('load_sb3_json', 'number', ['number', 'string']),
        loadBuiltin: c('load_builtin', 'number', ['number', 'string']),
        loadLog: c('get_load_log', 'string', ['number']),
        greenFlag: c('green_flag', null, ['number']),
        stopAll: c('stop_all', null, ['number']),
        step: c('runtime_step', null, ['number']),
        turbo: c('set_turbo_mode', null, ['number', 'number']),
        compat: c('set_compatibility_mode', null, ['number', 'number']),
        renderer: c('set_renderer_attached', null, ['number', 'number']),
        threads: c('thread_count', 'number', ['number']),
        dump: c('dump_state', 'string', ['number']),
        setHost: c('set_host_callback', null, ['number', 'number']),
        manifest: c('asset_manifest', 'string', ['number']),
        setSkin: c('set_costume_skin', null, ['number', 'number', 'number', 'number']),
        sync: c('sync_renderer', null, ['number']),
        key: c('post_key', null, ['number', 'string', 'number']),
        mouse: c('post_mouse', null, ['number', 'number', 'number', 'number', 'number', 'number']),
        answer: c('post_answer', null, ['number', 'string']),
        soundDone: c('sound_finished', null, ['number', 'number']),
        setStorageCb: c('set_storage_callback', null, ['number', 'number']),
        storageDone: c('storage_complete', null, ['number', 'number', 'number', 'number', 'number']),
        storageScope: c('set_storage_scope', null, ['number', 'string']),
        storagePrime: c('storage_prime', null, ['number', 'string']),
        storageAreaOf: c('storage_area', 'string', ['number']),
    };
    // Typed views into WASM memory for the host callback (heap views are re-read
    // on every call because memory growth replaces the underlying buffer).
    const mem = {
        string: p => m.UTF8ToString(p),
        f64: (p, n) => new Float64Array(m.HEAPF64.buffer, p, n),
        i32: (p, n) => new Int32Array(m.HEAP32.buffer, p, n),
        setF64: (p, arr) => { new Float64Array(m.HEAPF64.buffer, p, arr.length).set(arr); },
    };
    let hostFn = 0;
    let storageFn = 0;
    let scope = 'project';
    let storageHandler = null;
    const rt = f.create();
    const reply = (id, status, data) => {
        if (data && data.length) {
            const ptr = m._malloc(data.length);
            m.HEAPU8.set(data, ptr);
            f.storageDone(rt, id, status, ptr, data.length);
            m._free(ptr);
        } else {
            f.storageDone(rt, id, status, 0, 0);
        }
    };
    // The browser has a stage to draw on, so motion blocks request redraws.
    f.renderer(rt, 1);
    return {
        version: f.version(),
        loadProjectJson(text) { const ok = f.loadSb3(rt, text) === 1; return { ok, log: f.loadLog(rt) }; },
        loadBuiltin(name) { return f.loadBuiltin(rt, name) === 1; },
        greenFlag() { f.greenFlag(rt); },
        stopAll() { f.stopAll(rt); },
        step() { f.step(rt); },
        setTurbo(on) { f.turbo(rt, on ? 1 : 0); },
        setCompat(on) { f.compat(rt, on ? 1 : 0); },
        threadCount() { return f.threads(rt); },
        state() { return JSON.parse(f.dump(rt)); },
        mem,
        // handler(op, a, b, c, d, p) -> number; see runtime/include/lithium/c_api.h
        setHost(handler) {
            if (hostFn) m.removeFunction(hostFn);
            hostFn = handler ? m.addFunction((op, a, b, c, d, p) => handler(op, a, b, c, d, p), 'diddddi') : 0;
            f.setHost(rt, hostFn);
        },
        // OutList storage. handler(op, area, name, bytes) -> Promise<{status, data}>, with op
        // 0 write, 1 read, 2 exists, 3 delete and status 0 ok, 1 not found, 2 error.
        setStorage(handler) {
            if (storageFn) m.removeFunction(storageFn);
            storageHandler = handler;
            storageFn = handler ? m.addFunction((id, op, areaPtr, namePtr, dataPtr, len) => {
                const area = m.UTF8ToString(areaPtr), name = m.UTF8ToString(namePtr);
                const bytes = op === 0 ? m.HEAPU8.slice(dataPtr, dataPtr + len) : null;
                Promise.resolve().then(() => handler(op, area, name, bytes)).then(
                    r => reply(id, r.status, r.data), () => reply(id, 2, null));
            }, 'viiiiii') : 0;
            f.setStorageCb(rt, storageFn);
        },
        setStorageScope(name) { scope = String(name || 'project'); f.storageScope(rt, scope); },
        storageArea() { return f.storageAreaOf(rt); },
        primeStorageIndex(names) { f.storagePrime(rt, names.join('\n')); },
        assetManifest() { return JSON.parse(f.manifest(rt)); },
        setCostumeSkin(t, c, skin) { f.setSkin(rt, t, c, skin); },
        syncRenderer() { f.sync(rt); },
        postKey(key, down) { f.key(rt, key, down ? 1 : 0); },
        // flags: 1 x, 2 y, 4 down-changed, 8 down, 16 dragged
        postMouse(flags, x, y, w, h) { f.mouse(rt, flags, x, y, w, h); },
        postAnswer(text) { f.answer(rt, text); },
        soundFinished(id) { f.soundDone(rt, id); },
    };
}

// Returns the bytes of one file inside a zip (an .sb3 is a zip).
export async function readZipEntry(buffer, wanted) {
    const dv = new DataView(buffer);
    const bytes = new Uint8Array(buffer);
    const dec = new TextDecoder();
    let eocd = -1;
    for (let i = buffer.byteLength - 22; i >= Math.max(0, buffer.byteLength - 65557); i--) {
        if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) throw new Error('not a zip file');
    const count = dv.getUint16(eocd + 10, true);
    let p = dv.getUint32(eocd + 16, true);
    for (let n = 0; n < count; n++) {
        if (dv.getUint32(p, true) !== 0x02014b50) throw new Error('corrupt zip directory');
        const method = dv.getUint16(p + 10, true);
        const csize = dv.getUint32(p + 20, true);
        const nameLen = dv.getUint16(p + 28, true);
        const extraLen = dv.getUint16(p + 30, true);
        const commentLen = dv.getUint16(p + 32, true);
        const local = dv.getUint32(p + 42, true);
        const name = dec.decode(bytes.subarray(p + 46, p + 46 + nameLen));
        if (name === wanted) {
            const start = local + 30 + dv.getUint16(local + 26, true) + dv.getUint16(local + 28, true);
            const data = bytes.subarray(start, start + csize);
            if (method === 0) return data;
            if (method !== 8) throw new Error('unsupported zip compression method ' + method);
            const stream = new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
            return new Uint8Array(await new Response(stream).arrayBuffer());
        }
        p += 46 + nameLen + extraLen + commentLen;
    }
    throw new Error(wanted + ' not found in zip');
}

export async function readSb3ProjectJson(file) {
    const bytes = await readZipEntry(await file.arrayBuffer(), 'project.json');
    return new TextDecoder().decode(bytes);
}

// Reads a whole .sb3: project.json text and the zip buffer (for assets).
export async function readSb3(file) {
    const buffer = await file.arrayBuffer();
    const bytes = await readZipEntry(buffer, 'project.json');
    return { json: new TextDecoder().decode(bytes), buffer };
}

// IndexedDB-backed OutList storage: one record per saved list, keyed "<area>/<name>".
// Each put is a single transaction, so a write either lands completely or not at all.
export function indexedDbStorage(dbName = 'lithium-outlist') {
    let dbPromise = null;
    const open = () => dbPromise || (dbPromise = new Promise((resolve, reject) => {
        const req = indexedDB.open(dbName, 1);
        req.onupgradeneeded = () => req.result.createObjectStore('lists');
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
    }));
    const request = r => new Promise((resolve, reject) => { r.onsuccess = () => resolve(r.result); r.onerror = () => reject(r.error); });
    return {
        async handler(op, area, name, bytes) {
            const key = area + '/' + name;
            try {
                const db = await open();
                if (op === 0) {
                    await new Promise((res, rej) => {
                        const tx = db.transaction('lists', 'readwrite');
                        tx.objectStore('lists').put(bytes, key);
                        tx.oncomplete = res; tx.onerror = tx.onabort = () => rej(tx.error);
                    });
                    return { status: 0 };
                }
                const store = db.transaction('lists', op === 3 ? 'readwrite' : 'readonly').objectStore('lists');
                const value = await request(store.get(key));
                if (value === undefined) return { status: 1 };
                if (op === 3) {
                    await new Promise((res, rej) => {
                        const tx = db.transaction('lists', 'readwrite');
                        tx.objectStore('lists').delete(key);
                        tx.oncomplete = res; tx.onerror = tx.onabort = () => rej(tx.error);
                    });
                    return { status: 0 };
                }
                return { status: 0, data: op === 1 ? new Uint8Array(value) : undefined };
            } catch (e) {
                return { status: 2 };
            }
        },
        // Names saved under an area (for "exists?").
        async list(area) {
            const db = await open();
            const keys = await request(db.transaction('lists', 'readonly').objectStore('lists')
                .getAllKeys(IDBKeyRange.bound(area + '/', area + '/\uffff')));
            return keys.map(k => k.slice(area.length + 1));
        },
    };
}
