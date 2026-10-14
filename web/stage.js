// Browser-side Host for the Lithium runtime: draws with scratch-render (WebGL),
// plays sounds with Web Audio, shows speech bubbles and the ask prompt.
// Every runtime request arrives through handle(op, a, b, c, d, p); the op
// numbers are documented in runtime/include/lithium/c_api.h.
import { readZipEntry } from './lithium.js';

const GROUPS = ['background', 'video', 'pen', 'sprite'];
const EFFECTS_NATIVE = { w: 480, h: 360 };

export class StageHost {
    constructor(canvas, lith) {
        this.lith = lith;
        this.canvas = canvas;
        this.r = new window.ScratchRender(canvas);
        this.r.setLayerGroupOrdering(GROUPS);
        this.groupOf = new Map();       // drawable id -> layer group name
        this.penSkin = -1;
        this.bubbles = new Map();       // target uid -> {drawableId, skinId, type, text, onSpriteRight, targetDrawable}
        this.audio = null;
        this.buffers = [];              // per original target: AudioBuffer|null per sound
        this.targetSounds = new Map();  // uid -> [{source, gain, panner, req, index}]
        this.askBox = null;
        this.soundBank = new Map();     // md5ext -> Promise<AudioBuffer|null>
        this.zip = null;
    }

    // ---- assets --------------------------------------------------------
    async loadAssets(zipBuffer) {
        this.zip = zipBuffer;
        const manifest = this.lith.assetManifest();
        const cache = new Map();
        const read = async name => {
            if (!cache.has(name)) cache.set(name, readZipEntry(zipBuffer, name).catch(() => null));
            return cache.get(name);
        };
        const skins = new Map();
        this.skinList = [];
        for (let ti = 0; ti < manifest.length; ti++) {
            const t = manifest[ti];
            for (let ci = 0; ci < t.costumes.length; ci++) {
                const c = t.costumes[ci];
                const key = c.md5ext + '|' + c.rotationCenterX + '|' + c.rotationCenterY + '|' + c.bitmapResolution;
                if (!skins.has(key)) skins.set(key, this.makeSkin(c, await read(c.md5ext)));
                const skinId = await skins.get(key);
                if (skinId >= 0) { this.lith.setCostumeSkin(ti, ci, skinId); this.skinList.push([ti, ci, skinId]); }
            }
        }
        this.lith.syncRenderer();
        // Sounds decode lazily on first play, but start decoding now.
        this.buffers = manifest.map(t => t.sounds.map(s => this.decodeSound(s, read)));
        this.manifest = manifest;
    }

    // After the runtime reloads the (edited) project, give its costumes their skins again.
    reapplySkins() {
        for (const [ti, ci, skin] of this.skinList || []) this.lith.setCostumeSkin(ti, ci, skin);
        this.lith.syncRenderer();
    }

    async makeSkin(c, bytes) {
        try {
            if (!bytes) return -1;
            if (c.dataFormat === 'svg') {
                const text = new TextDecoder().decode(bytes);
                return this.r.createSVGSkin(text, [c.rotationCenterX, c.rotationCenterY]);
            }
            const blob = new Blob([bytes], { type: c.dataFormat === 'png' ? 'image/png' : 'image/jpeg' });
            const bmp = await createImageBitmap(blob);
            const canvas = document.createElement('canvas');
            canvas.width = bmp.width; canvas.height = bmp.height;
            canvas.getContext('2d').drawImage(bmp, 0, 0);
            const res = c.bitmapResolution || 1;
            return this.r.createBitmapSkin(canvas, res, [c.rotationCenterX / res, c.rotationCenterY / res]);
        } catch (e) {
            console.warn('costume failed to load', c.name, e);
            return -1;
        }
    }

    async decodeSound(s, read) {
        try {
            const bytes = await read(s.md5ext);
            if (!bytes) return null;
            this.ensureAudio();
            const copy = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
            return await this.audio.decodeAudioData(copy);
        } catch (e) {
            console.warn('sound failed to decode', s.name, e);
            return null;
        }
    }

    ensureAudio() {
        if (!this.audio) this.audio = new (window.AudioContext || window.webkitAudioContext)();
        if (this.audio.state === 'suspended') this.audio.resume().catch(() => {});
    }

    // ---- drawing -------------------------------------------------------
    draw() {
        this.positionBubbles();
        this.r.draw();
    }

    penLayer() {
        if (this.penSkin < 0) {
            this.penSkin = this.r.createPenSkin();
            const id = this.r.createDrawable('pen');
            this.groupOf.set(id, 'pen');
            this.r.updateDrawableSkinId(id, this.penSkin);
        }
        return this.penSkin;
    }

