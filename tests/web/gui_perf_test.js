// Runs perf_loop.sb3 in the GUI build through Lithium and reports the timer value the project says.
const http=require('http'),fs=require('fs'),path=require('path');
const { chromium } = require(process.env.PW || '/opt/npm-tools/node_modules/playwright-core');
const root=process.env.GUI_BUILD||'/home/claude/tw/scratch-gui/build';
const types={'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.png':'image/png','.wasm':'application/wasm','.json':'application/json'};
const srv=http.createServer((q,r)=>{let f=path.join(root,decodeURIComponent(q.url.split('?')[0]));if(q.url==='/'||q.url.startsWith('/?'))f=path.join(root,'editor.html');if(!fs.existsSync(f)||fs.statSync(f).isDirectory()){r.writeHead(404);r.end();return}r.writeHead(200,{'content-type':types[path.extname(f)]||'application/octet-stream'});fs.createReadStream(f).pipe(r)});
srv.listen(0,async()=>{
 const b=await chromium.launch({executablePath:'/opt/pw-browsers/chromium',args:['--use-angle=swiftshader','--use-gl=angle','--enable-unsafe-swiftshader','--no-sandbox']});
 const p=await b.newPage({viewport:{width:1400,height:800}});
 await p.goto('http://localhost:'+srv.address().port+'/editor.html');
 await p.waitForFunction(()=>window.vm&&window.vm.renderer&&window.vm.initialized,null,{timeout:60000});
 await new Promise(r=>setTimeout(r,2000));
 const sb3=fs.readFileSync(path.join(__dirname,'perf_loop.sb3')).toString('base64');
 await p.evaluate(async b64=>{const bin=Uint8Array.from(atob(b64),c=>c.charCodeAt(0));await window.vm.loadProject(bin.buffer);},sb3);
 await p.evaluate(()=>window.vm.lithium.init());
 for(let i=0;i<4;i++){
  const wall=await p.evaluate(()=>new Promise(res=>{const t0=performance.now();window.vm.greenFlag();const poll=()=>{const L=window.vm.lithium;if(L.active&&L.lith.state().targets.find(t=>t.name==='A').bubble!=='')res(performance.now()-t0);else setTimeout(poll,2)};poll()}));
  console.log('  wall time flag -> say:',wall.toFixed(0),'ms');
  const r=await p.evaluate(()=>{const s=window.vm.lithium.lith.state();return {say:s.targets.find(t=>t.name==='A').bubble,count:s.targets[0].variables.count}});
  console.log('run',i+1,'timer said',r.say,'s  count',r.count);
  await p.evaluate(()=>window.vm.stopAll());
  await new Promise(r=>setTimeout(r,300));
 }
 await b.close();srv.close();process.exit(0)});
