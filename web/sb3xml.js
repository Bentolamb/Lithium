// Converts between SB3 block JSON (what project.json stores) and the Blockly XML
// that the scratch-blocks workspace reads and writes. Pure functions over a small
// tree type {tag, attrs, children, text}, so the same code runs in the browser
// (DOM <-> tree adapters below) and in Node tests.
//
// The rules mirror scratch-vm: engine/blocks.js blockToXML (sb3 -> xml) and
// serialization/sb3.js (primitive compression, input shapes).

// opcode -> [primitive type code, field name]
const PRIMITIVES = {
    math_number: [4, 'NUM'], math_positive_number: [5, 'NUM'], math_whole_number: [6, 'NUM'], math_integer: [7, 'NUM'],
    math_angle: [8, 'NUM'], colour_picker: [9, 'COLOUR'], text: [10, 'TEXT'], event_broadcast_menu: [11, 'BROADCAST_OPTION'],
    data_variable: [12, 'VARIABLE'], data_listcontents: [13, 'LIST'],
};
const PRIMITIVE_BY_CODE = Object.fromEntries(Object.entries(PRIMITIVES).map(([op, [code, field]]) => [code, [op, field]]));
const FIELD_VARIABLE_TYPE = { VARIABLE: '', LIST: 'list', BROADCAST_OPTION: 'broadcast_msg' };

const el = (tag, attrs = {}, children = [], text) => ({ tag, attrs, children, text });

// ---------------------------------------------------------------- sb3 -> graph

// A normalised block graph: id -> {id, opcode, next, parent, shadow, topLevel, x, y,
// inputs: {name: {block, shadow}}, fields: {name: {value, id, variableType}}, mutation}.
export function sb3ToGraph(blocks) {
    const g = new Map();
    const primitiveBlock = (arr, id, parent, shadow) => {
        const [opcode, fieldName] = PRIMITIVE_BY_CODE[arr[0]];
        const field = { value: arr[1], id: arr[2] === undefined ? null : arr[2], variableType: FIELD_VARIABLE_TYPE[fieldName] };
        const node = { id, opcode, next: null, parent, shadow, topLevel: false, x: 0, y: 0, inputs: {}, fields: { [fieldName]: field }, mutation: null };
        if (arr.length > 3 && (arr[0] === 12 || arr[0] === 13)) { node.topLevel = true; node.x = arr[3]; node.y = arr[4]; node.parent = null; }
        g.set(id, node);
        return id;
    };
    const ref = (v, owner, name, shadow) => {
        if (v === null || v === undefined) return null;
        if (Array.isArray(v)) return primitiveBlock(v, `${owner}__${name}__${shadow ? 's' : 'b'}`, owner, shadow);
        return v;
    };
    for (const [id, b] of Object.entries(blocks)) {
        if (Array.isArray(b)) { primitiveBlock(b, id, null, false); continue; }
        const node = {
            id, opcode: b.opcode, next: b.next || null, parent: b.parent || null, shadow: !!b.shadow, topLevel: !!b.topLevel,
            x: b.x || 0, y: b.y || 0, inputs: {}, fields: {}, mutation: b.mutation || null,
        };
        for (const [name, v] of Object.entries(b.fields || {})) {
            node.fields[name] = { value: v[0], id: v.length > 1 ? v[1] : null, variableType: FIELD_VARIABLE_TYPE[name] };
        }
        for (const [name, v] of Object.entries(b.inputs || {})) {
            if (!Array.isArray(v)) continue;
            const kind = v[0];
            if (kind === 1) { const s = ref(v[1], id, name, true); node.inputs[name] = { block: s, shadow: s }; }
            else if (kind === 2) node.inputs[name] = { block: ref(v[1], id, name, false), shadow: null };
            else node.inputs[name] = { block: ref(v[1], id, name, false), shadow: ref(v[2], id, name, true) };
        }
        g.set(id, node);
    }
    // Primitives in a "same block and shadow" input are shadows; a non-shadow variable
    // reporter dropped into an input (kind 3 with a primitive block) stays a real block.
    return g;
}

