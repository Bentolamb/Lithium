// Builds tests/web/perf_loop.sb3: reset timer; repeat 100000 { change count by 1 }; say timer.
const fs = require('fs'), path = require('path'), os = require('os'), crypto = require('crypto');
const { execFileSync } = require('child_process');
const { Project, L, N, B, S, stack } = require('../diff/sb3gen.js');
const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20"><rect width="20" height="20" fill="#f00"/></svg>';
const bg = '<svg xmlns="http://www.w3.org/2000/svg" width="480" height="360"><rect width="480" height="360" fill="#fff"/></svg>';
const p = new Project(); const st = p.stage; st.defineVar('count', 0);
const h1 = crypto.createHash('md5').update(svg).digest('hex'), h2 = crypto.createHash('md5').update(bg).digest('hex');
st.props = { costumes: [{ assetId: h2, name: 'b', md5ext: h2 + '.svg', dataFormat: 'svg', rotationCenterX: 240, rotationCenterY: 180 }] };
const a = p.sprite('A', { costumes: [{ assetId: h1, name: 'c', md5ext: h1 + '.svg', dataFormat: 'svg', rotationCenterX: 10, rotationCenterY: 10 }] });
a.flag(S('sensing_resettimer'),
    S('control_repeat', { TIMES: N(100000), SUBSTACK: stack(S('data_changevariableby', { VALUE: N(1) }, { VARIABLE: ['count', st.varId('count')] })) }),
    S('looks_say', { MESSAGE: B('sensing_timer') }));
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sb3-'));
fs.writeFileSync(path.join(dir, 'project.json'), JSON.stringify(p));
fs.writeFileSync(path.join(dir, h1 + '.svg'), svg); fs.writeFileSync(path.join(dir, h2 + '.svg'), bg);
const out = path.resolve(process.argv[2] || path.join(__dirname, 'perf_loop.sb3'));
fs.rmSync(out, { force: true });
execFileSync('python3', ['-c', 'import zipfile,sys,os\nz=zipfile.ZipFile(sys.argv[1],"w",zipfile.ZIP_DEFLATED)\nfor f in os.listdir(sys.argv[2]): z.write(os.path.join(sys.argv[2],f),f)\nz.close()', out, dir]);
console.log('wrote', out);
