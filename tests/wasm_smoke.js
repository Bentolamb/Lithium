// Smoke test: load the WASM runtime in Node and drive it through the C API.
// Usage: node tests/wasm_smoke.js <path to lithium_wasm.js>
const assert = require('assert');
const path = require('path');

const modulePath = path.resolve(process.argv[2] || 'build-wasm/lithium_wasm.js');
const createLithium = require(modulePath);

createLithium().then(m => {
    const version = m.cwrap('lithium_version', 'string', []);
    const create = m.cwrap('lithium_runtime_create', 'number', []);
    const destroy = m.cwrap('lithium_runtime_destroy', null, ['number']);
    const step = m.cwrap('lithium_runtime_step', null, ['number']);
    // uint64_t crosses the WASM boundary as a BigInt, so convert explicitly.
    // Prefer 32-bit ints or doubles in hot-path APIs to avoid BigInt cost.
    const framesBig = m.cwrap('lithium_runtime_frame_count', 'bigint', ['number']);
    const frames = rt => Number(framesBig(rt));

    assert.strictEqual(version(), '0.0.1');
    const rt = create();
    assert.notStrictEqual(rt, 0);
    assert.strictEqual(frames(rt), 0);
    step(rt);
    step(rt);
    assert.strictEqual(frames(rt), 2);
    destroy(rt);
    console.log('wasm smoke test: PASS (version ' + version() + ')');
}).catch(err => {
    console.error('wasm smoke test: FAIL', err);
    process.exit(1);
});