    reset() {
        for (const uid of [...this.bubbles.keys()]) this.removeBubble(uid);
        this.stopAllSounds(-1);
        this.penSkin = -1;
    }

    // ---- bubbles -------------------------------------------------------
    removeBubble(uid) {
        const b = this.bubbles.get(uid);
        if (!b) return;
        this.r.destroyDrawable(b.drawableId, 'sprite');
        this.r.destroySkin(b.skinId);
        this.bubbles.delete(uid);
    }

    setBubble(uid, targetDrawable, think, text) {
        if (!text) { this.removeBubble(uid); return; }
        const type = think ? 'think' : 'say';
        let b = this.bubbles.get(uid);
        if (b) {
            b.type = type; b.text = text; b.targetDrawable = targetDrawable;
            this.r.updateTextSkin(b.skinId, type, text, b.onSpriteRight, [0, 0]);
        } else {
            b = { type, text, onSpriteRight: true, targetDrawable };
            b.drawableId = this.r.createDrawable('sprite');
            b.skinId = this.r.createTextSkin(type, text, true, [0, 0]);
            this.r.updateDrawableSkinId(b.drawableId, b.skinId);
            this.bubbles.set(uid, b);
        }
        this.positionBubble(b);
    }

    positionBubbles() { for (const b of this.bubbles.values()) this.positionBubble(b); }

    // Ported from scratch-vm's Scratch3LooksBlocks._positionBubble.
    positionBubble(b) {
        const [w, h] = this.r.getCurrentSkinSize(b.drawableId);
        let tb;
        try { tb = this.r.getBoundsForBubble(b.targetDrawable); } catch (e) { return; }
        const [sw, sh] = this.r.getNativeSize();
        const sb = { left: -sw / 2, right: sw / 2, top: sh / 2, bottom: -sh / 2 };
        if (b.onSpriteRight && w + tb.right > sb.right && tb.left - w > sb.left) {
            b.onSpriteRight = false;
            this.r.updateTextSkin(b.skinId, b.type, b.text, false, [0, 0]);
        } else if (!b.onSpriteRight && tb.left - w < sb.left && w + tb.right < sb.right) {
            b.onSpriteRight = true;
            this.r.updateTextSkin(b.skinId, b.type, b.text, true, [0, 0]);
        }
        const [w2, h2] = this.r.getCurrentSkinSize(b.drawableId);
        this.r.updateDrawablePosition(b.drawableId, [
            b.onSpriteRight ? Math.max(sb.left, Math.min(sb.right - w2, tb.right))
                            : Math.min(sb.right - w2, Math.max(sb.left, tb.left - w2)),
            Math.min(sb.top, tb.bottom + h2)]);
    }

    // ---- ask prompt ----------------------------------------------------
    showAsk(question) {
        if (!this.askBox) {
            const box = document.createElement('form');
            box.className = 'ask';
            box.innerHTML = '<label></label><input type="text" autocomplete="off"><button type="submit">&#10003;</button>';
            box.onsubmit = e => {
                e.preventDefault();
                const input = box.querySelector('input');
                const answer = input.value;
                input.value = '';
                this.lith.postAnswer(answer);
            };
            this.canvas.parentElement.appendChild(box);
            this.askBox = box;
        }
        this.askBox.style.display = 'flex';
        this.askBox.querySelector('label').textContent = question;
        this.askBox.querySelector('input').focus({ preventScroll: true });
    }
    hideAsk() { if (this.askBox) this.askBox.style.display = 'none'; }

    // ---- sound ---------------------------------------------------------
    soundList(uid) {
        let l = this.targetSounds.get(uid);
        if (!l) this.targetSounds.set(uid, l = []);
        return l;
    }

    async playSound(uid, index, req, owner) {
        const buffer = owner < 0 ? null : await (this.buffers[owner] || [])[index];
        if (!buffer) { if (req) this.lith.soundFinished(req); return; }
        this.ensureAudio();
        const src = this.audio.createBufferSource();
        src.buffer = buffer;
        const gain = this.audio.createGain();
        const panner = this.audio.createStereoPanner ? this.audio.createStereoPanner() : null;
        src.connect(gain);
        if (panner) { gain.connect(panner); panner.connect(this.audio.destination); } else gain.connect(this.audio.destination);
        const entry = { src, gain, panner, req, index };
        const list = this.soundList(uid);
        // A sound that is started again restarts: stop the old copy first.
        for (const e of list.filter(e => e.index === index)) this.stopEntry(uid, e, true);
        list.push(entry);
        this.applyEffects(uid, entry);
        src.onended = () => {
            const i = list.indexOf(entry);
            if (i >= 0) list.splice(i, 1);
            if (entry.req) this.lith.soundFinished(entry.req);
        };
        src.start();
    }

