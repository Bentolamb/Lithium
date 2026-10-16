const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vmModule = require('node:vm');
const {compare, turboSnapshot} = require('../integration/compare.js');

test('shadow bridge initializes, mirrors execution and records a final match', async () => {
    let lithiumThreads = 0;
    let lithiumValue = 0;
    let interval = null;
    const turbo = {
        runtime: {
            turboMode: false,
            threads: [],
            targets: [{isStage:true,isOriginal:true,getName:()=> 'Stage',
                variables:{counter:{name:'counter',type:'',value:0}}}],
            _step () {
                this.threads = [];
                this.targets[0].variables.counter.value = 10;
            }
        },
        toJSON: () => JSON.stringify({targets:[]}),
        greenFlag () {this.runtime.threads=[{}];},
        stopAll () {this.runtime.threads=[];}
    };
    const handlers = {
        runtime_create:()=>1,
        load_sb3_json:()=>1,
        get_load_log:()=> '',
        green_flag:()=> {lithiumThreads=1; lithiumValue=0;},
        stop_all:()=> {lithiumThreads=0;},
        runtime_step:()=> {lithiumThreads=0; lithiumValue=10;},
        dump_state:()=> JSON.stringify({threads:lithiumThreads,targets:[{
            name:'Stage',isStage:true,isOriginal:true,variables:{counter:lithiumValue},lists:{}
        }]}),
        set_turbo_mode:()=>{}
    };
    const module = {cwrap(name) {
        const handler=handlers[name.replace(/^lithium_/,'')];
        if (!handler) throw Error('Unknown C API: '+name);
        return handler;
    }};
    const window = {
        vm:turbo, LithiumCompare:{compare,turboSnapshot},
        createLithium:async()=>module
    };
    const sandbox = {
        window, console:{info(){},warn(){},log(){}},
        setInterval(callback) {interval=callback;return 1;},
        clearInterval() {interval=null;}
    };
    const code=fs.readFileSync(require.resolve('../integration/shadow.js'),'utf8');
    vmModule.runInNewContext(code,sandbox);
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(typeof interval,'function');
    interval();
    assert.equal(window.LithiumShadow.mode,'ready');
    turbo.greenFlag();
    assert.equal(window.LithiumShadow.mode,'shadow-running');
    turbo.runtime._step();
    assert.equal(window.LithiumShadow.mode,'shadow-finished');
    assert.equal(window.LithiumShadow.lastComparison.status,'match');
    assert.equal(window.LithiumShadow.frames,1);
    assert.equal(window.LithiumShadow.lastError,null);
});
