const test = require('node:test');
const assert = require('node:assert/strict');
const {compare, turboSnapshot} = require('../integration/compare.js');

const lithium = (value = 10) => ({
    threads: 0,
    targets: [
        {name:'Stage', isStage:true, isOriginal:true, variables:{counter:value},
            lists:{numbers:[1,2,3]}},
        {name:'Sprite1', isStage:false, isOriginal:true, x:100, y:-25,
            direction:90, size:100, visible:true, costume:0, variables:{}, lists:{}}
    ]
});
const vm = (value = 10) => ({
    runtime: {
        threads: [],
        targets: [
            {getName:()=> 'Stage', isStage:true, isOriginal:true,
                variables:{var1:{name:'counter',type:'',value},
                    list1:{name:'numbers',type:'list',value:[1,2,3]}}},
            {getName:()=> 'Sprite1', isStage:false, isOriginal:true,
                x:100, y:-25, direction:90, size:100, visible:true, currentCostume:0,
                variables:{}}
        ]
    }
});

test('identical Scratch state produces a match', () => {
    const result = compare(lithium(), turboSnapshot(vm()));
    assert.equal(result.status, 'match');
    assert.equal(result.match,true);
    assert.deepEqual(result.differences, []);
});
test('reports a scalar mismatch by target and variable name', () => {
    const result = compare(lithium(10), turboSnapshot(vm(100000)));
    assert.equal(result.status,'mismatch');
    assert.deepEqual(result.differences[0], {
        path:'Stage.variables.counter', lithium:10, turboWarp:100000
    });
});
test('detects list item and sprite position mismatches', () => {
    const a=lithium();
    a.targets[0].lists.numbers[1] = 20;
    a.targets[1].x = 101;
    const result=compare(a,turboSnapshot(vm()));
    assert.deepEqual(result.differences.map(x=>x.path),[
        'Stage.lists.numbers[1]','Sprite1.x'
    ]);
});
test('reports incomplete when either runtime is still running', () => {
    const a=lithium();
    a.threads=1;
    const result=compare(a,turboSnapshot(vm()));
    assert.equal(result.status,'incomplete');
    assert.equal(result.match,false);
});
test('reports missing and extra variables instead of ignoring them', () => {
    const a=lithium();
    a.targets[0].variables.extra=5;
    const result=compare(a,turboSnapshot(vm()));
    assert.equal(result.status,'mismatch');
    assert.equal(result.differences[0].path,'Stage.variables.extra');
});
test('numeric tolerances are explicit', () => {
    const a=lithium(10.00000000001);
    assert.equal(compare(a,turboSnapshot(vm())).status,'match');
    assert.equal(compare(a,turboSnapshot(vm()),{epsilon:0}).status,'mismatch');
});
test('different clone counts cannot pass', () => {
    const a=lithium();
    a.targets.push({...a.targets[1],isOriginal:false});
    const result=compare(a,turboSnapshot(vm()));
    assert.equal(result.status,'mismatch');
    assert.match(result.differences[0].path,/targetCount/);
});

test('name collisions are never reported as compatible', () => {
    const source = vm();
    source.runtime.targets[0].variables.other = {name:'counter',type:'',value:10};
    const actual = turboSnapshot(source);
    assert.equal(actual.ambiguities.length, 1);
    const result = compare(lithium(), actual);
    assert.equal(result.status, 'unsupported');
    assert.equal(result.match, false);
    assert.match(result.reason, /Duplicate variable/);
});
test('two empty runtimes cannot pass without any loaded project', () => {
    const result = compare({threads:0,targets:[]}, {threads:0,targets:[]});
    assert.equal(result.status, 'unsupported');
    assert.equal(result.match, false);
    assert.match(result.reason, /No project targets/);
});
