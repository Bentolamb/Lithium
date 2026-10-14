// Block editor: a scratch-blocks workspace per target, kept in sync with the
// project.json model through sb3xml.js. The runtime is fed by reloading the
// serialised model when the project runs.
import { workspaceTree, treeToXmlString, xmlTreeToSb3Blocks, domToTree } from './sb3xml.js';
import { makeToolbox } from './toolbox.js';
import { definePenBlocks } from './pen_blocks.js';

export class Editor {
    constructor(container, { onChange } = {}) {
        this.Blockly = window.Blockly;
        definePenBlocks(this.Blockly);
        this.container = container;
        this.onChange = onChange || (() => {});
        this.project = null;
        this.current = -1;
        this.workspace = null;
        this.loading = false;
        this.dirty = false;
        this.setupDialogs();
    }

    setupDialogs() {
        const B = this.Blockly;
        // Simple prompt-based dialogs (a nicer modal comes with the full editor UI).
        B.prompt = (message, defaultValue, callback) => {
            const v = window.prompt(message, defaultValue);
            callback(v === null ? null : v);
        };
        B.Procedures.externalProcedureDefCallback = (mutation, callback) => {
            // "Make a Block": name with %s / %b placeholders become number/text and boolean arguments.
            const name = window.prompt('Block name (use %s for a number/text argument, %b for a boolean)', 'my block');
            if (!name) { callback(null); return; }
            const ids = [], names = [], defaults = [];
            let n = 0;
            const proccode = name.replace(/%[sb]/g, tok => {
                n++;
                ids.push('arg' + n + '_' + Math.random().toString(36).slice(2, 7));
                names.push('arg' + n);
                defaults.push(tok === '%b' ? 'false' : '');
                return tok;
            });
            callback(this.mutationDom(proccode, ids, names, defaults));
        };
    }

    mutationDom(proccode, ids, names, defaults) {
        const mutation = document.createElementNS('http://www.w3.org/1999/xhtml', 'mutation');
        mutation.setAttribute('proccode', proccode);
        mutation.setAttribute('argumentids', JSON.stringify(ids));
        mutation.setAttribute('argumentnames', JSON.stringify(names));
        mutation.setAttribute('argumentdefaults', JSON.stringify(defaults));
        mutation.setAttribute('warp', 'false');
        return mutation;
    }

    // ---- project model ---------------------------------------------------
    loadProject(project) {
        this.project = project;
        this.current = -1;
        this.selectTarget(Math.min(1, project.targets.length - 1));
    }

    get target() { return this.project && this.project.targets[this.current]; }

    targetNames() { return this.project.targets.map(t => (t.isStage ? 'Stage' : t.name)); }

    selectTarget(index) {
        if (!this.project) return;
        if (this.workspace) this.saveCurrent();
        this.current = index;
        this.buildWorkspace();
    }

    toolboxContext() {
        const t = this.target;
        const stage = this.project.targets.find(x => x.isStage);
        const sounds = t.sounds || [];
        return {
            isStage: !!t.isStage, self: t.name,
            sprites: this.project.targets.filter(x => !x.isStage).map(x => x.name),
            costume: ((t.costumes || [])[0] || {}).name, backdrop: ((stage.costumes || [])[0] || {}).name,
            sound: (sounds[0] || {}).name,
        };
    }

    buildWorkspace() {
        const B = this.Blockly;
        this.loading = true;
        if (this.workspace) this.workspace.dispose();
        this.container.replaceChildren();
        const ws = B.inject(this.container, {
            toolbox: makeToolbox(this.toolboxContext()),
            media: 'vendor/media/',
            zoom: { controls: true, wheel: true, startScale: 0.75, maxScale: 3, minScale: 0.25, scaleSpeed: 1.2 },
            colours: undefined,
            grid: { spacing: 40, length: 2, colour: '#ddd' },
            sounds: false,
            trashcan: true,
            move: { scrollbars: true, drag: true, wheel: false },
        });
        this.workspace = ws;
        ws.registerToolboxCategoryCallback('VARIABLE', B.DataCategory);
        ws.registerToolboxCategoryCallback('PROCEDURE', B.Procedures.flyoutCategory);
        const xml = treeToXmlString(workspaceTree(this.target.blocks || {}, this.workspaceVariables()));
        B.Xml.clearWorkspaceAndLoadFromXml(B.Xml.textToDom(xml), ws);
        if (!ws.getVariablesOfType('broadcast_msg').length) ws.createVariable('message1', 'broadcast_msg');
        ws.addChangeListener(e => {
            if (this.loading || e.isUiEvent) return;
            this.dirty = true;
            this.onChange();
        });
        this.loading = false;
        this.dirty = false;
        ws.scrollCenter();
    }

    // Variables visible to the current target: the stage's (global) plus its own.
    workspaceVariables() {
        const t = this.target;
        const stage = this.project.targets.find(x => x.isStage);
        const out = [];
        const add = (src, isLocal) => {
            for (const [id, v] of Object.entries(src.variables || {})) out.push({ id, name: v[0], type: '', isLocal });
            for (const [id, v] of Object.entries(src.lists || {})) out.push({ id, name: v[0], type: 'list', isLocal });
            if (src.isStage) for (const [id, name] of Object.entries(src.broadcasts || {})) out.push({ id, name, type: 'broadcast_msg', isLocal: false });
        };
        if (!t.isStage) add(stage, false);
        add(t, !t.isStage);
        return out;
    }

    // Workspace -> model for the current target.
    saveCurrent() {
        if (!this.workspace || !this.target) return;
        const B = this.Blockly;
        const t = this.target;
        const stage = this.project.targets.find(x => x.isStage);
        const dom = B.Xml.workspaceToDom(this.workspace);
        const tree = domToTree(dom);
        // Variables defined in the workspace; keep existing values.
        const own = { variables: {}, lists: {} };
        const globals = { variables: {}, lists: {} };
        const broadcasts = {};
        for (const v of this.workspace.getAllVariables()) {
            const target = (!t.isStage && v.isLocal) ? own : (t.isStage ? own : globals);
            if (v.type === '') {
                const old = (t.variables || {})[v.id] || (stage.variables || {})[v.id];
                target.variables[v.id] = [v.name, old ? old[1] : 0];
            } else if (v.type === 'list') {
                const old = (t.lists || {})[v.id] || (stage.lists || {})[v.id];
                target.lists[v.id] = [v.name, old ? old[1] : []];
            } else if (v.type === 'broadcast_msg') {
                broadcasts[v.id] = v.name;
            }
        }
        t.blocks = xmlTreeToSb3Blocks(tree, () => 'b' + Math.random().toString(36).slice(2, 12));
        t.variables = own.variables;
        t.lists = own.lists;
        if (!t.isStage) {
            stage.variables = globals.variables;
            stage.lists = globals.lists;
        }
        stage.broadcasts = broadcasts;
    }

    // Project JSON for the runtime (saves the open workspace first).
    toProjectJson() {
        this.saveCurrent();
        return JSON.stringify(this.project);
    }

    resize() { if (this.workspace) this.Blockly.svgResize(this.workspace); }
}
