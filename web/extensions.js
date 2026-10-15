// Lithium-only extensions: Lithium Lab, Packin and OutList.
//
// The runtime (C++/WASM) implements the blocks; these classes describe them to the editor and
// give the built-in JavaScript runtime harmless fallbacks. When a project is saved the blocks are
// converted to a form other editors can open (see fallback.js).

const COLORS = {color1: '#314b72', color2: '#4a71a8', color3: '#608ac3'};
const TYPE = {COMMAND: 'command', REPORTER: 'reporter', BOOLEAN: 'Boolean'};

const warnedOnce = new Set();
const unavailable = name => {
    if (!warnedOnce.has(name)) {
        warnedOnce.add(name);
        console.warn(`${name} only runs in the Lithium runtime.`);
    }
};

const listMenu = {
    acceptReporters: false,
    items: 'getLists'
};

// Names of the lists the editing sprite (and the stage) can see.
const listNames = runtime => {
    const names = new Set();
    const add = target => {
        if (!target) return;
        for (const v of Object.values(target.variables)) {
            if (v.type === 'list') names.add(v.name);
        }
    };
    add(runtime.getEditingTarget());
    add(runtime.getTargetForStage());
    const sorted = Array.from(names).sort((a, b) => a.localeCompare(b));
    return sorted.length ? sorted : [{text: 'make a list first', value: ''}];
};

export class LithiumLab {
    constructor (runtime) {
        this.runtime = runtime;
    }
    getInfo () {
        return {
            id: 'lithiumlab',
            name: 'Lithium Lab',
            ...COLORS,
            blocks: [
                {opcode: 'isTurboWarp', blockType: TYPE.BOOLEAN, text: 'is TurboWarp?'},
                {opcode: 'isCompiled', blockType: TYPE.BOOLEAN, text: 'is Compiled?'},
                {opcode: 'isLithium', blockType: TYPE.BOOLEAN, text: 'is Lithium?'},
                {
                    opcode: 'isExtensionAvailable',
                    blockType: TYPE.BOOLEAN,
                    text: 'is extension [NAME] available?',
                    arguments: {NAME: {type: 'string', defaultValue: 'Packin'}}
                }
            ]
        };
    }
    // The built-in JavaScript runtime only runs these when Lithium is not running the project.
    // It is TurboWarp's own runtime, so detection by capability: its compiler options exist.
    isTurboWarp () {
        return !!this.runtime.compilerOptions && typeof this.runtime.setCompilerOptions === 'function';
    }
    isCompiled (args, util) {
        return !!(util && util.thread && util.thread.isCompiled);
    }
    isLithium () {
        return false;
    }
    isExtensionAvailable (args) {
        const id = String(args.NAME).toLowerCase().replace(/[\s_-]/g, '');
        return !!id && this.runtime.extensionManager.isBuiltinExtension(id) &&
            this.runtime.extensionManager.isExtensionLoaded(id);
    }
}

export class Packin {
    constructor (runtime) {
        this.runtime = runtime;
    }
    getInfo () {
        return {
            id: 'packin',
            name: 'Packin',
            ...COLORS,
            blocks: [
                {opcode: 'compress', blockType: TYPE.REPORTER, text: 'compress [DATA]',
                    arguments: {DATA: {type: 'string', defaultValue: 'hello hello hello'}}},
                {opcode: 'decompress', blockType: TYPE.REPORTER, text: 'decompress [DATA]',
                    arguments: {DATA: {type: 'string', defaultValue: ''}}},
                {opcode: 'compressedSize', blockType: TYPE.REPORTER, text: 'compressed size of [DATA]',
                    arguments: {DATA: {type: 'string', defaultValue: 'hello hello hello'}}},
                {opcode: 'originalSize', blockType: TYPE.REPORTER, text: 'original size of [DATA]',
                    arguments: {DATA: {type: 'string', defaultValue: ''}}},
                {opcode: 'isValid', blockType: TYPE.BOOLEAN, text: '[DATA] is valid Packin data?',
                    arguments: {DATA: {type: 'string', defaultValue: ''}}},
                {opcode: 'compressAtLevel', blockType: TYPE.REPORTER, text: 'compress [DATA] at level [LEVEL]',
                    arguments: {
                        DATA: {type: 'string', defaultValue: 'hello hello hello'},
                        LEVEL: {type: 'number', defaultValue: 6}
                    }}
            ]
        };
    }
    compress () { unavailable('Packin'); return ''; }
    decompress () { unavailable('Packin'); return ''; }
    compressedSize () { unavailable('Packin'); return 0; }
    originalSize () { unavailable('Packin'); return 0; }
    isValid () { unavailable('Packin'); return false; }
    compressAtLevel () { unavailable('Packin'); return ''; }
}

export class OutList {
    constructor (runtime) {
        this.runtime = runtime;
    }
    getInfo () {
        return {
            id: 'outlist',
            name: 'OutList',
            ...COLORS,
            blocks: [
                {opcode: 'saveList', blockType: TYPE.COMMAND, text: 'save list [LIST] to file [NAME]',
                    arguments: {LIST: {type: 'string', menu: 'lists'}, NAME: {type: 'string', defaultValue: 'my list'}}},
                {opcode: 'loadList', blockType: TYPE.COMMAND, text: 'load list from file [NAME] into [LIST]',
                    arguments: {NAME: {type: 'string', defaultValue: 'my list'}, LIST: {type: 'string', menu: 'lists'}}},
                {opcode: 'exists', blockType: TYPE.BOOLEAN, text: 'saved list [NAME] exists?',
                    arguments: {NAME: {type: 'string', defaultValue: 'my list'}}},
                {opcode: 'deleteList', blockType: TYPE.COMMAND, text: 'delete saved list [NAME]',
                    arguments: {NAME: {type: 'string', defaultValue: 'my list'}}},
                {opcode: 'unloadList', blockType: TYPE.COMMAND, text: 'unload list [LIST]',
                    arguments: {LIST: {type: 'string', menu: 'lists'}}},
                {opcode: 'lastError', blockType: TYPE.REPORTER, text: 'last OutList error'}
            ],
            menus: {lists: listMenu}
        };
    }
    getLists () { return listNames(this.runtime); }
    saveList () { unavailable('OutList'); }
    loadList () { unavailable('OutList'); }
    exists () { unavailable('OutList'); return false; }
    deleteList () { unavailable('OutList'); }
    unloadList () { unavailable('OutList'); }
    lastError () { return 'OutList only runs in the Lithium runtime.'; }
}

export const LITHIUM_EXTENSIONS = {lithiumlab: LithiumLab, packin: Packin, outlist: OutList};

export const registerLithiumExtensions = vm => {
    for (const [id, cls] of Object.entries(LITHIUM_EXTENSIONS)) {
        if (!vm.extensionManager.isBuiltinExtension(id)) vm.extensionManager.addBuiltinExtension(id, cls);
    }
};
