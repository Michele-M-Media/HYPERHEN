/* Translations. English text is the key; the page is translated in place and a language change reloads. */
var CRI = (function () {
  var LANGS = [
    ['en-US', 'English', 0], ['ar-SA', 'العربية', 1], ['zh-CN', '简体中文', 0], ['fr-FR', 'Français', 0],
    ['de-DE', 'Deutsch', 0], ['it-IT', 'Italiano', 0], ['ja-JP', '日本語', 0], ['ko-KR', '한국어', 0],
    ['fa-IR', 'فارسی', 1], ['pl-PL', 'Polski', 0], ['pt-BR', 'Português (Brasil)', 0], ['ru-RU', 'Русский', 0],
    ['es-ES', 'Español', 0], ['th-TH', 'ไทย', 0], ['tr-TR', 'Türkçe', 0], ['uk-UA', 'Українська', 0]
  ];
  var STORE = 'cr_lang';
  var SKIP = 'script, style, textarea, .console, .mono, .tile-name, .t-info h1, .live-txt b, .sup-name';
  var WS = /^([\s ]*)([\s\S]*?)([\s ]*)$/;

  function find(id) { for (var i = 0; i < LANGS.length; i++) if (LANGS[i][0] === id) return LANGS[i]; return null; }
  function saved() { try { return localStorage.getItem(STORE) || ''; } catch (e) { return ''; } }
  function detect() {
    var n = String(navigator.language || 'en').toLowerCase(), base = n.split('-')[0];
    for (var i = 0; i < LANGS.length; i++) if (LANGS[i][0].toLowerCase() === n) return LANGS[i][0];
    for (var j = 0; j < LANGS.length; j++) if (LANGS[j][0].split('-')[0] === base) return LANGS[j][0];
    return 'en-US';
  }
  var lang = find(saved()) ? saved() : detect();
  var data = (typeof CR_I18N !== 'undefined' && CR_I18N.tr[lang]) || null;
  var map = null;
  if (data) {
    map = {};
    for (var i = 0; i < CR_I18N.keys.length; i++) if (data[i]) map[CR_I18N.keys[i]] = data[i];
  }

  function t(key, vars) {
    var s = (map && map[key]) || key;
    return vars ? s.replace(/\{(\w+)\}/g, function (m, k) { return vars[k] != null ? vars[k] : m; }) : s;
  }

  function tr(str) {
    var m = WS.exec(str), v = m[2] && map[m[2]];
    return v ? m[1] + v + m[3] : null;
  }
  function skipped(el) { return !!(el && el.closest && el.closest(SKIP)); }
  function textNodes(root, fn) {
    var w = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null, false), n;
    while ((n = w.nextNode())) fn(n);
  }
  function doText(n) {
    if (n.__t === n.nodeValue || skipped(n.parentNode)) return;
    var v = tr(n.nodeValue);
    if (v !== null && v !== n.nodeValue) { n.nodeValue = v; n.__t = v; }
  }
  var ATTRS = ['title', 'aria-label', 'placeholder'];
  function doAttrs(el) {
    if (skipped(el)) return;
    for (var i = 0; i < ATTRS.length; i++) {
      var cur = el.getAttribute(ATTRS[i]);
      if (cur) { var v = tr(cur); if (v !== null && v !== cur) el.setAttribute(ATTRS[i], v); }
    }
  }
  function translate(root) {
    if (!map) return;
    if (root.nodeType === 3) { doText(root); return; }
    if (root.nodeType !== 1) return;
    textNodes(root, doText);
    if (root.hasAttribute && (root.hasAttribute('title') || root.hasAttribute('aria-label') || root.hasAttribute('placeholder'))) doAttrs(root);
    var els = root.querySelectorAll('[title], [aria-label], [placeholder]');
    for (var i = 0; i < els.length; i++) doAttrs(els[i]);
  }

  var obs = null;

  function start() {
    var root = document.documentElement;
    root.lang = lang;
    root.dir = find(lang)[2] ? 'rtl' : 'ltr';
    if (map) {
      translate(document.body);
      document.title = tr(document.title) || document.title;
      if (window.MutationObserver) {
        // Runs as a microtask, so changes are translated before the next paint.
        obs = new MutationObserver(function (recs) {
          for (var i = 0; i < recs.length; i++) {
            var r = recs[i];
            if (r.type === 'childList') { for (var j = 0; j < r.addedNodes.length; j++) translate(r.addedNodes[j]); }
            else translate(r.target);
          }
          obs.takeRecords();
        });
        obs.observe(document.body, { childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: ATTRS });
      }
    }
    root.classList.remove('i18n-wait');
  }

  function flag(id) {
    var svg = typeof CR_FLAGS !== 'undefined' && CR_FLAGS[id];
    return svg ? 'data:image/svg+xml,' + encodeURIComponent(svg) : '';
  }
  function pick(id) {
    if (!find(id)) return;
    try { localStorage.setItem(STORE, id); } catch (e) {}
    location.reload();
  }

  return { t: t, start: start, langs: LANGS, lang: lang, flag: flag, pick: pick };
})();
