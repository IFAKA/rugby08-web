// own_copy.js (emcc --pre-js, before gamefs.js) -- each player's own Rugby 08 on the party site. The site
// serves the program and a manifest of the game files (paths, sizes and the sha256 of each 8 MiB chunk: no
// content, tools/web/site_manifest.py). The first visit asks for the player's Rugby 08 folder once, checks
// every chunk against the manifest and keeps the chunks in the browser's private storage (OPFS,
// gamefs/<chunk name>); gamefs.js's worker reads them from there. Later visits start straight away.
// A local build (manifest without "own") serves the chunks itself and never asks.
Module.ownCopy = (function () {
  const DIR = 'gamefs';
  const READY = 'ready';

  async function dir() {
    return (await navigator.storage.getDirectory()).getDirectoryHandle(DIR, { create: true });
  }

  async function hex(buf) {
    const d = new Uint8Array(await crypto.subtle.digest('SHA-256', buf));
    return Array.from(d, (b) => b.toString(16).padStart(2, '0')).join('');
  }

  async function ready(d, id) {
    try {
      return (await (await (await d.getFileHandle(READY)).getFile()).text()) === id;
    } catch {
      return false;
    }
  }

  // A screen over the page in the game's colours: a message, a button, a progress line.
  function screen() {
    const box = document.createElement('div');
    box.id = 'owncopy';
    box.style.cssText =
      'position:fixed;inset:0;z-index:30;display:flex;flex-direction:column;align-items:center;' +
      'justify-content:center;gap:18px;background:#000;color:#eef5f0;text-align:center;padding:24px;' +
      'font:17px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif';
    const msg = document.createElement('div');
    msg.style.maxWidth = '34em';
    const btn = document.createElement('button');
    btn.style.cssText =
      'font:bold 18px system-ui,sans-serif;padding:12px 28px;border:0;border-radius:6px;' +
      'background:#3fbf6f;color:#04150b;cursor:pointer';
    const input = document.createElement('input');
    input.type = 'file';
    input.id = 'owncopy-input';
    input.webkitdirectory = true;
    input.hidden = true;
    const bar = document.createElement('div');
    bar.style.color = '#9db3a5';
    btn.onclick = () => input.click();
    box.append(msg, btn, input, bar);
    document.body.appendChild(box);
    return { box, msg, btn, input, bar };
  }

  // The picked folder's files by their path inside the game folder. The player may pick the game folder or
  // a folder above it: a manifest path is found as the end of a picked path.
  function finder(files) {
    const all = Array.from(files, (f) => [f.webkitRelativePath.replace(/\\/g, '/'), f]);
    return function (path) {
      const lower = path.toLowerCase();
      const hit = all.find(([p]) => p.toLowerCase().endsWith('/' + lower));
      return hit ? hit[1] : null;
    };
  }

  // Check every chunk of every file the game needs and store it; throws a message for the player.
  async function store(files, man, d, progress) {
    const find = finder(files);
    const plan = man.files.map((f) => [f, find(f.path)]);
    const missing = plan.filter(([, file]) => !file).map(([f]) => f.path);
    if (missing.length)
      throw (
        'This folder is not Rugby 08: ' +
        missing.length +
        ' game files are missing (for example ' +
        missing[0] +
        '). Pick the folder you installed Rugby 08 in (the one with the Audio and Data folders).'
      );
    const total = man.files.reduce((n, f) => n + f.size, 0);
    let done = 0;
    for (const [f, file] of plan) {
      if (file.size !== f.size) throw versionMessage(f.path);
      let pos = 0;
      for (const c of f.chunks) {
        const buf = await file.slice(pos, pos + c.size).arrayBuffer();
        if ((await hex(buf)) !== c.sha256) throw versionMessage(f.path);
        const w = await (await d.getFileHandle(c.name, { create: true })).createWritable();
        await w.write(buf);
        await w.close();
        pos += c.size;
        done += c.size;
        progress(done / total);
      }
    }
    const w = await (await d.getFileHandle(READY, { create: true })).createWritable();
    await w.write(man.id);
    await w.close();
  }

  function versionMessage(path) {
    return (
      'Your Rugby 08 is a different version from the one this site plays (' +
      path +
      ' differs). Everyone in a party needs the same version of the game.'
    );
  }

  // Resolves once this browser holds the manifest's game files (asking for the folder if it does not).
  async function prepare(man) {
    const d = await dir();
    if (await ready(d, man.id)) return;
    const ui = screen();
    ui.msg.textContent =
      'To play, this page needs your own copy of Rugby 08. Pick the folder you installed it in, once: ' +
      'the game files are checked and kept in this browser, nothing is uploaded.';
    ui.btn.textContent = 'Choose my Rugby 08 folder';
    await new Promise(function (resolve) {
      ui.input.onchange = async function () {
        if (!ui.input.files.length) return;
        ui.btn.disabled = true;
        ui.btn.hidden = true;
        ui.msg.textContent = 'Checking your game files…';
        try {
          await store(ui.input.files, man, d, (p) => (ui.bar.textContent = Math.floor(p * 100) + '%'));
          if (navigator.storage.persist) navigator.storage.persist();
          console.log('owncopy: stored ' + man.files.length + ' files');
          ui.box.remove();
          resolve();
        } catch (e) {
          console.log('owncopy: refused: ' + e);
          ui.msg.textContent = typeof e === 'string' ? e : 'Your game files could not be read (' + e + ').';
          ui.bar.textContent = '';
          ui.btn.textContent = 'Choose another folder';
          ui.btn.hidden = false;
          ui.btn.disabled = false;
          ui.input.value = '';
        }
      };
    });
  }

  return { prepare, DIR };
})();
