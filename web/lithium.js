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
    };
    const rt = f.create();
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
