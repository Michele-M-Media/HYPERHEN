(function () {
  'use strict';

  var t = CRI.t;
  var isPS5 = /PlayStation/i.test(navigator.userAgent);
  if (isPS5) document.documentElement.classList.add('ps5');
  var rootCls = document.documentElement.classList;

  /* ---------------------------------------------------------------- helpers */
  var $ = function (s, r) { return (r || document).querySelector(s); };
  var view = $('#view');
  var sleep = function (ms) { return new Promise(function (r) { setTimeout(r, ms); }); };
  function esc(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }
  function ic(name, cls) {
    return '<svg class="ic' + (cls ? ' ' + cls : '') + '"><use href="#i-' + name + '" xlink:href="#i-' + name + '"/></svg>';
  }
  function sect(icon, title, desc, side, tone) {
    return '<div class="sec-head"><div class="sec-txt"><span class="sec-ic' + (tone ? ' ' + tone : '') + '">' + ic(icon) + '</span><div><h2>' + title + '</h2>'
      + (desc ? '<p>' + desc + '</p>' : '') + '</div></div>' + (side ? '<div class="sec-side">' + side + '</div>' : '') + '</div>';
  }
  function stage(eyebrow, title, sub, acts) {
    return '<div class="stage"><div class="stage-txt"><div class="eyebrow">' + eyebrow + '</div><h1>' + title + '</h1>' + (sub ? '<p class="stage-sub">' + sub + '</p>' : '') + '</div>'
      + (acts ? '<div class="stage-act">' + acts + '</div>' : '') + '</div>';
  }
  function stateBox(kind, icon, title, text, acts, tech) {
    return '<div class="state ' + kind + '"><span class="state-ic">' + ic(icon) + '</span><h3>' + title + '</h3>' + (text ? '<p>' + text + '</p>' : '')
      + (acts ? '<div class="row">' + acts + '</div>' : '')
      + (tech ? '<details class="tech"><summary>' + t('Technical details') + '</summary><pre>' + esc(tech) + '</pre></details>' : '') + '</div>';
  }
  var errText = function (e) { return e ? [e.status ? 'HTTP ' + e.status : '', e.code || '', e.message || ''].filter(Boolean).join(' - ') : ''; };
  function store(k, v) {
    try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) {}
    return null;
  }
  var cleanId = function (v) { return String(v || '').trim().toUpperCase(); };
  var isTitleId = function (id) { return /^(PPSA|CUSA|ULUS|ULES|ULJS|ULKS|SLUS|SCUS|SLES|SCES|SLPS|SLPM|SCED|SLED|SCPS)[0-9A-Z]{5}$/.test(String(id || '').toUpperCase()); };

  /* ------------------------------------------------------------------- api */
  var sema = { n: 2, q: [] };
  function acquire() { return new Promise(function (res) { if (sema.n > 0) { sema.n--; res(); } else sema.q.push(res); }); }
  function release() { if (sema.q.length) sema.q.shift()(); else sema.n++; }

  async function call(url, opts, method, body) {
    opts = typeof opts === 'number' ? { timeoutMs: opts } : (opts || {});
    var ms = opts.timeoutMs || (method === 'POST' ? 15000 : 10000);
    var retries = typeof opts.retries === 'number' ? opts.retries : 2;
    await acquire();
    try {
      for (var attempt = 0; ; attempt++) {
        if (attempt > 0) await sleep(attempt * 500);
        var ctrl = new AbortController();
        var to = setTimeout(function () { ctrl.abort(); }, ms);
        try {
          var init = { method: method, cache: 'no-store', signal: ctrl.signal };
          if (body !== undefined) {
            init.headers = { 'Content-Type': 'application/json' };
            init.body = JSON.stringify(body);
          }
          var res = await fetch(url, init);
          var txt = await res.text();
          var js = null;
          try { js = JSON.parse(txt); } catch (e) {}
          if (!res.ok) {
            var err = new Error(js && (js.message || js.error) ? (js.message || js.error) : 'HTTP ' + res.status);
            err.status = res.status; err.body = js; err.code = js && js.error ? String(js.error) : '';
            if (res.status === 503 && err.code === 'too_many_requests' && attempt < retries) continue;
            throw err;
          }
          return js || { ok: true };
        } finally { clearTimeout(to); }
      }
    } finally { release(); }
  }
  var get = function (u, o) { return call(u, o, 'GET'); };
  var post = function (u, b, o) { return call(u, o, 'POST', b || {}); };

  /* ----------------------------------------------------------- toast/modal */
  function toast(msg, level) {
    var box = $('#toasts'), lv = level || 'info';
    msg = String(msg || '');
    var last = box.lastChild;
    if (last && last.getAttribute('data-m') === msg) return;
    var el = document.createElement('div');
    el.className = 'toast ' + lv;
    el.setAttribute('data-m', msg);
    el.innerHTML = ic(lv === 'success' ? 'check' : lv === 'info' ? 'bolt' : 'warn') + '<span></span>';
    el.lastChild.textContent = msg;
    box.appendChild(el);
    void el.offsetWidth;
    el.classList.add('in');
    while (box.children.length > 3) box.removeChild(box.firstChild);
    setTimeout(function () {
      el.classList.remove('in');
      setTimeout(function () { if (el.parentNode) el.parentNode.removeChild(el); }, 240);
    }, lv === 'error' ? 5200 : 3200);
  }
  var modalDone = null, modalFrom = null;
  function openModal(html, onClose, cls) {
    var card = $('#modalCard'), m = $('#modal');
    modalFrom = document.activeElement;
    card.className = 'modal-card' + (cls ? ' ' + cls : '');
    card.removeAttribute('aria-describedby');
    card.innerHTML = html;
    var h = card.querySelector('h2');
    if (h) { h.id = 'mTitle'; card.setAttribute('aria-labelledby', 'mTitle'); }
    else { card.removeAttribute('aria-labelledby'); card.setAttribute('aria-label', cls === 'palette' ? 'Search' : 'Dialog'); }
    m.classList.toggle('top', cls === 'palette');
    modalDone = onClose || null;
    m.hidden = false;
    void m.offsetWidth;
    m.classList.add('show');
    var f = card.querySelector(card.querySelector('.btn.danger') ? '.btn.ghost' : '.btn:not(.ghost), button, input');
    if (f) f.focus();
  }
  function closeModal(result) {
    var m = $('#modal');
    m.classList.remove('show');
    setTimeout(function () { if (!m.classList.contains('show')) m.hidden = true; }, 180);
    var cb = modalDone; modalDone = null;
    if (modalFrom && modalFrom.focus) { try { modalFrom.focus(); } catch (e) {} }
    modalFrom = null;
    if (cb) cb(result);
  }
  function confirmBox(title, text, okLabel, danger) {
    return new Promise(function (resolve) {
      openModal('<span class="dlg-ic' + (danger ? ' danger' : '') + '">' + ic(danger ? 'warn' : 'info') + '</span><h2>' + esc(title) + '</h2><p id="mDesc">' + esc(text) + '</p><div class="modal-actions">'
        + '<button class="btn ghost" data-act="modalNo">' + t('Cancel') + '</button>'
        + '<button class="btn' + (danger ? ' danger' : '') + '" data-act="modalYes">' + esc(okLabel || t('Continue')) + '</button></div>',
        function (r) { resolve(!!r); });
      var c = $('#modalCard'); if ($('#mDesc')) c.setAttribute('aria-describedby', 'mDesc');
    });
  }
  function flash(el, label) {
    if (!el || el.__flash) return;
    el.__flash = el.innerHTML;
    el.classList.add('done');
    el.innerHTML = ic('check', 'sm') + esc(label);
    setTimeout(function () { el.innerHTML = el.__flash; el.classList.remove('done'); el.__flash = null; }, 1500);
  }
  async function copyText(text, okMsg, btn) {
    text = String(text || '');
    if (!text.trim()) { toast('Nothing to copy.', 'warn'); return; }
    var done = function () { if (btn) flash(btn, 'Copied'); else toast(okMsg || 'Copied.', 'success'); };
    try {
      if (navigator.clipboard && navigator.clipboard.writeText) { await navigator.clipboard.writeText(text); done(); return; }
    } catch (e) {}
    try {
      var ta = document.createElement('textarea');
      ta.value = text; ta.style.cssText = 'position:fixed;left:-9999px;top:0';
      document.body.appendChild(ta); ta.select();
      var ok = document.execCommand('copy');
      document.body.removeChild(ta);
      if (ok) { done(); return; }
    } catch (e) {}
    toast('Copy failed. Select the text manually.', 'error');
  }
  // Lowercases and strips accents so "pokemon" finds "Pokémon".
  function fold(s) {
    var n = String(s == null ? '' : s);
    try { n = n.normalize('NFD').replace(/[̀-ͯ]/g, ''); } catch (e) {}
    return n.toLowerCase();
  }
  function skTiles(n) {
    var h = '';
    for (var i = 0; i < n; i++) h += '<div class="sk-tile"><div class="sk"></div><div class="sk sk-l"></div><div class="sk sk-l"></div></div>';
    return h;
  }
  function skRows(n) {
    var h = '';
    for (var i = 0; i < n; i++) h += '<div class="sk sk-row"></div>';
    return h;
  }

  /* ----------------------------------------------------------------- state */
  var S = {
    games: [], gamesLoaded: false, activity: {}, fav: new Set(), recent: [],
    filter: 'ALL', sort: 'name_asc', q: '',
    running: '', launching: '', launchBusy: false,
    state: null, cfg: {}, names: {}, lookupQ: [], lookupDone: {}, lookupBusy: false,
    dl: null, g: null
  };
  function newGame(id) {
    return { id: id, tab: 'cheats', doc: null, noFile: false, err: null, stateErr: null, rows: {}, dbg: null, filter: '', applying: {},
      auto: [], patch: null, patchAuto: [], loaded: false, patchLoaded: false };
  }

  var gid = function (g) { return g.titleId || g.title_id || ''; };
  function gname(g) { var id = gid(g); return S.names[id] || g.titleName || g.title_name || id || 'Unknown'; }
  function gplat(g) { var id = gid(g); return g.platform || (id.indexOf('PPSA') === 0 ? 'PS5' : id.indexOf('CUSA') === 0 ? 'PS4' : 'APP'); }
  function gicon(g) { return (g.iconUrl || '/appdb/icon?id=' + encodeURIComponent(gid(g))) + '&t=1'; }
  function findGame(id) { for (var i = 0; i < S.games.length; i++) if (gid(S.games[i]) === id) return S.games[i]; return null; }
  function isApp(g) { return g.kind === 'app' || g.isApp === true || gplat(g) === 'APP'; }
  function unresolvedName(id, n) {
    n = String(n || '').trim();
    return !n || n.toUpperCase() === id || /^(CUSA|PPSA|NPXS|ULUS|ULES|SLUS|SCUS|SLES|SCES)[-_]?[0-9A-Z]{5}$/i.test(n) || /^unknown/i.test(n);
  }
  function fmtPlay(s) {
    if (!s || s < 60) return '\u2014';
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
    return h ? t('{h}h {m}m', { h: h, m: m }) : t('{m}m', { m: m });
  }
  function fmtVer(v) {
    v = String(v || '').replace(/^v/i, '').trim();
    if (!v || v === 'unknown') return '\u2014';
    return v.replace(/^0+(?=\d)/, '');
  }
  function platBadge(g) {
    var p = gplat(g);
    return (p === 'PS5' || p === 'PS4') ? '<span class="plat-badge ' + p + '">' + p + '</span>' : '';
  }

  /* ------------------------------------------------------ favorites/recents */
  async function loadFavs() {
    try {
      var d = await get('/api/favorites', { retries: 0 });
      S.fav = new Set(Array.isArray(d.favorites) ? d.favorites : []);
      S.recent = Array.isArray(d.recents) ? d.recents : [];
    } catch (e) {}
  }
  function toggleFav(id) {
    var on = !S.fav.has(id);
    if (on) S.fav.add(id); else S.fav.delete(id);
    post('/api/favorites/set?id=' + encodeURIComponent(id) + '&fav=' + (on ? 1 : 0)).catch(function () {});
  }
  function pushRecent(id) {
    S.recent = [id].concat(S.recent.filter(function (x) { return x !== id; })).slice(0, 12);
    post('/api/favorites/recent?id=' + encodeURIComponent(id)).catch(function () {});
  }

  /* ----------------------------------------------------------------- games */
  async function loadGames(force) {
    if (S.loadingGames) return S.loadingGames;
    S.loadingGames = (async function () {
      try {
        var d = null;
        for (var i = 0; i < 4; i++) {
          try { d = await get(force ? '/api/games?refresh=1' : '/api/games', { timeoutMs: 15000 }); break; }
          catch (e) { if (i >= 3) throw e; await sleep((i + 1) * 1000); }
        }
        S.games = (d.games || d.titles || []).filter(function (g) { return isTitleId(gid(g)); }).map(function (g) {
          g.titleId = gid(g); g.running = !!g.running; return g;
        });
        S.gamesLoaded = true; S.gamesError = false;
        S.games.forEach(queueLookup);
        get('/api/activity', { retries: 0 }).then(function (a) {
          S.activity = {};
          (a.titles || []).forEach(function (t) { if (t.titleId) S.activity[t.titleId] = t; });
          if (route().page === 'games') paintGrid();
        }).catch(function () {});
      } catch (e) {
        S.gamesError = true;
        toast('Could not load the game list.', 'error');
      } finally { S.loadingGames = null; }
      var pg = route().page;
      if (pg === 'games') paintGrid(); else if (pg === 'game') paintHero();
    })();
    return S.loadingGames;
  }
  function queueLookup(g) {
    var id = gid(g);
    if (S.lookupDone[id] || !unresolvedName(id, gname(g))) return;
    S.lookupDone[id] = 1; S.lookupQ.push(id); pumpLookup();
  }
  async function pumpLookup() {
    if (S.lookupBusy) return;
    S.lookupBusy = true;
    while (S.lookupQ.length) {
      var id = S.lookupQ.shift();
      try {
        var r = await get('/api/titles/lookup?titleId=' + encodeURIComponent(id), { timeoutMs: 12000, retries: 0 });
        if (r && r.ok && r.name && !unresolvedName(id, r.name)) {
          S.names[id] = String(r.name);
          paintStatusChip();
          var p = route().page;
          if (p === 'games') paintGrid(); else if (p === 'game') paintHero();
        }
      } catch (e) {}
    }
    S.lookupBusy = false;
  }

  function inFilter(g, f) {
    var id = gid(g), app = isApp(g);
    if (f === 'FAV') return S.fav.has(id);
    if (f === 'RECENT') return S.recent.indexOf(id) >= 0;
    if (f === 'CHEATS') return !!g.hasCheat;
    if (f === 'APP') return app;
    if (f === 'ALL') return !app;
    return !app && gplat(g) === f;
  }
  // The folded name is cached on the game, since sorting and searching call it many times.
  function nkey(g) {
    var n = gname(g);
    if (g.__n !== n) { g.__n = n; g.__f = fold(n); }
    return g.__f;
  }
  function filtered() {
    var q = fold(S.q.trim());
    var list = S.games.filter(function (g) {
      if (!inFilter(g, S.filter)) return false;
      return !q || nkey(g).indexOf(q) >= 0 || gid(g).toLowerCase().indexOf(q) >= 0;
    });
    list.sort(function (a, b) {
      if (S.filter === 'RECENT') return S.recent.indexOf(gid(a)) - S.recent.indexOf(gid(b));
      var ar = gid(a) === S.running ? 0 : 1, br = gid(b) === S.running ? 0 : 1;
      if (ar !== br) return ar - br;
      var af = S.fav.has(gid(a)) ? 0 : 1, bf = S.fav.has(gid(b)) ? 0 : 1;
      if (af !== bf) return af - bf;
      if (S.sort === 'id_asc') return gid(a).localeCompare(gid(b));
      if (S.sort === 'cheats_first') { var ac = a.hasCheat ? 0 : 1, bc = b.hasCheat ? 0 : 1; if (ac !== bc) return ac - bc; }
      var an = nkey(a), bn = nkey(b);
      return S.sort === 'name_desc' ? bn.localeCompare(an) : an.localeCompare(bn);
    });
    return list;
  }

  var IMG_ATTR = ' onload="this.classList.add(\'ld\')" onerror="crImgErr(this)"';
  /* Cover requests can be refused when the console has too many open connections. */
  window.crImgErr = function (img) {
    var n = +img.getAttribute('data-try') || 0;
    if (n >= 4) { img.style.display = 'none'; return; }
    img.setAttribute('data-try', n + 1);
    var src = img.getAttribute('data-src') || img.getAttribute('src');
    img.setAttribute('data-src', src);
    setTimeout(function () { img.src = src + (src.indexOf('?') < 0 ? '?' : '&') + 'r=' + (n + 1); }, 600 * (n + 1) + Math.random() * 400);
  };
  function tileHtml(g, i) {
    var id = gid(g), live = id === S.running, fav = S.fav.has(id), name = gname(g);
    var tags = '';
    if (g.hasCheat) tags += '<span class="pill on">' + ic('cheat', 'sm') + 'Cheats</span>';
    if (g.hasPatch) tags += '<span class="pill patch">' + ic('patch', 'sm') + 'Patches</span>';
    if (g.autoloadCheats || g.autoloadPatches) tags += '<span class="pill ok">' + ic('save', 'sm') + 'Autoload</span>';
    var secs = g.playTime || (S.activity[id] ? S.activity[id].totalSeconds : 0);
    var canTrain = !isApp(g) || g.hasCheat;
    var play = '<button class="btn' + (live && canTrain ? ' ghost' : '') + '" data-act="launch" data-id="' + esc(id) + '"' + (S.launchBusy ? ' disabled' : '') + '>'
      + ic('play', 'fill') + (S.launching === id ? 'Launching' : 'Play') + '</button>';
    var train = canTrain ? '<button class="btn' + (live ? '' : ' ghost') + '" data-act="open" data-id="' + esc(id) + '">' + ic('bolt') + 'Trainer</button>' : '';
    return '<article class="tile' + (live ? ' running' : '') + '" data-act="open" data-id="' + esc(id) + '" tabindex="0" style="--i:' + Math.min(i || 0, 14) + '" aria-label="' + esc(name) + '">'
      + '<div class="tile-cover"><span class="ph" aria-hidden="true">' + esc(name.charAt(0).toUpperCase()) + '</span>'
      + '<div class="tile-img" style="background-image:url(' + esc(gicon(g)) + ')"></div>'
      + '<button class="icon-btn tile-fav' + (fav ? ' fav-on' : '') + '" data-act="fav" data-id="' + esc(id) + '" aria-label="' + (fav ? 'Remove from favorites' : 'Add to favorites') + '" aria-pressed="' + fav + '">' + ic('star', fav ? 'fill' : '') + '</button>'
      + (live ? '<span class="pill tile-live"><i class="dot live"></i>Running</span>' : '') + '</div>'
      + '<div class="tile-body"><div class="tile-name">' + esc(name) + '</div>'
      + '<div class="tile-id mono">' + esc(id) + '</div>'
      + '<div class="tile-tags">' + platBadge(g) + tags + '</div>'
      + '<div class="tile-stats"><div class="tile-stat"><span>Version</span><b>' + esc(fmtVer(g.version)) + '</b></div>'
      + '<div class="tile-stat"><span>Time played</span><b>' + fmtPlay(secs) + '</b></div>'
      + '<div class="tile-stat"><span>Last played</span><b>' + (g.lastAccessTime ? ago(g.lastAccessTime) : '\u2014') + '</b></div></div>'
      + '<div class="tile-actions">' + play + train + '</div></div></article>';
  }

  var CHIPS = [['ALL', 'All'], ['FAV', 'Favorites'], ['RECENT', 'Recent'], ['CHEATS', 'With cheats'], ['PS5', 'PS5'], ['PS4', 'PS4'], ['APP', 'Apps']];
  function paintChips() {
    var el = $('#chips'); if (!el) return;
    el.innerHTML = CHIPS.map(function (c) {
      var n = S.gamesLoaded ? S.games.filter(function (g) { return inFilter(g, c[0]); }).length : '';
      return '<button class="chip' + (S.filter === c[0] ? ' active' : '') + '" data-act="chip" data-f="' + c[0] + '" aria-pressed="' + (S.filter === c[0]) + '">' + c[1] + (n !== '' ? '<em>' + n + '</em>' : '') + '</button>';
    }).join('');
  }
  function paintLive() {
    var el = $('#liveBar'); if (!el) return;
    var g = S.running ? findGame(S.running) : null;
    if (!g) { el.innerHTML = ''; return; }
    var id = gid(g), ver = fmtVer(g.version);
    el.innerHTML = '<div class="playing" data-act="open" data-id="' + esc(id) + '" role="button" tabindex="0" aria-label="' + esc(gname(g)) + '"><div class="live-bg" style="background-image:url(' + esc(gicon(g)) + ')"></div>'
      + '<div class="cover live-cover"><span class="ph" aria-hidden="true">' + esc(gname(g).charAt(0).toUpperCase()) + '</span><div class="tile-img" style="background-image:url(' + esc(gicon(g)) + ')"></div></div>'
      + '<div class="live-txt"><div class="live-kicker"><i class="dot live"></i>Now playing</div><b>' + esc(gname(g)) + '</b>'
      + '<div class="live-meta">' + (platBadge(g) ? '<span style="margin:2px 8px 0 0">' + platBadge(g) + '</span>' : '') + (ver !== '\u2014' ? '<span class="pill">' + esc(ver) + '</span>' : '')
      + (g.hasCheat ? '<span class="pill on">' + ic('cheat', 'sm') + 'Cheats</span>' : '<span class="pill warn">' + ic('warn', 'sm') + 'No cheat file yet</span>') + '</div></div>'
      + '<button class="btn lg" data-act="open" data-id="' + esc(id) + '">' + ic('bolt') + 'Open trainer</button></div>';
  }

  function paintGrid() {
    var grid = $('#grid'); if (!grid) return;
    paintChips(); paintLive();
    var clr = $('#gclear'); if (clr) clr.hidden = !S.q;
    var list = filtered();
    var cnt = $('#gamesCount'); if (cnt) cnt.textContent = S.gamesLoaded ? t(list.length === 1 ? '{n} title' : '{n} titles', { n: list.length }) : '';
    if (!S.gamesLoaded && !S.gamesError) { grid.innerHTML = skTiles(10); return; }
    if (!list.length) {
      var filt = S.q || S.filter !== 'ALL';
      var box;
      if (S.gamesError && !S.games.length) box = stateBox('err', 'warn', 'Could not load your games', 'The game list is unavailable right now. Check that the console is awake, then try again.', '<button class="btn" data-act="refreshGames">' + ic('refresh') + 'Try again</button>');
      else if (!S.games.length) box = stateBox('info', 'games', 'No games found yet', 'CheatRunner lists the games installed on this console. Install one, then refresh the list.', '<button class="btn" data-act="refreshGames">' + ic('refresh') + 'Refresh</button>');
      else {
        var msg = S.filter === 'FAV' && !S.q ? 'No favorites yet. Tap the star on a game to add it.'
          : S.filter === 'RECENT' && !S.q ? 'Nothing recent. Launch a game or open its trainer.'
          : S.filter === 'CHEATS' && !S.q ? 'No games with cheats yet. Get some from Sources.'
          : 'No games match your search.';
        box = stateBox('', 'search', msg, '', filt ? '<button class="btn ghost" data-act="clearFilters">Clear filters</button>' : '');
      }
      grid.innerHTML = '<div style="grid-column:1/-1">' + box + '</div>';
      return;
    }
    // Stagger the tiles in only when the filter or sort changed, never on a poll repaint.
    var key = S.filter + '|' + S.sort;
    if (S.gridKey !== key) {
      S.gridKey = key; grid.classList.remove('enter'); void grid.offsetWidth; grid.classList.add('enter');
      clearTimeout(S.gridTimer); S.gridTimer = setTimeout(function () { grid.classList.remove('enter'); }, 900);
    }
    var lk = key + '|' + S.q;
    if (S.listKey !== lk) { S.listKey = lk; S.shown = GRID_CHUNK; }
    S.gridList = list;
    var shown = Math.min(S.shown, list.length);
    var html = list.slice(0, shown).map(tileHtml).join('') + (shown < list.length ? moreBar(list.length - shown) : '');
    if (grid.__h !== html) {
      grid.__h = html;
      withFocus(grid, function () { grid.innerHTML = html; });
      padStart(grid.querySelector('.tile'));
    }
  }
  var GRID_CHUNK = isPS5 ? 12 : 36;
  function moreBar(left) {
    return '<div class="grid-more" id="gridMore"><button class="btn ghost" data-act="gridMore">' + esc(t('Show more ({n})', { n: left })) + '</button></div>';
  }
  function growGrid() {
    var grid = $('#grid'), more = $('#gridMore'), list = S.gridList;
    if (!grid || !more || !list) return;
    var from = Math.min(S.shown, list.length);
    S.shown = Math.min(list.length, from + GRID_CHUNK);
    more.parentNode.removeChild(more);
    grid.insertAdjacentHTML('beforeend', list.slice(from, S.shown).map(tileHtml).join('') + (S.shown < list.length ? moreBar(list.length - S.shown) : ''));
    grid.__h = list.slice(0, S.shown).map(tileHtml).join('') + (S.shown < list.length ? moreBar(list.length - S.shown) : '');
  }
  var scrollTick = false;
  window.addEventListener('scroll', function () {
    if (scrollTick) return;
    scrollTick = true;
    requestAnimationFrame(function () {
      scrollTick = false;
      var m = $('#gridMore');
      if (m && m.getBoundingClientRect().top < window.innerHeight + 900) growGrid();
    });
  }, { passive: true });

  function viewGames() {
    S.gridKey = '';
    var welcome = '';
    if (S.gamesLoaded && S.games.length && !S.games.some(function (g) { return g.hasCheat; }) && !store('cr_welcome')) {
      welcome = '<div class="banner info">' + ic('bolt') + '<div class="txt"><b>Welcome to CheatRunner.</b> No cheat files yet. Grab community cheats from Sources, or open a game and drop a file in.</div>'
        + '<a class="btn sm" href="#/sources">Get cheats</a><button class="icon-btn" data-act="dismissWelcome" aria-label="Dismiss">' + ic('x') + '</button></div>';
    }
    view.innerHTML = stage('Library', 'Games', '<span id="gamesCount"></span>') + welcome + '<div id="liveBar"></div>'
      + '<div class="games-bar"><div class="search">' + ic('search') + '<input class="input" id="gq" data-in="q" placeholder="Search by name or title ID" aria-label="Search games" value="' + esc(S.q) + '" autocomplete="off">'
      + '<button class="icon-btn search-clear" id="gclear" data-act="clearQ" aria-label="Clear search" hidden>' + ic('x', 'sm') + '</button></div>'
      + '<select class="select" data-change="sort" aria-label="Sort games">'
      + [['name_asc', 'Name A-Z'], ['name_desc', 'Name Z-A'], ['id_asc', 'Title ID'], ['cheats_first', 'Cheats first']].map(function (o) { return '<option value="' + o[0] + '"' + (S.sort === o[0] ? ' selected' : '') + '>' + o[1] + '</option>'; }).join('')
      + '</select><button class="btn ghost" id="gRefresh" data-act="refreshGames">' + ic('refresh') + 'Refresh</button></div>'
      + '<div class="chips-bar"><div class="chips" id="chips" role="group" aria-label="Filter games"></div></div><div class="grid" id="grid"></div>';
    paintGrid();
  }

  /* ---------------------------------------------------------------- launch */
  async function launch(id) {
    if (!id || S.launchBusy) return;
    pushRecent(id);
    S.launchBusy = true; S.launching = id; paintGrid();
    try {
      var r = await get('/launch?titleId=' + encodeURIComponent(id));
      if (!(r && r.ok)) { toast('Launch failed.', 'error'); return; }
      toast(t('Launching {name}...', { name: gname(findGame(id) || { titleId: id }) }), 'success');
      var t0 = Date.now(), fails = 0;
      while (Date.now() - t0 < 40000) {
        var ls = null;
        try { ls = await get('/api/launch/status', { timeoutMs: 5000 }); fails = 0; }
        catch (e) { if (++fails >= 3) { toast('CheatRunner stopped responding during launch.', 'warn'); break; } await sleep(800); continue; }
        if (!ls.busy) {
          if (ls.phase === 'timeout' || ls.stale) toast('Launch timed out. Check if the game started.', 'warn');
          else if (ls.phase === 'ready' || ls.verified) toast(ls.message || 'Game launched.', 'success');
          else toast(t('Launch failed: {msg}', { msg: ls.message || id }), 'error');
          break;
        }
        await sleep(800);
      }
      await refreshStatus(); await loadGames(true);
    } catch (e) {
      toast('Launch request failed. The game may still be starting.', 'warn');
    } finally {
      S.launchBusy = false; S.launching = '';
      if (route().page === 'games') paintGrid();
    }
  }

  /* ------------------------------------------------------------ game page */
  var STATE_ON = { on: 1, on_unverified: 1, mixed: 1 };

  function rowMap(mods) {
    var m = {};
    (mods || []).forEach(function (r) {
      var i = Number(r.index); if (!isFinite(i)) return;
      m[i] = {
        state: String(r.state || 'unknown').toLowerCase(), reason: String(r.reason || ''),
        visual: String(r.visualState || '').toLowerCase(), canToggle: !!r.canToggle,
        nextOn: r.nextOn != null ? !!r.nextOn : null, block: String(r.blockReason || '')
      };
    });
    return m;
  }
  async function loadCheatState(id) {
    try {
      var st = await get('/api/cheats/state?titleId=' + encodeURIComponent(id) + '&debug=1');
      if (!S.g || S.g.id !== id) return;
      S.g.dbg = st; S.g.rows = rowMap(st.mods); S.g.stateErr = null;
    } catch (e) {
      // A missing file has its own state; any other failure means the live state could not be read.
      if (S.g && S.g.id === id && !(e.status === 404 && e.code === 'no cheat file')) S.g.stateErr = e;
    }
  }
  async function loadGamePage(id) {
    var g = S.g; g.err = null; g.noFile = false;
    try { g.doc = await get('/api/cheats?titleId=' + encodeURIComponent(id), { timeoutMs: 15000 }); }
    catch (e) {
      g.doc = null;
      if (e.status === 404 && e.code === 'no cheat file') g.noFile = true; else g.err = e;
    }
    if (S.g !== g) return;
    await loadCheatState(id);
    try { var a = await get('/api/cheats/profile?titleId=' + encodeURIComponent(id), { retries: 0 }); g.auto = Array.isArray(a.mods) ? a.mods : []; }
    catch (e) { g.auto = []; }
    g.loaded = true;
    if (S.g === g && route().page === 'game') { paintHero(); paintBody(); }
  }

  // What the trainer needs, one cell each, so a missing piece is visible at a glance.
  function readyHtml(g) {
    var game = findGame(g.id) || {}, live = S.running === g.id, d = g.dbg || {};
    function rd(cls, icon, label, val, sub) {
      return '<div class="rd ' + cls + '"><span class="rd-ic">' + ic(icon) + '</span><div class="rd-txt"><small>' + label + '</small><b>' + val + '</b>' + (sub ? '<span>' + sub + '</span>' : '') + '</div></div>';
    }
    var gameRd = live ? rd('ok', 'play', 'Game', 'Running', esc(gplat(game))) : rd('warn', 'power', 'Game', 'Not running', 'Start it to use cheats');
    var fileRd;
    if (!g.loaded) fileRd = rd('', 'file', 'Cheat file', 'Checking...', '');
    else if (g.err) fileRd = rd('err', 'warn', 'Cheat file', 'Could not load', 'Try again below');
    else if (g.noFile) fileRd = rd('warn', 'file', 'Cheat file', 'No file yet', 'Add one in Files or Sources');
    else {
      var cv = d.selectedCheatVersion && d.selectedCheatVersion !== 'unknown' ? d.selectedCheatVersion : ((g.doc && g.doc.version) || '');
      var gv = d.detectedVersion && d.detectedVersion !== 'unknown' ? d.detectedVersion : '';
      var fmt = String(d.cheatFormat || '').toUpperCase();
      var bad = cv && gv && (d.versionMatch === false || cv !== gv);
      fileRd = rd(bad ? 'warn' : 'ok', bad ? 'warn' : 'check', 'Cheat file', esc((fmt || 'File') + (cv ? ' \u00b7 ' + fmtVer(cv) : '')),
        cv && gv ? esc(bad ? t('File v{a} / game v{b}', { a: cv, b: gv }) : t('Matches v{v}', { v: gv })) : '');
    }
    var eng = S.offline ? rd('warn', 'refresh', 'Engine', 'Reconnecting...', '') : !S.state ? rd('', 'bolt', 'Engine', 'Checking...', '')
      : (S.state.cheats && S.state.cheats.engine) ? rd('ok', 'bolt', 'Engine', 'On', '') : rd('err', 'power', 'Engine', 'Off', 'Turn it on in Settings');
    return gameRd + fileRd + eng;
  }

  // Why the trainer cannot act yet, and the fix. Kept outside the tab body so polls never rebuild the list.
  function paintNotes() {
    var g = S.g, el = $('#gNotes'); if (!g || !el) return;
    var h = '', patches = g.tab === 'patches', ready = g.loaded && !g.err && !g.noFile && g.doc && g.doc.mods && g.doc.mods.length;
    if (g.tab !== 'files') {
      if (S.state && S.state.cheats && !S.state.cheats.engine) {
        h += '<div class="banner err">' + ic('power') + '<div class="txt"><b>The cheat engine is off.</b><span>Cheats and patches cannot be applied until it is turned on.</span></div><a class="btn sm" href="#/settings">Open Settings</a></div>';
      }
      if (S.running !== g.id && (patches || ready)) {
        h += '<div class="banner">' + ic('warn') + '<div class="txt"><b>' + (patches ? 'Start this game to apply patches.' : 'Start this game to use its cheats.') + '</b></div>'
          + '<button class="btn sm ghost" data-act="launch" data-id="' + esc(g.id) + '"' + (S.launchBusy ? ' disabled' : '') + '>' + ic('play', 'fill sm') + 'Play</button></div>';
      }
    }
    if (g.tab === 'cheats' && ready) {
      var anyCrash = Object.keys(g.rows).some(function (k) { return g.rows[k].state === 'crash_suspect'; });
      if (g.stateErr) h += '<div class="banner err">' + ic('warn') + '<div class="txt"><b>Could not read the live cheat state.</b><span>The switches may be out of date.</span></div><button class="btn sm" data-act="retryState">Try again</button></div>';
      if (anyCrash) h += '<div class="banner err">' + ic('warn') + '<div class="txt">A cheat crashed the game last time.</div><button class="btn sm danger" data-act="clearCrash">Clear flag</button></div>';
    }
    if (el.__h !== h) { el.__h = h; el.innerHTML = h; }
  }

  function paintHero() {
    var g = S.g, el = $('#gHero'); if (!g || !el) return;
    var game = findGame(g.id) || { titleId: g.id };
    var live = S.running === g.id, fav = S.fav.has(g.id);
    var ver = game.version && game.version !== 'unknown' ? game.version : '';
    var nm = gname(game);
    var credits = g.doc && Array.isArray(g.doc.credits) ? g.doc.credits.filter(Boolean) : [];
    el.innerHTML = '<div class="t-bg" style="background-image:url(' + esc(gicon(game)) + ')"></div>'
      + '<div class="cover t-cover"><span class="ph" aria-hidden="true">' + esc(nm.charAt(0).toUpperCase()) + '</span><div class="tile-img" style="background-image:url(' + esc(gicon(game)) + ')"></div></div>'
      + '<div class="t-info"><div class="eyebrow">Trainer</div><h1>' + esc(nm) + '</h1><div class="muted mono">' + esc(g.id) + '</div>'
      + '<div class="t-tags"><span class="pill">' + esc(gplat(game)) + '</span>'
      + (ver ? '<span class="pill">' + esc(fmtVer(ver)) + '</span>' : '')
      + (live ? '<span class="pill ok"><i class="dot live"></i>Running</span>' : '<span class="pill">Not running</span>') + '</div>'
      + (credits.length ? '<div class="credits"><span class="credits-l">Credits</span>' + credits.map(function (n) { return '<span class="credit">' + ic('user', 'sm') + esc(n) + '</span>'; }).join('') + '</div>' : '') + '</div>'
      + '<div class="t-act"><button class="icon-btn' + (fav ? ' fav-on' : '') + '" data-act="fav" data-id="' + esc(g.id) + '" aria-label="' + (fav ? 'Remove from favorites' : 'Add to favorites') + '" aria-pressed="' + fav + '">' + ic('star', fav ? 'fill' : '') + '</button>'
      + '<button class="btn' + (live ? ' ghost' : '') + '" data-act="launch" data-id="' + esc(g.id) + '"' + (S.launchBusy ? ' disabled' : '') + '>' + ic('play', 'fill sm') + (live ? 'Relaunch' : 'Play') + '</button></div>';
    var rd = $('#gReady'); if (rd) rd.innerHTML = readyHtml(g);
    paintNotes();
  }

  function viewGame(id, tab) {
    if (!S.g || S.g.id !== id) {
      S.g = newGame(id);
      pushRecent(id);
      loadGamePage(id);
    } else if (S.g.loaded) {
      var keep = S.g;
      loadCheatState(id).then(function () { if (S.g === keep && route().page === 'game' && keep.tab === 'cheats') paintMods(); });
    }
    S.g.tab = tab === 'patches' || tab === 'files' ? tab : 'cheats';
    view.innerHTML = '<button class="back" data-act="back">' + ic('back') + 'All games</button>'
      + '<div class="t-head" id="gHero"></div><div class="ready" id="gReady"></div>'
      + '<div class="tabs" role="tablist">'
      + [['cheats', 'Cheats'], ['patches', 'Patches'], ['files', 'Files']].map(function (x) { return '<button class="tab' + (S.g.tab === x[0] ? ' active' : '') + '" role="tab" aria-selected="' + (S.g.tab === x[0]) + '" data-act="tab" data-t="' + x[0] + '">' + x[1] + '</button>'; }).join('')
      + '</div><div id="gNotes"></div><div id="gBody" role="tabpanel"></div>';
    paintHero();
    paintBody();
    if (S.g.tab === 'patches' && !S.g.patchLoaded) loadPatches();
  }

  function paintBody() {
    var g = S.g, el = $('#gBody'); if (!g || !el) return;
    paintNotes();
    if (g.tab === 'patches') return paintPatches();
    if (g.tab === 'files') return paintFiles();
    if (!g.loaded) { el.innerHTML = skRows(5); return; }
    if (g.err) {
      el.innerHTML = stateBox('err', 'warn', 'Could not load the cheat file', 'The file is there, but it could not be read. It may be damaged or still being copied.',
        '<button class="btn" data-act="retryGame">' + ic('refresh') + 'Try again</button><button class="btn ghost" data-act="tab" data-t="files">Choose another file</button>', errText(g.err));
      padStart(el.querySelector('.btn'));
      return;
    }
    if (g.noFile) {
      el.innerHTML = stateBox('info', 'bolt', 'No cheat file for this game yet', 'Download community cheats from Sources, or add a file from your computer.',
        '<a class="btn" href="#/sources">' + ic('sources') + 'Get cheats</a><button class="btn ghost" data-act="tab" data-t="files">' + ic('upload') + 'Add a file</button>');
      padStart(el.querySelector('.btn'));
      return;
    }
    if (!g.doc || !Array.isArray(g.doc.mods) || !g.doc.mods.length) {
      el.innerHTML = stateBox('warn', 'file', 'This cheat file has no cheats', 'The file loaded but it is empty. Try another file for this game.',
        '<button class="btn" data-act="tab" data-t="files">Choose another file</button>');
      padStart(el.querySelector('.btn'));
      return;
    }
    var d = g.dbg || {}, fname = String(d.cheatPath || '').split('/').pop();
    el.innerHTML = ''
      + '<div class="mods-head">' + (g.doc.mods.length >= 6 ? '<div class="search">' + ic('search') + '<input class="input" data-in="modq" placeholder="Search cheats" aria-label="Search cheats" value="' + esc(g.filter) + '" autocomplete="off"></div>' : '')
      + '<span class="mods-sum" id="modStats"></span><div class="row"><button class="btn soft warn" data-act="disableAll" id="btnDisableAll">' + ic('power', 'sm') + 'Disable all</button>'
      + '<button class="btn soft" data-act="saveAuto" title="Re-apply the cheats that are ON every time this game launches">' + ic('save', 'sm') + 'Save autoload</button>'
      + (g.auto.length ? '<button class="btn quiet" data-act="clearAuto">' + ic('trash', 'sm') + esc(t('Clear autoload ({n})', { n: g.auto.length })) + '</button>' : '') + '</div></div>'
      + (fname ? '<div class="muted small" style="margin:-4px 0 14px"><span class="mono">' + esc(fname) + '</span>' + '</div>' : '')
      + '<div id="modList"></div>';
    paintMods();
  }

  function paintMods() {
    var g = S.g, list = $('#modList'); if (!g || !list || !g.doc) return;
    var mods = g.doc.mods, q = fold(g.filter.trim());
    var notRunning = S.running !== g.id;
    var active = 0;
    Object.keys(g.rows).forEach(function (k) { if (STATE_ON[g.rows[k].state]) active++; });
    var stat = $('#modStats'); if (stat) { stat.textContent = t('{a} of {n} on', { a: active, n: mods.length }); stat.className = 'mods-sum' + (active ? ' has' : ''); }
    var da = $('#btnDisableAll'); if (da) da.hidden = !active;
    paintNotes();

    function row(m, i) {
      var r = g.rows[i] || { state: 'unknown', reason: '', visual: '', canToggle: false, nextOn: null, block: '' };
      var on = r.visual === 'on' || r.visual === 'mixed' || !!STATE_ON[r.state];
      var busy = !!g.applying[i];
      var crash = r.state === 'crash_suspect';
      var dis = busy || notRunning || !r.canToggle;
      var note = '';
      if (crash) note = '<div class="mod-note err">' + ic('warn') + 'Crashed the game last time</div>';
      else if (r.state === 'mixed') note = '<div class="mod-note">' + ic('warn') + 'Partly applied</div>';
      else if (r.state === 'conflict') note = '<div class="mod-note">' + ic('warn') + esc(r.reason || 'Conflicts with another cheat') + '</div>';
      else if (!on && (r.state === 'mismatch' || r.state === 'address_unresolved')) note = '<div class="mod-note">' + ic('warn') + 'May not match this game version</div>';
      else if (!notRunning && !r.canToggle && !on) note = '<div class="mod-note lock">' + ic('lock') + esc(r.block || 'This cheat cannot be toggled right now.') + '</div>';
      return '<div class="mod' + (on ? ' is-on' : '') + (crash ? ' is-err' : '') + (notRunning ? ' is-off-lock' : '') + '"><span class="mod-ic">' + ic('bolt') + '</span><div class="mod-main"><div class="mod-name">' + esc(m.name || t('Cheat #{n}', { n: i })) + '</div>'
        + (m.description ? '<div class="mod-desc">' + esc(m.description) + '</div>' : '') + note + '</div>'
        + '<button class="switch' + (on ? ' on' : '') + (busy ? ' busy' : '') + '" data-act="toggle" data-i="' + i + '"' + (dis ? ' disabled' : '')
        + ' role="switch" aria-checked="' + on + '" aria-label="' + esc(m.name || 'Cheat') + '"></button></div>';
    }
    var cap = q ? Infinity : (g.modShown || MOD_CHUNK), left = cap, hidden = 0;
    function group(title, items) {
      var f = q ? items.filter(function (x) { return fold(x.m.name).indexOf(q) >= 0; }) : items;
      var part = f.slice(0, Math.max(0, left)); left -= part.length; hidden += f.length - part.length;
      return part.length ? (title ? '<div class="group-title">' + title + '</div>' : '') + part.map(function (x) { return row(x.m, x.i); }).join('') : '';
    }
    var tog = [], btn = [];
    mods.forEach(function (m, i) { (String(m.type || 'checkbox').toLowerCase() === 'button' ? btn : tog).push({ m: m, i: i }); });
    var both = tog.length && btn.length;
    var html = group(both ? 'Toggles' : '', tog) + group(both ? 'Actions' : '', btn);
    if (hidden) html += '<div class="grid-more"><button class="btn ghost" data-act="modMore">' + esc(t('Show more ({n})', { n: hidden })) + '</button></div>';
    html = html || stateBox('', 'search', esc(t('No cheats match "{q}".', { q: g.filter })), '', '');
    if (list.__h !== html) { list.__h = html; withFocus(list, function () { list.innerHTML = html; }); }
    var last = g.lastSw != null ? list.querySelector('.switch[data-i="' + g.lastSw + '"]:not([disabled])') : null;
    padStart(last || list.querySelector('.switch:not([disabled])') || $('#gHero .btn'));
  }

  var TOGGLE_ERR = {
    app_not_stable: 'The game is still starting. Try again in a few seconds.',
    apply_in_progress: 'Another cheat is being applied. Try again in a moment.',
    crash_suspect: 'This cheat crashed the game before. Clear the flag to retry.',
    game_not_running: 'Start the game first.', base_not_ready: 'The game is still loading.'
  };
  async function toggleMod(i, sw) {
    var g = S.g; if (!g || g.applying[i]) return;
    var r = g.rows[i];
    if (!r || !r.canToggle) { toast(r && r.block ? r.block : 'This cheat cannot be toggled right now.', 'warn'); return; }
    var next = r.nextOn;
    g.applying[i] = 1;
    // Flip the switch right away; a failed apply repaints it back.
    if (sw) {
      sw.classList.add('busy'); sw.classList.toggle('on', !!next); sw.setAttribute('aria-checked', next ? 'true' : 'false');
      if (sw.parentNode) sw.parentNode.classList.toggle('is-on', !!next);
    }
    try {
      await get('/api/cheats/toggle?titleId=' + encodeURIComponent(g.id) + '&index=' + i + '&on=' + (next ? 1 : 0), 20000);
    } catch (e) {
      toast(TOGGLE_ERR[e.code] || e.message || 'Could not toggle the cheat.', e.code === 'app_not_stable' || e.code === 'apply_in_progress' ? 'warn' : 'error');
    }
    await loadCheatState(g.id);
    delete g.applying[i];
    if (S.g === g) paintBody();
  }

  /* ----------------------------------------------------------- files tab */
  function paintFiles() {
    var g = S.g, el = $('#gBody'); if (!g || !el) return;
    var d = g.dbg || {}, cands = Array.isArray(d.candidates) ? d.candidates : [];
    var manual = d.selectionMode === 'manual';
    var html = '<div class="panel">' + sect('file', 'Cheat files for this game', 'Saved in /data/cheatrunner/cheats. The file marked In use is the one the trainer reads.',
      (manual ? '<button class="btn sm ghost" data-act="autoSelect">Auto-select</button>' : ''));
    if (!cands.length) html += stateBox('', 'file', 'No local file found', 'Download one from Sources or drop a file below.', '<a class="btn sm" href="#/sources">' + ic('sources', 'sm') + 'Get cheats</a>');
    cands.forEach(function (c) {
      var name = String(c.filename || c.path || '').split('/').pop();
      var wrong = c.match === 'wrong_version';
      html += '<div class="item' + (c.selected ? ' sel' : '') + '"><div class="item-main"><div class="item-title">' + esc(name) + '</div>'
        + '<div class="item-sub">' + esc([String(c.format || '').toUpperCase(), c.version && c.version !== 'unknown' ? 'v' + c.version : ''].filter(Boolean).join(' - ')) + '</div></div>'
        + (wrong ? '<span class="pill warn">' + ic('warn', 'sm') + 'Other version</span>' : c.match === 'exact' ? '<span class="pill ok">' + ic('check', 'sm') + 'Exact</span>' : '')
        + (c.selected ? '<span class="pill on">In use</span>' : '<button class="btn sm ghost" data-act="selectFile" data-path="' + esc(c.path || '') + '" data-wrong="' + (wrong ? 1 : 0) + '">Use</button>') + '</div>';
    });
    if (cands.length && g.doc) html += '<div class="row" style="margin-top:10px;justify-content:flex-end"><button class="btn sm quiet" data-act="deleteLocal">' + ic('trash', 'sm') + 'Delete the file in use</button></div>';
    html += '</div><div class="panel">' + sect('upload', 'Add cheats', 'A .mc4, .shn or .json file from your computer.')
      + '<div class="drop" data-act="pickUpload" id="dropZone" role="button" tabindex="0">' + ic('upload') + 'Drop a .mc4, .shn or .json file here, or click to browse</div></div>';
    el.innerHTML = html;
  }
  async function uploadFile(file) {
    var g = S.g; if (!g || !file) return;
    if (!/\.(mc4|shn|json)$/i.test(file.name)) { toast('Use a .mc4, .shn or .json file.', 'warn'); return; }
    try {
      var res = await fetch('/api/cheats/upload?titleId=' + encodeURIComponent(g.id) + '&filename=' + encodeURIComponent(file.name), { method: 'POST', body: await file.arrayBuffer(), cache: 'no-store' });
      if (!res.ok) { var j = null; try { j = await res.json(); } catch (e) {} toast((j && (j.message || j.error)) || 'Upload failed.', 'error'); return; }
      toast('Cheat file added.', 'success');
      g.loaded = false; await loadGames(true); await loadGamePage(g.id);
    } catch (e) { toast('Upload failed.', 'error'); }
  }

  /* --------------------------------------------------------- patches tab */
  async function loadPatches(quiet) {
    var g = S.g; if (!g) return;
    var el = $('#gBody'); if (el && !quiet) el.innerHTML = skRows(4);
    try {
      g.patch = await get('/api/patches?titleId=' + encodeURIComponent(g.id));
      if (!quiet) try { var a = await get('/api/patches/profile?titleId=' + encodeURIComponent(g.id), { retries: 0 }); g.patchAuto = Array.isArray(a.entryIds) ? a.entryIds : []; } catch (e) { g.patchAuto = []; }
    } catch (e) { if (quiet) return; g.patch = { error: errText(e) || 'error' }; }
    g.patchLoaded = true;
    if (S.g === g && g.tab === 'patches') paintPatches();
  }
  function paintPatches() {
    var g = S.g, el = $('#gBody'), d = g && g.patch; if (!el) return;
    if (!d) { el.innerHTML = skRows(4); return; }
    if (d.error) {
      el.innerHTML = stateBox('err', 'warn', 'Could not load the patches', 'The patch files for this game could not be read. Try again in a moment.',
        '<button class="btn" data-act="retryPatches">' + ic('refresh') + 'Try again</button>', d.error);
      return;
    }
    var entries = d.entries || [];
    if (!d.hasPatchFile || !entries.length) {
      el.innerHTML = stateBox('info', 'file', 'No patches for this game', 'Download the official patch set from Sources, or put your XML patches in /data/cheatrunner/patches/xml.',
        '<a class="btn" href="#/sources">' + ic('sources') + 'Get patches</a>');
      return;
    }
    var html = '<div class="banner info">' + ic('info') + '<div class="txt">Some patches only work if applied at game start: save the autoload and restart the game.</div>'
      + '<button class="btn sm soft" data-act="patchSaveAuto">' + ic('save', 'sm') + 'Save autoload</button>'
      + (g.patchAuto.length ? '<button class="btn sm quiet" data-act="patchClearAuto">' + ic('trash', 'sm') + esc(t('Clear autoload ({n})', { n: g.patchAuto.length })) + '</button>' : '') + '</div>';
    var pshown = Math.min(entries.length, g.patchShown || PATCH_CHUNK);
    entries.slice(0, pshown).forEach(function (e, i) {
      var vs = e.versionStatus || '', br = e.blockReason || '';
      var ver = vs === 'mask' ? t('Any version') : vs === 'match' ? t('v{v} matches', { v: e.appVer }) : vs === 'mismatch' ? t('Needs v{v}', { v: e.appVer }) : e.appVer ? 'v' + e.appVer : t('Any version');
      var act;
      if (e.applied) act = '<span class="pill ok">' + ic('check', 'sm') + 'Applied</span><button class="btn sm ghost" data-act="patchRestore" data-i="' + i + '">Restore</button>';
      else if (e.canApply) act = '<button class="btn sm" data-act="patchApply" data-i="' + i + '">Apply</button>';
      else if (br === 'version_mismatch') act = '<span class="pill warn">' + ic('warn', 'sm') + 'Other version</span><button class="btn sm ghost" data-act="patchForce" data-i="' + i + '">Force apply</button>';
      else act = '<span class="muted small">' + esc(br === 'missing_game' ? 'Start the game' : br === 'unsupported_line_type' ? 'Unsupported patch' : br.replace(/_/g, ' ')) + '</span>';
      html += '<div class="item' + (e.applied ? ' ok' : '') + '"><div class="item-main"><div class="item-title">' + esc(e.name) + '</div>'
        + '<div class="item-meta">' + (e.author ? '<span class="item-author">' + ic('user', 'sm') + esc(t('by {a}', { a: e.author })) + '</span>' : '') + '<span class="item-ver">' + esc(ver) + '</span></div>'
        + (e.note ? '<div class="item-note">' + esc(e.note) + '</div>' : '') + '</div>' + act + '</div>';
    });
    if (pshown < entries.length) html += '<div class="grid-more"><button class="btn ghost" data-act="patchMore">' + esc(t('Show more ({n})', { n: entries.length - pshown })) + '</button></div>';
    if (el.__h !== html) { el.__h = html; el.innerHTML = html; }
  }
  var PATCH_CHUNK = isPS5 ? 25 : 60;
  var MOD_CHUNK = isPS5 ? 30 : 200;
  async function patchCall(kind, i, force) {
    var g = S.g, e = g && g.patch && g.patch.entries && g.patch.entries[i]; if (!e) return;
    var id = e.entryId ? '&entryId=' + encodeURIComponent(e.entryId) : '&index=' + i;
    var btn = $('[data-act="' + (kind === 'restore' ? 'patchRestore' : force ? 'patchForce' : 'patchApply') + '"][data-i="' + i + '"]');
    if (btn) btn.disabled = true;
    try {
      var r = await get('/api/patches/' + kind + '?titleId=' + encodeURIComponent(g.id) + id + (force ? '&force=1' : ''), 20000);
      if (r && r.ok) { await loadPatches(true); }
      else { toast((r && (r.message || r.error)) || 'Failed.', 'error'); if (btn) btn.disabled = false; }
    } catch (ex) { toast(ex.message || 'Failed.', 'error'); if (btn) btn.disabled = false; }
  }

  /* --------------------------------------------------------------- sources */
  var repoPolling = false;
  function viewSources() {
    view.innerHTML = stage('Downloads', 'Sources', 'Download ready-made cheats and patches for your games. Downloads keep running while you browse.')
      + '<div class="src-cols"><div class="panel">' + sect('sources', 'Community cheat repositories', 'Download ready-made cheat files for your games. They are saved to /data/cheatrunner/cheats, and downloads keep running in the background.')
      + '<div class="row" style="margin-bottom:8px"><select class="select grow" id="repoSource" aria-label="Cheat source"><option value="hencollection">HEN Cheats Collection (' + t('Recommended') + ')</option><option value="ps5cheats">PS5 Cheats</option><option value="goldhen">GoldHEN Cheats</option><option value="henppsa">HEN PPSA Cheats</option></select></div>'
      + '<div class="row"><label class="row small muted chk-row"><input type="checkbox" class="chk" id="repoOverwrite">Also update files that changed</label></div>'
      + '<div class="row" style="margin-top:12px"><button class="btn half" data-act="repoGo">' + ic('sources') + 'Download selected</button><button class="btn ghost half" data-act="repoAll">Download all sources</button></div>'
      + '<div id="repoOut"></div>'
      + '<div class="src-link"><div class="grow"><b>Browse cheats online</b><div class="muted small">Search every game with cheats and check if yours is covered.</div></div>'
      + '<a class="btn ghost" href="https://hencheats.vercel.app/" target="_blank" rel="noopener noreferrer">' + ic('search', 'sm') + 'Open site</a></div></div>'
      + '<div class="panel">' + sect('file', 'Community patch repositories', 'Download community patch sets. Patches are saved to /data/cheatrunner/patches.', '', 'info')
      + '<div class="row" style="margin-bottom:8px"><select class="select grow" id="patchSource" aria-label="Patch source"><option value="crpatches">CheatRunner Patches</option><option value="psgamepatch">PS-Game-Patch</option></select></div>'
      + '<div class="row"><label class="row small muted chk-row"><input type="checkbox" class="chk" id="patchOverwrite">Also update files that changed</label></div>'
      + '<div class="row" style="margin-top:12px"><button class="btn half" data-act="patchRepoGo">' + ic('sources') + 'Download patches</button>'
      + '<a class="btn ghost half" href="https://github.com/notmaj0r/CheatRunner_Patches" target="_blank" rel="noopener noreferrer">' + ic('github', 'sm') + 'Open repository</a></div>'
      + '<div id="repoOutP"></div></div></div>';
    paintRepo(S.dl);
    get('/api/cheats/repo/download/status', { timeoutMs: 4000, retries: 0 }).then(function (st) { S.dl = st; paintRepo(st); paintDlChip(st); if (st && st.state === 'running') watchDownload(); }).catch(function () {});
  }
  function isPatchSrc(s) { return s === 'psgamepatch' || s === 'crpatches'; }
  var REPO_NAMES = { hencollection: 'HEN Cheats Collection', ps5cheats: 'PS5 Cheats', goldhen: 'GoldHEN Cheats', henppsa: 'HEN PPSA Cheats', psgamepatch: 'PS-Game-Patch', crpatches: 'CheatRunner Patches', all: 'all sources' };
  function fmtBytes(n) {
    if (!n) return '0 KB';
    if (n < 1048576) return Math.max(1, Math.round(n / 1024)) + ' KB';
    return (n / 1048576).toFixed(n < 10485760 ? 2 : 1) + ' MB';
  }
  function fmtSpan(sec) {
    sec = Math.round(sec);
    if (sec < 0 || !isFinite(sec)) return '—';
    if (sec < 60) return t('{s}s', { s: sec });
    return t('{m}m {s}s', { m: Math.floor(sec / 60), s: (sec % 60 < 10 ? '0' : '') + (sec % 60) });
  }
  function dlStat(id, icon, label) { return '<div class="dl-stat"><span>' + ic(icon, 'sm') + label + '</span><b id="' + id + '">—</b></div>'; }
  var dlBuilt = false;
  function paintRepo(st) {
    var forPatches = !!st && isPatchSrc(st.source);
    var out = $(forPatches ? '#repoOutP' : '#repoOut'), other = $(forPatches ? '#repoOut' : '#repoOutP'); if (!out) return;
    if (other && other.firstChild) { other.innerHTML = ''; dlBuilt = false; }
    if (!st || !st.state || st.state === 'idle') { out.innerHTML = ''; dlBuilt = false; return; }
    if (!dlBuilt || !$('#dlBar')) {
      out.innerHTML = '<div class="dl" id="dl"><div class="dl-top"><div><div class="dl-title" id="dlTitle"></div><div class="dl-sub" id="dlSub"></div></div><div class="dl-pct" id="dlPct">0%</div></div>'
        + '<div class="progress dl-bar" role="progressbar" aria-valuemin="0" aria-valuemax="100" id="dlTrack"><i id="dlBar"></i></div>'
        + '<div class="dl-stats">' + dlStat('dlSpeed', 'gauge', 'Speed') + dlStat('dlEta', 'refresh', 'Time left') + dlStat('dlFiles', 'file', 'Files')
        + dlStat('dlBytes', 'sources', 'Downloaded') + dlStat('dlSame', 'check', 'Up to date') + dlStat('dlFail', 'warn', 'Failed') + '</div>'
        + '<div class="dl-file mono" id="dlFile"></div><div class="dl-res" id="dlRes"></div></div>';
      dlBuilt = true;
    }
    var run = st.state === 'running', arch = run && st.phase === 'archive';
    var pct = st.state === 'done' ? 100 : Math.min(100, Number(st.pct || 0));
    var same = (st.skipped || 0) + (st.unchanged || 0), processed = (st.downloaded || 0) + same + (st.failed || 0);
    var name = st.source === 'all' ? t('all sources') : REPO_NAMES[st.source] || st.source || 'cheats';
    var multi = st.sourceTotal > 1 ? t('Source {i} of {n}', { i: st.sourceIndex, n: st.sourceTotal }) + ' \u00b7 ' : '';
    var PH = { listing: ['Checking {name}', 'Reading the file list from GitHub'], archive: ['Downloading {name}', 'Fetching the whole repository in one go'],
      extract: ['Unpacking {name}', forPatches ? 'Saving the patch files' : 'Saving the cheat files'], downloading: ['Downloading {name}', 'Comparing and downloading files one by one'] };
    var ph = PH[st.phase] || PH.downloading;
    $('#dl').className = 'dl' + (run ? ' run' : st.state === 'done' ? ' done' : ' err') + (arch ? ' ind' : '');
    $('#dlTitle').textContent = run ? t(ph[0], { name: name }) : st.state === 'done' ? t('Finished {name}', { name: name }) : t('Download failed');
    $('#dlSub').textContent = run ? multi + t(ph[1]) : t('Took {t}', { t: fmtSpan(st.elapsedSec || 0) });
    $('#dlPct').textContent = arch ? fmtBytes(st.bytes || 0) : pct + '%';
    $('#dlBar').style.width = arch ? '' : pct + '%';
    var tr = $('#dlTrack'); if (tr) { if (arch) tr.removeAttribute('aria-valuenow'); else tr.setAttribute('aria-valuenow', pct); tr.setAttribute('aria-label', $('#dlTitle').textContent); }
    $('#dlSpeed').textContent = run && st.bytesPerSec > 0 ? fmtBytes(st.bytesPerSec) + '/s' : st.bytes > 0 && !run ? t('avg {s}', { s: fmtBytes(st.bytesPerSec) + '/s' }) : '\u2014';
    $('#dlEta').textContent = run ? fmtSpan(st.etaSec) : '\u2014';
    $('#dlFiles').textContent = processed + (st.total ? ' / ' + st.total : '');
    $('#dlBytes').textContent = fmtBytes(st.bytes || 0);
    $('#dlSame').textContent = String(same);
    $('#dlFail').textContent = String(st.failed || 0);
    $('#dlFile').textContent = run && st.current ? st.current : '';
    // Finished: say what happened and what to do next. Failed: a plain reason and a retry.
    $('#dlRes').innerHTML = st.state === 'done'
      ? '<span class="pill ok">' + ic('check', 'sm') + (st.downloaded ? t('{n} new or updated file(s) saved', { n: st.downloaded }) : t('Everything is already up to date')) + '</span>'
        + (forPatches ? '' : '<button class="btn sm" data-act="goGames">' + ic('games', 'sm') + 'See your games</button>')
      : st.state === 'error' ? '<span class="pill err">' + ic('warn', 'sm') + esc(st.error || t('Download failed')) + '</span><button class="btn sm" data-act="repoRetry">' + ic('refresh', 'sm') + 'Try again</button>' : '';
  }
  // Shows the running download in the top bar from any page; paints the panel only on Sources.
  function paintDlChip(st) {
    var run = !!st && st.state === 'running', chip = $('#dlChip'), dot = $('#dotSources');
    if (chip) chip.hidden = !run;
    if (dot) dot.hidden = !run;
    if (!run || !chip) return;
    var arch = st.phase === 'archive', pct = Math.min(100, Number(st.pct || 0));
    $('#dlChipTxt').textContent = arch ? t('Downloading') : t('Downloading {p}%', { p: pct });
    $('#dlChipBar').style.width = arch ? '40%' : pct + '%';
  }
  async function watchDownload() {
    if (repoPolling) return; repoPolling = true;
    try {
      for (;;) {
        var st = await get('/api/cheats/repo/download/status', { timeoutMs: 8000, retries: 0 }).catch(function () { return null; });
        if (st) S.dl = st;
        paintDlChip(st);
        if (route().page === 'sources') paintRepo(st);
        if (st && st.state === 'running') S.dlSawRun = true;
        if (!st || st.state !== 'running') {
          if (st && S.dlSawRun) {
            S.dlSawRun = false;
            if (st.state === 'done') toast(st.downloaded ? t('{n} new or updated file(s) saved', { n: st.downloaded }) : t('Everything is already up to date'), 'success');
            else if (st.state === 'error') toast(t('Download failed'), 'error');
            if (st.state === 'done' && !isPatchSrc(st.source)) loadGames(true);
          }
          break;
        }
        await sleep(isPS5 ? 2500 : 1200);
      }
    } finally { repoPolling = false; }
  }
  async function repoStart(all, patches) {
    var src = patches ? ($('#patchSource') || {}).value || 'crpatches' : all ? 'all' : ($('#repoSource') || {}).value || 'hencollection';
    var ovr = ($(patches ? '#patchOverwrite' : '#repoOverwrite') || {}).checked ? 1 : 0;
    S.dlLast = { all: all, patches: patches };
    try {
      var j = await post('/api/cheats/repo/download?source=' + encodeURIComponent(src) + '&overwrite=' + ovr);
      if (j && (j.state === 'running' || j.ok)) { S.dlSawRun = true; watchDownload(); } else toast((j && (j.message || j.error)) || 'Could not start.', 'error');
    } catch (e) { if (e.body && e.body.state === 'running') watchDownload(); else toast(e.message || 'Could not start the download.', 'error'); }
  }

  /* ---------------------------------------------------------------- system */
  var logs = { seq: -1, list: [], min: 0, q: '', atEnd: true };

  function viewSystem() {
    view.innerHTML = stage('Health', 'System', 'Live health of the cheat engine and memory patching, and what CheatRunner has been doing.',
        '<button class="btn sm ghost" data-act="qr">' + ic('qr', 'sm') + 'QR code</button><button class="btn sm ghost" data-act="copyUrl">' + ic('link', 'sm') + 'Copy URL</button>')
      + '<div id="healthBanner"></div><div class="panel"><div class="kv" id="statusKv"></div></div>'
      + '<div class="panel">' + sect('terminal', 'Logs', 'What CheatRunner has been doing - useful when something does not work.', '', 'info')
      + '<div class="log-tools"><div class="seg" role="group" aria-label="Log level filter">'
      + [[0, 'All'], [2, 'Warnings and errors'], [3, 'Errors only']].map(function (o) { return '<button class="seg-b' + (logs.min === o[0] ? ' on' : '') + '" data-act="logLvl" data-v="' + o[0] + '" aria-pressed="' + (logs.min === o[0]) + '">' + o[1] + '</button>'; }).join('')
      + '</div><input class="input" data-in="logq" placeholder="Filter" aria-label="Filter logs" value="' + esc(logs.q) + '" autocomplete="off">'
      + '<button class="btn sm ghost" data-act="copyLogs">' + ic('copy', 'sm') + 'Copy all</button></div>'
      + '<div class="console" id="logOut" role="log" aria-live="off" tabindex="0" aria-label="Logs"><span class="muted">Loading logs...</span></div>'
      + '<div class="log-foot"><span id="logFollow"></span><button class="btn sm quiet" id="logJump" data-act="logJump" hidden>' + ic('back', 'sm') + 'Jump to latest</button></div>'
      + '<div class="row" style="margin-top:8px"><button class="btn sm danger" data-act="shutdown" id="btnShutdown" hidden>' + ic('power', 'sm') + 'Shut down payload</button></div></div>'
      + '<div class="panel">' + sect('info', 'About', '')
      + '<div class="about-brand"><img src="/CheatRunner.png" alt="" width="64" height="64"><div><div class="about-name">CheatRunner</div><span class="ver-text">v' + esc(String((S.state || {}).version || '').replace(/^v/, '')) + '</span></div></div>'
      + '<p class="about-tag">CheatRunner - Cheat Trainer for your PS5!</p>'
      + '<p class="muted small" style="margin-top:4px">Made by maj0r - \ud83c\udde7\ud83c\uddf7</p>'
      + '<p class="muted small" style="margin-top:8px">For users. By users.</p>'
      + '<div style="margin-top:16px;padding-top:14px;border-top:1px solid var(--border)">'
      + '<div class="muted small" style="margin-bottom:6px">Source</div>'
      + '<a class="link-card" href="https://github.com/notmaj0r/CheatRunner" target="_blank" rel="noopener noreferrer">'
      + '<span class="link-card-ic">' + ic('github') + '</span><span class="link-card-txt"><b>CheatRunner</b><small>Leave the project a star! \u2b50</small></span>' + ic('chev', 'sm')
      + '</a></div></div>';
    paintStatus(); refreshLogs();
    var lo = $('#logOut'); if (lo) lo.addEventListener('scroll', logScroll, { passive: true });
  }
  // Follows the newest line while the reader is at the bottom; offers a jump back when they scroll up.
  function logScroll() {
    var out = $('#logOut'); if (!out) return;
    logs.atEnd = out.scrollTop + out.clientHeight >= out.scrollHeight - 30;
    paintFollow();
  }
  function paintFollow() {
    var f = $('#logFollow'), j = $('#logJump');
    if (f) f.textContent = logs.atEnd ? t('Following the latest lines') : t('Paused while you read');
    if (j) j.hidden = logs.atEnd;
  }
  function paintStatus() {
    var kv = $('#statusKv'); if (!kv) return;
    var s = S.state || {}, c = s.cheats || {}, p = s.privileges || {};
    var cheatGames = S.games.filter(function (g) { return g.hasCheat; }).length, patchGames = S.games.filter(function (g) { return g.hasPatch; }).length;
    function cell(k, v, cls) { return '<div class="' + (cls || '') + '"><small>' + (cls ? '<i class="dot"></i>' : '') + k + '</small><b>' + esc(v) + '</b></div>'; }
    var privOk = !s.privileges || (p.uid_ok && p.authid_ok);
    kv.innerHTML = cell('Cheat engine', c.engine ? 'On' : 'Off', c.engine ? 'cool' : 'hot')
      + cell('Memory patching', c.canPatchGameMemory ? 'Ready' : 'Not ready', c.canPatchGameMemory ? 'cool' : 'warm')
      + (s.privileges ? cell('Privileges', privOk ? 'OK' : 'Missing', privOk ? 'cool' : 'warm') : '')
      + cell('Games with cheats', cheatGames + ' / ' + S.games.length) + cell('Games with patches', patchGames + ' / ' + S.games.length)
      + cell('Version', 'v' + String(s.version || '').replace(/^v/, ''));
    var good = !!c.engine && !!c.canPatchGameMemory && privOk, hb = $('#healthBanner');
    if (hb) hb.innerHTML = !S.state ? '' : '<div class="banner ' + (good ? 'ok' : '') + '">' + ic(good ? 'check' : 'warn') + '<div class="txt"><b>' + (good ? 'Everything looks good.' : 'Something needs attention - check the details below.') + '</b></div>'
      + (!c.engine ? '<a class="btn sm" href="#/settings">Open Settings</a>' : '') + '</div>';
    var sd = $('#btnShutdown'); if (sd) sd.hidden = !(s.dev && s.dev.reloadEnabled);
  }
  function logLine(x) { return '[' + String(x.level || 'info').toLowerCase() + '] [' + (x.tag || 'core') + '] ' + (x.message || ''); }
  function renderLogs() {
    var out = $('#logOut'); if (!out) return;
    var q = fold(logs.q), rank = { debug: 0, info: 1, warn: 2, error: 3 };
    var list = logs.list.filter(function (x) {
      var lv = String(x.level || 'info').toLowerCase();
      return (rank[lv] == null ? 1 : rank[lv]) >= logs.min && (!q || fold(logLine(x)).indexOf(q) >= 0);
    }).slice(-300);
    var atEnd = logs.atEnd;
    out.innerHTML = list.map(function (x) { return '<div class="' + esc(String(x.level || 'info').toLowerCase()) + '">' + esc(logLine(x)) + '</div>'; }).join('')
      || '<span class="muted">' + (logs.list.length ? 'No matching lines.' : 'No logs yet.') + '</span>';
    if (atEnd) out.scrollTop = out.scrollHeight;
    paintFollow();
  }
  async function refreshLogs() {
    var out = $('#logOut'); if (!out) return;
    try {
      var d = await get(logs.seq >= 0 ? '/api/logs?since=' + logs.seq + '&limit=200' : '/api/logs?limit=200', { timeoutMs: 8000, retries: 0 });
      var last = typeof d.lastSeq === 'number' ? d.lastSeq : -1;
      if (last >= 0 && logs.seq >= 0 && last < logs.seq) { logs.list = []; logs.seq = -1; }
      var prev = logs.seq;
      var chunk = (d.logs || []).filter(function (x) { return prev < 0 || typeof x.seq !== 'number' || x.seq > prev; });
      logs.list = prev < 0 ? chunk.slice() : logs.list.concat(chunk);
      if (last >= 0) logs.seq = last;
      if (logs.list.length > 1500) logs.list = logs.list.slice(-1500);
      renderLogs();
    } catch (e) {}
  }

  /* -------------------------------------------------------------- settings */
  var THEMES = [['dark', 'Dark', '#0a0e16', '#10161f', '#e11d48'], ['crimson', 'Crimson', '#0f0a0c', '#171013', '#e11d48'], ['midnight', 'Midnight', '#070b16', '#0d1424', '#3b82f6'],
    ['oled', 'OLED', '#000', '#0b0b0c', '#e11d48'], ['light', 'Light', '#f4f6fa', '#ffffff', '#d6123f']];
  function applyTheme(t) {
    var ok = THEMES.some(function (x) { return x[0] === t; });
    document.documentElement.setAttribute('data-theme', ok ? t : 'midnight');
    store('cr_theme', ok ? t : 'midnight');
  }
  var saveTimer = 0;
  function saveState(text, cls) {
    var el = $('#saveState'); if (!el) return;
    el.textContent = text; el.className = 'save-state show ' + (cls || '');
    clearTimeout(saveTimer);
    if (cls !== 'err') saveTimer = setTimeout(function () { el.classList.remove('show'); }, 1600);
  }
  async function setCfg(key, val) {
    saveState('Saving...', '');
    try { await post('/api/config/set?key=' + encodeURIComponent(key) + '&value=' + encodeURIComponent(String(val))); S.cfg[key] = val; saveState('Saved', 'ok'); return true; }
    catch (e) { saveState(e.message || 'Could not save. Try again.', 'err'); return false; }
  }
  async function viewSettings() {
    view.innerHTML = stage('Preferences', 'Settings', 'Appearance, the cheat engine and controller options. Changes are saved as you make them.', '<span class="save-state" id="saveState" role="status" aria-live="polite"></span>')
      + '<div class="panel">' + skRows(4) + '</div>';
    var failed = null;
    try { S.cfg = await get('/api/config', { retries: 1 }); applyTheme(S.cfg.theme); } catch (e) { failed = e; }
    if (route().page !== 'settings') return;
    if (failed) {
      view.innerHTML = stage('Preferences', 'Settings', '')
        + stateBox('err', 'warn', 'Could not load the settings', 'The current values could not be read, so nothing is shown rather than showing wrong ones.', '<button class="btn" data-act="retrySettings">' + ic('refresh') + 'Try again</button>', errText(failed));
      return;
    }
    var c = S.cfg;
    function sw(key, title, hint) { return '<div class="set-row sw"><div class="set-txt"><b>' + title + '</b><span>' + hint + '</span></div><div class="set-ctl"><button class="switch' + (c[key] ? ' on' : '') + '" data-act="cfgSwitch" data-k="' + key + '" role="switch" aria-checked="' + (c[key] ? 'true' : 'false') + '" aria-label="' + esc(title) + '"></button></div></div>'; }
    function num(key, title, hint, min, max) { return '<div class="set-row"><div class="set-txt"><b>' + title + '</b><span>' + hint + '</span></div><div class="set-ctl"><input class="input" type="number" data-change="cfgNum" data-k="' + key + '" min="' + min + '" max="' + max + '" value="' + Number(c[key] || 0) + '"></div></div>'; }
    var cur = THEMES.some(function (x) { return x[0] === c.theme; }) ? c.theme : 'midnight', lite = rootCls.contains('lite');
    view.innerHTML = stage('Preferences', 'Settings', 'Appearance, the cheat engine and controller options. Changes are saved as you make them.', '<span class="save-state" id="saveState" role="status" aria-live="polite"></span>')
      + '<div class="panel">' + sect('settings', 'Appearance', 'Theme, effects and language of this dashboard.')
      + '<div class="themes" role="radiogroup" aria-label="Theme">' + THEMES.map(function (x) {
        return '<button class="theme-opt' + (cur === x[0] ? ' on' : '') + '" role="radio" aria-checked="' + (cur === x[0]) + '" data-act="themeSet" data-v="' + x[0] + '"><span class="theme-prev" style="background:' + x[2] + '"><i class="p-rail" style="background:' + x[3] + '"></i><i class="p-card" style="background:' + x[3] + '"></i><i class="p-acc" style="background:' + x[4] + '"></i></span>'
          + '<span class="theme-name">' + x[1] + ic('check', 'sm') + '</span></button>'; }).join('') + '</div>'
      + '<div class="set-row"><div class="set-txt"><b>Visual effects</b><span>Reduced turns off blur, shadows and animation. This is the default in the PS5 browser.</span></div><div class="set-ctl"><div class="seg" role="group" aria-label="Visual effects">'
      + [['full', 'Full'], ['reduced', 'Reduced']].map(function (x) { var on = (lite ? 'reduced' : 'full') === x[0]; return '<button class="seg-b' + (on ? ' on' : '') + '" data-act="fxSet" data-v="' + x[0] + '" aria-pressed="' + on + '">' + x[1] + '</button>'; }).join('') + '</div></div></div>'
      + '<div class="set-row"><div class="set-txt"><b>Language</b><span>Language of this dashboard.</span></div><div class="set-ctl lang"><button class="btn ghost lang-btn" data-act="langPick"><img class="flag" src="' + CRI.flag(CRI.lang) + '" alt="">' + esc(langName(CRI.lang)) + '</button></div></div></div>'
      + '<div class="panel">' + sect('bolt', 'Cheat engine', 'The essentials for applying cheats and patches.')
      + sw('cheat_engine', 'Cheat engine', 'Master switch for applying cheats and patches.')
      + sw('tile_autoinstall_enabled', 'Home-screen tile', 'Install the CheatRunner tile on the PS5 home screen at boot.')
      + sw('cheat_mark_crash_suspect', 'Crash detection', 'Flag a cheat if the game closes right after it was turned on.')
      + '</div>'
      + hotkeyCard(c)
      + '<div class="panel"><details class="adv"><summary>Advanced</summary>'
      + num('cheat_min_stable_ms', 'Wait after game start (ms)', 'Cheats are held back this long after a game launches.', 0, 60000)
      + num('cheat_apply_cooldown_ms', 'Gap between cheats (ms)', 'Minimum time between two cheat toggles.', 0, 10000)
      + sw('cheat_master_code_fixup', 'Master-code fixup', 'Resolve master-code dependent cheats automatically.')
      + '<div class="set-row"><div class="set-txt"><b>Log level</b><span>How much detail is written to the log.</span></div><div class="set-ctl"><select class="select" data-change="cfgLog" aria-label="Log level">'
      + ['debug', 'info', 'warn', 'error'].map(function (l) { return '<option' + (c.log_level === l ? ' selected' : '') + '>' + l + '</option>'; }).join('') + '</select></div></div>'
      + '</details></div>'
      + '<div class="panel">' + sect('flag', 'Troubleshooting', 'Send a report when something does not work.')
      + '<div class="set-row"><div class="set-txt"><b>Report an issue</b><span>Copies the diagnostics and opens the issue page on GitHub.</span></div><div class="set-ctl"><button class="btn ghost" data-act="reportIssue">' + ic('flag', 'sm') + 'Report an issue</button></div></div></div>'
      + '<div class="panel danger"><div class="danger-head">' + ic('warn') + 'Danger zone</div>'
      + '<div class="set-row"><div class="set-txt"><b>Reset to defaults</b><span>Cheats, patches and autoload profiles are not touched.</span></div><div class="set-ctl"><button class="btn danger" data-act="resetCfg">Reset to defaults</button></div></div></div>';
  }
  var HOTKEY_BUTTONS = ['Left', 'Up', 'Right', 'Down', 'Square', 'Triangle', 'Circle', 'Cross', 'Options', 'L1', 'R1', 'L2', 'R2', 'L3', 'R3', 'TouchPad'];
  // Injects into SceShellUI, so this stays off by default and gets its own risk banner.
  function hotkeyCard(c) {
    function btnSel(key, cur) {
      return '<select class="select" data-change="cfgHotkeyBtn" data-k="' + key + '" aria-label="Buttons">'
        + HOTKEY_BUTTONS.map(function (b) { return '<option value="' + b + '"' + (cur === b ? ' selected' : '') + '>' + b + '</option>'; }).join('') + '</select>';
    }
    return '<div class="panel">' + sect('pad', 'Controller shortcut and game menu', 'Adds a \u2605 CheatRunner entry to each game\'s options menu, and a button combo that opens the trainer for the running game.', '', 'warn')
      + '<div class="banner warn">' + ic('warn') + '<div class="txt">Hooks into the system UI. Off by default - on some firmwares this can crash the home screen.</div></div>'
      + '<div class="set-row sw"><div class="set-txt"><b>Enable shortcut</b><span>Hold two buttons together to open the trainer for the running game.</span></div>'
      + '<div class="set-ctl"><button class="switch' + (c.hotkey_enabled ? ' on' : '') + '" data-act="cfgSwitch" data-k="hotkey_enabled" role="switch" aria-checked="' + (c.hotkey_enabled ? 'true' : 'false') + '" aria-label="Enable controller shortcut"></button></div></div>'
      + '<div class="set-row"><div class="set-txt"><b>Buttons</b><span>Held together to trigger the shortcut.</span></div>'
      + '<div class="set-ctl wide">' + btnSel('hotkey_button_a', c.hotkey_button_a) + btnSel('hotkey_button_b', c.hotkey_button_b) + '</div></div>'
      + '<div class="set-row"><div class="set-txt"><b>Hold time (ms)</b><span>How long to hold the combo before it triggers.</span></div>'
      + '<div class="set-ctl"><input class="input" type="number" data-change="cfgNum" data-k="hotkey_hold_ms" min="100" max="5000" value="' + Number(c.hotkey_hold_ms || 800) + '"></div></div>'
      + '</div>';
  }

  /* ---------------------------------------------------------- global polls */
  function paintStatusChip() {
    var chip = $('#statusChip'); if (!chip) return;
    var eng = S.offline ? 'warn' : !S.state ? '' : (S.state.cheats && S.state.cheats.engine) ? 'ok' : 'err';
    var g = S.running ? findGame(S.running) : null;
    chip.className = 'status-chip ' + eng + (S.running ? '' : ' idle');
    $('#stEngine').textContent = S.offline ? t('Reconnecting...') : !S.state ? t('Engine') : eng === 'ok' ? t('Engine on') : t('Engine off');
    $('#stGame').textContent = S.running ? (g ? gname(g) : S.running) : t('No game');
    var d = $('#dotGames'); if (d) d.hidden = !S.running;
  }
  function setOffline(on) {
    if (S.offline === on) return;
    S.offline = on;
    paintStatusChip();
    if (route().page === 'game') paintHero();
    if (!on) refreshState();
  }
  async function refreshStatus() {
    try {
      var s = await get('/api/status', { retries: 0 });
      S.fail = 0; setOffline(false);
      var id = cleanId(s.titleId || s.title_id);
      var running = isTitleId(id) ? id : '';
      S.browserUrl = s.browserUrl || s.browser_url || (location.origin + '/');
      if (running !== S.running) {
        // A game just closed: the console writes its play time then, so reload the list shortly after.
        if (S.running && !running) setTimeout(function () { loadGames(true); }, 4000);
        S.running = running;
        S.games.forEach(function (x) { x.running = gid(x) === running; });
        var p = route().page;
        if (p === 'games') paintGrid(); else if (p === 'game') { paintHero(); paintBody(); }
      }
      paintStatusChip();
    } catch (e) { S.fail = (S.fail || 0) + 1; if (S.fail >= 2) setOffline(true); }
  }
  async function refreshState() {
    try {
      S.state = await get('/api/state', { retries: 0 });
      paintStatusChip();
      Array.prototype.forEach.call(document.querySelectorAll('.ver-text'), function (el) { el.textContent = 'v' + String(S.state.version || '').replace(/^v/, ''); });
      var p = route().page;
      if (p === 'system') paintStatus(); else if (p === 'game') paintHero();
    } catch (e) {}
  }
  async function checkUpdate() {
    var cur = String((S.state || {}).version || '').replace(/^v/, ''), info = null;
    if (!cur) return;
    try { info = JSON.parse(localStorage.getItem('cr_upd') || 'null'); } catch (e) {}
    if (!info || Date.now() - info.at > 6 * 3600000) {
      try {
        var r = await fetch('https://api.github.com/repos/notmaj0r/CheatRunner/releases/latest');
        if (!r.ok) return;
        info = { at: Date.now(), tag: String((await r.json()).tag_name || '') };
        localStorage.setItem('cr_upd', JSON.stringify(info));
      } catch (e) { return; }
    }
    var latest = info.tag.replace(/^v/, ''), a = latest.split('.'), b = cur.split('.'), newer = false;
    for (var i = 0; i < Math.max(a.length, b.length); i++) {
      var x = parseInt(a[i], 10) || 0, y = parseInt(b[i], 10) || 0;
      if (x !== y) { newer = x > y; break; }
    }
    if (!newer) return;
    $('#updLink').hidden = false;
    var seen = ''; try { seen = localStorage.getItem('cr_upd_seen') || ''; } catch (e) {}
    if (seen !== latest) {
      toast(t('Version {v} is available.', { v: latest }), 'info');
      try { localStorage.setItem('cr_upd_seen', latest); } catch (e) {}
    }
  }
  async function refreshLaunch() {
    if (S.launching) return;
    try {
      var ls = await get('/api/launch/status', { retries: 0 });
      var busy = !!ls.busy;
      if (busy !== S.launchBusy) { S.launchBusy = busy; if (route().page === 'games') paintGrid(); }
    } catch (e) {}
  }
  function poller(fn, ms, when) {
    var fails = 0;
    (function tick() {
      var delay = isPS5 ? ms * 2 : ms;
      if (document.hidden) delay = ms * 3;
      else if (!when || when()) {
        Promise.resolve(fn()).then(function () { fails = 0; }, function () { fails++; });
      }
      setTimeout(tick, fails ? Math.min(ms * (1 << Math.min(fails, 4)), 60000) : delay);
    })();
  }

  /* ---------------------------------------------------------------- router */
  function route() {
    var h = location.hash || '#/games';
    var m = /^#trainer=(.+)$/.exec(h);
    if (m) return { page: 'game', id: cleanId(m[1]), tab: 'cheats' };
    var p = h.replace(/^#\/?/, '').split('/');
    if (p[0] === 'game' && p[1]) return { page: 'game', id: cleanId(p[1]), tab: p[2] || 'cheats' };
    if (p[0] === 'sources' || p[0] === 'system' || p[0] === 'settings') return { page: p[0] };
    return { page: 'games' };
  }
  var TITLES = { games: 'Games', game: 'Trainer', sources: 'Sources', system: 'System', settings: 'Settings' };
  var lastKey = '';
  function enter(el) { if (!el || rootCls.contains('lite')) return; el.classList.remove('page-in'); void el.offsetWidth; el.classList.add('page-in'); }
  function render() {
    var r = route(), key = r.page + ':' + (r.id || '');
    Array.prototype.forEach.call(document.querySelectorAll('.rail-item[data-nav]'), function (a) {
      a.classList.toggle('active', a.getAttribute('data-nav') === (r.page === 'game' ? 'games' : r.page));
    });
    if (key !== lastKey) window.scrollTo(0, 0);
    document.title = t('{page} - CheatRunner', { page: t(r.page === 'game' ? 'Trainer' : TITLES[r.page]) });
    if (r.page === 'games') viewGames();
    else if (r.page === 'game') viewGame(r.id, r.tab);
    else if (r.page === 'sources') viewSources();
    else if (r.page === 'system') viewSystem();
    else viewSettings();
    if (key !== lastKey) enter(view); else if (r.page === 'game') enter($('#gBody'));
    lastKey = key;
    if (r.page === 'sources' || r.page === 'system' || r.page === 'settings') padStart($('#view .btn, #view .switch, #view .select'));
  }
  window.addEventListener('hashchange', render);
  (function () {
    var bar = $('.topbar'), busy = false;
    if (!isPS5) window.addEventListener('scroll', function () {
      if (busy) return; busy = true;
      requestAnimationFrame(function () { bar.classList.toggle('scrolled', (window.pageYOffset || 0) > 4); busy = false; });
    }, { passive: true });
  })();

  /* ------------------------------------------------------------------ palette */
  var pal = { on: false, sel: 0, list: [] };
  function palItems(q) {
    var all = [
      { k: 'Page', t: 'Games', s: 'Browse your library', i: 'games', go: '#/games' },
      { k: 'Page', t: 'Sources', s: 'Download community cheats', i: 'sources', go: '#/sources' },
      { k: 'Page', t: 'System', s: 'Status and logs', i: 'system', go: '#/system' },
      { k: 'Page', t: 'Settings', s: 'Theme and cheat engine', i: 'settings', go: '#/settings' }
    ];
    var run = S.running ? findGame(S.running) : null;
    if (run) all.push({ k: 'Action', t: t('Open trainer: {name}', { name: gname(run) }), s: 'Running now', i: 'bolt', go: '#/game/' + S.running });
    all.push({ k: 'Action', t: 'Refresh game list', s: 'Rescan installed games', i: 'refresh', fn: 'refreshGames' });
    all.push({ k: 'Action', t: 'Show QR code', s: 'Open the dashboard on your phone', i: 'qr', fn: 'qr' });
    all.push({ k: 'Action', t: 'Support this project', s: 'Ko-Fi, coffee, crypto, PIX', i: 'heart', fn: 'support' });
    var games = S.games.map(function (g) { return { k: 'Game', t: gname(g), s: gid(g) + ' · ' + gplat(g), i: 'play', go: '#/game/' + gid(g) }; });
    if (!q) {
      var rec = S.recent.map(findGame).filter(Boolean).slice(0, 6).map(function (g) { return { k: 'Recent', t: gname(g), s: gid(g), i: 'play', go: '#/game/' + gid(g) }; });
      return all.concat(rec);
    }
    return all.concat(games).filter(function (x) { return fold(x.t + ' ' + x.s).indexOf(q) >= 0; }).slice(0, 40);
  }
  function palRender(q) {
    pal.list = palItems(fold(q || '').trim());
    if (pal.sel >= pal.list.length) pal.sel = 0;
    var box = $('#palList'); if (!box) return;
    box.innerHTML = pal.list.length ? pal.list.map(function (x, i) {
      return '<button class="pal-item' + (i === pal.sel ? ' sel' : '') + '" role="option" aria-selected="' + (i === pal.sel) + '" data-act="palGo" data-i="' + i + '">'
        + ic(x.i, x.i === 'play' ? 'fill sm' : '') + '<span class="pal-txt"><b>' + esc(x.t) + '</b><small>' + esc(x.s) + '</small></span><span class="pal-kind">' + x.k + '</span></button>';
    }).join('') : '<div class="pal-empty">Nothing matches "' + esc(q) + '".</div>';
  }
  function palMove(d) {
    if (!pal.list.length) return;
    pal.sel = (pal.sel + d + pal.list.length) % pal.list.length;
    palRender($('#palQ').value);
    var el = $('#palList .sel'); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest' });
  }
  function palChoose(i) {
    var x = pal.list[i]; if (!x) return;
    closeModal(false);
    if (x.go) go(x.go); else if (x.fn) actions[x.fn]($('#gRefresh') || document.createElement('button'));
  }
  function openPalette() {
    if (!$('#modal').hidden) return;
    pal.sel = 0; pal.on = true;
    openModal('<div class="pal-in">' + ic('search') + '<input id="palQ" data-in="palq" placeholder="Search games, pages and actions" aria-label="Search games, pages and actions" autocomplete="off"><kbd>Esc</kbd></div>'
      + '<div class="pal-list" id="palList" role="listbox"></div>'
      + '<div class="pal-foot"><span>↑↓ navigate</span><span>Enter open</span></div>', function () { pal.on = false; }, 'palette');
    palRender('');
    $('#palQ').focus();
  }
  function ago(ts) {
    var d = Math.max(0, Math.floor(Date.now() / 1000) - Number(ts || 0));
    if (d < 60) return t('just now');
    if (d < 3600) return t('{n}m ago', { n: Math.floor(d / 60) });
    if (d < 86400) return t('{n}h ago', { n: Math.floor(d / 3600) });
    return t('{n}d ago', { n: Math.floor(d / 86400) });
  }
  function setRail(min) {
    if (min) document.documentElement.setAttribute('data-rail', 'min'); else document.documentElement.removeAttribute('data-rail');
    store('cr_rail', min ? 'min' : 'max');
    var b = $('#railBtn'); if (b) { b.title = min ? 'Expand menu' : 'Collapse menu'; b.setAttribute('aria-label', b.title); }
  }

  /* ---------------------------------------------------------------- language */
  function langName(id) { for (var i = 0; i < CRI.langs.length; i++) if (CRI.langs[i][0] === id) return CRI.langs[i][1]; return id; }
  function langHtml() {
    return '<h2>Language</h2><div class="lang-grid">' + CRI.langs.map(function (l) {
      var on = l[0] === CRI.lang;
      return '<button class="lang-opt' + (on ? ' on' : '') + '" data-act="langSet" data-id="' + l[0] + '" lang="' + l[0] + '"><img class="flag" src="' + CRI.flag(l[0]) + '" alt=""><span>' + esc(l[1]) + '</span>' + (on ? ic('check', 'sm') : '') + '</button>';
    }).join('') + '</div><div class="modal-actions"><button class="btn ghost" data-act="modalNo">Close</button></div>';
  }

  /* -------------------------------------------------------------------- QR */
  function qrSvg(text) {
    var q = qrcode(0, 'M'); q.addData(text); q.make();
    var n = q.getModuleCount(), pad = 3, size = n + pad * 2, d = '';
    for (var r = 0; r < n; r++) for (var c = 0; c < n; c++) if (q.isDark(r, c)) d += 'M' + (c + pad) + ' ' + (r + pad) + 'h1v1h-1z';
    return '<svg width="220" height="220" viewBox="0 0 ' + size + ' ' + size + '" shape-rendering="crispEdges" role="img" aria-label="QR code for the dashboard address">'
      + '<rect width="' + size + '" height="' + size + '" fill="#fff"/><path d="' + d + '" fill="#111"/></svg>';
  }
  function qrHtml() {
    var url = S.browserUrl || location.origin + '/';
    return '<h2>' + ic('qr', 'sm') + ' Open on your phone</h2><p>Scan this with your phone camera. Your phone must be on the same Wi-Fi as the PS5.</p>'
      + '<div class="qr-box">' + qrSvg(url) + '</div><div class="qr-url mono">' + esc(url) + '</div>'
      + '<div class="modal-actions"><button class="btn ghost" data-act="copyUrl">' + ic('copy', 'sm') + 'Copy link</button><button class="btn" data-act="modalNo">Close</button></div>';
  }

  /* --------------------------------------------------------------- support */
  var SUPPORTERS = ['primo', 'lal3x', 'jawad', 'anonymous', 'Maffioh', 'X-F1REBALL-X', 'Br0ken4life', 'Nimrod', 'PCYGOR', 'Lucas Clinio Maraia', 'Vinas', 'Someone', 'Bruno Sanglard', 'AirKingBR', 'Marcus', 'Jeffs', 'cavalcanti27'];
  var NEON = ['#00ff88', '#cc44ff', '#ff9900', '#ffe600', '#00aaff', '#ff4040'];
  var CRYPTO = [
    { label: 'BTC', addr: 'bc1ppfcad9ytyzmmzq3nfatfcag7q38rqcpsvxfqusq0uzv4gh82pa4syeknlk' },
    { label: 'BCH (Bitcoin Cash)', addr: 'qz2h97c2pyexn3fwhveu33tn5xkd3qlfn5796zhtcq' },
    { label: 'USDC (Solana)', addr: 'BNpezXSQWuUR9HWGjzQUS7AeEBCwiY2ZGxArWNwYXKKV' },
    { label: 'USDT (TON)', addr: 'UQA18f4cVS0778fL_nIssdN1GIHRYR7dQ3UnNgjPwgYvIQi_' }
  ];
  function supportHtml(view) {
    if (view === 'crypto') {
      return '<div class="sup-bar"><button class="icon-btn sup-round" data-act="supportBack" aria-label="Back">' + ic('back') + '</button>'
        + '<div class="sup-bar-txt"><h2>' + ic('bitcoin', 'sm') + ' Crypto support</h2><span>Select an address to copy it in full.</span></div>'
        + '<button class="icon-btn sup-round" data-act="modalNo" aria-label="Close">' + ic('x') + '</button></div>'
        + '<div class="sup-list">' + CRYPTO.map(function (c) {
          return '<button class="sup-addr" data-act="copyAddr" data-a="' + esc(c.addr) + '" data-l="' + esc(c.label) + '">'
            + '<div class="sup-addr-top"><b>' + esc(c.label) + '</b><span>Copy</span></div><div class="mono">' + esc(c.addr) + '</div></button>';
        }).join('') + '</div>';
    }
    function opt(cls, icon, title, sub, href) {
      var inner = '<span class="sup-opt-ic">' + ic(icon) + '</span><span class="sup-opt-txt"><b>' + title + '</b>' + (sub ? '<small>' + sub + '</small>' : '') + '</span>' + ic('chev', 'sm');
      return href ? '<a class="sup-opt ' + cls + '" href="' + href + '" target="_blank" rel="noopener noreferrer">' + inner + '</a>'
        : '<button class="sup-opt ' + cls + '" data-act="supportCrypto">' + inner + '</button>';
    }
    return '<div class="sup-hero"><button class="icon-btn sup-round sup-close" data-act="modalNo" aria-label="Close">' + ic('x') + '</button>'
      + '<div class="sup-heart">' + ic('heart') + '</div><h2>Support CheatRunner</h2>'
      + '<p>If CheatRunner was useful to you, consider supporting the project development! 🫶</p>'
      + '<p class="sup-goal">All contributions will go toward our goal of acquiring a low-firmware PS5 (1.xx–4.xx) for research, development, and future upgrades.</p></div>'
      + '<div class="sup-grid">'
      + opt('sup-kofi', 'coffee', 'Ko-Fi', '', 'https://ko-fi.com/maj0r')
      + opt('sup-coffee', 'coffee', 'Buy Me a Coffee', '', 'https://buymeacoffee.com/maj0r')
      + opt('sup-crypto', 'bitcoin', 'Crypto', 'Choose a network', '')
      + opt('sup-pix', 'pix', 'PIX', 'Support from Brazil', 'https://livepix.gg/notmaj0r')
      + '</div><div class="sup-thanks"><div class="sup-thanks-h">Supporters</div><div class="sup-names">'
      + SUPPORTERS.map(function (n, i) { return '<span class="sup-name" style="color:' + NEON[i % NEON.length] + ';animation-delay:-' + (i * 0.45).toFixed(2) + 's">' + esc(n) + '</span>'; }).join('')
      + '</div></div>';
  }
  function swapSupport(view) {
    var card = $('#modalCard');
    card.innerHTML = supportHtml(view);
    var h = card.querySelector('h2'); if (h) { h.id = 'mTitle'; card.setAttribute('aria-labelledby', 'mTitle'); }
    var f = card.querySelector('button, a'); if (f) f.focus();
  }

  /* --------------------------------------------------------------- actions */
  var go = function (h) { location.hash = h; };
  var actions = {
    statusGo: function () { go(S.running ? '#/game/' + S.running : '#/system'); },
    goSources: function () { go('#/sources'); },
    goGames: function () { go('#/games'); },
    retryGame: function () { var g = S.g; if (!g) return; g.loaded = false; paintHero(); paintBody(); loadGamePage(g.id); },
    retryState: async function () { var g = S.g; if (!g) return; await loadCheatState(g.id); paintHero(); paintBody(); },
    retryPatches: function () { if (S.g) { S.g.patch = null; loadPatches(); } },
    retrySettings: function () { viewSettings(); },
    repoRetry: function () { var l = S.dlLast || {}; repoStart(l.all, l.patches); },
    logLvl: function (el) {
      logs.min = parseInt(el.getAttribute('data-v'), 10) || 0;
      Array.prototype.forEach.call(document.querySelectorAll('[data-act="logLvl"]'), function (b) { var on = b === el; b.classList.toggle('on', on); b.setAttribute('aria-pressed', on ? 'true' : 'false'); });
      renderLogs();
    },
    logJump: function () { var o = $('#logOut'); if (o) { o.scrollTop = o.scrollHeight; logs.atEnd = true; paintFollow(); } },
    themeSet: function (el) {
      var v = el.getAttribute('data-v'); applyTheme(v); setCfg('theme', v);
      Array.prototype.forEach.call(document.querySelectorAll('[data-act="themeSet"]'), function (b) { var on = b === el; b.classList.toggle('on', on); b.setAttribute('aria-checked', on ? 'true' : 'false'); });
    },
    fxSet: function (el) {
      var v = el.getAttribute('data-v'); store('cr_effects', v); rootCls.toggle('lite', v === 'reduced');
      Array.prototype.forEach.call(document.querySelectorAll('[data-act="fxSet"]'), function (b) { var on = b === el; b.classList.toggle('on', on); b.setAttribute('aria-pressed', on ? 'true' : 'false'); });
    },
    modalYes: function () { closeModal(true); },
    modalNo: function () { closeModal(false); },
    open: function (el) { go('#/game/' + el.getAttribute('data-id')); },
    back: function () { go('#/games'); },
    launch: function (el) { launch(el.getAttribute('data-id')); },
    fav: function (el) {
      var id = el.getAttribute('data-id'); toggleFav(id);
      if (route().page === 'game') paintHero(); else paintGrid();
      var nb = document.querySelector('[data-act="fav"][data-id="' + id + '"]');
      if (nb && S.fav.has(id)) { nb.classList.add('pop'); setTimeout(function () { nb.classList.remove('pop'); }, 450); }
    },
    chip: function (el) { S.filter = el.getAttribute('data-f'); paintGrid(); },
    clearQ: function () { S.q = ''; var i = $('#gq'); if (i) { i.value = ''; i.focus(); } paintGrid(); },
    clearFilters: function () { S.q = ''; S.filter = 'ALL'; var i = $('#gq'); if (i) i.value = ''; paintGrid(); },
    refreshGames: async function (el) {
      var btn = $('#gRefresh') || el;
      if (btn.classList.contains('is-busy')) return;
      btn.classList.add('is-busy'); S.gamesError = false;
      await loadGames(true);
      try { await get('/api/patches/rescan', { retries: 0 }); } catch (e) {}
      btn.classList.remove('is-busy');
    },
    dismissWelcome: function () { store('cr_welcome', '1'); viewGames(); },
    tab: function (el) { go('#/game/' + S.g.id + '/' + el.getAttribute('data-t')); },
    toggle: function (el) { toggleMod(parseInt(el.getAttribute('data-i'), 10), el); },
    disableAll: async function () {
      try { var r = await get('/api/cheats/disable-all?titleId=' + encodeURIComponent(S.g.id), 30000); toast(r && r.disabled ? t('{n} cheat(s) turned off.', { n: r.disabled }) : t('Nothing was on.'), 'success'); }
      catch (e) { toast(e.message || 'Could not disable cheats.', 'error'); }
      await loadCheatState(S.g.id); paintBody();
    },
    saveAuto: async function () {
      var g = S.g, names = [];
      g.doc.mods.forEach(function (m, i) { if (g.rows[i] && STATE_ON[g.rows[i].state] && m.name) names.push(m.name); });
      try { await post('/api/cheats/profile/save?titleId=' + encodeURIComponent(g.id), { mods: names }); g.auto = names; toast(names.length ? t('Autoload saved ({n}). These turn on when the game launches.', { n: names.length }) : t('Autoload saved empty.'), 'success'); paintBody(); }
      catch (e) { toast('Could not save autoload.', 'error'); }
    },
    clearAuto: async function () {
      try { await post('/api/cheats/profile/clear?titleId=' + encodeURIComponent(S.g.id)); S.g.auto = []; toast('Autoload cleared.', 'success'); paintBody(); }
      catch (e) { toast('Could not clear autoload.', 'error'); }
    },
    clearCrash: async function () {
      try { await get('/api/cheats/clear-crash-flags?titleId=' + encodeURIComponent(S.g.id)); }
      catch (e) { toast(e.message || 'Failed.', 'error'); }
      await loadCheatState(S.g.id); paintBody();
    },
    selectFile: async function (el) {
      var wrong = el.getAttribute('data-wrong') === '1';
      if (wrong && !(await confirmBox('Use another version?', 'This file is for a different game version than the one installed.', 'Use anyway', true))) return;
      try {
        await post('/api/cheats/select?titleId=' + encodeURIComponent(S.g.id) + '&path=' + encodeURIComponent(el.getAttribute('data-path')) + (wrong ? '&force=1' : ''));
        S.g.loaded = false; await loadGamePage(S.g.id); paintBody(); toast('Cheat file selected.', 'success');
      } catch (e) { toast(e.message || 'Could not select the file.', 'error'); }
    },
    autoSelect: async function () {
      try { await post('/api/cheats/select/auto?titleId=' + encodeURIComponent(S.g.id)); S.g.loaded = false; await loadGamePage(S.g.id); paintBody(); toast('Auto-select restored.', 'success'); }
      catch (e) { toast(e.message || 'Failed.', 'error'); }
    },
    deleteLocal: async function () {
      var fn = String((S.g.dbg || {}).cheatPath || '').split('/').pop() || t('the cheat file');
      if (!(await confirmBox(t('Delete {file}?', { file: fn }), 'This removes the file from the console. You can download it again from Sources.', 'Delete file', true))) return;
      try { await get('/api/cheats/delete?titleId=' + encodeURIComponent(S.g.id)); toast('Cheat file deleted.', 'warn'); S.g.loaded = false; await loadGames(true); await loadGamePage(S.g.id); paintBody(); }
      catch (e) { toast(e.message || 'Delete failed.', 'error'); }
    },
    pickUpload: function () { var i = document.createElement('input'); i.type = 'file'; i.accept = '.mc4,.shn,.json'; i.onchange = function () { if (i.files[0]) uploadFile(i.files[0]); }; i.click(); },
    patchApply: function (el) { patchCall('apply', parseInt(el.getAttribute('data-i'), 10), false); },
    patchForce: async function (el) {
      var pe = S.g.patch.entries[parseInt(el.getAttribute('data-i'), 10)];
      if (await confirmBox(t('Force apply {name}?', { name: pe ? pe.name : t('patch') }), 'It targets another game version, so it may crash the game. Restore puts the original bytes back.', 'Apply anyway', true)) patchCall('apply', parseInt(el.getAttribute('data-i'), 10), true);
    },
    patchRestore: function (el) { patchCall('restore', parseInt(el.getAttribute('data-i'), 10), false); },
    patchSaveAuto: async function () {
      var ids = (S.g.patch.entries || []).filter(function (e) { return e.applied && e.entryId; }).map(function (e) { return e.entryId; });
      try { await post('/api/patches/profile/save?titleId=' + encodeURIComponent(S.g.id), { entryIds: ids }); S.g.patchAuto = ids; toast('Patch autoload saved.', 'success'); paintPatches(); }
      catch (e) { toast('Could not save autoload.', 'error'); }
    },
    patchClearAuto: async function () {
      try { await post('/api/patches/profile/clear?titleId=' + encodeURIComponent(S.g.id)); S.g.patchAuto = []; toast('Patch autoload cleared.', 'success'); paintPatches(); }
      catch (e) { toast('Could not clear autoload.', 'error'); }
    },
    repoGo: function () { repoStart(false); },
    repoAll: function () { repoStart(true); },
    patchRepoGo: function () { repoStart(false, true); },
    copyUrl: function (el) { copyText(S.browserUrl || location.origin + '/', 'URL copied.', el); },
    copyLogs: async function (el) {
      var text = logs.list.map(logLine).join('\n');
      try { var d = await get('/api/logs/export'); text = (d.logs || []).map(logLine).join('\n') || text; } catch (e) {}
      copyText(text, 'Logs copied.', el);
    },
    shutdown: async function () {
      if (!(await confirmBox('Shut down CheatRunner?', 'Send a new CheatRunner.elf from your PC afterwards.', 'Shut down', true))) return;
      try { await post('/api/dev/shutdown'); } catch (e) {}
      toast('Waiting for CheatRunner to come back...', 'info');
      for (;;) { await sleep(2000); try { await fetch('/api/state?ts=' + Date.now(), { cache: 'no-store' }).then(function (r) { if (r.ok) location.reload(); }); } catch (e) {} }
    },
    cfgSwitch: async function (el) {
      var k = el.getAttribute('data-k'), nv = el.classList.contains('on') ? 0 : 1;
      if (await setCfg(k, nv)) { el.classList.toggle('on', !!nv); el.setAttribute('aria-checked', nv ? 'true' : 'false'); }
    },
    resetCfg: async function () {
      if (!(await confirmBox('Reset settings?', 'All settings go back to their defaults. The web port is kept.', 'Reset', true))) return;
      try { await post('/api/config/reset'); toast('Settings reset.', 'success'); viewSettings(); } catch (e) { toast('Reset failed.', 'error'); }
    },
    reportIssue: async function () {
      try { var d = await get('/api/dev/diag'); await copyText(JSON.stringify(d, null, 2), 'Diagnostics copied. Paste them into the issue.'); } catch (e) { toast('Could not copy diagnostics.', 'warn'); }
      window.open('https://github.com/notmaj0r/CheatRunner/issues', '_blank');
    },
    support: function () { openModal(supportHtml('main'), null, 'support'); },
    gridMore: function () { growGrid(); },
    modMore: function () { if (S.g) { S.g.modShown = (S.g.modShown || MOD_CHUNK) + MOD_CHUNK; paintMods(); } },
    patchMore: function () { if (S.g) { S.g.patchShown = (S.g.patchShown || PATCH_CHUNK) + PATCH_CHUNK; paintPatches(); } },
    langPick: function () { openModal(langHtml(), null, 'lang'); },
    langSet: function (el) { CRI.pick(el.getAttribute('data-id')); },
    qr: function () { if (typeof qrcode === 'undefined') { toast('QR code is not available.', 'warn'); return; } openModal(qrHtml(), null, 'qr'); },
    cmdk: function () { openPalette(); },
    palGo: function (el) { palChoose(parseInt(el.getAttribute('data-i'), 10)); },
    railToggle: function () { setRail(document.documentElement.getAttribute('data-rail') !== 'min'); },
    supportCrypto: function () { swapSupport('crypto'); },
    supportBack: function () { swapSupport('main'); },
    copyAddr: function (el) { copyText(el.getAttribute('data-a'), t('{label} address copied', { label: el.getAttribute('data-l') })); }
  };
  $('#supportBtn').setAttribute('data-act', 'support');
  $('#langFlag').src = CRI.flag(CRI.lang);

  document.addEventListener('click', function (e) {
    if (e.target === $('#modal')) { closeModal(false); return; }
    var t = e.target.closest ? e.target.closest('[data-act]') : null;
    if (!t || t.disabled) return;
    var fn = actions[t.getAttribute('data-act')];
    if (!fn) return;
    if (t.tagName !== 'A') e.preventDefault();
    if (t.getAttribute('data-act') === 'fav' || t.getAttribute('data-act') === 'launch') e.stopPropagation();
    fn(t, e);
  });
  document.addEventListener('input', function (e) {
    var k = e.target.getAttribute && e.target.getAttribute('data-in');
    if (k === 'q') { S.q = e.target.value || ''; clearTimeout(S.qTimer); S.qTimer = setTimeout(paintGrid, isPS5 ? 200 : 60); }
    else if (k === 'modq') { S.g.filter = e.target.value || ''; paintMods(); }
    else if (k === 'palq') { pal.sel = 0; palRender(e.target.value || ''); }
    else if (k === 'logq') { logs.q = e.target.value || ''; renderLogs(); }
  });
  document.addEventListener('change', function (e) {
    var k = e.target.getAttribute && e.target.getAttribute('data-change');
    if (!k) return;
    var v = e.target.value;
    if (k === 'sort') { S.sort = v; paintGrid(); }
    else if (k === 'cfgTheme') { applyTheme(v); setCfg('theme', v); }
    else if (k === 'cfgEffects') { store('cr_effects', v); rootCls.toggle('lite', v === 'reduced'); }
    else if (k === 'cfgLog') setCfg('log_level', v);
    else if (k === 'logLevel') { logs.min = parseInt(v, 10) || 0; renderLogs(); }
    else if (k === 'cfgNum') { var n = parseInt(v, 10); if (!isNaN(n)) setCfg(e.target.getAttribute('data-k'), n); }
    else if (k === 'cfgHotkeyBtn') {
      setCfg(e.target.getAttribute('data-k'), v).then(function (ok) { if (!ok) viewSettings(); });
    }
  });
  document.addEventListener('keydown', function (e) {
    var modalOpen = !$('#modal').hidden;
    if ((e.ctrlKey || e.metaKey) && (e.key === 'k' || e.key === 'K')) { e.preventDefault(); if (pal.on) closeModal(false); else openPalette(); return; }
    if (pal.on && modalOpen) {
      if (e.key === 'ArrowDown') { e.preventDefault(); palMove(1); return; }
      if (e.key === 'ArrowUp') { e.preventDefault(); palMove(-1); return; }
      if (e.key === 'Enter') { e.preventDefault(); palChoose(pal.sel); return; }
    }
    var t = e.target, typing = t && (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA');
    if (e.key === 'Escape') {
      if (modalOpen) { closeModal(false); return; }
      if (typing && t.getAttribute('data-in') === 'q' && S.q) { actions.clearQ(); return; }
    }
    if (modalOpen && e.key === 'Tab') {
      var f = $('#modalCard').querySelectorAll('button:not([disabled]), input, a[href]');
      if (!f.length) return;
      var first = f[0], last = f[f.length - 1];
      if (e.shiftKey && t === first) { e.preventDefault(); last.focus(); }
      else if (!e.shiftKey && t === last) { e.preventDefault(); first.focus(); }
      return;
    }
    if (modalOpen && e.key === 'Enter' && t.tagName === 'INPUT') { var ok = $('#modalCard .btn:not(.ghost)'); if (ok) { e.preventDefault(); ok.click(); } return; }
    if (e.key === 'Enter' && t && t.getAttribute && t.getAttribute('role') !== 'switch' && /(^| )(tile|now)( |$)/.test(t.className || '') && t.getAttribute('data-act') === 'open') { actions.open(t); return; }
    if (e.key === '/' && !typing && !modalOpen && $('#gq')) { e.preventDefault(); $('#gq').focus(); }
  });
  document.addEventListener('dragover', function (e) { var z = e.target.closest && e.target.closest('#dropZone'); if (z) { e.preventDefault(); z.classList.add('over'); } });
  document.addEventListener('dragleave', function (e) { var z = e.target.closest && e.target.closest('#dropZone'); if (z) z.classList.remove('over'); });
  document.addEventListener('drop', function (e) {
    var z = e.target.closest && e.target.closest('#dropZone'); if (!z) return;
    e.preventDefault(); z.classList.remove('over');
    if (e.dataTransfer && e.dataTransfer.files[0]) uploadFile(e.dataTransfer.files[0]);
  });

  /* -------------------------------------------------------------- focus keep */
  // The PS5 D-pad only starts moving once something has focus, so give each page a starting control.
  function padStart(el) {
    var ae = document.activeElement;
    if (!isPS5 || !el || (ae && ae !== document.body)) return;
    try { el.focus({ preventScroll: true }); } catch (e) { el.focus(); }
  }
  document.addEventListener('focusin', function (e) {
    var el = e.target;
    if (S.g && el.classList && el.classList.contains('switch') && el.getAttribute('data-act') === 'toggle') S.g.lastSw = el.getAttribute('data-i');
  });
  // Keeps the focused control focused when a list repaints under it.
  function focusSig(el) {
    if (!el || el === document.body) return '';
    var sig = el.tagName.toLowerCase();
    ['data-act', 'data-id', 'data-i', 'data-k', 'data-t', 'data-f'].forEach(function (a) { if (el.hasAttribute(a)) sig += '[' + a + '="' + el.getAttribute(a) + '"]'; });
    return sig;
  }
  function withFocus(box, fn) {
    var ae = document.activeElement, sig = ae && box.contains(ae) ? focusSig(ae) : '';
    fn();
    if (!sig) return;
    var n = null; try { n = box.querySelector(sig); } catch (e) {}
    if (n && n !== document.activeElement) try { n.focus({ preventScroll: true }); } catch (e2) { n.focus(); }
  }

  /* ---------------------------------------------------------------- startup */
  (async function start() {
    var cr = $('.rail-credit'); if (cr) cr.innerHTML = t('Made with {heart} by maj0r', { heart: '<span aria-hidden="true">&#10084;&#65039;</span>' });
    CRI.start();
    $('#cmdKbd').textContent = /Mac|iPhone|iPad/.test(navigator.platform || '') ? '\u2318K' : 'Ctrl K';
    setRail(document.documentElement.getAttribute('data-rail') === 'min');
    render();
    try { var c = await get('/api/config', { retries: 0 }); S.cfg = c; applyTheme(c.theme); } catch (e) {}
    await Promise.all([loadFavs(), refreshState(), refreshStatus()]);
    checkUpdate();
    watchDownload();
    await loadGames(false);
    if (route().page === 'games') viewGames();
    poller(refreshStatus, 7000);
    poller(refreshLaunch, 4000);
    poller(refreshState, 10000);
    poller(refreshLogs, 6000, function () { return route().page === 'system'; });
    poller(async function () {
      var g = S.g; if (!g || route().page !== 'game' || g.tab !== 'cheats' || !g.loaded || Object.keys(g.applying).length) return;
      await loadCheatState(g.id);
      if (S.g === g && route().page === 'game' && g.tab === 'cheats') paintMods();
    }, 6000);
  })();
})();
