import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync, writeFileSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const script = fileURLToPath(new URL('../integration/inject_shadow.mjs', import.meta.url));
test('injects exactly once into a generated editor page', () => {
    const dir = mkdtempSync(join(tmpdir(), 'lithium-html-'));
    try {
        const file = join(dir, 'index.html');
        writeFileSync(file, '<html><body><div>Editor</div></body></html>');
        for (let n = 0; n < 2; n++) {
            const result = spawnSync(process.execPath, [script, dir], {encoding:'utf8'});
            assert.equal(result.status, 0, result.stderr);
        }
        const html = readFileSync(file, 'utf8');
        assert.equal(html.split('/lithium/shadow.js').length - 1, 1);
        assert.ok(html.indexOf('lithium_wasm.js') < html.indexOf('shadow.js'));
        assert.match(html, /<\/body>/);
    } finally {
        rmSync(dir,{recursive:true,force:true});
    }
});
test('fails if no supported HTML entrypoint exists', () => {
    const dir = mkdtempSync(join(tmpdir(), 'lithium-html-'));
    try {
        const result = spawnSync(process.execPath, [script, dir], {encoding:'utf8'});
        assert.notEqual(result.status, 0);
    } finally {
        rmSync(dir,{recursive:true,force:true});
    }
});

test('upgrades a stale two-script injection into three ordered scripts', () => {
    const dir = mkdtempSync(join(tmpdir(), 'lithium-upgrade-'));
    try {
        const file = join(dir, 'editor.html');
        writeFileSync(file, '<html><body><script src="/lithium/lithium_wasm.js"></script>\n' +
            '<script src="/lithium/shadow.js"></script></body></html>');
        const result = spawnSync(process.execPath, [script, dir], {encoding:'utf8'});
        assert.equal(result.status, 0, result.stderr);
        const html = readFileSync(file, 'utf8');
        for (const name of ['lithium_wasm', 'compare', 'shadow']) {
            assert.equal(html.split('/lithium/' + name + '.js').length - 1, 1);
        }
        assert.ok(html.indexOf('/lithium/lithium_wasm.js') < html.indexOf('/lithium/compare.js'));
        assert.ok(html.indexOf('/lithium/compare.js') < html.indexOf('/lithium/shadow.js'));
    } finally { rmSync(dir, {recursive:true,force:true}); }
});
