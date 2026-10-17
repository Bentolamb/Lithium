import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync, mkdirSync, writeFileSync, rmSync, unlinkSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const script = fileURLToPath(new URL('../integration/verify_bundle.mjs', import.meta.url));
const names = ['lithium_wasm.js', 'compare.js', 'shadow.js', 'panel.js'];
const html = '<html><body>' + names.map(x => `<script src="/lithium/${x}"></script>`).join('') +
    '</body></html>';
function fixture() {
    const dir = mkdtempSync(join(tmpdir(), 'lithium-bundle-'));
    mkdirSync(join(dir, 'lithium'));
    for (const name of names) writeFileSync(join(dir, 'lithium', name), '/* stub */');
    writeFileSync(join(dir, 'lithium', 'lithium_wasm.wasm'), Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]));
    writeFileSync(join(dir, 'index.html'), html);
    writeFileSync(join(dir, 'editor.html'), html);
    return dir;
}
const run = dir => spawnSync(process.execPath, [script, dir], {encoding:'utf8'});

test('complete WebAssembly editor bundle passes integrity checks', () => {
    const dir = fixture();
    try { assert.equal(run(dir).status, 0); }
    finally { rmSync(dir, {recursive:true, force:true}); }
});
test('missing comparator asset fails packaging integrity checks', () => {
    const dir = fixture();
    try {
        unlinkSync(join(dir, 'lithium', 'compare.js'));
        const result = run(dir);
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, /compare\.js/);
    } finally { rmSync(dir, {recursive:true, force:true}); }
});
test('stale two-script editor fails even when assets exist', () => {
    const dir = fixture();
    try {
        writeFileSync(join(dir, 'editor.html'), html.replace(
            '<script src="/lithium/compare.js"></script>', ''));
        const result = run(dir);
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, /editor\.html/);
    } finally { rmSync(dir, {recursive:true, force:true}); }
});
test('invalid Wasm binary is rejected', () => {
    const dir = fixture();
    try {
        writeFileSync(join(dir, 'lithium', 'lithium_wasm.wasm'), 'not wasm');
        const result = run(dir);
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, /WebAssembly header/);
    } finally { rmSync(dir, {recursive:true, force:true}); }
});