// ------------------------------------------------------------------ graph -> xml

const xmlAttr = v => String(v);

function mutationToTree(m) {
    const attrs = {};
    for (const [k, v] of Object.entries(m)) {
        if (k === 'tagName' || k === 'children') continue;
        attrs[k] = xmlAttr(v);
    }
    const kids = (m.children || []).map(mutationToTree);
    return { tag: m.tagName || 'mutation', attrs, children: kids };
}

function blockToTree(g, id, top) {
    const b = g.get(id);
    if (!b) return null;
    const attrs = { id: b.id, type: b.opcode };
    if (top && b.topLevel) { attrs.x = String(b.x); attrs.y = String(b.y); }
    const node = el(b.shadow ? 'shadow' : 'block', attrs);
    if (b.mutation) node.children.push(mutationToTree(b.mutation));
    for (const [name, inp] of Object.entries(b.inputs)) {
        if (!inp.block && !inp.shadow) continue;
        const v = el('value', { name });
        if (inp.block) v.children.push(blockToTree(g, inp.block, false));
        if (inp.shadow && inp.shadow !== inp.block) v.children.push(blockToTree(g, inp.shadow, false));
        node.children.push(v);
    }
    for (const [name, f] of Object.entries(b.fields)) {
        const fa = { name };
        if (f.id) fa.id = f.id;
        if (typeof f.variableType === 'string') fa.variabletype = f.variableType;
        node.children.push(el('field', fa, [], String(f.value)));
    }
    if (b.next) {
        const n = blockToTree(g, b.next, false);
        if (n) node.children.push(el('next', {}, [n]));
    }
    return node;
}

// Top-level scripts of one target as XML trees (children of <xml>).
export function sb3BlocksToXmlTrees(blocks) {
    const g = sb3ToGraph(blocks);
    const out = [];
    for (const b of g.values()) if (b.topLevel && !b.shadow) out.push(blockToTree(g, b.id, true));
    return out;
}

// <xml> root with <variables> and the scripts; variables: [{id, name, type, isLocal}].
export function workspaceTree(blocks, variables = []) {
    const root = el('xml', { xmlns: 'http://www.w3.org/1999/xhtml' });
    if (variables.length) {
        root.children.push(el('variables', {}, variables.map(v =>
            el('variable', { type: v.type || '', id: v.id, ...(v.isLocal ? { islocal: 'true' } : {}) }, [], v.name))));
    }
    root.children.push(...sb3BlocksToXmlTrees(blocks));
    return root;
}

// ------------------------------------------------------------------ xml -> graph

let autoId = 0;

// Reads the <block>/<shadow> elements under `root` (a workspace <xml> element).
export function xmlTreeToGraph(root, newId = () => 'blk' + (++autoId)) {
    const g = new Map();
    const read = (node, parent, top) => {
        const id = node.attrs.id || newId();
        const b = {
            id, opcode: node.attrs.type, next: null, parent, shadow: node.tag === 'shadow', topLevel: top,
            x: top ? Number(node.attrs.x) || 0 : 0, y: top ? Number(node.attrs.y) || 0 : 0, inputs: {}, fields: {}, mutation: null,
        };
        g.set(id, b);
        for (const c of node.children) {
            if (c.tag === 'mutation') {
                const m = { tagName: 'mutation', children: (c.children || []).map(toMutation) };
                Object.assign(m, c.attrs);
                b.mutation = m;
            } else if (c.tag === 'field') {
                b.fields[c.attrs.name] = { value: c.text === undefined ? '' : c.text, id: c.attrs.id || null, variableType: c.attrs.variabletype };
            } else if (c.tag === 'value' || c.tag === 'statement') {
                let block = null, shadow = null;
                for (const k of c.children) {
                    if (k.tag === 'block') block = read(k, id, false);
                    else if (k.tag === 'shadow') shadow = read(k, id, false);
                }
                if (block === null && shadow !== null) block = shadow;
                if (block !== null || shadow !== null) b.inputs[c.attrs.name] = { block, shadow };
            } else if (c.tag === 'next') {
                const k = c.children.find(x => x.tag === 'block' || x.tag === 'shadow');
                if (k) { b.next = read(k, id, false); }
            }
        }
        return id;
    };
    const toMutation = c => ({ tagName: c.tag, children: (c.children || []).map(toMutation), ...c.attrs });
    for (const c of root.children) {
        if (c.tag === 'block' || c.tag === 'shadow') read(c, null, true);
    }
    // a next block's parent is the previous block
    for (const b of g.values()) if (b.next && g.has(b.next)) g.get(b.next).parent = b.id;
    return g;
}

