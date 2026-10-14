// Reference runner: executes a project.json in the real scratch-vm, headless,
// and prints the same JSON shape as lithium_run.
//   node ref_run.js <project.json> [--frames N]
'use strict';
const fs = require('fs');
const VM = require('scratch-vm');

function norm(v) {
    if (typeof v === 'number' && !Number.isFinite(v)) return Number.isNaN(v) ? 'NaN' : (v > 0 ? 'Infinity' : '-Infinity');
    return v;
}

async function run(jsonText, frames) {
    const vm = new VM();
    // The sequencer's work budget derives from this; vm.start() normally sets it.
    vm.runtime.currentStepTime = 1000 / 60;
    await vm.loadProject(Buffer.from(jsonText));
    const bubbles = new Map();
    vm.runtime.on('SAY', (target, type, text) => bubbles.set(target, text));
    vm.greenFlag();
    for (let i = 0; i < frames; i++) { vm.runtime._step(); await new Promise(r => setImmediate(r)); }
    const targets = vm.runtime.targets.map(t => ({
        name: t.sprite.name,
        isStage: t.isStage, isOriginal: t.isOriginal,
        size: norm(t.size), visible: t.visible, costume: t.currentCostume, volume: norm(t.volume),
        rotationStyle: t.rotationStyle,
        effects: ['color', 'fisheye', 'whirl', 'pixelate', 'mosaic', 'brightness', 'ghost'].map(k => norm(t.effects[k])),
        pen: (() => { const a = t._customState['Scratch.pen'] || { penDown: false, penAttributes: { diameter: 1 } };
            return { down: !!a.penDown, size: norm((a.penAttributes || {}).diameter || 1), color: [norm(a.color === undefined ? 66.66 : a.color), norm(a.saturation === undefined ? 100 : a.saturation), norm(a.brightness === undefined ? 100 : a.brightness), norm(a.transparency || 0)] }; })(),
        bubble: bubbles.get(t) || '',
        x: norm(t.x), y: norm(t.y), direction: norm(t.direction),
        variables: Object.fromEntries(Object.values(t.variables).filter(v => v.type === '').map(v => [v.name, norm(v.value)])),
        lists: Object.fromEntries(Object.values(t.variables).filter(v => v.type === 'list').map(v => [v.name, v.value.map(norm)])),
    }));
    return { threads: vm.runtime.threads.length, targets, layerOrder: vm.runtime.executableTargets.map(t => t.isStage ? 'Stage' : t.sprite.name) };
}

module.exports = { run };

if (require.main === module) {
    const frames = process.argv.includes('--frames') ? Number(process.argv[process.argv.indexOf('--frames') + 1]) : 10;
    const origWarn = console.warn;
    console.warn = () => {};
    run(fs.readFileSync(process.argv[2], 'utf8'), frames).then(r => {
        console.log(JSON.stringify(r));
        process.exit(0);
    }).catch(e => { console.error(e); process.exit(1); });
}
