#!/usr/bin/env node
// Inject the diagnostic Wasm bridge into static TurboWarp editor pages.
// Repair stale/partial injections rather than silently accepting them.
import {readFileSync, writeFileSync, existsSync} from 'node:fs';
import {join, resolve} from 'node:path';

const dir = resolve(process.argv[2] || '');
const targets = ['index.html', 'editor.html'];
const sources = [
    '/lithium/lithium_wasm.js',
    '/lithium/compare.js',
    '/lithium/shadow.js',
    '/lithium/panel.js'
];
// A new workflow commit changes the URLs, preventing cached older bridge
// scripts from being reused at the same localhost address.
const version = (process.env.GITHUB_SHA || 'development').replace(/[^a-zA-Z0-9]/g, '').slice(0, 12);
const scripts = sources.map(src => `<script src="${src}?v=${version}"></script>`).join('\n') + '\n';
let count = 0;
for (const name of targets) {
    const path = join(dir, name);
    if (!existsSync(path)) continue;
    let html = readFileSync(path, 'utf8');
    if (!/<\/body>/i.test(html)) throw Error(name + ': no closing body tag');
    // This catches both old two-script builds and complete three-script builds.
    // Remove existing bridge script tags, then insert the canonical ordered set.
    html = html.replace(/<script\b[^>]*\bsrc=["']\/lithium\/(?:lithium_wasm|compare|shadow|panel)\.js(?:\?[^"']*)?["'][^>]*>\s*<\/script>\s*/gi, '');
    html = html.replace(/<\/body>/i, scripts + '</body>');
    const found = sources.map(src => html.split(`<script src="${src}?v=${version}"></script>`).length - 1);
    if (found.some(n => n !== 1)) throw Error(name + ': missing or duplicated bridge script: ' + JSON.stringify(found));
    if (!(html.indexOf(sources[0]) < html.indexOf(sources[1]) &&
          html.indexOf(sources[1]) < html.indexOf(sources[2]) &&
          html.indexOf(sources[2]) < html.indexOf(sources[3]))) {
        throw Error(name + ': invalid bridge script order');
    }
    writeFileSync(path, html);
    console.log('Verified Lithium bridge scripts:', name);
    count++;
}
if (!count) throw Error('Could not find index.html or editor.html in ' + dir);
