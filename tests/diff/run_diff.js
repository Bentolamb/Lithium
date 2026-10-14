// Differential test runner: scratch-vm vs Lithium on generated projects.
//   node run_diff.js [--bin ../../build/lithium_run] [--only substring] [--verbose] [--keep dir]
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const { run } = require('./ref_run.js');
const { cases } = require('./cases.js');

const args = process.argv.slice(2);
const opt = (n, d) => (args.includes(n) ? args[args.indexOf(n) + 1] : d);
const bin = path.resolve(__dirname, opt('--bin', '../../build/lithium_run'));
const only = opt('--only', '');
const verbose = args.includes('--verbose');
const keepDir = opt('--keep', null);
const showMax = Number(opt('--show', 12));

// Lithium prints doubles like 1.0; normalise through JSON.parse. -0 == 0 in JS.
function sameExact(a, b) {
    if (typeof a !== typeof b) return false;
    if (typeof a === 'number') return a === b || (Number.isNaN(a) && Number.isNaN(b));
    return a === b;
}

// Distance in units in the last place between two finite doubles.
const f64 = new Float64Array(1), i64 = new BigInt64Array(f64.buffer);
function ulps(a, b) {
    const key = x => { f64[0] = x; const n = i64[0]; return n < 0n ? -(n & 0x7fffffffffffffffn) : n; };
    const d = key(a) - key(b);
    return Number(d < 0n ? -d : d);
}

let maxUlp = 0;
// `tol` = allowed ulp difference for libm-dependent results (0 = exact).
function compare(ref, lit, tol) {
    const diffs = [];
    const same = (a, b) => {
        if (typeof a === 'number' && typeof b === 'number' && tol > 0 && Number.isFinite(a) && Number.isFinite(b) && a !== b) {
            const u = ulps(a, b);
            if (u <= tol) { if (u > maxUlp) maxUlp = u; return true; }
        }
        return sameExact(a, b);
    };
    if (ref.targets.length !== lit.targets.length) diffs.push(`target count ${ref.targets.length} vs ${lit.targets.length}`);
    for (let ti = 0; ti < Math.min(ref.targets.length, lit.targets.length); ti++) {
        const rt = ref.targets[ti], lt = lit.targets[ti];
        if ((rt.isStage ? 'Stage' : rt.name) !== lt.name || rt.isOriginal !== lt.isOriginal) { diffs.push(`target ${ti}: ref=${rt.name}${rt.isOriginal ? '' : '(clone)'} lithium=${lt.name}${lt.isOriginal ? '' : '(clone)'}`); continue; }
        if (!rt.isStage) for (const k of ['x', 'y', 'direction']) if (!same(rt[k], lt[k])) diffs.push(`${rt.name}.${k}: ref=${JSON.stringify(rt[k])} lithium=${JSON.stringify(lt[k])}`);
        const names = new Set([...Object.keys(rt.variables), ...Object.keys(lt.variables)]);
        for (const n of names) if (!same(rt.variables[n], lt.variables[n])) diffs.push(`${rt.name}.${n}: ref=${JSON.stringify(rt.variables[n])} lithium=${JSON.stringify(lt.variables[n])}`);
        if (lt.lists) {
            const ln = new Set([...Object.keys(rt.lists || {}), ...Object.keys(lt.lists)]);
            for (const n of ln) {
                const a = (rt.lists || {})[n], b = lt.lists[n];
                if (!a || !b || a.length !== b.length || a.some((x, i) => !same(x, b[i]))) diffs.push(`${rt.name}.list ${n}: ref=${JSON.stringify(a)} lithium=${JSON.stringify(b)}`);
            }
        }
    }
    if (ref.threads !== lit.threads) diffs.push(`running threads: ref=${ref.threads} lithium=${lit.threads}`);
    return diffs;
}

(async () => {
    const tmp = keepDir || fs.mkdtempSync(path.join(os.tmpdir(), 'lithium-diff-'));
    fs.mkdirSync(tmp, { recursive: true });
    const warn = console.warn;
    let failed = 0, total = 0, compared = 0;
    for (const c of cases()) {
        if (only && !c.name.includes(only)) continue;
        total++;
        const file = path.join(tmp, c.name.replace(/\W/g, '_') + '.json');
        const text = JSON.stringify(c.project);
        fs.writeFileSync(file, text);
        let ref, lit;
        try { ref = await run(text, c.frames); } catch (e) { console.log(`FAIL ${c.name}: reference run failed: ${e.message || JSON.stringify(e)}`); failed++; continue; }
        const r = spawnSync(bin, [file, '--frames', String(c.frames), '--seed', '1', '--tick', '0.0005'], { encoding: 'utf8', maxBuffer: 1 << 28 });
        if (r.status !== 0) { console.log(`FAIL ${c.name}: lithium_run exited ${r.status}: ${r.stderr.trim().split('\n').slice(0, 3).join(' | ')}`); failed++; continue; }
        lit = JSON.parse(r.stdout);
        const diffs = compare(ref, lit, c.ulp || 0);
        const nvars = ref.targets.reduce((n, t) => n + Object.keys(t.variables).length, 0);
        compared += nvars;
        if (diffs.length) {
            failed++;
            console.log(`FAIL ${c.name}: ${diffs.length} differences (of ${nvars} variables)`);
            for (const d of diffs.slice(0, verbose ? 1e9 : showMax)) console.log('   ' + d);
        } else if (verbose) console.log(`ok   ${c.name} (${nvars} variables)`);
    }
    console.log(`\n${total - failed}/${total} cases identical, ${compared} variable values compared` + (maxUlp ? ` (largest tolerated libm difference: ${maxUlp} ulp)` : ''));
    if (!keepDir) fs.rmSync(tmp, { recursive: true, force: true });
    process.exit(failed ? 1 : 0);
})();
