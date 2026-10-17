#!/usr/bin/env node
// Verify that the archived static editor has every Lithium asset and that
// every HTML entrypoint loads the bridge in the correct order.
import {existsSync, readFileSync, statSync} from 'node:fs';
import {join, resolve} from 'node:path';

const root = resolve(process.argv[2] || '.');
const scripts = ['lithium_wasm.js', 'compare.js', 'shadow.js', 'panel.js'];
for (const file of [...scripts, 'lithium_wasm.wasm']) {
    const path = join(root, 'lithium', file);
    if (!existsSync(path) || statSync(path).size === 0) {
        throw Error('Missing or empty build artifact: ' + path);
    }
}
const wasmMagic = readFileSync(join(root, 'lithium', 'lithium_wasm.wasm')).subarray(0, 4);
if (!wasmMagic.equals(Buffer.from([0x00, 0x61, 0x73, 0x6d]))) {
    throw Error('Lithium binary does not have a WebAssembly header');
}

for (const name of ['index.html', 'editor.html']) {
    const path = join(root, name);
    if (!existsSync(path)) throw Error('Missing entrypoint ' + name);
    const html = readFileSync(path, 'utf8');
    let previous = -1;
    for (const file of scripts) {
        const tag = `<script src="/lithium/${file}"></script>`;
        const first = html.indexOf(tag);
        const second = first < 0 ? -1 : html.indexOf(tag, first + tag.length);
        if (first < 0 || second !== -1) throw Error(name + ': missing/duplicate script ' + file);
        if (first <= previous) throw Error(name + ': wrong script order at ' + file);
        previous = first;
    }
    console.log('Verified Lithium integration in ' + name);
}
console.log('PASS: all diagnostic bridge scripts and WebAssembly artifacts present');
