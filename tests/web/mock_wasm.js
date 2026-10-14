// Stand-in for build-wasm/lithium_wasm.js, used only by the UI test: it
// implements the C API surface the web shell calls with a trivial simulation,
// so the page logic (zip reading, controls, drawing, tables) can be tested
// where no Emscripten toolchain exists. Real integration = tests/wasm_smoke.js.
window.createLithium = async function () {
    const S = { project: null, running: false, frames: 0, log: '', turbo: 0, compat: 0, renderer: 0 };
    const impl = {
        runtime_create: () => 1,
        runtime_destroy: () => {},
        version: () => 'mock',
        load_sb3_json: (rt, text) => {
            try {
                const p = JSON.parse(text);
                S.project = p.targets.map(t => ({ name: t.name, isStage: !!t.isStage, x: t.x || 0, y: t.y || 0,
                    direction: t.direction ?? 90, size: 100, visible: true,
                    variables: Object.fromEntries(Object.values(t.variables || {}).map(v => [v[0], v[1]])) }));
                S.log = 'warning: mock loader\n';
                return 1;
            } catch (e) { S.log = 'error: ' + e.message + '\n'; return 0; }
        },
        load_builtin: (rt, name) => { S.project = [{ name: 'Stage', isStage: true, variables: { count: 0 }, x: 0, y: 0, direction: 90, size: 100, visible: true },
                                                  { name: 'Sprite1', isStage: false, variables: {}, x: 0, y: 0, direction: 90, size: 100, visible: true }]; return 1; },
        get_load_log: () => S.log,
        green_flag: () => { S.running = true; },
        stop_all: () => { S.running = false; },
        runtime_step: () => { S.frames++; if (S.running && S.project) { const s = S.project.find(t => !t.isStage); if (s) s.x = (s.x + 10) % 200; } },
        set_turbo_mode: (rt, on) => { S.turbo = on; },
        set_compatibility_mode: (rt, on) => { S.compat = on; },
        set_renderer_attached: (rt, on) => { S.renderer = on; },
        thread_count: () => (S.running ? 1 : 0),
        dump_state: () => JSON.stringify({ frames: S.frames, threads: S.running ? 1 : 0, targets: S.project || [], _mock: S }),
    };
    return { cwrap: name => impl[name.replace(/^lithium_/, '')] };
};
