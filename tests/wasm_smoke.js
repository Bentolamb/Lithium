// WASM test: load the runtime in Node and run built-in projects through the C API.
// Usage: node tests/wasm_smoke.js <path to lithium_wasm.js>
const assert = require('assert');
const fs = require('fs');
const path = require('path');

const createLithium = require(path.resolve(process.argv[2] || 'build-wasm/lithium_wasm.js'));

createLithium().then(m => {
    const version = m.cwrap('lithium_version', 'string', []);
    const create = m.cwrap('lithium_runtime_create', 'number', []);
    const destroy = m.cwrap('lithium_runtime_destroy', null, ['number']);
    const loadBuiltin = m.cwrap('lithium_load_builtin', 'number', ['number', 'string']);
    const setRenderer = m.cwrap('lithium_set_renderer_attached', null, ['number', 'number']);
    const setTurbo = m.cwrap('lithium_set_turbo_mode', null, ['number', 'number']);
    const useClock = m.cwrap('lithium_use_manual_clock', null, ['number', 'number']);
    const greenFlag = m.cwrap('lithium_green_flag', null, ['number']);
    const stopAll = m.cwrap('lithium_stop_all', null, ['number']);
    const step = m.cwrap('lithium_runtime_step', null, ['number']);
    const frames = m.cwrap('lithium_runtime_frame_count', 'number', ['number']);
    const threads = m.cwrap('lithium_thread_count', 'number', ['number']);
    const varStr = m.cwrap('lithium_get_variable_string', 'string', ['number', 'string', 'string']);
    const getPos = m.cwrap('lithium_get_position', 'number', ['number', 'string', 'number', 'number']);
    const pos = (rt, target) => {
        const p = m._malloc(16);
        assert.strictEqual(getPos(rt, target, p, p + 8), 1);
        const r = [m.getValue(p, 'double'), m.getValue(p + 8, 'double')];
        m._free(p);
        return r;
    };

    assert.strictEqual(version(), '0.0.2');
    const rt = create();
    assert.notStrictEqual(rt, 0);
    assert.strictEqual(loadBuiltin(rt, 'nope'), 0);

    // Headless repeat loop completes inside one step.
    useClock(rt, 0);
    assert.strictEqual(loadBuiltin(rt, 'repeat_counter'), 1);
    greenFlag(rt);
    assert.strictEqual(threads(rt), 1);
    step(rt);
    assert.strictEqual(varStr(rt, 'Stage', 'count'), '10');
    assert.strictEqual(threads(rt), 0);
    assert.strictEqual(frames(rt), 1);

    // Renderer attached: one loop iteration per frame.
    assert.strictEqual(loadBuiltin(rt, 'slide'), 1);
    setRenderer(rt, 1);
    greenFlag(rt);
    for (let i = 1; i <= 10; i++) {
        step(rt);
        assert.strictEqual(pos(rt, 'Sprite1')[0], 10 * i);
    }
    assert.strictEqual(threads(rt), 1);
    step(rt);
    assert.strictEqual(threads(rt), 0);

    // Turbo removes the redraw gate.
    setTurbo(rt, 1);
    greenFlag(rt);
    step(rt);
    assert.strictEqual(pos(rt, 'Sprite1')[0], 100);
    setTurbo(rt, 0);

    // Work-time budget stops a forever loop; stop all clears it.
    setRenderer(rt, 0);
    useClock(rt, 0.5);
    assert.strictEqual(loadBuiltin(rt, 'forever_counter'), 1);
    greenFlag(rt);
    step(rt);
    assert.strictEqual(threads(rt), 1);
    assert.ok(Number(varStr(rt, 'Stage', 'count')) > 1);
    stopAll(rt);
    assert.strictEqual(threads(rt), 0);

    // SB3 loading: the fixtures from TurboWarp.
    const loadSb3 = m.cwrap('lithium_load_sb3_json', 'number', ['number', 'string']);
    const loadLog = m.cwrap('lithium_get_load_log', 'string', ['number']);
    const dump = m.cwrap('lithium_dump_state', 'string', ['number']);
    const fixture = n => fs.readFileSync(path.join(__dirname, 'projects', n + '.json'), 'utf8');
    assert.strictEqual(loadSb3(rt, 'garbage'), 0);
    assert.ok(loadLog(rt).startsWith('error:'));
    useClock(rt, 0);
    setRenderer(rt, 0);
    assert.strictEqual(loadSb3(rt, fixture('count')), 1);
    greenFlag(rt);
    step(rt);
    assert.strictEqual(varStr(rt, 'Stage', 'count'), '1000');
    assert.strictEqual(loadSb3(rt, fixture('mathmix')), 1);
    greenFlag(rt);
    step(rt);
    assert.strictEqual(varStr(rt, 'Stage', 'a'), '7.299999999999999');
    assert.strictEqual(varStr(rt, 'Stage', 's'), '7.299999999999999abc');
    assert.strictEqual(loadSb3(rt, fixture('slide')), 1);
    greenFlag(rt);
    step(rt);
    const state = JSON.parse(dump(rt));
    assert.strictEqual(state.targets[1].x, 110);
    assert.strictEqual(state.targets[1].y, -50);

    destroy(rt);
    console.log('wasm test: PASS (version ' + version() + ')');
}).catch(err => {
    console.error('wasm test: FAIL', err);
    process.exit(1);
});
