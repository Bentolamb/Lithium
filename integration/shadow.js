/* Lithium shadow engine diagnostic adapter.
 * Runs alongside TurboWarp without changing VM state or project behavior.
 * Does not claim scheduling equivalence; intended for first integration only.
 */
(() => {
    'use strict';
    const status = {mode: 'loading', frames: 0, loadLog: '', lastError: null};
    window.LithiumShadow = status;
    const safe = fn => {
        try { return fn(); } catch (error) {
            status.lastError = String(error && error.stack || error);
            status.mode = 'error';
            console.warn('[Lithium shadow]', error);
        }
    };
    function attach(vm, module) {
        if (!vm || !vm.runtime || typeof vm.greenFlag !== 'function') return false;
        if (vm.__lithiumShadowAttached) return true;
        const c = (name, returnType, args) => module.cwrap('lithium_' + name, returnType, args);
        const f = {
            create: c('runtime_create', 'number', []),
            load: c('load_sb3_json', 'number', ['number', 'string']),
            log: c('get_load_log', 'string', ['number']),
            greenFlag: c('green_flag', null, ['number']),
            stop: c('stop_all', null, ['number']),
            step: c('runtime_step', null, ['number']),
            dump: c('dump_state', 'string', ['number']),
            turbo: c('set_turbo_mode', null, ['number', 'number'])
        };
        const handle = f.create();
        if (!handle) throw new Error('Lithium runtime creation failed');
        status.mode = 'ready';
        status.vm = vm;
        status.snapshot = () => safe(() => JSON.parse(f.dump(handle)));
        // Only mirror when an actual Scratch project can be serialized.
        const originalFlag = vm.greenFlag;
        vm.greenFlag = function (...args) {
            safe(() => {
                const project = vm.toJSON();
                if (typeof project !== 'string') throw new Error('Scratch VM toJSON() did not return text');
                const loaded = f.load(handle, project);
                status.loadLog = f.log(handle);
                if (!loaded) {
                    status.mode = 'unsupported-project';
                    return;
                }
                f.turbo(handle, vm.runtime.turboMode ? 1 : 0);
                f.greenFlag(handle);
                status.frames = 0;
                status.mode = 'shadow-running';
            });
            return originalFlag.apply(this, args);
        };
        if (typeof vm.stopAll === 'function') {
            const originalStop = vm.stopAll;
            vm.stopAll = function (...args) {
                safe(() => f.stop(handle));
                if (status.mode === 'shadow-running') status.mode = 'ready';
                return originalStop.apply(this, args);
            };
        }
        if (typeof vm.runtime._step === 'function') {
            const originalStep = vm.runtime._step;
            vm.runtime._step = function (...args) {
                // TurboWarp always executes first and remains authoritative.
                const result = originalStep.apply(this, args);
                if (status.mode === 'shadow-running') {
                    safe(() => { f.step(handle); status.frames++; });
                }
                return result;
            };
        } else {
            status.mode = 'ready-no-step-hook';
        }
        vm.__lithiumShadowAttached = true;
        console.info('[Lithium shadow] connected (diagnostic only; TurboWarp remains authoritative)');
        return true;
    }
    async function initialize() {
        // Generated Emscripten factory is loaded by the HTML before this script.
        if (typeof window.createLithium !== 'function') {
            throw new Error('createLithium factory not found');
        }
        const module = await window.createLithium({
            locateFile: file => '/lithium/' + file
        });
        let retries = 0;
        const timer = setInterval(() => {
            if (safe(() => attach(window.vm, module))) {
                clearInterval(timer);
            } else if (++retries > 240) {
                clearInterval(timer);
                status.mode = 'no-vm';
            }
        }, 250);
    }
    initialize().catch(err => {
        status.mode = 'error';
        status.lastError = String(err);
        console.warn('[Lithium shadow] initialization failed', err);
    });
})();
