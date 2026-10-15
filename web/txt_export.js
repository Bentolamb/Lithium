// Exports a whole project as readable text: Python-like layout, Scratch block names.
//   when_flag_clicked:
//       score = 0
//       repeat 10:
//           score += 1
// Takes the project.json object (real opcodes, not the placeholders written for other editors).

const CORE = new Set(['motion', 'looks', 'sound', 'event', 'control', 'sensing', 'operator', 'data', 'procedures', 'argument']);
const INFIX = {
    operator_add: '+', operator_subtract: '-', operator_multiply: '*', operator_divide: '/', operator_mod: '%',
    operator_lt: '<', operator_gt: '>', operator_equals: '==', operator_and: 'and', operator_or: 'or'
};
const RENAME = {
    sayforsecs: 'say_for_secs', thinkforsecs: 'think_for_secs', nextcostume: 'next_costume', switchcostumeto: 'switch_costume',
    nextbackdrop: 'next_backdrop', switchbackdropto: 'switch_backdrop', changeeffectby: 'change_effect', seteffectto: 'set_effect',
    cleargraphiceffects: 'clear_graphic_effects', changesizeby: 'change_size', setsizeto: 'set_size', gotofrontback: 'go_to_layer',
    goforwardbackwardlayers: 'move_layers', movesteps: 'move', turnright: 'turn_right', turnleft: 'turn_left', gotoxy: 'go_to_xy',
    goto: 'go_to', glidesecstoxy: 'glide_to_xy', glideto: 'glide_to', pointindirection: 'point_in_direction',
    pointtowards: 'point_towards', changexby: 'change_x', setx: 'set_x', changeyby: 'change_y', sety: 'set_y',
    ifonedgebounce: 'bounce_if_on_edge', setrotationstyle: 'set_rotation_style', xposition: 'x_position', yposition: 'y_position',
    askandwait: 'ask_and_wait', keypressed: 'key_pressed', mousedown: 'mouse_down', mousex: 'mouse_x', mousey: 'mouse_y',
    touchingobject: 'touching', touchingcolor: 'touching_color', distanceto: 'distance_to', resettimer: 'reset_timer',
    dayssince2000: 'days_since_2000', setdragmode: 'set_drag_mode', broadcastandwait: 'broadcast_and_wait',
    playuntildone: 'play_until_done', stopallsounds: 'stop_all_sounds', changevolumeby: 'change_volume', setvolumeto: 'set_volume',
    penDown: 'pen_down', penUp: 'pen_up', clear: 'pen_clear', stamp: 'stamp', setPenColorToColor: 'set_pen_color',
    changePenSizeBy: 'change_pen_size', setPenSizeTo: 'set_pen_size'
};
const snake = s => s.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toLowerCase();

const isIdent = s => /^[A-Za-z_][A-Za-z0-9_]*$/.test(s);
const nameOf = s => (isIdent(s) ? s : `{${s}}`);
const quote = v => JSON.stringify(String(v));

const formatPrimitive = p => {
    const [type, value] = p;
    if (type >= 4 && type <= 8) {
        return value === '' || value === null || value === undefined ? '0' : String(value);
    }
    if (type === 9) return String(value);
    if (type === 10) return quote(value);
    if (type === 11) return quote(value);
    if (type === 12) return nameOf(String(value));
    if (type === 13) return nameOf(String(value));
    return quote(value);
};

