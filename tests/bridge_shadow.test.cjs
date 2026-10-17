const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vmModule = require('node:vm');
const {compare, turboSnapshot} = require('../integration/compare.js');

async function setup(options = {}) {
    let lithiumThreads = 0;
    let lithiumValue = 0;
    let stepCalls = 0;
    let dumpCalls = 0;
    let interval = null;
    const turbo = {
        runtime: {
            turboMode: false,
            threads: [],
            targets: [{isStage: true, isOriginal: true, getName: () => 'Stage',
                variables: {counter: {name: 'counter', type: '', value: 0}}}],
            _step() {
                this.targets[0].variables.counter.value = 10;
                this.threads = [];
            }
        },
        toJSON: () => JSON.stringify({targets: []}),
        greenFlag() { this.runtime.threads = [{}]; },
        stopAll() { this.runtime.threads = []; }
    };
    const handlers = {
        runtime_create: () => 1,
        load_sb3_json: () => options.loadOk === false ? 0 : 1,
        get_load_log: () => options.loadLog || '',
        green_flag: () => { lithiumThreads = 1; lithiumValue = 0; },
        stop_all: () => { lithiumThreads = 0; },
        runtime_step: () => {
            stepCalls++;
            if (options.stepError) throw new Error('Injected shadow failure');
            if (stepCalls >= (options.finishAfter || 1)) {
                lithiumThreads = 0;
                lithiumValue = 10;
            }
        },
        thread_count: () => lithiumThreads,
        dump_state: () => {
            dumpCalls++;
            return JSON.stringify({threads: lithiumThreads, targets: [{
                name: 'Stage', isStage: true, isOriginal: true,
                variables: {counter: lithiumValue}, lists: {}
            }]});
        },
        set_turbo_mode: () => {}
    };
    const module = {cwrap(name) {
        const handler = handlers[name.replace(/^lithium_/, '')];
        if (!handler) throw Error('Unknown C API: ' + name);
        return handler;
    }};
    const window = {vm: turbo, LithiumCompare: {compare, turboSnapshot}, createLithium: async () => module};
    const sandbox = {
        window,
        console: {info() {}, warn() {}, log() {}},
        setInterval(callback) { interval = callback; return 1; },
        clearInterval() { interval = null; }
    };
    const code = fs.readFileSync(require.resolve('../integration/shadow.js'), 'utf8');
    vmModule.runInNewContext(code, sandbox);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(typeof interval, 'function');
    interval();
    assert.equal(window.LithiumShadow.mode, 'ready');
    return {vm: turbo, shadow: window.LithiumShadow,
        getStats: () => ({stepCalls, dumpCalls})};
}

test('no comparison can pass before a green flag', async () => {
    const {shadow} = await setup();
    const result = shadow.compare();
    assert.equal(result.status, 'not-run');
    assert.equal(result.match, false);
    assert.equal(shadow.runId, 0);
});

test('shadow execution mirrors steps and records a complete match', async () => {
    const {vm, shadow} = await setup();
    vm.greenFlag();
    assert.equal(shadow.mode, 'shadow-running');
    assert.equal(shadow.compare().status, 'incomplete');
    vm.runtime._step();
    assert.equal(shadow.mode, 'shadow-finished');
    assert.equal(shadow.lastComparison.status, 'match');
    assert.equal(shadow.lastComparison.match, true);
    assert.equal(shadow.frames, 1);
    assert.equal(shadow.completed, true);
    assert.equal(shadow.lastError, null);
});

test('each new green flag invalidates the prior successful comparison', async () => {
    const {vm, shadow} = await setup();
    vm.greenFlag();
    vm.runtime._step();
    assert.equal(shadow.lastComparison.status, 'match');
    vm.greenFlag();
    assert.equal(shadow.runId, 2);
    assert.equal(shadow.lastComparison, null);
    assert.equal(shadow.compare().status, 'incomplete');
});

test('unsupported loader warnings never report a match', async () => {
    const {vm, shadow} = await setup({loadLog: 'warning: unknown opcode ignored'});
    vm.greenFlag();
    vm.runtime._step();
    assert.equal(shadow.mode, 'shadow-finished');
    assert.equal(shadow.lastComparison.status, 'unsupported');
    assert.equal(shadow.lastComparison.match, false);
    assert.match(shadow.lastComparison.reason, /unsupported/i);
});

test('rejected project loads never pass as matching', async () => {
    const {vm, shadow} = await setup({loadOk: false, loadLog: 'error: invalid project'});
    vm.greenFlag();
    assert.equal(shadow.mode, 'unsupported-project');
    assert.equal(shadow.compare().status, 'unsupported');
    assert.equal(shadow.compare().match, false);
});

test('pressing Stop makes the comparison aborted', async () => {
    const {vm, shadow} = await setup({finishAfter: 5});
    vm.greenFlag();
    vm.stopAll();
    assert.equal(shadow.compare().status, 'aborted');
    assert.equal(shadow.compare().match, false);
});

test('intermediate shadow steps check thread_count instead of serializing full snapshots', async () => {
    const {vm, shadow, getStats} = await setup({finishAfter: 3});
    vm.greenFlag();
    for (let i = 0; i < 2; i++) vm.runtime._step();
    assert.equal(shadow.mode, 'shadow-running');
    assert.equal(getStats().dumpCalls, 0);
    vm.runtime._step();
    assert.equal(shadow.lastComparison.status, 'match');
    assert.equal(getStats().dumpCalls, 1);
});

test('shadow exceptions are isolated from TurboWarp execution', async () => {
    const {vm, shadow} = await setup({stepError: true});
    vm.greenFlag();
    assert.doesNotThrow(() => vm.runtime._step());
    assert.equal(shadow.mode, 'error');
    assert.equal(shadow.compare().status, 'error');
    assert.match(shadow.lastError, /Injected shadow failure/);
});
