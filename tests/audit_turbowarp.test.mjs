import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {execFileSync, spawnSync} from 'node:child_process';

const script = resolve('tools/audit_turbowarp.mjs');
function fixture() {
    const root = mkdtempSync(join(tmpdir(), 'lithium-upstream-test-'));
    for (const name of ['scratch-gui', 'scratch-vm', 'scratch-render']) {
        const dir = join(root, name);
        mkdirSync(dir);
        execFileSync('git', ['init', '-q', dir]);
        execFileSync('git', ['-C', dir, 'config', 'user.email', 'test@example.invalid']);
        execFileSync('git', ['-C', dir, 'config', 'user.name', 'Test']);
        execFileSync('git', ['-C', dir, 'remote', 'add', 'origin', `https://github.com/TurboWarp/${name}.git`]);
        const dependencies = name === 'scratch-gui'
            ? Object.fromEntries(['scratch-vm','scratch-render','scratch-audio','scratch-blocks','scratch-paint']
                .map(v => [v, 'github:TurboWarp/'+v+'#develop'])) : {};
        writeFileSync(join(dir, 'package.json'), JSON.stringify({name, license:'MPL-2.0',
            scripts:{build:'webpack'}, dependencies}));
        writeFileSync(join(dir, 'package-lock.json'), '{}');
        execFileSync('git', ['-C', dir, 'add', '.']);
        execFileSync('git', ['-C', dir, 'commit', '-qm', 'fixture']);
    }
    return root;
}
function run(root) {return spawnSync(process.execPath,[script,root],{encoding:'utf8'});}

test('records SHAs and GUI dependency pins without modifying checkout', () => {
    const root=fixture();
    try {
        const result=run(root);
        assert.equal(result.status,0,result.stderr);
        const audit=JSON.parse(readFileSync(join(root,'UPSTREAM_AUDIT.json'),'utf8'));
        assert.match(audit.sources['scratch-vm'].sha,/^[a-f0-9]{40}$/);
        assert.equal(audit.integration['scratch-vm'],'github:TurboWarp/scratch-vm#develop');
        assert.equal(audit.sources['scratch-gui'].dirty,false);
    } finally {rmSync(root,{recursive:true,force:true});}
});
test('rejects altered repository origin', () => {
    const root=fixture();
    try {
        execFileSync('git',['-C',join(root,'scratch-vm'),'remote','set-url','origin',
            'https://github.com/attacker/scratch-vm.git']);
        const result=run(root);
        assert.notEqual(result.status,0);
        assert.match(result.stderr,/unexpected origin/);
    } finally {rmSync(root,{recursive:true,force:true});}
});
test('rejects missing GUI dependency', () => {
    const root=fixture();
    try {
        const file=join(root,'scratch-gui','package.json');
        const pkg=JSON.parse(readFileSync(file));
        delete pkg.dependencies['scratch-paint'];
        writeFileSync(file,JSON.stringify(pkg));
        const result=run(root);
        assert.notEqual(result.status,0);
        assert.match(result.stderr,/scratch-paint/);
    } finally {rmSync(root,{recursive:true,force:true});}
});
