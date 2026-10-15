// Lithium desktop shell: serves the bundled editor over a privileged lithium:// scheme
// (needed for WebAssembly streaming, service workers and fetch) and opens it in a window.
const { app, BrowserWindow, protocol, net, shell, Menu, dialog } = require('electron');
const path = require('path');
const fs = require('fs');
const { pathToFileURL } = require('url');

const guiRoot = app.isPackaged
    ? path.join(process.resourcesPath, 'gui')
    : path.join(__dirname, 'gui');

protocol.registerSchemesAsPrivileged([{
    scheme: 'lithium',
    privileges: { standard: true, secure: true, supportFetchAPI: true, stream: true, corsEnabled: true }
}]);

if (!app.requestSingleInstanceLock()) { app.quit(); }

let win = null;
function createWindow() {
    win = new BrowserWindow({
        width: 1360, height: 860, minWidth: 900, minHeight: 600,
        backgroundColor: '#314b72',
        title: 'Lithium',
        icon: path.join(__dirname, 'icon.ico'),
        autoHideMenuBar: true,
        webPreferences: { contextIsolation: true, sandbox: true, spellcheck: false }
    });
    Menu.setApplicationMenu(null);
    win.loadURL('lithium://app/editor.html');
    win.webContents.setWindowOpenHandler(({ url }) => {
        if (/^https?:/.test(url)) shell.openExternal(url);
        return { action: 'deny' };
    });
    win.webContents.on('will-navigate', (e, url) => {
        if (!url.startsWith('lithium://')) { e.preventDefault(); if (/^https?:/.test(url)) shell.openExternal(url); }
    });
    // The editor asks "leave site?" through beforeunload when the project has unsaved changes.
    // Electron cancels the close silently in that case, so the window seemed impossible to close.
    win.webContents.on('will-prevent-unload', e => {
        const choice = dialog.showMessageBoxSync(win, {
            type: 'question', buttons: ['Close without saving', 'Cancel'], defaultId: 1, cancelId: 1,
            title: 'Lithium', message: 'This project has changes that are not saved.',
            detail: 'Use File > Save to your computer first if you want to keep them.'
        });
        if (choice === 0) e.preventDefault(); // preventDefault here means "ignore the page, close anyway"
    });
    win.on('closed', () => { win = null; });
}

app.whenReady().then(() => {
    protocol.handle('lithium', req => {
        const u = new URL(req.url);
        let rel = decodeURIComponent(u.pathname);
        if (rel === '/' || rel === '') rel = '/editor.html';
        const file = path.normalize(path.join(guiRoot, rel));
        if (!file.startsWith(guiRoot) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
            return new Response('Not found', { status: 404 });
        }
        return net.fetch(pathToFileURL(file).toString()).then(r => {
            // file:// responses have no MIME for .wasm
            if (file.endsWith('.wasm')) return new Response(r.body, { headers: { 'content-type': 'application/wasm' } });
            return r;
        });
    });
    createWindow();
    app.on('activate', () => { if (!win) createWindow(); });
});
app.on('second-instance', () => { if (win) { if (win.isMinimized()) win.restore(); win.focus(); } });
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
