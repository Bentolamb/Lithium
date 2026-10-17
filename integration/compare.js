/* State comparator shared by the browser integration and Node tests.
 * Deliberately reports differences, not equivalence of VM semantics.
 */
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    if (root) root.LithiumCompare = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    function same(a, b, epsilon) {
        if (typeof a === 'number' && typeof b === 'number') {
            if (Number.isNaN(a) && Number.isNaN(b)) return true;
            return a === b || (Number.isFinite(a) && Number.isFinite(b) &&
                Math.abs(a - b) <= epsilon * Math.max(1, Math.abs(a), Math.abs(b)));
        }
        return Object.is(a, b);
    }

    function turboSnapshot(vm) {
        if (!vm || !vm.runtime || !Array.isArray(vm.runtime.targets)) {
            throw new Error('TurboWarp runtime targets unavailable');
        }
        const ambiguities = [];
        return {
            threads: Array.isArray(vm.runtime.threads) ? vm.runtime.threads.length : null,
            ambiguities,
            targets: vm.runtime.targets.map(t => {
                // Scratch variable names are user-controlled, including names
                // such as "__proto__". Null-prototype dictionaries prevent
                // special names from disappearing or modifying the prototype.
                const vars = Object.create(null);
                const lists = Object.create(null);
                const targetName = t.getName();
                for (const variable of Object.values(t.variables || {})) {
                    const isList = variable.type === 'list';
                    if (!isList && variable.type !== '') continue;
                    const destination = isList ? lists : vars;
                    if (Object.prototype.hasOwnProperty.call(destination, variable.name)) {
                        ambiguities.push(targetName + ': duplicated ' +
                            (isList ? 'list ' : 'variable ') + JSON.stringify(variable.name));
                    }
                    destination[variable.name] = isList ? Array.from(variable.value || []) : variable.value;
                }
                return {
                    name: t.getName(),
                    isStage: !!t.isStage,
                    isOriginal: t.isOriginal !== false,
                    x: t.x, y: t.y, direction: t.direction, size: t.size,
                    visible: t.visible, costume: t.currentCostume,
                    variables: vars, lists
                };
            })
        };
    }

    function compare(lithium, turbo, options) {
        const epsilon = options && options.epsilon !== undefined ? options.epsilon : 1e-9;
        if (!Number.isFinite(epsilon) || epsilon < 0) throw new Error('Invalid epsilon');
        if (!lithium || !Array.isArray(lithium.targets) || !turbo || !Array.isArray(turbo.targets)) {
            throw new Error('Snapshots must have targets arrays');
        }
        const differences = [];
        const add = (path, expected, actual) => differences.push({path, lithium: expected, turboWarp: actual});
        const grouped = snapshot => {
            const map = new Map();
            for (const target of snapshot.targets) {
                const key = JSON.stringify([target.name, !!target.isStage, target.isOriginal !== false]);
                if (!map.has(key)) map.set(key, []);
                map.get(key).push(target);
            }
            return map;
        };
        const left = grouped(lithium);
        const right = grouped(turbo);
        const keys = new Set([...left.keys(), ...right.keys()]);
        for (const key of keys) {
            const a = left.get(key) || [];
            const b = right.get(key) || [];
            const [name, isStage, isOriginal] = JSON.parse(key);
            const label = (isStage ? 'Stage' : name) + (isOriginal ? '' : ' [clone]');
            if (a.length !== b.length) add(label + '.targetCount', a.length, b.length);
            for (let index = 0; index < Math.min(a.length, b.length); index++) {
                const lt = a[index], tw = b[index], prefix = label + (index ? '#' + (index + 1) : '');
                for (const collection of ['variables', 'lists']) {
                    const lvals = lt[collection] || {};
                    const tvals = tw[collection] || {};
                    for (const name of new Set([...Object.keys(lvals), ...Object.keys(tvals)])) {
                        const hasL = Object.prototype.hasOwnProperty.call(lvals, name);
                        const hasT = Object.prototype.hasOwnProperty.call(tvals, name);
                        const lval = lvals[name], tval = tvals[name];
                        const path = prefix + '.' + collection + '.' + name;
                        if (!hasL || !hasT) { add(path, hasL ? lval : '[missing]', hasT ? tval : '[missing]'); continue; }
                        if (Array.isArray(lval) || Array.isArray(tval)) {
                            if (!Array.isArray(lval) || !Array.isArray(tval) || lval.length !== tval.length) {
                                add(path, lval, tval); continue;
                            }
                            for (let i = 0; i < lval.length; i++) {
                                if (!same(lval[i], tval[i], epsilon)) add(path + '[' + i + ']', lval[i], tval[i]);
                            }
                        } else if (!same(lval, tval, epsilon)) add(path, lval, tval);
                    }
                }
                if (!isStage) {
                    for (const field of ['x', 'y', 'direction', 'size', 'visible', 'costume']) {
                        if (!same(lt[field], tw[field], epsilon)) add(prefix + '.' + field, lt[field], tw[field]);
                    }
                }
            }
        }
        const running = lithium.threads !== 0 || turbo.threads !== 0;
        const ambiguities = turbo.ambiguities || [];
        const emptyProject = lithium.targets.length === 0 || turbo.targets.length === 0;
        const cannotVerify = ambiguities.length > 0 || emptyProject;
        return {
            status: running ? 'incomplete' : cannotVerify ? 'unsupported' :
                (differences.length ? 'mismatch' : 'match'),
            match: !running && !cannotVerify && differences.length === 0,
            reason: emptyProject ? 'No project targets were loaded' :
                ambiguities.length ? 'Duplicate variable/list names make name-keyed snapshots ambiguous' : undefined,
            ambiguities,
            differences,
            comparedTargets: Math.min(lithium.targets.length, turbo.targets.length),
            lithiumThreads: lithium.threads,
            turboWarpThreads: turbo.threads
        };
    }

    return {turboSnapshot, compare};
});
