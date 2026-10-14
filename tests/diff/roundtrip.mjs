// Editor converter test: every differential-test project goes sb3 -> Blockly XML text -> sb3
// and must behave identically (scratch-vm results equal, Lithium output equal).
//   node roundtrip.mjs [--bin ../../build-rel/lithium_run]
import fs from 'fs';
import os from 'os';
import path from 'path';
import { createRequire } from 'module';
import { spawnSync } from 'child_process';
import { DOMParser } from '@xmldom/xmldom';
import { workspaceTree, treeToXmlString, xmlStringToTree, xmlTreeToSb3Blocks } from '../../web/sb3xml.js';

const require = createRequire(import.meta.url);
const { run } = require('./ref_run.js');
const { cases } = require('./cases.js');
const args = process.argv.slice(2);
const bin = path.resolve(import.meta.dirname, args.includes('--bin') ? args[args.indexOf('--bin') + 1] : '../../build-rel/lithium_run');
console.warn = () => {};

function roundtrip(project) {
    const copy = JSON.parse(JSON.stringify(project));
    for (const t of copy.targets) {
        const xml = treeToXmlString(workspaceTree(t.blocks));
        const tree = xmlStringToTree(xml, DOMParser);
        t.blocks = xmlTreeToSb3Blocks(tree);
    }
    return copy;
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'rt-'));
let total = 0, failed = 0;
const lith = (text, frames, name) => {
    const f = path.join(tmp, name.replace(/\W/g, '_') + '.json');
    fs.writeFileSync(f, text);
    const r = spawnSync(bin, [f, '--frames', String(frames), '--seed', '1', '--tick', '0.0005'], { encoding: 'utf8', maxBuffer: 1 << 28 });
    return r.status === 0 ? r.stdout : 'ERR ' + r.stderr;
};
for (const c of cases()) {
    total++;
    const original = JSON.stringify(c.project);
    const converted = JSON.stringify(roundtrip(JSON.parse(original)));
    let a, b;
    try { a = JSON.stringify(await run(original, c.frames)); b = JSON.stringify(await run(converted, c.frames)); }
    catch (e) { console.log(`FAIL ${c.name}: ${e.message || JSON.stringify(e)}`); failed++; continue; }
    const la = lith(original, c.frames, c.name), lb = lith(converted, c.frames, c.name + '_rt');
    if (a !== b) { console.log(`FAIL ${c.name}: scratch-vm differs after round trip`); failed++; }
    else if (la !== lb) { console.log(`FAIL ${c.name}: lithium differs after round trip`); failed++; }
}
console.log(failed ? `${failed}/${total} FAILED` : `${total}/${total} projects survive sb3 -> XML -> sb3`);
process.exit(failed ? 1 : 0);