export const projectToText = (project, title = 'Project') => {
    const out = [];
    out.push(`# ${title} - exported from Lithium as text`);
    out.push('# Layout is Python-like; names are Scratch block names. One-way: this file cannot be opened as a project.');

    for (const target of project.targets) {
        const blocks = target.blocks || {};
        const get = id => (typeof id === 'string' ? blocks[id] : null);

        const inputExpr = (block, name) => {
            const input = block.inputs && block.inputs[name];
            if (!input) {
                const f = block.fields && block.fields[name];
                return f ? quote(f[0]) : '0';
            }
            const v = input[1];
            if (v === null || v === undefined) {
                const shadow = input[2];
                return Array.isArray(shadow) ? formatPrimitive(shadow) : '0';
            }
            if (Array.isArray(v)) return formatPrimitive(v);
            return expr(v);
        };
        const hasInput = (b, n) => !!(b.inputs && b.inputs[n] && b.inputs[n][1] !== null && b.inputs[n][1] !== undefined);
        const field = (b, n) => (b.fields && b.fields[n] ? String(b.fields[n][0]) : '');

        const procName = proccode => {
            const words = proccode.replace(/%[sb]/g, ' ').trim().split(/\s+/).filter(Boolean);
            return words.length ? words.join('_') : 'custom_block';
        };
        const argNames = proccodeBlock => {
            const m = proccodeBlock.mutation;
            if (!m) return [];
            try { return JSON.parse(m.argumentnames || '[]'); } catch (e) { return []; }
        };

        const genericName = opcode => {
            const i = opcode.indexOf('_');
            const cat = opcode.slice(0, i), rest = opcode.slice(i + 1);
            if (CORE.has(cat)) return RENAME[rest] || snake(rest);
            return `${cat}.${snake(rest)}`;
        };
        const isMenu = b => (b.shadow || /_menu($|_)/.test(b.opcode)) && !Object.keys(b.inputs || {}).length &&
            Object.keys(b.fields || {}).length === 1;

        // Expression for a reporter/boolean block.
        function expr (id) {
            const b = get(id);
            if (!b) return Array.isArray(blocks[id]) ? formatPrimitive(blocks[id]) : '0';
            if (Array.isArray(b)) return formatPrimitive(b);
            const op = b.opcode;
            if (isMenu(b)) return quote(Object.values(b.fields)[0][0]);
            if (INFIX[op]) return `(${inputExpr(b, Object.keys(b.inputs)[0])} ${INFIX[op]} ${inputExpr(b, Object.keys(b.inputs)[1])})`;
            switch (op) {
            case 'operator_not': return `not ${inputExpr(b, 'OPERAND')}`;
            case 'operator_join': return `join(${inputExpr(b, 'STRING1')}, ${inputExpr(b, 'STRING2')})`;
            case 'operator_random': return `random(${inputExpr(b, 'FROM')}, ${inputExpr(b, 'TO')})`;
            case 'operator_round': return `round(${inputExpr(b, 'NUM')})`;
            case 'operator_length': return `len(${inputExpr(b, 'STRING')})`;
            case 'operator_letter_of': return `${inputExpr(b, 'STRING')}[${inputExpr(b, 'LETTER')}]`;
            case 'operator_contains': return `(${inputExpr(b, 'STRING2')} in ${inputExpr(b, 'STRING1')})`;
            case 'operator_mathop': return `${field(b, 'OPERATOR')}(${inputExpr(b, 'NUM')})`;
            case 'data_variable': return nameOf(field(b, 'VARIABLE'));
            case 'data_listcontents': return nameOf(field(b, 'LIST'));
            case 'data_itemoflist': return `${nameOf(field(b, 'LIST'))}[${inputExpr(b, 'INDEX')}]`;
            case 'data_itemnumoflist': return `${nameOf(field(b, 'LIST'))}.index(${inputExpr(b, 'ITEM')})`;
            case 'data_lengthoflist': return `len(${nameOf(field(b, 'LIST'))})`;
            case 'data_listcontainsitem': return `(${inputExpr(b, 'ITEM')} in ${nameOf(field(b, 'LIST'))})`;
            case 'argument_reporter_string_number':
            case 'argument_reporter_boolean': return nameOf(field(b, 'VALUE'));
            case 'procedures_call': return callText(b);
            default: return callGeneric(b);
            }
        }

        function callGeneric (b) {
            const args = [];
            for (const f of Object.keys(b.fields || {})) args.push(`${f.toLowerCase()}=${quote(b.fields[f][0])}`);
            for (const name of Object.keys(b.inputs || {})) {
                if (/^SUBSTACK/.test(name)) continue;
                args.push(inputExpr(b, name));
            }
            return `${genericName(b.opcode)}(${args.join(', ')})`;
        }

        function callText (b) {
            const m = b.mutation || {};
            let ids = [];
            try { ids = JSON.parse(m.argumentids || '[]'); } catch (e) { ids = []; }
            if (m.proccode === 'Lithium-only Extension block') return `lithium_only_block(${m.lithiumop || ''})`;
            return `${procName(m.proccode || 'custom_block')}(${ids.map(id => inputExpr(b, id)).join(', ')})`;
        }

        // Lines for a stack starting at block id, indented by `depth`.
        function stack (id, depth) {
            const lines = [];
            const pad = '    '.repeat(depth);
            let cur = id;
            const seen = new Set();
            while (typeof cur === 'string' && !seen.has(cur)) {
                seen.add(cur);
                const b = get(cur);
                if (!b || Array.isArray(b)) break;
                const sub = name => (hasInput(b, name) ? stack(b.inputs[name][1], depth + 1) : [`${pad}    pass`]);
                const op = b.opcode;
                switch (op) {
                case 'control_repeat': lines.push(`${pad}repeat ${inputExpr(b, 'TIMES')}:`, ...sub('SUBSTACK')); break;
                case 'control_forever': lines.push(`${pad}forever:`, ...sub('SUBSTACK')); break;
                case 'control_if': lines.push(`${pad}if ${inputExpr(b, 'CONDITION')}:`, ...sub('SUBSTACK')); break;
                case 'control_if_else':
                    lines.push(`${pad}if ${inputExpr(b, 'CONDITION')}:`, ...sub('SUBSTACK'), `${pad}else:`, ...sub('SUBSTACK2'));
                    break;
                case 'control_repeat_until': lines.push(`${pad}repeat_until ${inputExpr(b, 'CONDITION')}:`, ...sub('SUBSTACK')); break;
                case 'control_while': lines.push(`${pad}while ${inputExpr(b, 'CONDITION')}:`, ...sub('SUBSTACK')); break;
                case 'control_for_each':
                    lines.push(`${pad}for ${nameOf(field(b, 'VARIABLE'))} in range(${inputExpr(b, 'VALUE')}):`, ...sub('SUBSTACK'));
                    break;
                case 'control_wait': lines.push(`${pad}wait(${inputExpr(b, 'DURATION')})`); break;
                case 'control_wait_until': lines.push(`${pad}wait_until(${inputExpr(b, 'CONDITION')})`); break;
                case 'control_stop': lines.push(`${pad}stop(${quote(field(b, 'STOP_OPTION'))})`); break;
                case 'control_create_clone_of': lines.push(`${pad}create_clone_of(${inputExpr(b, 'CLONE_OPTION')})`); break;
                case 'control_delete_this_clone': lines.push(`${pad}delete_this_clone()`); break;
                case 'data_setvariableto': lines.push(`${pad}${nameOf(field(b, 'VARIABLE'))} = ${inputExpr(b, 'VALUE')}`); break;
                case 'data_changevariableby': lines.push(`${pad}${nameOf(field(b, 'VARIABLE'))} += ${inputExpr(b, 'VALUE')}`); break;
                case 'data_showvariable': lines.push(`${pad}show_variable(${nameOf(field(b, 'VARIABLE'))})`); break;
                case 'data_hidevariable': lines.push(`${pad}hide_variable(${nameOf(field(b, 'VARIABLE'))})`); break;
                case 'data_addtolist': lines.push(`${pad}${nameOf(field(b, 'LIST'))}.append(${inputExpr(b, 'ITEM')})`); break;
                case 'data_deleteoflist': lines.push(`${pad}del ${nameOf(field(b, 'LIST'))}[${inputExpr(b, 'INDEX')}]`); break;
                case 'data_deletealloflist': lines.push(`${pad}${nameOf(field(b, 'LIST'))}.clear()`); break;
                case 'data_insertatlist':
                    lines.push(`${pad}${nameOf(field(b, 'LIST'))}.insert(${inputExpr(b, 'INDEX')}, ${inputExpr(b, 'ITEM')})`);
                    break;
                case 'data_replaceitemoflist':
                    lines.push(`${pad}${nameOf(field(b, 'LIST'))}[${inputExpr(b, 'INDEX')}] = ${inputExpr(b, 'ITEM')}`);
                    break;
                case 'procedures_call': lines.push(`${pad}${callText(b)}`); break;
                case 'event_broadcast': lines.push(`${pad}broadcast(${inputExpr(b, 'BROADCAST_INPUT')})`); break;
                case 'event_broadcastandwait': lines.push(`${pad}broadcast_and_wait(${inputExpr(b, 'BROADCAST_INPUT')})`); break;
                default: lines.push(`${pad}${callGeneric(b)}`);
                }
                cur = b.next;
            }
            return lines;
        }

        // ---- the target ----
        out.push('');
        out.push(`${target.isStage ? 'stage' : 'sprite'} ${quote(target.name)}:`);
        const vars = Object.values(target.variables || {}).map(v => `${nameOf(v[0])} = ${JSON.stringify(v[1])}`);
        const lists = Object.values(target.lists || {}).map(l => `${nameOf(l[0])} = ${JSON.stringify(l[1])}`);
        if (vars.length) out.push(`    # variables: ${vars.join(', ')}`);
        if (lists.length) out.push(`    # lists: ${lists.join(', ')}`);
        if ((target.costumes || []).length) out.push(`    # costumes: ${target.costumes.map(c => c.name).join(', ')}`);
        if ((target.sounds || []).length) out.push(`    # sounds: ${target.sounds.map(c => c.name).join(', ')}`);

        const tops = Object.entries(blocks).filter(([, b]) => !Array.isArray(b) && b.topLevel)
            .sort((a, b) => (a[1].y - b[1].y) || (a[1].x - b[1].x));
        for (const [id, b] of tops) {
            out.push('');
            const op = b.opcode;
            let header = null;
            if (op === 'procedures_definition') {
                const proto = get(b.inputs && b.inputs.custom_block && b.inputs.custom_block[1]);
                const m = proto && proto.mutation ? proto.mutation : {};
                const names = proto ? argNames(proto) : [];
                header = `def ${procName(m.proccode || 'custom_block')}(${names.join(', ')}):${m.warp === 'true' ? '  # run without screen refresh' : ''}`;
            } else if (op === 'event_whenflagclicked') header = 'when_flag_clicked:';
            else if (op === 'event_whenkeypressed') header = `when_key_pressed(${quote(field(b, 'KEY_OPTION'))}):`;
            else if (op === 'event_whenthisspriteclicked') header = 'when_this_sprite_clicked:';
            else if (op === 'event_whenstageclicked') header = 'when_stage_clicked:';
            else if (op === 'event_whenbroadcastreceived') header = `when_i_receive(${quote(field(b, 'BROADCAST_OPTION'))}):`;
            else if (op === 'event_whenbackdropswitchesto') header = `when_backdrop_switches_to(${quote(field(b, 'BACKDROP'))}):`;
            else if (op === 'event_whengreaterthan') header = `when ${field(b, 'WHENGREATERTHANMENU').toLowerCase()} > ${inputExpr(b, 'VALUE')}:`;
            else if (op === 'control_start_as_clone') header = 'when_i_start_as_a_clone:';
            else if (op === 'event_whentouchingobject') header = `when_touching(${inputExpr(b, 'TOUCHINGOBJECTMENU')}):`;
            if (header) {
                out.push(`    ${header}`);
                const body = stack(b.next, 2);
                out.push(...(body.length ? body : ['        pass']));
            } else if (!/^(procedures_prototype)$/.test(op)) {
                // a loose block or stack
                out.push('    # loose blocks (not under a hat):');
                if (/^(operator_|data_variable|data_listcontents|sensing_|motion_.*position|looks_(size|costumenumbername|backdropnumbername))/.test(op) &&
                    b.next === null) {
                    out.push(`    ${expr(id)}`);
                } else {
                    out.push(...stack(id, 1));
                }
            }
        }
    }
    out.push('');
    return out.join('\n');
};
