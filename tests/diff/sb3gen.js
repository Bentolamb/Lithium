// Tiny project.json builder for generating test programs.
//
// Expressions:   L('text')  literal text      N('5') number literal
//                V('name')  variable reporter  B(opcode, inputs, fields) reporter block
// Statements:    S(opcode, inputs, fields)     inputs may hold {stack:[...]} branches
'use strict';

const L = v => ({ lit: String(v) });
const N = v => ({ lit: String(v), num: true });
const V = name => ({ varRef: name });
const B = (op, inputs = {}, fields = {}) => ({ op, inputs, fields });
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
        this.n = 0;
    }
    id(prefix = 'b') { return `${this.name.replace(/\W/g, '')}_${prefix}${this.n++}`; }
    varId(name) { return 'v_' + name; }
    defineVar(name, initial = 0) { this.variables[this.varId(name)] = [name, initial]; }
    defineList(name, items = []) { this.lists['l_' + name] = [name, items]; }

    // Compile an input value to SB3 input JSON.
    input(name, spec) {
        if (spec && spec.stack) {
            const first = this.chain(spec.stack, false);
            return first ? [2, first] : null;
        }
        if (spec && spec.lit !== undefined) return [1, [spec.num ? 4 : 10, spec.lit]];
        if (spec && spec.varRef) return [3, [12, spec.varRef, this.varId(spec.varRef)], [10, '']];
        if (spec && spec.listRef) return [3, [13, spec.listRef, 'l_' + spec.listRef], [10, '']];
        if (spec && spec.op) return [3, this.block(spec, false), [10, '']];
        throw new Error('bad input spec ' + JSON.stringify(spec));
    }
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
            isStage: this.isStage, name: this.name, variables: this.variables, lists: this.lists, broadcasts: {},
            blocks: this.blocks, comments: {}, currentCostume: 0, costumes: [COSTUME], sounds: [], volume: 100, layerOrder: this.isStage ? 0 : this.order,
        };
        if (this.isStage) return { ...base, tempo: 60, videoTransparency: 50, videoState: 'on', textToSpeechLanguage: null };
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
            monitors: [], extensions: [],
            meta: { semver: '3.0.0', vm: '0.2.0', agent: 'lithium-diff' },
        };
    }
}

module.exports = { L, N, V, B, S, stack, Project };
