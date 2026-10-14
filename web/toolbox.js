// Toolbox (block palette) for the editor: only blocks the runtime implements.
// ctx: {costume, backdrop, sound, sprites: [names], isStage}

const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const num = v => ['math_number', 'NUM', v];
const pos = v => ['math_positive_number', 'NUM', v];
const ang = v => ['math_angle', 'NUM', v];
const txt = v => ['text', 'TEXT', v];
const col = v => ['colour_picker', 'COLOUR', v];
const menu = (op, field, value) => ['menu', op, field, value];

const field = (name, value) => name === 'BROADCAST_OPTION'
    ? `<field name="${name}" variabletype="broadcast_msg">${esc(value)}</field>`
    : `<field name="${name}">${esc(value)}</field>`;

function shadow(spec) {
    if (spec[0] === 'menu') return `<shadow type="${spec[1]}">${field(spec[2], spec[3])}</shadow>`;
    return `<shadow type="${spec[0]}">${field(spec[1], spec[2])}</shadow>`;
}
// b('motion_movesteps', {STEPS: num(10)}, {FIELD: 'v'})
const b = (type, inputs = {}, fields = {}) =>
    `<block type="${type}">` + Object.entries(inputs).map(([k, v]) => `<value name="${k}">${shadow(v)}</value>`).join('') +
    Object.entries(fields).map(([k, v]) => field(k, v)).join('') + '</block>';

const category = (name, id, colour, secondary, body, custom) =>
    `<category name="${name}" id="${id}" colour="${colour}" secondaryColour="${secondary}"${custom ? ` custom="${custom}"` : ''}>${body}</category>`;

