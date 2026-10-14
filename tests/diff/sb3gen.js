// Tiny project.json builder for generating test programs.
//
// Expressions:   L('text')  literal text      N('5') number literal
//                V('name')  variable reporter  B(opcode, inputs, fields) reporter block
// Statements:    S(opcode, inputs, fields)     inputs may hold {stack:[...]} branches
'use strict';

const L = v => ({ lit: String(v) });
const N = v => ({ lit: String(v), num: true });
const V = name => ({ varRef: name });
// Broadcast message input: BC('go', 'id_go') is the dropdown; BCX(expr, 'go', 'id_go') a reporter over it.
const BC = (name, id) => ({ bc: true, name, id: id === undefined ? 'bc_' + name : id });
const BCX = (expr, name, id) => ({ bcExpr: expr, name, id: id === undefined ? 'bc_' + name : id });
// Field values for list blocks: lf('L') -> LIST field; bf('go') -> BROADCAST_OPTION field.
const lf = name => [name, 'l_' + name];
const bf = (name, id) => [name, id === undefined ? 'bc_' + name : id];
const B = (op, inputs = {}, fields = {}, mutation) => ({ op, inputs, fields, ...(mutation ? { mutation } : {}) });
// Menu shadow block, e.g. CLONE('_myself_').
const MENU = (op, field, value) => ({ menu: op, field, value });
const CLONE = value => MENU('control_create_clone_of_menu', 'CLONE_OPTION', value);
// Argument reporters inside a custom block definition.
const ARG = name => B('argument_reporter_string_number', {}, { VALUE: [name, null] });
const ARGB = name => B('argument_reporter_boolean', {}, { VALUE: [name, null] });
// Call a custom block returned by Target.defineProc; values[i] undefined leaves the slot empty.
const CALL = (proc, ...values) => {
    const inputs = {};
    proc.ids.forEach((id, i) => { if (values[i] !== undefined) inputs[id] = values[i]; });
    return B('procedures_call', inputs, {}, { tagName: 'mutation', children: [], proccode: proc.proccode,
        argumentids: JSON.stringify(proc.ids), warp: String(!!proc.warp) });
};
const S = B;
const stack = (...stmts) => ({ stack: stmts });

// scratch-vm refuses targets without a costume; the asset is never loaded.
const COSTUME = { assetId: 'cd21514d0531fdffb22204e0ec5ed84a', name: 'costume1', md5ext: 'cd21514d0531fdffb22204e0ec5ed84a.svg',
                  dataFormat: 'svg', rotationCenterX: 0, rotationCenterY: 0 };

class Target {
    constructor(project, name, isStage, props = {}) {
        this.project = project;
        this.name = name;
        this.isStage = isStage;
        this.props = props;
        this.blocks = {};
        this.variables = {};
        this.lists = {};
        this.broadcasts = {};
        this.n = 0;
    }
    id(prefix = 'b') { return `${this.name.replace(/\W/g, '')}_${prefix}${this.n++}`; }
    varId(name) { return 'v_' + name; }
    defineVar(name, initial = 0) { this.variables[this.varId(name)] = [name, initial]; }
    defineList(name, items = []) { this.lists['l_' + name] = [name, items]; }
    defineBroadcast(name, id) { this.broadcasts[id === undefined ? 'bc_' + name : id] = name; }

