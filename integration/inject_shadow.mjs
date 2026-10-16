#!/usr/bin/env node
// Attach Lithium's diagnostic WASM engine to the static TurboWarp build.
// Run after upstream npm run build. This does not rewrite scratch-vm.
import {readFileSync, writeFileSync, existsSync} from 'node:fs';
import {join, resolve} from 'node:path';

const dir = resolve(process.argv[2] || '');
const targets = ['index.html', 'editor.html'];
const scripts = '<script src="/lithium/lithium_wasm.js"></script>\n' +
    '<script src="/lithium/compare.js"></script>\n' +
    '<script src="/lithium/shadow.js"></script>\n';
let count = 0;
for (const name of targets) {
    const path = join(dir, name);
    if (!existsSync(path)) continue;
    let html = readFileSync(path, 'utf8');
    if (html.includes('/lithium/shadow.js')) {
        console.log('Already configured:', name);
        count++;
        continue;
    }
    if (!/<\/body>/i.test(html)) throw Error(name + ': no closing body tag');
    html = html.replace(/<\/body>/i, scripts + '</body>');
    writeFileSync(path, html);
    console.log('Added shadow engine:', name);
    count++;
}
if (!count) throw Error('Could not find index.html or editor.html in ' + dir);
