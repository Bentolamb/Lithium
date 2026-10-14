// Generated differential test cases. Each case is a project run for a few
// frames in scratch-vm and in Lithium; every variable, position and direction
// must match.
'use strict';
const { Project, L, N, V, B, S, stack } = require('./sb3gen.js');

const NUMS = ['', ' ', '0', '-0', '1', '-1', '2', '0.5', '-1.5', '10', '9', '100', '1e3', '1e21', '-1e21', '0x10', '0b11', '0o7',
              ' 12 ', '12abc', 'abc', '.5', '5.', '+5', 'Infinity', '-Infinity', 'NaN', 'true', '1e308', '1e-7', '123456789012345680000',
              '0.1', '0.30000000000000004'];
const TEXTS = ['', ' ', 'a', 'A', 'b', 'abc', 'ABC', 'Abc', '10', '9', '1', '01', '1.0', 'true', 'false', '0', 'hello world', 'héllo', 'HÉLLO',
               '😀', 'a😀b', ' x ', '\t', 'Infinity', 'null', 'undefined'];
const ANGLES = ['0', '90', '180', '-90', '270', '360', '450', '-270', '45', '30', '60', '89.999', '1e10', '0.5', '-180', '181', '-181', 'abc', 'Infinity'];

// One project holding many "set r<i> to <expr>" statements.
function exprProject(name, exprs, extra) {
    const p = new Project();
    const st = p.stage;
    const stmts = [];
    exprs.forEach((e, i) => {
        st.defineVar('r' + i, 'UNSET');
        stmts.push(st.set('r' + i, e));
    });
    if (extra) extra(p, st);
    st.flag(...stmts);
    return { name, project: p, frames: 3, n: exprs.length };
}

const binary = (op, a, b, ins) => B(op, { [ins[0]]: L(a), [ins[1]]: L(b) });

