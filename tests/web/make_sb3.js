// Builds tests/web/render_test.sb3: two sprites with SVG costumes, a sound, pen and speech.
//   node make_sb3.js [outfile]
'use strict';
const fs = require('fs');
const path = require('path');
const os = require('os');
const { execFileSync } = require('child_process');
const { Project, L, N, V, B, S, MENU, stack } = require('../diff/sb3gen.js');

const svg = (w, h, body) => `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">${body}</svg>`;
const assets = {
    'backdrop.svg': svg(480, 360, '<rect width="480" height="360" fill="#ffffff"/><rect x="0" y="300" width="480" height="60" fill="#2a6df4"/>'),
    'red.svg': svg(40, 40, '<rect width="40" height="40" fill="#ff0000"/>'),
    'green.svg': svg(40, 40, '<circle cx="20" cy="20" r="20" fill="#00c000"/>'),
};
// 0.2 s of a 440 Hz sine, 16-bit mono WAV.
function wav() {
    const rate = 22050, n = Math.floor(rate * 0.2), data = Buffer.alloc(n * 2);
    for (let i = 0; i < n; i++) data.writeInt16LE(Math.round(8000 * Math.sin(2 * Math.PI * 440 * i / rate)), i * 2);
    const h = Buffer.alloc(44);
    h.write('RIFF', 0); h.writeUInt32LE(36 + data.length, 4); h.write('WAVEfmt ', 8); h.writeUInt32LE(16, 16);
    h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22); h.writeUInt32LE(rate, 24); h.writeUInt32LE(rate * 2, 28);
    h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34); h.write('data', 36); h.writeUInt32LE(data.length, 40);
    return Buffer.concat([h, data]);
}
const costume = (name, file, cx, cy) => ({ assetId: file.split('.')[0], name, md5ext: file, dataFormat: 'svg', rotationCenterX: cx, rotationCenterY: cy });

const p = new Project();
const st = p.stage;
st.defineVar('touch', 0); st.defineVar('done', 0);
st.props = { costumes: [costume('backdrop1', 'backdrop.svg', 240, 180)] };
const a = p.sprite('A', { x: 0, y: 0, costumes: [costume('red', 'red.svg', 20, 20)],
    sounds: [{ assetId: 'pop', name: 'pop', md5ext: 'pop.wav', dataFormat: 'wav', rate: 22050, sampleCount: 4410 }] });
const b = p.sprite('B', { x: -100, y: -60, costumes: [costume('green', 'green.svg', 20, 20)] });
const setG = (name, expr) => S('data_setvariableto', { VALUE: expr }, { VARIABLE: [name, st.varId(name)] });
a.flag(
    S('motion_gotoxy', { X: N(-90), Y: N(-50) }),
    setG('touch', B('sensing_touchingobject', { TOUCHINGOBJECTMENU: MENU('sensing_touchingobjectmenu', 'TOUCHINGOBJECTMENU', 'B') })),
    S('pen_penDown'),
    S('motion_gotoxy', { X: N(100), Y: N(60) }),
    S('pen_penUp'),
    S('looks_say', { MESSAGE: L('Hello') }),
    S('sound_playuntildone', { SOUND_MENU: MENU('sound_sounds_menu', 'SOUND_MENU', 'pop') }),
    setG('done', N(1)),
);

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sb3-'));
// Asset ids are real md5 hashes so that scratch-vm / the TurboWarp GUI accept the project.
let json = JSON.stringify(p);
const files = Object.assign({}, assets, { 'pop.wav': wav() });
for (const [n, c] of Object.entries(files)) {
    const hash = require('crypto').createHash('md5').update(c).digest('hex');
    const ext = n.split('.')[1], id = n.split('.')[0];
    json = json.split('"assetId":"' + id + '"').join('"assetId":"' + hash + '"').split('"md5ext":"' + n + '"').join('"md5ext":"' + hash + '.' + ext + '"');
    fs.writeFileSync(path.join(dir, hash + '.' + ext), c);
}
fs.writeFileSync(path.join(dir, 'project.json'), json);
const out = path.resolve(process.argv[2] || path.join(__dirname, 'render_test.sb3'));
fs.rmSync(out, { force: true });
execFileSync('python3', ['-c', 'import zipfile,sys,os\nz=zipfile.ZipFile(sys.argv[1],"w",zipfile.ZIP_DEFLATED)\nfor f in os.listdir(sys.argv[2]): z.write(os.path.join(sys.argv[2],f),f)\nz.close()', out, dir]);
console.log('wrote', out);
