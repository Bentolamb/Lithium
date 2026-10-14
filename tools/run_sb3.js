// Run an .sb3 in the Lithium WASM runtime and print the final state.
//
//   node tools/run_sb3.js <project.sb3> [--wasm build-wasm/lithium_wasm.js]
//        [--seconds 5] [--frames N] [--renderer] [--turbo] [--compat]
//
// Without --frames it steps at 60 Hz in real time until no thread is left or
// --seconds elapse (default 5; projects with a forever loop run until then).
// With --frames it steps exactly N times as fast as possible.
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

// Minimal zip reader: enough to pull project.json out of an .sb3.
function readZipEntry(buf, wanted) {
    let eocd = -1;
    for (let i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--) {
        if (buf.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) throw new Error('not a zip file');
    const count = buf.readUInt16LE(eocd + 10);
    let p = buf.readUInt32LE(eocd + 16);
    for (let n = 0; n < count; n++) {
        if (buf.readUInt32LE(p) !== 0x02014b50) throw new Error('corrupt zip directory');
        const method = buf.readUInt16LE(p + 10);
        const csize = buf.readUInt32LE(p + 20);
        const nameLen = buf.readUInt16LE(p + 28);
        const extraLen = buf.readUInt16LE(p + 30);
        const commentLen = buf.readUInt16LE(p + 32);
        const localOffset = buf.readUInt32LE(p + 42);
        const name = buf.toString('utf8', p + 46, p + 46 + nameLen);
        if (name === wanted) {
            const lNameLen = buf.readUInt16LE(localOffset + 26);
            const lExtraLen = buf.readUInt16LE(localOffset + 28);
            const start = localOffset + 30 + lNameLen + lExtraLen;
            const data = buf.subarray(start, start + csize);
            if (method === 0) return data;
            if (method === 8) return zlib.inflateRawSync(data);
            throw new Error('unsupported zip compression method ' + method);
        }
        p += 46 + nameLen + extraLen + commentLen;
    }
    throw new Error(wanted + ' not found in zip');
}

function parseArgs(argv) {
    const o = { file: null, wasm: 'build-wasm/lithium_wasm.js', seconds: 5, frames: null,
                renderer: false, turbo: false, compat: false };
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--wasm') o.wasm = argv[++i];
        else if (a === '--seconds') o.seconds = Number(argv[++i]);
        else if (a === '--frames') o.frames = Number(argv[++i]);
        else if (a === '--renderer') o.renderer = true;
        else if (a === '--turbo') o.turbo = true;
        else if (a === '--compat') o.compat = true;
        else if (!o.file) o.file = a;
        else throw new Error('unexpected argument ' + a);
    }
    if (!o.file) throw new Error('usage: node tools/run_sb3.js <project.sb3> [options]');
    return o;
}

async function main() {
    const o = parseArgs(process.argv.slice(2));
    const projectJson = o.file.endsWith('.json')
        ? fs.readFileSync(o.file, 'utf8')
        : readZipEntry(fs.readFileSync(o.file), 'project.json').toString('utf8');
    const m = await require(path.resolve(o.wasm))();
    const c = (name, ret, args) => m.cwrap('lithium_' + name, ret, args);
    const rt = c('runtime_create', 'number', [])();
    const ok = c('load_sb3_json', 'number', ['number', 'string'])(rt, projectJson);
    const log = c('get_load_log', 'string', ['number'])(rt);
    if (log) process.stderr.write(log);
    if (!ok) process.exit(1);
    c('set_renderer_attached', null, ['number', 'number'])(rt, o.renderer ? 1 : 0);
    c('set_turbo_mode', null, ['number', 'number'])(rt, o.turbo ? 1 : 0);
    c('set_compatibility_mode', null, ['number', 'number'])(rt, o.compat ? 1 : 0);
    const step = c('runtime_step', null, ['number']);
    const threads = c('thread_count', 'number', ['number']);

    const t0 = process.hrtime.bigint();
    c('green_flag', null, ['number'])(rt);
    let steps = 0;
    if (o.frames !== null) {
        for (; steps < o.frames; steps++) step(rt);
    } else {
        const frameMs = o.compat ? 1000 / 30 : 1000 / 60;
        const deadline = Date.now() + o.seconds * 1000;
        while (threads(rt) > 0 && Date.now() < deadline) {
            const began = Date.now();
            step(rt);
            steps++;
            const left = frameMs - (Date.now() - began);
            if (left > 1) await new Promise(r => setTimeout(r, left));
        }
    }
    const ms = Number(process.hrtime.bigint() - t0) / 1e6;
    const state = JSON.parse(c('dump_state', 'string', ['number'])(rt));
    console.log(JSON.stringify({ steps, elapsedMs: Math.round(ms * 10) / 10,
                                 runningThreads: state.threads, targets: state.targets }, null, 2));
    c('runtime_destroy', null, ['number'])(rt);
}

main().catch(e => { console.error(e.message); process.exit(1); });