// ------------------------------------------------------------------ graph -> sb3

export function graphToSb3(g) {
    const out = {};
    const prim = b => {
        const info = PRIMITIVES[b.opcode];
        if (!info || b.next || Object.keys(b.inputs).length) return null;
        const f = b.fields[info[1]];
        if (!f) return null;
        const arr = [info[0], f.value];
        if (b.opcode === 'event_broadcast_menu') arr.push(f.id);
        else if (b.opcode === 'data_variable' || b.opcode === 'data_listcontents') {
            arr.push(f.id);
            if (b.topLevel) arr.push(Math.round(b.x) || 0, Math.round(b.y) || 0);
        }
        return arr;
    };
    const inlined = new Set();
    const asRef = id => {
        if (id === null || id === undefined) return null;
        const b = g.get(id);
        if (!b) return null;
        const p = prim(b);
        if (p) { inlined.add(id); return p; }
        return id;
    };
    for (const b of g.values()) {
        const p = prim(b);
        if (p && !b.topLevel) continue;  // compressed into its parent input
        if (p && b.topLevel) { out[b.id] = p; continue; }
        const o = { opcode: b.opcode, next: b.next, parent: b.parent, inputs: {}, fields: {}, shadow: b.shadow, topLevel: b.topLevel };
        if (b.topLevel) { o.x = Math.round(b.x) || 0; o.y = Math.round(b.y) || 0; }
        for (const [name, inp] of Object.entries(b.inputs)) {
            if (inp.block === inp.shadow) o.inputs[name] = [1, asRef(inp.block)];
            else if (inp.shadow === null) o.inputs[name] = [2, asRef(inp.block)];
            else o.inputs[name] = [3, asRef(inp.block), asRef(inp.shadow)];
        }
        for (const [name, f] of Object.entries(b.fields)) o.fields[name] = f.id ? [f.value, f.id] : [f.value, null];
        if (b.mutation) o.mutation = b.mutation;
        out[b.id] = o;
    }
    return out;
}

// Convenience: workspace XML tree -> sb3 blocks object.
export function xmlTreeToSb3Blocks(root, newId) { return graphToSb3(xmlTreeToGraph(root, newId)); }

// ------------------------------------------------------- text / DOM adapters

const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

export function treeToXmlString(t) {
    const attrs = Object.entries(t.attrs || {}).map(([k, v]) => ` ${k}="${esc(v)}"`).join('');
    const inner = (t.children || []).map(treeToXmlString).join('') + (t.text !== undefined ? esc(t.text) : '');
    return inner === '' ? `<${t.tag}${attrs}/>` : `<${t.tag}${attrs}>${inner}</${t.tag}>`;
}

// DOM Element -> tree (browser, or Node with @xmldom).
export function domToTree(node) {
    const attrs = {};
    for (const a of Array.from(node.attributes || [])) attrs[a.name] = a.value;
    const children = [];
    let text = '';
    let hasText = false;
    for (const c of Array.from(node.childNodes)) {
        if (c.nodeType === 1) children.push(domToTree(c));
        else if (c.nodeType === 3 || c.nodeType === 4) { text += c.nodeValue; hasText = true; }
    }
    const t = { tag: node.tagName.toLowerCase(), attrs, children };
    if (hasText && !children.length) t.text = text;
    return t;
}

export function xmlStringToTree(str, DOMParserCtor) {
    const P = DOMParserCtor || globalThis.DOMParser;
    const doc = new P().parseFromString(str, 'text/xml');
    return domToTree(doc.documentElement);
}
