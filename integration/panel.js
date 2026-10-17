/* Optional, minimal in-editor Lithium diagnostics.
 * TurboWarp remains the source of all visible Scratch project behavior.
 * All project-derived content is inserted with textContent, never as HTML.
 */
(() => {
    'use strict';
    if (typeof document === 'undefined') return;

    const css = document.createElement('style');
    css.textContent = `
#lithium-diagnostics { position:fixed; bottom:14px; left:14px; z-index:2147483600;
  font:12px/1.45 system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
  color:#f2f5ff; }
#lithium-diagnostics button { font:inherit; cursor:pointer; }
#lithium-diagnostics .li-chip { background:#171c2a; color:#fff;
  border:1px solid #586279; border-radius:999px; padding:7px 12px;
  box-shadow:0 4px 18px rgba(0,0,0,.25); }
#lithium-diagnostics .li-chip:focus-visible,#lithium-diagnostics button:focus-visible {
  outline:2px solid #86c5ff; outline-offset:2px; }
#lithium-diagnostics .li-panel { margin-bottom:8px; width:min(320px,calc(100vw - 28px));
  max-height:min(65vh,520px); overflow:auto; padding:13px; box-sizing:border-box;
  background:#161b28; border:1px solid #586279; border-radius:12px;
  box-shadow:0 8px 30px rgba(0,0,0,.35); }
#lithium-diagnostics .li-panel[hidden] {display:none}
#lithium-diagnostics .li-title {display:flex;justify-content:space-between;align-items:center;gap:8px;
  font-weight:700;font-size:14px;margin-bottom:8px;}
#lithium-diagnostics .li-note {color:#b8c1d5;margin:6px 0;white-space:pre-wrap;overflow-wrap:anywhere}
#lithium-diagnostics .li-result {font-weight:700;margin-top:8px;overflow-wrap:anywhere}
#lithium-diagnostics .li-actions {display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
#lithium-diagnostics .li-actions button {border:1px solid #73829f;border-radius:6px;
  padding:5px 9px;background:#26354f;color:#f7f9ff}
#lithium-diagnostics .li-diffs {padding-left:17px;margin:7px 0;overflow-wrap:anywhere}
#lithium-diagnostics[data-status="match"] .li-chip {border-color:#54c38c}
#lithium-diagnostics[data-status="mismatch"] .li-chip,
#lithium-diagnostics[data-status="error"] .li-chip {border-color:#ff9a90}
#lithium-diagnostics[data-status="unsupported"] .li-chip {border-color:#e3b968}
`;
    document.head.appendChild(css);

    const root = document.createElement('aside');
    root.id = 'lithium-diagnostics';
    root.setAttribute('aria-label', 'Lithium shadow runtime diagnostics');

    const panel = document.createElement('section');
    panel.className = 'li-panel';
    panel.hidden = true;
    const title = document.createElement('div');
    title.className = 'li-title';
    title.textContent = 'Lithium diagnostics';
    const label = document.createElement('div');
    label.className = 'li-result';
    const note = document.createElement('p');
    note.className = 'li-note';
    const details = document.createElement('p');
    details.className = 'li-note';
    const differences = document.createElement('ul');
    differences.className = 'li-diffs';
    const actions = document.createElement('div');
    actions.className = 'li-actions';
    const recheck = document.createElement('button');
    recheck.type = 'button';
    recheck.textContent = 'Compare now';
    const copy = document.createElement('button');
    copy.type = 'button';
    copy.textContent = 'Copy report';
    actions.append(recheck, copy);
    panel.append(title, label, note, details, differences, actions);

    const chip = document.createElement('button');
    chip.type = 'button';
    chip.className = 'li-chip';
    chip.setAttribute('aria-expanded', 'false');
    chip.setAttribute('aria-controls', 'lithium-diagnostics-panel');
    panel.id = 'lithium-diagnostics-panel';
    chip.textContent = 'Lithium shadow: loading';
    root.append(panel, chip);
    document.body.appendChild(root);

    const labelFor = {
        'ready': 'Ready · not tested',
        'loading': 'Loading',
        'loading-project': 'Loading project',
        'shadow-running': 'Running',
        'shadow-finished': 'Finished',
        'not-run': 'Not tested',
        'match': 'Match',
        'mismatch': 'Mismatch',
        'incomplete': 'Incomplete',
        'unsupported': 'Unsupported features',
        'unsupported-project': 'Unsupported project',
        'aborted': 'Stopped',
        'error': 'Error'
    };
    let manualResult = null;
    let manualRunId = -1;

    function current() {
        const shadow = window.LithiumShadow;
        if (!shadow) return {status: 'loading', reason: 'Waiting for WebAssembly'};
        const report = manualResult && manualRunId === shadow.runId
            ? manualResult : shadow.lastComparison;
        return {shadow, report, status: report ? report.status : shadow.mode,
            reason: report && report.reason};
    }
    function render() {
        const {shadow, report, status, reason} = current();
        root.dataset.status = status;
        const display = labelFor[status] || status;
        chip.textContent = 'Lithium shadow: ' + display;
        label.textContent = 'Result: ' + display;
        note.textContent = 'Diagnostic mode only. TurboWarp still executes the actual project.';
        const frames = shadow ? shadow.frames : 0;
        const runId = shadow ? shadow.runId : 0;
        details.textContent = 'Run #' + runId + ' · shadow steps: ' + frames +
            (reason ? '\n' + reason : '') +
            (shadow && shadow.loadLog ? '\nLoader: ' + shadow.loadLog.slice(0, 350) : '') +
            (shadow && shadow.lastError ? '\nError: ' + shadow.lastError.slice(0, 350) : '');
        differences.replaceChildren();
        if (report && report.differences) {
            for (const diff of report.differences.slice(0, 8)) {
                const li = document.createElement('li');
                li.textContent = diff.path + ': Wasm ' + JSON.stringify(diff.lithium) +
                    ' / TurboWarp ' + JSON.stringify(diff.turboWarp);
                differences.append(li);
            }
            if (report.differences.length > 8) {
                const li = document.createElement('li');
                li.textContent = (report.differences.length - 8) + ' more differences';
                differences.append(li);
            }
        }
    }
    chip.addEventListener('click', () => {
        panel.hidden = !panel.hidden;
        chip.setAttribute('aria-expanded', String(!panel.hidden));
        render();
    });
    recheck.addEventListener('click', () => {
        const shadow = window.LithiumShadow;
        if (shadow && typeof shadow.compare === 'function') {
            manualResult = shadow.compare();
            manualRunId = shadow.runId;
        }
        render();
    });
    copy.addEventListener('click', async () => {
        const shadow = window.LithiumShadow;
        if (!shadow) return;
        const report = manualResult && manualRunId === shadow.runId ? manualResult : shadow.lastComparison;
        const text = JSON.stringify({
            version: shadow.version, mode: shadow.mode, runId: shadow.runId,
            frames: shadow.frames, loadLog: shadow.loadLog,
            lastError: shadow.lastError, comparison: report
        }, null, 2);
        try {
            await navigator.clipboard.writeText(text);
            copy.textContent = 'Copied';
        } catch (error) {
            copy.textContent = 'Clipboard unavailable';
        }
    });
    setInterval(render, 500);
    render();
})();