    stopEntry(uid, e, silent) {
        const list = this.soundList(uid);
        const i = list.indexOf(e);
        if (i >= 0) list.splice(i, 1);
        e.src.onended = null;
        try { e.src.stop(); } catch (err) { /* already stopped */ }
        if (e.req && !silent) this.lith.soundFinished(e.req);
        else if (e.req && silent) this.lith.soundFinished(e.req);
    }

    stopSound(uid, index) { for (const e of this.soundList(uid).filter(e => e.index === index)) this.stopEntry(uid, e); }

    stopAllSounds(uid) {
        const uids = uid < 0 ? [...this.targetSounds.keys()] : [uid];
        for (const u of uids) for (const e of [...this.soundList(u)]) this.stopEntry(u, e);
    }

    setSoundEffects(uid, volume, pitch, pan) {
        this.effects = this.effects || new Map();
        this.effects.set(uid, { volume, pitch, pan });
        for (const e of this.soundList(uid)) this.applyEffects(uid, e);
    }

    applyEffects(uid, e) {
        const fx = (this.effects && this.effects.get(uid)) || { volume: 100, pitch: 0, pan: 0 };
        e.gain.gain.value = fx.volume / 100;
        if (e.panner) e.panner.pan.value = Math.max(-1, Math.min(1, fx.pan / 100));
        e.src.playbackRate.value = Math.pow(2, fx.pitch / 120);
    }

    // ---- the runtime's request entry point ---------------------------------
    handle(op, a, b, c, d, p) {
        const m = this.lith.mem;
        const r = this.r;
        switch (op) {
            case 1: { const g = GROUPS[a]; const id = r.createDrawable(g); this.groupOf.set(id, g); return id; }
            case 2: r.destroyDrawable(a, this.groupOf.get(a) || 'sprite'); this.groupOf.delete(a); return 0;
            case 3: r.updateDrawableSkinId(a, b); return 0;
            case 4: r.updateDrawablePosition(a, [b, c]); return 0;
            case 5: r.updateDrawableDirectionScale(a, b, [c, d]); return 0;
            case 6: r.updateDrawableVisible(a, b !== 0); return 0;
            case 7: r.updateDrawableEffect(a, m.string(p), b); return 0;
            case 8: {
                const group = GROUPS[c & 15];
                const res = r.setDrawableOrder(a, b, group, (c & 16) !== 0, d === 0 ? undefined : d);
                return typeof res === 'number' ? res : 0;
            }
            case 9: { const f = r.getFencedPositionOfDrawable(a, [b, c]); m.setF64(p, f); return 0; }
            case 10: { m.setF64(p, r.getCurrentSkinSize(a)); return 0; }
            case 11: {
                const bd = r.getBounds(a);
                if (!bd) return 0;
                m.setF64(p, [bd.left, bd.right, bd.top, bd.bottom]);
                return 1;
            }
            case 12: return r.drawableTouching(a, b, c) ? 1 : 0;
            case 13: return r.isTouchingDrawables(a, Array.from(m.i32(p, b))) ? 1 : 0;
            case 14: return r.isTouchingColor(a, Array.from(m.i32(p, 3))) ? 1 : 0;
            case 15: { const v = Array.from(m.i32(p, 6)); return r.isTouchingColor(a, v.slice(0, 3), v.slice(3)) ? 1 : 0; }
            case 16: { const id = r.pick(a, b); return typeof id === 'number' ? id : -1; }
            case 17: r.penClear(this.penLayer()); return 0;
            case 18: r.penStamp(this.penLayer(), a); return 0;
            case 19: { const v = m.f64(p, 7); r.penPoint(this.penLayer(), { color4f: Array.from(v.slice(0, 4)), diameter: v[4] }, v[5], v[6]); return 0; }
            case 20: { const v = m.f64(p, 9); r.penLine(this.penLayer(), { color4f: Array.from(v.slice(0, 4)), diameter: v[4] }, v[5], v[6], v[7], v[8]); return 0; }
            case 21: this.setBubble(a, b, c === 1, p ? m.string(p) : ''); return 0;
            case 22: if (a) this.showAsk(m.string(p)); else this.hideAsk(); return 0;
            case 23: this.playSound(a, b, c, d); return 0;
            case 24: this.stopSound(a, b); return 0;
            case 25: this.stopAllSounds(a); return 0;
            case 26: this.setSoundEffects(a, b, c, d); return 0;
            case 27: this.removeBubble(a); this.targetSounds.delete(a); return 0;
            default: return 0;
        }
    }
}
