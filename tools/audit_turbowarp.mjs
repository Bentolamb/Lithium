#!/usr/bin/env node
// Audit official TurboWarp checkouts before dependency installation.
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';

const workspace = resolve(process.argv[2] || '.upstream-turbowarp');
const repos = ['scratch-gui', 'scratch-vm', 'scratch-render'];
const fail = message => { console.error(`ERROR: ${message}`); process.exitCode = 1; };
const git = (cwd, ...args) => execFileSync('git', ['-C', cwd, ...args], {encoding: 'utf8'}).trim();
const summary = {schema: 1, sources: {}, integration: {}};

for (const name of repos) {
    const dir = join(workspace, name);
    if (!existsSync(join(dir, '.git'))) { fail(`${name}: missing git checkout`); continue; }
    try {
        const origin = git(dir, 'remote', 'get-url', 'origin');
        const allowed = [`https://github.com/TurboWarp/${name}.git`, `git@github.com:TurboWarp/${name}.git`];
        if (!allowed.includes(origin)) { fail(`${name}: unexpected origin ${origin}`); continue; }
        const sha = git(dir, 'rev-parse', 'HEAD');
        const dirty = git(dir, 'status', '--porcelain').length > 0;
        const pkg = JSON.parse(readFileSync(join(dir, 'package.json'), 'utf8'));
        const lockfile = ['package-lock.json', 'npm-shrinkwrap.json', 'yarn.lock', 'pnpm-lock.yaml']
            .find(f => existsSync(join(dir, f))) || null;
        summary.sources[name] = {origin, sha, dirty, license: pkg.license || null, version: pkg.version || null,
            lockfile, scripts: {start: pkg.scripts?.start || null, test: pkg.scripts?.test || null,
                build: pkg.scripts?.build || null}};
        if (dirty) console.warn(`WARNING: ${name} working tree has modifications`);
        if (!lockfile) console.warn(`WARNING: ${name} has no recognized lockfile; don't assume npm ci works`);
    } catch (e) { fail(`${name}: ${e.message}`); }
}

if (Object.keys(summary.sources).length === repos.length) {
    const gui = JSON.parse(readFileSync(join(workspace, 'scratch-gui', 'package.json'), 'utf8'));
    const required = ['scratch-vm', 'scratch-render', 'scratch-audio', 'scratch-blocks', 'scratch-paint'];
    const dependencies = gui.dependencies || {};
    for (const name of required) {
        const spec = dependencies[name] || null;
        summary.integration[name] = spec;
        if (!spec) fail(`scratch-gui dependency not found: ${name}`);
    }
}

if (!process.exitCode) {
    const dest = join(workspace, 'UPSTREAM_AUDIT.json');
    writeFileSync(dest, JSON.stringify(summary, null, 2) + '\n');
    console.log(`PASS: audited ${repos.length} TurboWarp repositories; report: ${dest}`);
    console.log(`GUI dependencies: ${JSON.stringify(summary.integration)}`);
}
