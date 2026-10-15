// Lithium-only blocks survive a save/open round trip through the placeholder form, and the
// editor extension definitions match the opcodes the C++ runtime implements.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { toFallbackJSON, fromFallbackObject, shapeOf, PLACEHOLDER } from '../../web/fallback.js';
import { LITHIUM_EXTENSIONS } from '../../web/extensions.js';

const project = {
    targets: [{
        isStage: true, name: 'Stage', variables: {}, lists: { L: ['L', ['a']] },
        blocks: {
            hat: { opcode: 'event_whenflagclicked', next: 'save', parent: null, inputs: {}, fields: {}, topLevel: true, x: 0, y: 0 },
            save: { opcode: 'outlist_saveList', next: 'set', parent: 'hat', inputs: { NAME: [1, [10, 'data']] }, fields: { LIST: ['L', null] } },
            set: { opcode: 'data_setvariableto', next: null, parent: 'save', inputs: { VALUE: [3, 'cmp', [10, '']] }, fields: { VARIABLE: ['v', 'id'] } },
            cmp: { opcode: 'packin_compress', next: null, parent: 'set', inputs: { DATA: [1, [10, 'abc']] }, fields: {}, shadow: false },
        },
    }],
    extensions: ['pen', 'outlist', 'packin'], monitors: [], meta: { semver: '3.0.0' },
};
const original = JSON.stringify(project);
const saved = JSON.parse(toFallbackJSON(original));
assert.deepEqual(saved.extensions, ['pen']);
const b = saved.targets[0].blocks;
assert.equal(b.save.opcode, 'procedures_call');
assert.equal(b.save.mutation.proccode, PLACEHOLDER);
assert.equal(b.save.mutation.argumentids, '[]');
assert.equal(b.save.mutation.return, undefined);
assert.equal(b.cmp.mutation.return, '1');
assert.equal(b.save.next, 'set'); // connections are unchanged
assert.equal(b.hat.opcode, 'event_whenflagclicked');

// restores from the project map
const back1 = fromFallbackObject(JSON.parse(JSON.stringify(saved)));
assert.deepEqual(back1.targets[0].blocks, project.targets[0].blocks);
assert.deepEqual(new Set(back1.extensions), new Set(['pen', 'outlist', 'packin']));

// restores when another editor dropped meta (inputs and fields stay on the block)
const stripped = JSON.parse(JSON.stringify(saved));
delete stripped.meta.lithium;
const back2 = fromFallbackObject(stripped);
assert.equal(back2.targets[0].blocks.save.opcode, 'outlist_saveList');
assert.deepEqual(back2.targets[0].blocks.save.fields, { LIST: ['L', null] });
assert.equal(back2.targets[0].blocks.cmp.opcode, 'packin_compress');
assert.ok(back2.extensions.includes('packin'));

// a placeholder with nothing to restore from stays an empty custom block
const bare = JSON.parse(JSON.stringify(saved));
delete bare.meta.lithium;
delete bare.targets[0].blocks.save.mutation.lithiumop;
assert.equal(fromFallbackObject(bare).targets[0].blocks.save.opcode, 'procedures_call');

// projects without Lithium blocks are returned untouched; bad input does not throw
const plain = JSON.stringify({ targets: [{ name: 'Stage', blocks: {} }], extensions: ['pen'] });
assert.equal(toFallbackJSON(plain), plain);
assert.equal(toFallbackJSON('not json'), 'not json');

// every extension block is implemented by the runtime, and the other way round
const cpp = fs.readFileSync(new URL('../../runtime/src/primitives_lithium.cpp', import.meta.url), 'utf8');
const implemented = new Set([...cpp.matchAll(/\{"((?:lithiumlab|packin|outlist)_\w+)"/g)].map(m => m[1]));
const declared = new Set();
for (const [id, Cls] of Object.entries(LITHIUM_EXTENSIONS)) {
    for (const blk of new Cls({}).getInfo().blocks) {
        declared.add(`${id}_${blk.opcode}`);
        assert.equal(shapeOf(`${id}_${blk.opcode}`), { command: 0, reporter: 1, Boolean: 2 }[blk.blockType], blk.opcode);
    }
}
assert.deepEqual([...declared].sort(), [...implemented].sort());
console.log('fallback tests passed:', declared.size, 'blocks');