export function makeToolbox(ctx) {
    const sprites = (ctx.sprites || []).filter(n => n !== ctx.self);
    const m = {
        goto: menu('motion_goto_menu', 'TO', '_random_'), glideto: menu('motion_glideto_menu', 'TO', '_random_'),
        towards: menu('motion_pointtowards_menu', 'TOWARDS', '_mouse_'),
        costume: menu('looks_costume', 'COSTUME', ctx.costume || ''), backdrop: menu('looks_backdrops', 'BACKDROP', ctx.backdrop || ''),
        sound: menu('sound_sounds_menu', 'SOUND_MENU', ctx.sound || ''),
        touching: menu('sensing_touchingobjectmenu', 'TOUCHINGOBJECTMENU', '_mouse_'),
        distance: menu('sensing_distancetomenu', 'DISTANCETOMENU', '_mouse_'),
        key: menu('sensing_keyoptions', 'KEY_OPTION', 'space'),
        clone: menu('control_create_clone_of_menu', 'CLONE_OPTION', '_myself_'),
        of: menu('sensing_of_object_menu', 'OBJECT', sprites[0] || '_stage_'),
        param: menu('pen_menu_colorParam', 'colorParam', 'color'),
        broadcast: ['menu', 'event_broadcast_menu', 'BROADCAST_OPTION', 'message1'],
    };
    const motion = ctx.isStage ? '' : [
        b('motion_movesteps', { STEPS: num(10) }), b('motion_turnright', { DEGREES: num(15) }), b('motion_turnleft', { DEGREES: num(15) }),
        '<sep gap="36"></sep>',
        b('motion_goto', { TO: m.goto }), b('motion_gotoxy', { X: num(0), Y: num(0) }),
        b('motion_glideto', { SECS: num(1), TO: m.glideto }), b('motion_glidesecstoxy', { SECS: num(1), X: num(0), Y: num(0) }),
        '<sep gap="36"></sep>',
        b('motion_pointindirection', { DIRECTION: ang(90) }), b('motion_pointtowards', { TOWARDS: m.towards }),
        '<sep gap="36"></sep>',
        b('motion_changexby', { DX: num(10) }), b('motion_setx', { X: num(0) }), b('motion_changeyby', { DY: num(10) }), b('motion_sety', { Y: num(0) }),
        '<sep gap="36"></sep>',
        b('motion_ifonedgebounce'), b('motion_setrotationstyle', {}, { STYLE: 'left-right' }),
        '<sep gap="36"></sep>',
        b('motion_xposition'), b('motion_yposition'), b('motion_direction'),
    ].join('');
    const looks = [
        ctx.isStage ? '' : [
            b('looks_sayforsecs', { MESSAGE: txt('Hello!'), SECS: num(2) }), b('looks_say', { MESSAGE: txt('Hello!') }),
            b('looks_thinkforsecs', { MESSAGE: txt('Hmm...'), SECS: num(2) }), b('looks_think', { MESSAGE: txt('Hmm...') }),
            '<sep gap="36"></sep>',
            b('looks_switchcostumeto', { COSTUME: m.costume }), b('looks_nextcostume'),
        ].join(''),
        b('looks_switchbackdropto', { BACKDROP: m.backdrop }), b('looks_switchbackdroptoandwait', { BACKDROP: m.backdrop }), b('looks_nextbackdrop'),
        '<sep gap="36"></sep>',
        ctx.isStage ? '' : [b('looks_changesizeby', { CHANGE: num(10) }), b('looks_setsizeto', { SIZE: num(100) }), '<sep gap="36"></sep>'].join(''),
        b('looks_changeeffectby', { CHANGE: num(25) }, { EFFECT: 'COLOR' }), b('looks_seteffectto', { VALUE: num(0) }, { EFFECT: 'COLOR' }), b('looks_cleargraphiceffects'),
        '<sep gap="36"></sep>',
        ctx.isStage ? '' : [b('looks_show'), b('looks_hide'), '<sep gap="36"></sep>',
            b('looks_gotofrontback', {}, { FRONT_BACK: 'front' }), b('looks_goforwardbackwardlayers', { NUM: ['math_integer', 'NUM', 1] }, { FORWARD_BACKWARD: 'forward' }),
            '<sep gap="36"></sep>', b('looks_costumenumbername', {}, { NUMBER_NAME: 'number' })].join(''),
        b('looks_backdropnumbername', {}, { NUMBER_NAME: 'number' }),
        ctx.isStage ? '' : b('looks_size'),
    ].join('');
    const sound = [
        b('sound_playuntildone', { SOUND_MENU: m.sound }), b('sound_play', { SOUND_MENU: m.sound }), b('sound_stopallsounds'),
        '<sep gap="36"></sep>',
        b('sound_changeeffectby', { VALUE: num(10) }, { EFFECT: 'PITCH' }), b('sound_seteffectto', { VALUE: num(100) }, { EFFECT: 'PITCH' }), b('sound_cleareffects'),
        '<sep gap="36"></sep>',
        b('sound_changevolumeby', { VOLUME: num(-10) }), b('sound_setvolumeto', { VOLUME: num(100) }), b('sound_volume'),
    ].join('');
    const events = [
        b('event_whenflagclicked'), b('event_whenkeypressed', {}, { KEY_OPTION: 'space' }),
        ctx.isStage ? b('event_whenstageclicked') : b('event_whenthisspriteclicked'),
        b('event_whenbackdropswitchesto', {}, { BACKDROP: ctx.backdrop || '' }),
        '<sep gap="36"></sep>',
        b('event_whengreaterthan', { VALUE: num(10) }, { WHENGREATERTHANMENU: 'LOUDNESS' }),
        '<sep gap="36"></sep>',
        b('event_whenbroadcastreceived', {}, { BROADCAST_OPTION: 'message1' }),
        b('event_broadcast', { BROADCAST_INPUT: m.broadcast }), b('event_broadcastandwait', { BROADCAST_INPUT: m.broadcast }),
    ].join('');
    const control = [
        b('control_wait', { DURATION: pos(1) }), '<sep gap="36"></sep>',
        `<block type="control_repeat"><value name="TIMES">${shadow(['math_whole_number', 'NUM', 10])}</value></block>`,
        '<block type="control_forever"></block>', '<sep gap="36"></sep>',
        '<block type="control_if"></block>', '<block type="control_if_else"></block>',
        '<block type="control_wait_until"></block>', '<block type="control_repeat_until"></block>', '<sep gap="36"></sep>',
        b('control_stop', {}, { STOP_OPTION: 'all' }), '<sep gap="36"></sep>',
        ctx.isStage ? '' : [b('control_start_as_clone'), b('control_create_clone_of', { CLONE_OPTION: m.clone }), b('control_delete_this_clone')].join(''),
    ].join('');
    const sensing = [
        ctx.isStage ? '' : [b('sensing_touchingobject', { TOUCHINGOBJECTMENU: m.touching }), b('sensing_touchingcolor', { COLOR: col('#55b8ff') }),
            b('sensing_coloristouchingcolor', { COLOR: col('#ff0000'), COLOR2: col('#00ff00') }), b('sensing_distanceto', { DISTANCETOMENU: m.distance }), '<sep gap="36"></sep>'].join(''),
        b('sensing_askandwait', { QUESTION: txt("What's your name?") }), b('sensing_answer'), '<sep gap="36"></sep>',
        b('sensing_keypressed', { KEY_OPTION: m.key }), b('sensing_mousedown'), b('sensing_mousex'), b('sensing_mousey'),
        ctx.isStage ? '' : b('sensing_setdragmode', {}, { DRAG_MODE: 'draggable' }),
        '<sep gap="36"></sep>', b('sensing_loudness'), '<sep gap="36"></sep>', b('sensing_timer'), b('sensing_resettimer'), '<sep gap="36"></sep>',
        b('sensing_of', { OBJECT: m.of }, { PROPERTY: ctx.isStage ? 'backdrop #' : 'x position' }), '<sep gap="36"></sep>',
        b('sensing_current', {}, { CURRENTMENU: 'YEAR' }), b('sensing_dayssince2000'), b('sensing_username'),
    ].join('');
    const operators = [
        b('operator_add', { NUM1: num(''), NUM2: num('') }), b('operator_subtract', { NUM1: num(''), NUM2: num('') }),
        b('operator_multiply', { NUM1: num(''), NUM2: num('') }), b('operator_divide', { NUM1: num(''), NUM2: num('') }), '<sep gap="36"></sep>',
        b('operator_random', { FROM: num(1), TO: num(10) }), '<sep gap="36"></sep>',
        b('operator_gt', { OPERAND1: txt(''), OPERAND2: txt(50) }), b('operator_lt', { OPERAND1: txt(''), OPERAND2: txt(50) }),
        b('operator_equals', { OPERAND1: txt(''), OPERAND2: txt(50) }), '<sep gap="36"></sep>',
        '<block type="operator_and"></block>', '<block type="operator_or"></block>', '<block type="operator_not"></block>', '<sep gap="36"></sep>',
        b('operator_join', { STRING1: txt('apple '), STRING2: txt('banana') }), b('operator_letter_of', { LETTER: pos(1), STRING: txt('apple') }),
        b('operator_length', { STRING: txt('apple') }), b('operator_contains', { STRING1: txt('apple'), STRING2: txt('a') }), '<sep gap="36"></sep>',
        b('operator_mod', { NUM1: num(''), NUM2: num('') }), b('operator_round', { NUM: num('') }), b('operator_mathop', { NUM: num('') }, { OPERATOR: 'abs' }),
    ].join('');
    const pen = [
        b('pen_clear'), '<sep gap="36"></sep>', ctx.isStage ? '' : b('pen_stamp'), ctx.isStage ? '' : b('pen_penDown'), ctx.isStage ? '' : b('pen_penUp'),
        '<sep gap="36"></sep>',
        b('pen_setPenColorToColor', { COLOR: col('#0fBD8C') }), b('pen_changePenColorParamBy', { COLOR_PARAM: m.param, VALUE: num(10) }),
        b('pen_setPenColorParamTo', { COLOR_PARAM: m.param, VALUE: num(50) }), '<sep gap="36"></sep>',
        b('pen_changePenSizeBy', { SIZE: num(1) }), b('pen_setPenSizeTo', { SIZE: num(1) }),
    ].join('');
    return '<xml style="display: none">' +
        (ctx.isStage ? '' : category('Motion', 'motion', '#4C97FF', '#3373CC', motion)) +
        category('Looks', 'looks', '#9966FF', '#774DCB', looks) +
        category('Sound', 'sound', '#CF63CF', '#BD42BD', sound) +
        category('Events', 'events', '#FFD500', '#CCAA00', events) +
        category('Control', 'control', '#FFAB19', '#CF8B17', control) +
        category('Sensing', 'sensing', '#4CBFE6', '#2E8EB8', sensing) +
        category('Operators', 'operators', '#40BF4A', '#389438', operators) +
        category('Variables', 'data', '#FF8C1A', '#DB6E00', '', 'VARIABLE') +
        category('My Blocks', 'more', '#FF6680', '#FF3355', '', 'PROCEDURE') +
        category('Pen', 'pen', '#0fBD8C', '#0B8E69', pen) +
        '</xml>';
}
