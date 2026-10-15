// Saving and opening projects that use Lithium-only blocks in editors that do not know them.
//
// On save each Lithium-only block becomes a call to an empty custom block named exactly
// "Lithium-only Extension block", so scripts keep their shape and connections. What is needed to
// bring the block back is kept in three places, so no single editor behaviour loses it:
//   1. the block's own inputs and fields, left as they were,
//   2. a "lithiumop" attribute on the block's mutation (the original opcode),
//   3. a project-level map in meta.lithium (opcode, inputs, fields, mutation, block shape).
// The Lithium extensions are removed from the project's extension list on save (other editors
// refuse to load a project that names an extension they do not have) and added back on load.
// The converted block is only a placeholder elsewhere: it does nothing there.

export const PLACEHOLDER = 'Lithium-only Extension block';
const EXTENSIONS = ['lithiumlab', 'outlist', 'packin'];
const REPORTERS = new Set(['outlist_lastError', 'packin_compress', 'packin_decompress', 'packin_compressedSize',
    'packin_originalSize', 'packin_compressAtLevel']);
const BOOLEANS = new Set(['outlist_exists', 'packin_isValid', 'lithiumlab_isTurboWarp', 'lithiumlab_isCompiled',
    'lithiumlab_isLithium', 'lithiumlab_isExtensionAvailable']);

// 0 command, 1 reporter, 2 boolean (scratch-blocks' PROCEDURES_CALL_TYPE_*).
export const shapeOf = opcode => (REPORTERS.has(opcode) ? 1 : BOOLEANS.has(opcode) ? 2 : 0);
const extensionOf = opcode => opcode.slice(0, opcode.indexOf('_'));
const isLithiumOpcode = opcode => typeof opcode === 'string' && EXTENSIONS.includes(extensionOf(opcode));

// project.json text -> project.json text for other editors. Unchanged when no Lithium block is used.
export const toFallbackJSON = text => {
    let project;
    try {
        project = JSON.parse(text);
    } catch (e) {
        return text;
    }
    if (!project || !Array.isArray(project.targets)) return text;
    const restore = {};
    let count = 0;
    for (const target of project.targets) {
        for (const [id, block] of Object.entries(target.blocks || {})) {
            if (Array.isArray(block) || !isLithiumOpcode(block.opcode)) continue;
            const shape = shapeOf(block.opcode);
            restore[`${target.name}/${id}`] = {
                opcode: block.opcode,
                shape,
                inputs: block.inputs || {},
                fields: block.fields || {},
                mutation: block.mutation || null
            };
            const mutation = {
                tagName: 'mutation',
                children: [],
                proccode: PLACEHOLDER,
                argumentids: '[]',
                warp: 'false',
                lithiumop: block.opcode
            };
            if (shape) mutation.return = String(shape);
            block.opcode = 'procedures_call';
            block.mutation = mutation;
            count++;
        }
    }
    if (!count) return text;
    if (Array.isArray(project.extensions)) {
        project.extensions = project.extensions.filter(e => !EXTENSIONS.includes(e));
    }
    if (project.extensionURLs) {
        for (const e of EXTENSIONS) delete project.extensionURLs[e];
    }
    project.meta = project.meta || {};
    project.meta.lithium = {version: 1, blocks: restore};
    return JSON.stringify(project);
};

// Parsed project -> same object with placeholders turned back into Lithium blocks.
export const fromFallbackObject = project => {
    if (!project || !Array.isArray(project.targets)) return project;
    const map = (project.meta && project.meta.lithium && project.meta.lithium.blocks) || {};
    const used = new Set();
    for (const target of project.targets) {
        for (const [id, block] of Object.entries(target.blocks || {})) {
            if (Array.isArray(block) || block.opcode !== 'procedures_call') continue;
            const mutation = block.mutation;
            if (!mutation || mutation.proccode !== PLACEHOLDER) continue;
            const saved = map[`${target.name}/${id}`];
            const opcode = saved ? saved.opcode : mutation.lithiumop;
            if (!isLithiumOpcode(opcode)) continue; // an empty custom block with no way back
            block.opcode = opcode;
            if (saved) {
                block.inputs = saved.inputs;
                block.fields = saved.fields;
                if (saved.mutation) block.mutation = saved.mutation; else delete block.mutation;
            } else {
                delete block.mutation;
            }
            used.add(extensionOf(opcode));
        }
    }
    if (used.size) {
        project.extensions = Array.from(new Set([...(project.extensions || []), ...used]));
    }
    return project;
};