function* cases() {
    // ---- arithmetic ------------------------------------------------------
    for (const [op, ins] of [['operator_add', ['NUM1', 'NUM2']], ['operator_subtract', ['NUM1', 'NUM2']],
                             ['operator_multiply', ['NUM1', 'NUM2']], ['operator_divide', ['NUM1', 'NUM2']],
                             ['operator_mod', ['NUM1', 'NUM2']]]) {
        const ex = [];
        for (const a of NUMS) for (const b of NUMS) ex.push(binary(op, a, b, ins));
        yield exprProject(op, ex);
    }
    // ---- comparison, logic ---------------------------------------------
    for (const op of ['operator_lt', 'operator_gt', 'operator_equals']) {
        const ex = [];
        const pool = [...new Set([...NUMS, ...TEXTS])];
        for (const a of pool) for (const b of pool) ex.push(binary(op, a, b, ['OPERAND1', 'OPERAND2']));
        yield exprProject(op, ex);
    }
    {
        const bools = ['', '0', '1', 'true', 'false', 'FALSE', 'False', 'abc', ' ', '0.0', 'NaN', '-0', 'null'];
        const ex = [];
        for (const a of bools) for (const b of bools) { ex.push(binary('operator_and', a, b, ['OPERAND1', 'OPERAND2'])); ex.push(binary('operator_or', a, b, ['OPERAND1', 'OPERAND2'])); }
        for (const a of bools) ex.push(B('operator_not', { OPERAND: L(a) }));
        yield exprProject('and_or_not', ex);
    }
    // ---- unary math ------------------------------------------------------
    {
        const ex = [];
        for (const a of NUMS) ex.push(B('operator_round', { NUM: L(a) }));
        yield exprProject('operator_round', ex);
    }
    {
        const ops = ['abs', 'floor', 'ceiling', 'sqrt', 'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'ln', 'log', 'e ^', '10 ^'];
        const vals = [...NUMS, '90', '270', '-90', '-270', '180', '360', '450', '45', '30', '60', '720', '1', '-1', '0.5', '2', '3', '7', '1000', '-100'];
        for (const op of ops) {
            const ex = [...new Set(vals)].map(a => B('operator_mathop', { NUM: L(a) }, { OPERATOR: op }));
            yield { ...exprProject('mathop_' + op.replace(/\W/g, '_'), ex), ulp: 4 };
        }
    }
    // ---- strings ---------------------------------------------------------
    {
        const ex = [];
        for (const a of TEXTS) for (const b of TEXTS) ex.push(binary('operator_join', a, b, ['STRING1', 'STRING2']));
        yield exprProject('operator_join', ex);
    }
    {
        const ex = [];
        for (const a of TEXTS) ex.push(B('operator_length', { STRING: L(a) }));
        yield exprProject('operator_length', ex);
    }
    {
        const ex = [];
        for (const i of ['0', '1', '2', '3', '5', '-1', '1.5', '2.9', 'abc', '', '1e3', 'Infinity']) for (const s of TEXTS) {
            ex.push(B('operator_letter_of', { LETTER: L(i), STRING: L(s) }));
        }
        yield exprProject('operator_letter_of', ex);
    }
    {
        const ex = [];
        for (const a of TEXTS) for (const b of TEXTS) ex.push(binary('operator_contains', a, b, ['STRING1', 'STRING2']));
        yield exprProject('operator_contains', ex);
    }
    {
        const ex = [];
        for (const a of ['5', '2.5', '-3', '1e3', ' 7 ']) ex.push(B('operator_random', { FROM: L(a), TO: L(a) }));
        yield exprProject('operator_random_degenerate', ex);
    }
    // ---- nesting and variable reporters ----------------------------------
    {
        const p = new Project();
        const st = p.stage;
        const vars = { n: 5, s: 'hello', t: true, e: '', z: 0, f: 1.5, w: ' 3 ' };
        for (const [k, v] of Object.entries(vars)) st.defineVar(k, v);
        const names = Object.keys(vars);
        const stmts = [];
        let i = 0;
        const out = e => { st.defineVar('o' + i, 'UNSET'); stmts.push(st.set('o' + i, e)); i++; };
        for (const a of names) {
            out(V(a));
            out(B('operator_join', { STRING1: V(a), STRING2: L('!') }));
            out(B('operator_add', { NUM1: V(a), NUM2: N('1') }));
            out(B('operator_length', { STRING: V(a) }));
            out(B('operator_not', { OPERAND: V(a) }));
            for (const b of names) {
                out(B('operator_equals', { OPERAND1: V(a), OPERAND2: V(b) }));
                out(B('operator_lt', { OPERAND1: V(a), OPERAND2: V(b) }));
                out(B('operator_multiply', { NUM1: V(a), NUM2: V(b) }));
            }
        }
        // nested: ((n + 1) * 2) joined with letter 2 of s
        out(B('operator_join', { STRING1: B('operator_multiply', { NUM1: B('operator_add', { NUM1: V('n'), NUM2: N('1') }), NUM2: N('2') }),
                                 STRING2: B('operator_letter_of', { LETTER: N('2'), STRING: V('s') }) }));
        out(B('operator_and', { OPERAND1: B('operator_lt', { OPERAND1: V('n'), OPERAND2: N('10') }), OPERAND2: B('operator_not', { OPERAND: V('e') }) }));
        st.flag(...stmts);
        yield { name: 'variable_reporters', project: p, frames: 3 };
    }
    // ---- variables: change/set with odd types -----------------------------
    {
        const p = new Project();
        const st = p.stage;
        const init = { a: 0, b: 'abc', c: true, d: '', e: '5', f: '1e3', g: ' 2 ', h: -0.5, i: '0x10', j: 'Infinity' };
        for (const [k, v] of Object.entries(init)) st.defineVar(k, v);
        const stmts = [];
        for (const k of Object.keys(init)) { stmts.push(st.change(k, N('1'))); stmts.push(st.change(k, L('abc'))); stmts.push(st.change(k, N('-2.5'))); }
        st.flag(...stmts);
        yield { name: 'change_variable', project: p, frames: 3 };
    }
    // ---- control flow ----------------------------------------------------
    {
        const p = new Project();
        const st = p.stage;
        const tests = [];
        const mk = (name, ...body) => { st.defineVar(name, 0); tests.push([name, body]); };
        for (const times of ['0', '1', '3', '2.5', '1.5', '0.5', '-1', 'abc', '', ' 4 ', '1e1', '3.4999', '-0.5']) {
            const v = 'rep_' + times.replace(/\W/g, '_');
            st.defineVar(v, 0);
            tests.push([v, [S('control_repeat', { TIMES: L(times), SUBSTACK: stack(st.change(v, N('1'))) })]]);
        }
        mk('until', st.set('until', N('0')), S('control_repeat_until', { CONDITION: B('operator_gt', { OPERAND1: V('until'), OPERAND2: N('4') }), SUBSTACK: stack(st.change('until', N('1'))) }));
        mk('until_never_enter', S('control_repeat_until', { CONDITION: B('operator_equals', { OPERAND1: N('1'), OPERAND2: N('1') }), SUBSTACK: stack(st.change('until_never_enter', N('1'))) }));
        mk('ifelse', S('control_if_else', { CONDITION: B('operator_lt', { OPERAND1: N('1'), OPERAND2: N('2') }),
                                           SUBSTACK: stack(st.set('ifelse', L('then'))), SUBSTACK2: stack(st.set('ifelse', L('else'))) }));
        mk('ifelse2', S('control_if_else', { CONDITION: L('0'), SUBSTACK: stack(st.set('ifelse2', L('then'))), SUBSTACK2: stack(st.set('ifelse2', L('else'))) }));
        mk('if_empty', S('control_if', { CONDITION: L('1') }), st.set('if_empty', L('after')));
        mk('ifelse_empty_branch', S('control_if_else', { CONDITION: L('0'), SUBSTACK: stack(st.set('ifelse_empty_branch', L('then'))) }), st.set('ifelse_empty_branch', L('after')));
        mk('nested', S('control_repeat', { TIMES: N('3'), SUBSTACK: stack(
            S('control_repeat', { TIMES: N('4'), SUBSTACK: stack(st.change('nested', N('1'))) }),
            S('control_if', { CONDITION: B('operator_gt', { OPERAND1: V('nested'), OPERAND2: N('5') }), SUBSTACK: stack(st.change('nested', N('100'))) })) }));
        mk('waituntil', st.set('waituntil', N('1')), S('control_wait_until', { CONDITION: B('operator_equals', { OPERAND1: V('waituntil'), OPERAND2: N('1') }) }), st.set('waituntil', N('2')));
        mk('stop_this', st.set('stop_this', N('1')), S('control_stop', {}, { STOP_OPTION: 'this script' }), st.set('stop_this', N('2')));
        mk('stop_in_loop', S('control_repeat', { TIMES: N('10'), SUBSTACK: stack(st.change('stop_in_loop', N('1')),
            S('control_if', { CONDITION: B('operator_equals', { OPERAND1: V('stop_in_loop'), OPERAND2: N('3') }), SUBSTACK: stack(S('control_stop', {}, { STOP_OPTION: 'this script' })) })) }),
            st.set('stop_in_loop', N('999')));
        for (const [v, body] of tests) st.flag(...body);
        // stop all from a second script: others stop immediately; order matters
        st.defineVar('sa_a', 0); st.defineVar('sa_b', 0);
        st.flag(st.set('sa_a', N('1')), S('control_stop', {}, { STOP_OPTION: 'all' }), st.set('sa_a', N('2')));
        st.flag(st.set('sa_b', N('1')), S('control_repeat', { TIMES: N('5'), SUBSTACK: stack(st.change('sa_b', N('1'))) }));
        yield { name: 'control_flow', project: p, frames: 4 };
    }
    // ---- motion ----------------------------------------------------------
    {
        const p = new Project();
        p.stage.defineVar('probe', 'UNSET');
        let k = 0;
        const sprite = (steps, props) => { const s = p.sprite('S' + k++, props); s.flag(...steps); return s; };
        for (const d of ANGLES) {
            const s = p.sprite('dir' + k++);
            s.flag(S('motion_pointindirection', { DIRECTION: L(d) }), S('motion_movesteps', { STEPS: N('10') }));
        }
        for (const d of ANGLES) {
            const s = p.sprite('turn' + k++);
            s.flag(S('motion_turnright', { DEGREES: L(d) }), S('motion_turnleft', { DEGREES: L('7') }), S('motion_movesteps', { STEPS: N('-3.3') }));
        }
        for (const d of ['0', '30', '45', '60', '120', '135', '-45', '-135', '170']) {
            const s = p.sprite('walk' + k++);
            s.flag(S('motion_pointindirection', { DIRECTION: L(d) }),
                   S('control_repeat', { TIMES: N('20'), SUBSTACK: stack(S('motion_movesteps', { STEPS: N('7') }), S('motion_turnright', { DEGREES: N('13') })) }));
        }
        {
            const s = p.sprite('xy' + k++);
            s.flag(S('motion_gotoxy', { X: L('10'), Y: L('abc') }), S('motion_changexby', { DX: L('5.5') }), S('motion_changeyby', { DY: L('-3') }),
                   S('motion_setx', { X: L('1e3') }), S('motion_sety', { Y: L(' 7 ') }));
        }
        {
            const s = p.sprite('prec' + k++);
            p.stage.defineVar('px', 'UNSET'); p.stage.defineVar('py', 'UNSET'); p.stage.defineVar('pd', 'UNSET');
            s.flag(S('motion_gotoxy', { X: L('0.0000000001'), Y: L('2.9999999999') }),
                   p.stage.set('px', B('motion_xposition')), p.stage.set('py', B('motion_yposition')), p.stage.set('pd', B('motion_direction')));
        }
        {
            const s = p.sprite('prec2' + k++);
            p.stage.defineVar('px2', 'UNSET'); p.stage.defineVar('py2', 'UNSET');
            s.flag(S('motion_gotoxy', { X: L('1.0000001'), Y: L('-0.0000000004') }), p.stage.set('px2', B('motion_xposition')), p.stage.set('py2', B('motion_yposition')));
        }
        yield { name: 'motion', project: p, frames: 3, ulp: 4 };
    }
}

module.exports = { cases };