    // Compile an input value to SB3 input JSON.
    input(name, spec) {
        if (spec && spec.stack) {
            const first = this.chain(spec.stack, false);
            return first ? [2, first] : null;
        }
        if (spec && spec.menu) {
            const mid = this.id('m');
            this.blocks[mid] = { opcode: spec.menu, next: null, parent: null, inputs: {}, fields: { [spec.field]: [spec.value, null] }, shadow: true, topLevel: false };
            return [1, mid];
        }
        if (spec && spec.bc) return [1, [11, spec.name, spec.id]];
        if (spec && spec.bcExpr) return [3, this.blockFor(spec.bcExpr), [11, spec.name, spec.id]];
        if (spec && spec.lit !== undefined) return [1, [spec.num ? 4 : 10, spec.lit]];
        if (spec && spec.varRef) return [3, [12, spec.varRef, this.varId(spec.varRef)], [10, '']];
        if (spec && spec.listRef) return [3, [13, spec.listRef, 'l_' + spec.listRef], [10, '']];
        if (spec && spec.op) return [3, this.block(spec, false), [10, '']];
        throw new Error('bad input spec ' + JSON.stringify(spec));
    }
    blockFor(spec) { return this.block(spec, false); }
    // Create a block (and its nested reporters); returns its id.
    block(spec, topLevel) {
        const id = this.id();
        const inputs = {};
        for (const [k, v] of Object.entries(spec.inputs || {})) {
            const compiled = this.input(k, v);
            if (compiled) inputs[k] = compiled;
        }
        const fields = {};
        for (const [k, v] of Object.entries(spec.fields || {})) {
            fields[k] = Array.isArray(v) ? v : [v, null];
        }
        this.blocks[id] = { opcode: spec.op, next: null, parent: null, inputs, fields, shadow: false, topLevel: !!topLevel,
                            ...(topLevel ? { x: 0, y: 0 } : {}), ...(spec.mutation ? { mutation: spec.mutation } : {}) };
        return id;
    }
    // Define a custom block. args: [{name, bool}]; returns a handle for CALL().
    defineProc(proccode, args, warp, ...body) {
        const ids = args.map(a => `arg_${this.name.replace(/\W/g, '')}_${a.name}`);
        const protoId = this.id('p');
        const shadows = {};
        args.forEach((a, i) => {
            const sid = this.id('a');
            this.blocks[sid] = { opcode: a.bool ? 'argument_reporter_boolean' : 'argument_reporter_string_number', next: null, parent: protoId,
                                 inputs: {}, fields: { VALUE: [a.name, null] }, shadow: true, topLevel: false };
            shadows[ids[i]] = [1, sid];
        });
        const defId = this.id('d');
        this.blocks[protoId] = { opcode: 'procedures_prototype', next: null, parent: defId, inputs: shadows, fields: {}, shadow: true, topLevel: false,
            mutation: { tagName: 'mutation', children: [], proccode, argumentids: JSON.stringify(ids), argumentnames: JSON.stringify(args.map(a => a.name)),
                        argumentdefaults: JSON.stringify(args.map(a => a.bool ? 'false' : '')), warp: String(!!warp) } };
        this.blocks[defId] = { opcode: 'procedures_definition', next: null, parent: null, inputs: { custom_block: [1, protoId] }, fields: {},
                               shadow: false, topLevel: true, x: 0, y: 0 };
        const first = this.chain(body, false);
        if (first) { this.blocks[defId].next = first; this.blocks[first].parent = defId; }
        return { proccode, ids, warp };
    }
    // Create a stack of statements; returns the first id.
    chain(stmts, topLevel) {
        let first = null, prev = null;
        for (const s of stmts) {
            const id = this.block(s, topLevel && prev === null);
            if (first === null) first = id;
            if (prev !== null) { this.blocks[prev].next = id; this.blocks[id].parent = prev; }
            prev = id;
        }
        return first;
    }
    script(hat, ...stmts) { return this.chain([hat, ...stmts], true); }
    flag(...stmts) { return this.script(S('event_whenflagclicked'), ...stmts); }
    // set <name> to <expr>
    set(name, expr) { return S('data_setvariableto', { VALUE: expr }, { VARIABLE: [name, this.varId(name)] }); }
    change(name, expr) { return S('data_changevariableby', { VALUE: expr }, { VARIABLE: [name, this.varId(name)] }); }

    toJSON() {
        const base = {
            isStage: this.isStage, name: this.name, variables: this.variables, lists: this.lists, broadcasts: this.broadcasts,
            blocks: this.blocks, comments: {}, currentCostume: 0, costumes: [COSTUME], sounds: [], volume: 100, layerOrder: this.isStage ? 0 : this.order,
        };
        if (this.isStage) return { ...base, tempo: 60, videoTransparency: 50, videoState: 'on', textToSpeechLanguage: null, ...this.props };
        return { ...base, visible: true, x: 0, y: 0, size: 100, direction: 90, draggable: false, rotationStyle: 'all around', ...this.props };
    }
}

class Project {
    constructor() {
        this.stage = new Target(this, 'Stage', true);
        this.stage.broadcastNames = [];
        this.sprites = [];
    }
    sprite(name, props = {}) {
        const t = new Target(this, name, false, props);
        t.order = this.sprites.length + 1;
        this.sprites.push(t);
        return t;
    }
    toJSON() {
        return {
            targets: [this.stage.toJSON(), ...this.sprites.map(s => s.toJSON())],
            monitors: [], extensions: Object.values(this.stage.blocks).concat(...this.sprites.map(x => Object.values(x.blocks))).some(b => /^pen_/.test(b.opcode)) ? ['pen'] : [],
            meta: { semver: '3.0.0', vm: '0.2.0', agent: 'lithium-diff' },
        };
    }
}

module.exports = { L, N, V, B, S, BC, BCX, lf, bf, stack, Project, ARG, ARGB, CALL, CLONE, MENU };
