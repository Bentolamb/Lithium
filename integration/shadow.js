/* Lithium shadow engine diagnostic adapter.
 * TurboWarp remains authoritative. Only completed, supported, successfully
 * loaded shadow runs may produce a "match" result.
 */
(() => {
    'use strict';

    const status = {
        version: 2,
        mode: 'loading',
        frames: 0,
        runId: 0,
        hasRun: false,
        completed: false,
        loadLog: '',
        lastError: null,
        lastComparison: null
    };
    window.LithiumShadow = status;

    const safe = fn => {
        try {
            return fn();
        } catch (error) {
            status.lastError = String(error && (error.stack || error));
            status.mode = 'error';
            console.warn('[Lithium shadow]', error);
            return undefined;
        }
    };

    const report = (kind, reason) => ({
        status: kind,
        match: false,
        reason,
        differences: [],
        runId: status.runId,
        shadowFrames: status.frames
    });

    // The C++ loader can accept a project yet warn that some blocks were
    // ignored. Those projects must never receive a green "match" verdict.
    const hasUnsupportedFeatures = log =>
        /(^|\n)\s*(?:warning|error|unsupported|unknown|dropped|ignored|unimplemented)\b/i.test(log);

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
            threads: c('thread_count', 'number', ['number']),
            turbo: c('set_turbo_mode', null, ['number', 'number'])
        };
        const handle = f.create();
        if (!handle) throw new Error('Lithium runtime creation failed');

        status.vm = vm;
        status.mode = 'ready';
        status.snapshot = () => safe(() => JSON.parse(f.dump(handle)));
        status.compare = () => safe(() => {
            if (!status.hasRun) {
                return report('not-run', 'Press the green flag to execute a project before comparing');
            }
            if (status.mode === 'error') return report('error', status.lastError || 'Shadow runtime error');
            if (status.mode === 'unsupported-project') return report('unsupported', status.loadLog || 'Project not loaded');
            if (status.mode === 'aborted') return report('aborted', 'Project was stopped before completion');
            if (!window.LithiumCompare) throw new Error('Lithium comparator is unavailable');

            const lithium = JSON.parse(f.dump(handle));
            const turbo = window.LithiumCompare.turboSnapshot(vm);
            const comparison = window.LithiumCompare.compare(lithium, turbo);
            comparison.runId = status.runId;
            comparison.shadowFrames = status.frames;

            // Two empty thread lists can happen before the first scheduler
            // step. That is not proof that the project executed.
            if (!status.completed) {
                comparison.status = 'incomplete';
                comparison.match = false;
                comparison.reason = 'Waiting for both engines to complete the current run';
            } else if (hasUnsupportedFeatures(status.loadLog)) {
                comparison.status = 'unsupported';
                comparison.match = false;
                comparison.reason = 'Lithium reported unsupported or dropped project features';
                comparison.loadLog = status.loadLog;
            }
            return comparison;
        });

        // Load from TurboWarp's current project on each flag, so the Wasm
        // runtime sees editor changes. No shadow state affects the real VM.
        const originalFlag = vm.greenFlag;
        vm.greenFlag = function (...args) {
            status.hasRun = true;
            status.completed = false;
            status.lastComparison = null;
            status.lastError = null;
            status.loadLog = '';
            status.frames = 0;
            status.runId++;
            safe(() => {
                status.mode = 'loading-project';
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
                status.mode = 'shadow-running';
            });
            return originalFlag.apply(this, args);
        };

        if (typeof vm.stopAll === 'function') {
            const originalStop = vm.stopAll;
            vm.stopAll = function (...args) {
                safe(() => f.stop(handle));
                if (status.mode === 'shadow-running') {
                    status.mode = 'aborted';
                    status.completed = false;
                    status.lastComparison = null;
                }
                return originalStop.apply(this, args);
            };
        }

        if (typeof vm.runtime._step === 'function') {
            const originalStep = vm.runtime._step;
            vm.runtime._step = function (...args) {
                const result = originalStep.apply(this, args);
                if (status.mode === 'shadow-running') {
                    safe(() => {
                        f.step(handle);
                        status.frames++;
                        // C API thread_count avoids expensive JSON serialization
                        // on every frame. Full snapshots happen only at the end.
                        const turboThreads = vm.runtime.threads;
                        if (f.threads(handle) === 0 &&
                            Array.isArray(turboThreads) && turboThreads.length === 0) {
                            status.completed = true;
                            status.mode = 'shadow-finished';
                            status.lastComparison = status.compare();
                            if (status.lastComparison) {
                                console.info('[Lithium shadow] final state comparison', status.lastComparison);
                            }
                        }
                    });
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
        if (typeof window.createLithium !== 'function') throw new Error('createLithium factory not found');
        const module = await window.createLithium({locateFile: file => '/lithium/' + file});
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
    initialize().catch(error => {
        status.mode = 'error';
        status.lastError = String(error);
        console.warn('[Lithium shadow] initialization failed', error);
    });
})();
