let cwd = "/";
let entries = [];
let selected = new Set();
let focusedPath = null;
let trackedTask = null;
let busy = false;
let clipboard = null;
let pendingOverlayText = "";
let pendingOverlayLabel = "";
let pendingAbortController = null;
let taskRefreshPath = "/";
let hoverPaused = false;
let hoverResumeTimer = 0;
let hoveredRow = null;
let loadingPath = null;
let directoryLoadingTimer = 0;
let directoryLoadingStartedAt = 0;
let taskOverlayTimer = 0;
let lastCompletionId = null;
let taskPollFailedAlertShown = false;
let taskPollPromise = null;
let localActionBusy = false;
let modalBackgroundLocked = false;
let specialStoragePaths = {};
let hasSpecialMntStorage = false;
let textEditorPath = null;
let textEditorVersion = null;
let textEditorOriginal = "";
let textEditorBusy = false;
let permissionItems = [];
let permissionOriginal = "";
let permissionBusy = false;
let extractItems = [];
let pkgInfoItem = null;
let pkgInfoRequestId = 0;
let downloadFrame = null;
let downloadQueue = [];
let downloadAck = 0;
let uploadXhr = null;
let uploadTerminalAbort = false;
let L = {};

const APP_VERSION = "v1.11";
const SMB_PASSWORD_KEY = "ps5-web-file-mgr:smb-password:";
const smbPasswords = {};
const smbReady = {};

const LAST_PATH_KEY = "ps5-web-file-mgr:last-path";
const SORT_KEY = "ps5-web-file-mgr:list-sort";
const ARCHIVE_PASSWORD_KEY = "ps5-web-file-mgr:archive-password";
const LOADING_DISPLAY_DELAY = 250;
const TASK_OVERLAY_DISPLAY_DELAY = 1200;
const SELECT_ALL_LOADING_THRESHOLD = 1000;
const EXTRACT_NAME_LIMIT = 48;
const ARCHIVE_FILE_RE = /\.(7z|001|zip|zipx|rar|arj|bz2|bzip2|tbz2?|cab|gz|gzip|tgz|tpz|lzh|lha|tar|xz|txz|z|taz|zst|tzst|xar|xip|cpio|lzma|pmd)$/i;
const ARCHIVE_COMPOUND_SUFFIX_RE = /\.(tar\.(?:gz|bz2|xz|zst)|tbz2?|tgz|tpz|txz|taz|tzst)$/i;
const ARCHIVE_SIMPLE_SUFFIX_RE = /\.(7z|zipx?|rar|arj|bz2|bzip2|cab|gz|gzip|lzh|lha|tar|xz|z|zst|xar|xip|cpio|lzma|pmd)$/i;
const SORT_KEYS = { name: true, type: true, size: true, mtime: true, mode: true };

let sortKey = "";
let sortDir = "none";
let nameFilter = "";

const filesEl = document.getElementById("files");
const contentEl = document.getElementById("content");
const contentLoadingEl = document.getElementById("contentLoading");
const contentLoadingTextEl = contentLoadingEl.querySelector(".content-loading-text");
const emptyEl = document.getElementById("empty");
const pathEl = document.getElementById("path");
const spaceInfoEl = document.getElementById("spaceInfo");
const manageSmbBtn = document.getElementById("manageSmbBtn");
const smbManagerOverlayEl = document.getElementById("smbManagerOverlay");
const smbConnectionListEl = document.getElementById("smbConnectionList");
const smbManagerAddBtn = document.getElementById("smbManagerAddBtn");
const smbManagerCloseBtn = document.getElementById("smbManagerCloseBtn");
const smbSharePickerEl = document.getElementById("smbSharePicker");
const smbShareSelectEl = document.getElementById("smbShareSelect");
const smbManagerActionsEl = document.getElementById("smbManagerActions");
let smbShareChoice = null;
let smbConnections = [];
const statusEl = document.getElementById("statusText");
const versionEl = document.getElementById("versionText");
const nameFilterBtn = document.getElementById("nameFilterBtn");
const nameFilterTextEl = document.getElementById("nameFilterText");
const tasksEl = document.getElementById("tasks");
const overlayEl = document.getElementById("taskOverlay");
const selectAllEl = document.getElementById("selectAll");
const parentBtn = document.getElementById("parentBtn");
const pasteBtn = document.getElementById("pasteBtn");
const pasteVerbEl = document.getElementById("pasteVerb");
const pasteNameEl = document.getElementById("pasteName");
const pasteCountEl = document.getElementById("pasteCount");
const pasteTargetTextEl = document.getElementById("pasteTargetText");
const installPkgBtn = document.getElementById("installPkgBtn");
const extractBtn = document.getElementById("extractBtn");
const clearClipboardBtn = document.getElementById("clearClipboardBtn");
const downloadBtn = document.getElementById("downloadBtn");
const uploadMenuEl = document.getElementById("uploadMenu");
const uploadBtn = document.getElementById("uploadBtn");
const uploadMenuBtn = document.getElementById("uploadMenuBtn");
const uploadFolderBtn = document.getElementById("uploadFolderBtn");
const uploadFilesEl = document.getElementById("uploadFiles");
const uploadFolderEl = document.getElementById("uploadFolder");
const dropUploadOverlayEl = document.getElementById("dropUploadOverlay");
const initLoadingEl = document.getElementById("initLoading");
const exitBtn = document.getElementById("exitBtn");
const textEditorOverlayEl = document.getElementById("textEditorOverlay");
const textEditorPathEl = document.getElementById("textEditorPath");
const textEditorEl = document.getElementById("textEditor");
const textEditorStatusEl = document.getElementById("textEditorStatus");
const textEditorCloseBtn = document.getElementById("textEditorCloseBtn");
const textEditorSaveBtn = document.getElementById("textEditorSaveBtn");
const newTextBtn = document.getElementById("newTextBtn");
const imagePreviewOverlayEl = document.getElementById("imagePreviewOverlay");
const imagePreviewNameEl = document.getElementById("imagePreviewName");
const imagePreviewEl = document.getElementById("imagePreview");
const imagePreviewCloseBtn = document.getElementById("imagePreviewCloseBtn");
const pkgInfoOverlayEl = document.getElementById("pkgInfoOverlay");
const pkgInfoImageEl = document.getElementById("pkgInfoImage");
const pkgInfoTitleEl = document.getElementById("pkgInfoTitle");
const pkgInfoFieldsEl = document.getElementById("pkgInfoFields");
const pkgInfoCloseBtn = document.getElementById("pkgInfoCloseBtn");
const pkgInfoInstallBtn = document.getElementById("pkgInfoInstallBtn");
const permissionOverlayEl = document.getElementById("permissionOverlay");
const permissionPathEl = document.getElementById("permissionPath");
const permissionModeEl = document.getElementById("permissionMode");
const permissionRecursiveOptionEl = document.getElementById("permissionRecursiveOption");
const permissionRecursiveEl = document.getElementById("permissionRecursive");
const permissionCancelBtn = document.getElementById("permissionCancelBtn");
const permissionApplyBtn = document.getElementById("permissionApplyBtn");
const extractOverlayEl = document.getElementById("extractOverlay");
const extractPathEl = document.getElementById("extractPath");
const extractSeparateEl = document.getElementById("extractSeparate");
const extractSeparateTextEl = document.getElementById("extractSeparateText");
const extractCurrentEl = document.getElementById("extractCurrent");
const extractCustomEl = document.getElementById("extractCustom");
const extractCustomTextEl = document.getElementById("extractCustomText");
const extractCustomPathEl = document.getElementById("extractCustomPath");
const extractCancelBtn = document.getElementById("extractCancelBtn");
const extractApplyBtn = document.getElementById("extractApplyBtn");
const permissionChecks = [
  document.getElementById("permissionOwnerRead"),
  document.getElementById("permissionOwnerWrite"),
  document.getElementById("permissionOwnerExecute"),
  document.getElementById("permissionGroupRead"),
  document.getElementById("permissionGroupWrite"),
  document.getElementById("permissionGroupExecute"),
  document.getElementById("permissionOtherRead"),
  document.getElementById("permissionOtherWrite"),
  document.getElementById("permissionOtherExecute")
];

function chooseLanguage() {
  const langs = navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language || ""];
  const lang = String(langs[0] || "").toLowerCase();
  return lang.indexOf("zh") === 0 ? "zh" : "en";
}

function loadLanguage() {
  return new Promise(resolve => {
    const lang = chooseLanguage();
    initLoadingEl.textContent = lang === "zh" ? "加载中..." : "Loading...";
    const script = document.createElement("script");
    script.src = "/lang-" + lang + ".js";
    script.onload = () => {
      L = window.WFM_LANG || {};
      document.documentElement.lang = lang === "zh" ? "zh-CN" : "en";
      resolve();
    };
    script.onerror = () => {
      L = {};
      resolve();
    };
    document.head.appendChild(script);
  });
}

function t(key, params) {
  let text = L[key] || key;
  for (const name in params || {}) {
    text = text.replace(new RegExp("\\{" + name + "\\}", "g"), params[name]);
  }
  return text;
}

function backendErrorText(code, arg, fallback) {
  if (!code) return fallback || t("backendError");
  const params = { path: arg || "", arg: arg || "" };
  if (code === "no_space") {
    const parts = String(arg || "").split(",");
    params.required = formatBytes(parts[0] || 0, false);
    params.available = formatBytes(parts[1] || 0, false);
  }
  const key = "err_" + code;
  return L[key] ? t(key, params) : fallback || t("backendError");
}

function updateNameFilterButton() {
  nameFilterTextEl.textContent = nameFilter;
  nameFilterTextEl.title = nameFilter;
  nameFilterTextEl.hidden = !nameFilter;
  nameFilterBtn.classList.toggle("active", Boolean(nameFilter));
  const label = t(nameFilter ? "clearNameFilter" : "filterName");
  nameFilterBtn.title = label;
  nameFilterBtn.setAttribute("aria-label", label);
}

function applyStaticText() {
  document.title = t("appTitle");
  for (const el of document.querySelectorAll("[data-i18n]")) {
    el.textContent = t(el.dataset.i18n);
  }
  if (isPlayStationBrowser()) {
    for (const el of document.querySelectorAll(".remote-only")) el.hidden = true;
  }
  exitBtn.title = t("exit");
  exitBtn.setAttribute("aria-label", t("exit"));
  parentBtn.title = t("parent");
  parentBtn.setAttribute("aria-label", t("parent"));
  versionEl.textContent = APP_VERSION;
  updateNameFilterButton();
}

function nextPaint() {
  return new Promise(resolve => {
    if (window.requestAnimationFrame) {
      requestAnimationFrame(() => requestAnimationFrame(() => resolve()));
    }
    else setTimeout(resolve, 0);
  });
}

function delay(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function showContentLoading(text) {
  contentLoadingTextEl.textContent = text || t("readDir");
  contentLoadingEl.style.top = contentEl.scrollTop + "px";
  contentLoadingEl.hidden = false;
  contentEl.classList.add("loading");
  updateButtons();
}

function startContentLoadingTimer() {
  if (!contentLoadingEl.hidden || directoryLoadingTimer) return;
  directoryLoadingStartedAt = Date.now();
  directoryLoadingTimer = setTimeout(() => {
    directoryLoadingTimer = 0;
    showContentLoading();
  }, LOADING_DISPLAY_DELAY);
}

async function showContentLoadingAfterDelay() {
  if (!contentLoadingEl.hidden) return;
  const elapsed = directoryLoadingStartedAt ? Date.now() - directoryLoadingStartedAt : 0;
  const remaining = LOADING_DISPLAY_DELAY - elapsed;
  if (remaining > 0) await delay(remaining);
  clearTimeout(directoryLoadingTimer);
  directoryLoadingTimer = 0;
  showContentLoading();
}

function hideContentLoading() {
  clearTimeout(directoryLoadingTimer);
  directoryLoadingTimer = 0;
  directoryLoadingStartedAt = 0;
  contentEl.classList.toggle("loading", modalBackgroundLocked);
  contentLoadingEl.hidden = true;
  updateButtons();
}

async function request(path, params, options) {
  const qs = new URLSearchParams(params || {});
  const fetchOptions = Object.assign({ method: "POST" }, options || {});
  if (path.indexOf("/api/smb/") !== 0 && path !== "/api/space") {
    await ensureSmbPaths(params);
    if (fetchOptions.body && fetchOptions.headers &&
        fetchOptions.headers["Content-Type"] === "application/x-www-form-urlencoded") {
      const values = {};
      new URLSearchParams(fetchOptions.body).forEach((value, key) => { values[key] = value; });
      await ensureSmbPaths(values);
    }
  }
  const response = await fetch(path + (qs.toString() ? "?" + qs.toString() : ""), fetchOptions);
  if (!response.ok) {
    const data = await response.json();
    const error = new Error(backendErrorText(data.error_code, data.error_arg, data.error));
    error.code = data.error_code;
    // Recheck browser credentials on the next operation after a backend restart/error.
    if (path.indexOf("/api/smb/") !== 0 && path !== "/api/space") {
      for (const root of Object.keys(smbReady)) delete smbReady[root];
    }
    throw error;
  }
  return response;
}

async function api(path, params, options) {
  const data = await (await request(path, params, options)).json();
  if (!data.ok) throw new Error(backendErrorText(data.error_code, data.error_arg, data.error));
  return data;
}

async function apiForm(path, params, options) {
  const fetchOptions = Object.assign({
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(params || {}).toString()
  }, options || {});
  const data = await (await request(path, null, fetchOptions)).json();
  if (!data.ok) throw new Error(backendErrorText(data.error_code, data.error_arg, data.error));
  return data;
}

function decodeFsText(text) {
  text = String(text || "");
  if (typeof TextDecoder === "undefined" || typeof Uint8Array === "undefined") return text;

  let out = "";
  let bytes = [];
  let raw = "";
  let changed = false;

  function flushBytes() {
    if (!bytes.length) return;
    const decoded = decodeFsBytes(bytes, raw);
    if (decoded !== raw) changed = true;
    out += decoded;
    bytes = [];
    raw = "";
  }

  for (let i = 0; i < text.length; i++) {
    const code = text.charCodeAt(i);
    if (code >= 0x80 && code <= 0xff) {
      bytes.push(code & 0xff);
      raw += text.charAt(i);
      continue;
    }
    flushBytes();
    out += text.charAt(i);
  }
  flushBytes();
  return changed ? out : text;
}

function decodeFsBytes(bytes, fallback) {
  const data = new Uint8Array(bytes);
  if (isUtf8Bytes(data)) {
    try {
      return new TextDecoder("utf-8").decode(data);
    } catch (err) {
      return fallback;
    }
  }
  try {
    return new TextDecoder("gbk").decode(data);
  } catch (err) {
    try {
      return new TextDecoder("gb18030").decode(data);
    } catch (err2) {
      return fallback;
    }
  }
}

function isUtf8Bytes(bytes) {
  for (let i = 0; i < bytes.length;) {
    const c = bytes[i];
    let n = 0;
    let cp = 0;

    if (c < 0x80) {
      i++;
      continue;
    }
    if ((c & 0xe0) === 0xc0) {
      n = 2;
      cp = c & 0x1f;
    } else if ((c & 0xf0) === 0xe0) {
      n = 3;
      cp = c & 0x0f;
    } else if ((c & 0xf8) === 0xf0) {
      n = 4;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + n > bytes.length) return false;
    for (let j = 1; j < n; j++) {
      const t = bytes[i + j];
      if ((t & 0xc0) !== 0x80) return false;
      cp = (cp << 6) | (t & 0x3f);
    }
    if ((n === 2 && cp < 0x80) ||
        (n === 3 && cp < 0x800) ||
        (n === 4 && (cp < 0x10000 || cp > 0x10ffff)) ||
        (cp >= 0xd800 && cp <= 0xdfff)) return false;
    i += n;
  }
  return true;
}

function displayName(item) {
  return item.displayName || decodeFsText(item.name);
}

function displayPath(path) {
  return decodeFsText(path);
}

function renderEndOfPath(element, path) {
  element.textContent = path;
  if (element.scrollWidth <= element.clientWidth) return;

  let low = 0;
  let high = path.length;
  while (low < high) {
    const count = Math.ceil((low + high) / 2);
    element.textContent = "..." + path.slice(path.length - count);
    if (element.scrollWidth <= element.clientWidth) low = count;
    else high = count - 1;
  }
  element.textContent = "..." + path.slice(path.length - low);
}

function renderAddressPath() {
  const path = displayPath(cwd);
  pathEl.title = path;
  renderEndOfPath(pathEl, path);
}

async function fetchText(path, params) {
  const response = await request(path, params, { method: "GET" });
  return {
    text: await response.text(),
    version: response.headers.get("X-Text-Version") || ""
  };
}

function formatSize(size, type) {
  if (type === "d") return "";
  return formatBytes(size, true);
}

function formatBytes(size, decimals) {
  const units = ["B", "KB", "MB", "GB", "TB"];
  let value = Number(size || 0);
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  if (!unit) return value + " B";
  return (decimals ? value.toFixed(2) : Math.round(value)) + " " + units[unit];
}

function formatTime(seconds) {
  if (!seconds) return "";
  const date = new Date(seconds * 1000);
  const year = date.getFullYear();
  const month = date.getMonth() + 1;
  const day = date.getDate();
  const time = [date.getHours(), date.getMinutes(), date.getSeconds()]
    .map(value => value < 10 ? "0" + value : value).join(":");
  return document.documentElement.lang === "en" ?
    month + "/" + day + "/" + year + " " + time :
    year + "/" + month + "/" + day + " " + time;
}

function formatSpeed(bytes) {
  return formatSize(bytes) + "/s";
}

function formatDuration(seconds) {
  if (!isFinite(seconds) || seconds < 0) return "--";
  seconds = Math.ceil(seconds);
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const secs = seconds % 60;
  if (hours) return t("durationHours", { hours, minutes });
  if (minutes) return t("durationMinutes", { minutes, seconds: secs });
  return t("durationSeconds", { seconds: secs });
}

function averageEta(task, done, total) {
  if (!total || !done || done >= total) return "--";
  const eta = Number(task.eta || 0);
  return eta > 0 ? formatDuration(eta) : "--";
}

function taskElapsed(task) {
  if (!task || !Object.prototype.hasOwnProperty.call(task, "total_elapsed")) return "--";
  return formatDuration(Number(task.total_elapsed || 0));
}

function opLabel(op) {
  return { copy: t("copy"), move: t("move"), delete: t("delete"), chmod: t("permissionsTitle"), download: t("download"), upload: t("upload"), extract: t("extract"), pkg_install: t("installPackage") }[op] || op;
}

function taskOpLabel(op) {
  return { copy: t("copying"), move: t("moving"), delete: t("deleting"), chmod: t("changingPermissions"), download: t("downloading"), upload: t("uploading"), extract: t("extracting"), pkg_install: t("installPackage") }[op] || op;
}

function isPlayStationBrowser() {
  return /PlayStation/i.test(navigator.userAgent || "");
}

function stateLabel(state) {
  return { queued: t("queued"), running: t("running"), done: t("done"), failed: t("failed"), canceled: t("canceled") }[state] || state;
}

function isTerminalTask(task) {
  return ["done", "failed", "canceled"].includes(task.state);
}

function trackTask(id, op, fromClipboard, clearClipboardOnDone) {
  trackedTask = {
    id,
    op,
    fromClipboard: Boolean(fromClipboard),
    clearClipboardOnDone: Boolean(clearClipboardOnDone),
    cancelRequested: false,
    state: "queued"
  };
}

function requestTaskCancel(id) {
  if (trackedTask && trackedTask.id === id) {
    trackedTask.cancelRequested = true;
    if (trackedTask.op === "download") downloadQueue = [];
  }
}

function clearTrackedTask() {
  if (trackedTask && trackedTask.op === "download" && downloadFrame) {
    downloadFrame.parentNode.removeChild(downloadFrame);
    downloadFrame = null;
  }
  if (trackedTask && trackedTask.op === "upload" && uploadXhr) {
    uploadTerminalAbort = true;
    uploadXhr.abort();
  }
  trackedTask = null;
}

function clearClipboardAfterPaste() {
  clipboard = null;
  renderClipboard();
}

function taskFailureMessage(task) {
  return t("taskFailed", {
    label: opLabel(task.op),
    error: backendErrorText(task.error_code, task.error_arg, task.error || task.current)
  });
}

function handleTerminalTask(task) {
  if (task.op === "extract" && task.state === "failed" &&
      task.error_code === "archive_overwrite_required" && trackedTask &&
      trackedTask.id === task.id && trackedTask.extractRequest) {
    const request = trackedTask.extractRequest;
    const password = trackedTask.extractPassword;
    const passwordPrompted = trackedTask.extractPasswordPrompted;
    clearTrackedTask();
    if (confirm(t("extractOverwriteConfirm", {
      names: task.error || ""
    }))) {
      startExtractRequest(request, password, true, passwordPrompted);
      return true;
    }
    return false;
  }
  if (task.op === "extract" && task.state === "failed" &&
      task.error_code === "archive_password_required" && trackedTask &&
      trackedTask.id === task.id && trackedTask.extractRequest &&
      !trackedTask.extractPasswordPrompted) {
    const request = trackedTask.extractRequest;
    const overwrite = trackedTask.extractOverwrite;
    clearTrackedTask();
    const password = prompt(t("archivePasswordPrompt"),
      readSavedArchivePassword());
    if (password !== null && password.length) {
      saveArchivePassword(password);
      startExtractRequest(request, password, overwrite, true);
      return true;
    }
    return false;
  }
  if (task.op === "pkg_install") {
    if (task.state === "failed") {
      const message = taskFailureMessage(task);
      setStatus(message);
      alert(message);
    }
    return false;
  }
  if (task.state === "failed") {
    if (task.op === "download") downloadQueue = [];
    clearTrackedTask();
    const message = taskFailureMessage(task);
    setStatus(message);
    alert(message);
    return false;
  }
  if (task.state === "canceled") {
    if (task.op === "download") downloadQueue = [];
    clearTrackedTask();
    setStatus(t("taskCanceled", { label: opLabel(task.op) }));
    return false;
  }
  if (task.state === "done") {
    if (trackedTask && trackedTask.id === task.id &&
        (trackedTask.fromClipboard || trackedTask.clearClipboardOnDone)) clearClipboardAfterPaste();
    clearTrackedTask();
    setStatus(t("actionDone", { label: opLabel(task.op) }));
  }
  return false;
}

function setStatus(text) {
  statusEl.textContent = text;
}

function setBusy(value) {
  busy = value;
  updateButtons();
}

function setModalBackgroundLocked(value) {
  modalBackgroundLocked = value;
  contentEl.classList.toggle("loading", value || !contentLoadingEl.hidden);
  if (value) clearHoverPath();
  updateButtons();
}

function backgroundInteractionLocked() {
  return Boolean(modalBackgroundLocked || busy || loadingPath ||
    contentEl.classList.contains("loading"));
}

function showTaskOverlay() {
  setBusy(true);
  if (!overlayEl.hidden || taskOverlayTimer) return;
  taskOverlayTimer = setTimeout(() => {
    taskOverlayTimer = 0;
    overlayEl.hidden = false;
    updateButtons();
  }, TASK_OVERLAY_DISPLAY_DELAY);
}

function hideTaskOverlay() {
  clearTimeout(taskOverlayTimer);
  taskOverlayTimer = 0;
  overlayEl.hidden = true;
  document.querySelector(".shell").classList.toggle("modal-open", modalBackgroundLocked);
}

function showActionFailed(label, error) {
  const message = t("actionFailed", { label, error });
  setStatus(message);
  alert(message);
}

function readSavedPath() {
  try {
    return localStorage.getItem(LAST_PATH_KEY) || "/";
  } catch (err) {
    return "/";
  }
}

function readSavedArchivePassword() {
  try {
    return localStorage.getItem(ARCHIVE_PASSWORD_KEY) || "";
  } catch (err) {
    return "";
  }
}

function saveArchivePassword(password) {
  try {
    localStorage.setItem(ARCHIVE_PASSWORD_KEY, password);
  } catch (err) {
  }
}

function historyPath() {
  const hash = window.location.hash || "";
  if (hash.length <= 1) return "";
  try {
    const path = decodeURIComponent(hash.slice(1));
    return path.charAt(0) === "/" || /^smb:\/\/[^/]+\/[^/]+/.test(path) ? path : "";
  } catch (err) {
    return "";
  }
}

function writeHistoryPath(path, replace) {
  if (!window.history || !history.pushState) return;
  const hash = "#" + encodeURIComponent(path || "/");
  if (window.location.hash === hash) return;
  const state = { path: path || "/" };
  if (replace && history.replaceState) history.replaceState(state, "", hash);
  else history.pushState(state, "", hash);
}

function pathHistoryChain(path) {
  const clean = String(path || "/").replace(/\/+$/, "") || "/";
  if (/^smb:\/\//.test(clean)) {
    const segments = clean.slice(6).split("/");
    const chain = ["/"];
    let current = "smb://" + segments[0];
    for (let i = 1; i < segments.length; i++) {
      current += "/" + segments[i];
      chain.push(current);
    }
    return chain;
  }
  const parts = clean.split("/");
  const chain = ["/"];
  let current = "";
  for (let i = 1; i < parts.length; i++) {
    if (!parts[i]) continue;
    current += "/" + parts[i];
    chain.push(current);
  }
  return chain;
}

function seedHistoryPath(path) {
  if (!window.history || !history.pushState || !history.replaceState) return;
  const chain = pathHistoryChain(path);
  for (let i = 0; i < chain.length; i++) {
    writeHistoryPath(chain[i], i === 0);
  }
}

function historyBlocked() {
  return Boolean(busy || loadingPath || pendingAbortController ||
    pendingOverlayText || taskOverlayTimer || !overlayEl.hidden || !smbManagerOverlayEl.hidden ||
    !contentLoadingEl.hidden || contentEl.classList.contains("loading") ||
    !textEditorOverlayEl.hidden || !imagePreviewOverlayEl.hidden ||
    !pkgInfoOverlayEl.hidden || !permissionOverlayEl.hidden ||
    !extractOverlayEl.hidden);
}

function savePath(path) {
  try {
    localStorage.setItem(LAST_PATH_KEY, path || "/");
  } catch (err) {
  }
}

function readSavedSort() {
  try {
    const value = localStorage.getItem(SORT_KEY) || "";
    const parts = value.split(":");
    if (value === "none") {
      sortKey = "";
      sortDir = "none";
    } else if (SORT_KEYS[parts[0]] && (parts[1] === "asc" || parts[1] === "desc")) {
      sortKey = parts[0];
      sortDir = parts[1];
    }
  } catch (err) {
  }
}

function saveSort() {
  try {
    localStorage.setItem(SORT_KEY, sortDir === "none" ? "none" : sortKey + ":" + sortDir);
  } catch (err) {
  }
}

function fitSpaceOptions() {
  const select = document.getElementById("spaceSelect");
  if (!select || !select.clientWidth) return;
  const style = getComputedStyle(select);
  const context = document.createElement("canvas").getContext("2d");
  context.font = style.font || style.fontSize + " " + style.fontFamily;
  // Let very narrow headers wrap the controls rather than clip capacity text.
  if (window.innerWidth <= 640) {
    let minimum = 0;
    for (const option of select.options) {
      const text = option.smbName === undefined ? option.textContent : "SMB …" + option.spaceSuffix;
      minimum = Math.max(minimum, context.measureText(text).width);
    }
    const prefix = spaceInfoEl.querySelector(".space-prefix");
    const padding = parseFloat(style.paddingLeft) + parseFloat(style.paddingRight) + 32 +
      parseFloat(style.marginLeft) + (prefix ? prefix.getBoundingClientRect().width : 0);
    spaceInfoEl.style.minWidth = Math.min(Math.ceil(minimum + padding), spaceInfoEl.parentNode.clientWidth) + "px";
  } else {
    spaceInfoEl.style.minWidth = "";
  }
  // Reserve padding and the native dropdown arrow; only shorten SMB names.
  const width = select.clientWidth - parseFloat(style.paddingLeft) -
    parseFloat(style.paddingRight) - 30;
  for (const option of select.options) {
    if (option.smbName === undefined) continue;
    const name = Array.from(option.smbName);
    let text = "SMB " + name.join("") + option.spaceSuffix;
    if (context.measureText(text).width > width) {
      while (name.length && context.measureText("SMB " + name.join("") + "…" + option.spaceSuffix).width > width) {
        name.pop();
      }
      text = "SMB " + name.join("") + "…" + option.spaceSuffix;
    }
    option.textContent = text;
  }
  const selected = select.options[select.selectedIndex];
  select.title = selected ? selected.title : "";
}

async function refreshSpaces() {
  try {
    const data = await api("/api/space", { path: cwd });
    const previousConnections = smbConnections;
    smbConnections = (data.spaces || []).filter(item => /^smb:\/\//.test(item.path || ""));
    for (const previous of previousConnections) {
      const next = smbConnections.find(item => item.path === previous.path);
      if (!next) forgetSmbPassword(previous.path);
      else if (next.user !== previous.user || next.domain !== previous.domain) delete smbReady[previous.path];
    }
    if (!smbManagerOverlayEl.hidden && !localActionBusy) renderSmbConnections();
    updateSpecialStoragePaths(data.spaces || []);
    spaceInfoEl.innerHTML = "";
    const select = document.createElement("select");
    select.id = "spaceSelect";
    select.className = "space-select";
    select.disabled = spaceNavigationLocked();
    let currentIndex = -1;
    let internalIndex = -1;
    if ((data.spaces || []).length) {
      const prefix = document.createElement("label");
      prefix.className = "space-prefix";
      prefix.htmlFor = select.id;
      prefix.textContent = t("disks");
      spaceInfoEl.appendChild(prefix);
    }
    for (const item of data.spaces || []) {
      const option = document.createElement("option");
      option.value = item.path || "";
      const remote = /^smb:\/\//.test(item.path || "");
      const name = remote ? displayPath(item.path.replace(/\/+$/, "").split("/").pop()) :
        item.label_key ? t(item.label_key) : displayPath(item.label);
      const label = remote ? "SMB " + name : name;
      const suffix = item.available === false ?
        t("diskUnavailable", { name: "" }) :
        item.available === null ? t("diskSpaceUnknown", { name: "" }) :
        t("diskSpace", { name: "", free: formatBytes(item.free, false),
          total: formatBytes(item.total, false) });
      option.textContent = label + suffix;
      if (remote) {
        option.smbName = name;
        option.spaceSuffix = suffix;
      }
      option.title = item.path + " " + option.textContent;
      if (item.label_key === "storageInternal") internalIndex = select.options.length;
      if (item.current) currentIndex = select.options.length;
      select.appendChild(option);
    }
    if (currentIndex < 0 && cwd.charAt(0) === "/") currentIndex = internalIndex;
    select.selectedIndex = currentIndex;
    select.addEventListener("change", async () => {
      if (!select.value || spaceNavigationLocked()) {
        select.selectedIndex = currentIndex;
        fitSpaceOptions();
        return;
      }
      const ok = await load(select.value, undefined, false, true, "seed");
      if (!ok) select.selectedIndex = currentIndex;
      fitSpaceOptions();
    });
    if (select.options.length) {
      spaceInfoEl.appendChild(select);
      fitSpaceOptions();
    }
    renderAddressPath();
    if (!spaceNavigationLocked() && previousConnections.some(item => smbContainsPath(item.path, cwd)) &&
        !smbConnections.some(item => smbContainsPath(item.path, cwd))) {
      await load("/", undefined, true, true, "replace");
    }
  } catch (err) {
    renderAddressPath();
  }
}

function spaceNavigationLocked() {
  return backgroundInteractionLocked() || !contentLoadingEl.hidden;
}

function readSmbPassword(item) {
  let value = smbPasswords[item.path];
  if (!value) {
    try { value = JSON.parse(localStorage.getItem(SMB_PASSWORD_KEY + item.path)); } catch (err) {}
  }
  return value && value.user === item.user && value.domain === item.domain &&
    typeof value.password === "string" ? value.password : null;
}

function saveSmbPassword(item, password) {
  const value = { user: item.user, domain: item.domain, password: password };
  smbPasswords[item.path] = value;
  try { localStorage.setItem(SMB_PASSWORD_KEY + item.path, JSON.stringify(value)); } catch (err) {}
}

function forgetSmbPassword(path) {
  delete smbPasswords[path];
  delete smbReady[path];
  try { localStorage.removeItem(SMB_PASSWORD_KEY + path); } catch (err) {}
}

function askSmbPassword(path) {
  const password = prompt(t("smbPasswordPrompt", { path: displayPath(path) }), "");
  if (typeof password !== "string") {
    const error = new Error(t("smbPasswordCanceled"));
    error.code = "smb_password_canceled";
    throw error;
  }
  return password;
}

async function unlockSmb(item) {
  let password = readSmbPassword(item);
  const guest = /^(guest|anonymous)$/i.test(item.user || "Guest");
  if (password === null && !guest) password = askSmbPassword(item.path);
  try {
    let data;
    try {
      data = await apiForm("/api/smb/connect", { address: item.path, password: password === null ? "" : password });
    } catch (err) {
      if (password !== null || err.code !== "smb_password_required") throw err;
      password = askSmbPassword(item.path);
      data = await apiForm("/api/smb/connect", { address: item.path, password: password });
    }
    saveSmbPassword(data, password === null ? "" : password);
  } catch (err) {
    forgetSmbPassword(item.path);
    throw err;
  }
}

async function ensureSmbPaths(values) {
  const paths = [];
  for (const key of Object.keys(values || {})) {
    const value = values[key];
    if (typeof value === "string") paths.push.apply(paths, value.split("\n").filter(path => /^smb:\/\//.test(path)));
  }
  if (!paths.length) return;
  if (!smbConnections.length) {
    const data = await api("/api/space");
    smbConnections = (data.spaces || []).filter(item => /^smb:\/\//.test(item.path || ""));
  }
  for (const path of paths) {
    const item = smbConnections.filter(item => smbContainsPath(item.path, path))
      .sort((a, b) => b.path.length - a.path.length)[0];
    if (!item) continue;
    if (!smbReady[item.path]) smbReady[item.path] = unlockSmb(item);
    try { await smbReady[item.path]; }
    catch (err) { delete smbReady[item.path]; throw err; }
  }
}

function smbContainsPath(root, path) {
  return path === root || path.indexOf(root + "/") === 0;
}

function renderSmbConnections() {
  smbConnectionListEl.textContent = "";
  if (!smbConnections.length) {
    const empty = document.createElement("div");
    empty.className = "smb-connections-empty";
    empty.textContent = t("smbConnectionsEmpty");
    smbConnectionListEl.appendChild(empty);
  }
  for (const item of smbConnections) {
    const row = document.createElement("div");
    row.className = "smb-connection-row";
    const path = document.createElement("span");
    path.className = "smb-connection-path";
    path.textContent = displayPath(item.path);
    row.appendChild(path);
    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "danger";
    remove.textContent = t("removeSmb");
    remove.setAttribute("aria-label", t("removeSmb") + " " + displayPath(item.path));
    remove.addEventListener("click", () => removeSmbConnection(item.path));
    row.appendChild(remove);
    smbConnectionListEl.appendChild(row);
  }
  updateButtons();
}

function openSmbManager() {
  if (spaceNavigationLocked()) return;
  setModalBackgroundLocked(true);
  smbManagerOverlayEl.hidden = false;
  renderSmbConnections();
  smbManagerAddBtn.focus();
  refreshSpaces();
}

function closeSmbManager() {
  if (smbShareChoice) { smbShareChoice(null); return; }
  if (localActionBusy) return;
  smbManagerOverlayEl.hidden = true;
  setModalBackgroundLocked(false);
  manageSmbBtn.focus();
}

function smbManagerBusy() {
  return Boolean(busy || loadingPath || !contentLoadingEl.hidden);
}

async function removeSmbConnection(path) {
  if (smbManagerBusy() || !confirm(t("smbRemoveConfirm", { path: displayPath(path) }))) return;
  try {
    localActionBusy = true;
    setBusy(true);
    await apiForm("/api/smb/remove", { path: path });
    forgetSmbPassword(path);
    if (smbContainsPath(path, cwd)) await load("/", undefined, true, true, "replace");
    await refreshSpaces();
  } catch (err) {
    alert(t("smbRemoveFailed", { error: err.message }));
  } finally {
    localActionBusy = false;
    setModalBackgroundLocked(!smbManagerOverlayEl.hidden);
    setBusy(false);
    renderSmbConnections();
    smbManagerAddBtn.focus();
  }
}

function chooseSmbShare(shares) {
  smbShareSelectEl.textContent = "";
  for (const rawName of shares) {
    const name = decodeFsText(rawName);
    const option = document.createElement("option");
    option.value = name;
    option.textContent = name;
    smbShareSelectEl.appendChild(option);
  }
  smbConnectionListEl.hidden = true;
  smbManagerActionsEl.hidden = true;
  smbSharePickerEl.hidden = false;
  smbShareSelectEl.focus();
  return new Promise(resolve => {
    smbShareChoice = value => {
      smbShareChoice = null;
      smbSharePickerEl.hidden = true;
      smbShareSelectEl.textContent = "";
      smbConnectionListEl.hidden = false;
      smbManagerActionsEl.hidden = false;
      resolve(value);
    };
  });
}

async function addSmbConnection() {
  if (smbManagerBusy()) return;
  const address = prompt(t("smbAddressPrompt"), "smb://");
  if (typeof address !== "string") return;
  const input = address.trim();
  if (!input || /^smb:\/\/$/i.test(input)) return;
  try {
    localActionBusy = true;
    setBusy(true);
    const authority = /^smb:\/\/([^/]+)(?:\/|$)/.exec(input);
    let clean = input;
    let password = "";
    if (authority && authority[1].indexOf("@") >= 0) {
      const at = authority[1].lastIndexOf("@");
      const credentials = authority[1].slice(0, at);
      const colon = credentials.indexOf(":");
      password = colon >= 0 ? decodeURIComponent(credentials.slice(colon + 1)) : askSmbPassword(input);
      if (colon >= 0) clean = input.replace(authority[1], credentials.slice(0, colon) + "@" + authority[1].slice(at + 1));
    }
    if (/^smb:\/\/[^/]+\/?$/.test(clean)) {
      let listing;
      try {
        listing = await apiForm("/api/smb/shares", { address: clean, password: password });
      } catch (err) {
        if (password || err.code !== "smb_password_required") throw err;
        password = askSmbPassword(clean);
        listing = await apiForm("/api/smb/shares", { address: clean, password: password });
      }
      if (!listing.shares.length) throw new Error(t("smbNoShares"));
      const share = await chooseSmbShare(listing.shares);
      if (share === null) return;
      clean = clean.replace(/\/$/, "") + "/" + share;
    }
    const data = await apiForm("/api/smb/add", { address: clean, password: password });
    saveSmbPassword(data, password);
    smbReady[data.path] = Promise.resolve();
    await refreshSpaces();
    await load(data.path, undefined, true, true, "seed");
    smbManagerOverlayEl.hidden = true;
  } catch (err) {
    if (err.code !== "smb_password_canceled") alert(t("smbConnectFailed", { error: err.message }));
  } finally {
    localActionBusy = false;
    setModalBackgroundLocked(!smbManagerOverlayEl.hidden);
    setBusy(false);
    if (smbManagerOverlayEl.hidden) manageSmbBtn.focus();
    else smbManagerAddBtn.focus();
  }
}

function updateSpecialStoragePaths(spaces) {
  const next = {};
  let hasMnt = false;
  for (let i = 0; i < spaces.length; i++) {
    const path = spaces[i].path || "";
    if (/^\/mnt\/(?:usb|ext)[0-9]+$/.test(path)) {
      next[path] = true;
      hasMnt = true;
    }
  }

  const changed = hasMnt !== hasSpecialMntStorage ||
    Object.keys(next).join("\n") !== Object.keys(specialStoragePaths).join("\n");
  specialStoragePaths = next;
  hasSpecialMntStorage = hasMnt;
  if (changed && entries.length) render();
}

function visibleEntries() {
  if (!nameFilter) return entries;
  const query = nameFilter.toLowerCase();
  return entries.filter(item => displayName(item).toLowerCase().includes(query));
}

function selectedEntries() {
  return entries.filter(item => selected.has(item.path));
}

function pathJoin(dir, name) {
  return dir === "/" ? "/" + name : dir.replace(/\/+$/, "") + "/" + name;
}

function pathIsSameOrChild(parent, child) {
  parent = String(parent || "").replace(/\/+$/, "") || "/";
  child = String(child || "").replace(/\/+$/, "") || "/";
  return child === parent || (parent !== "/" && child.indexOf(parent + "/") === 0);
}

function isEditableText(item) {
  return item.type === "-" &&
    /\.(txt|json|xml|ini|cfg|conf|md|log|lua|js|css|html?|c|h|cpp|hpp|sh|csv|ya?ml|shn)$/i.test(item.name);
}

function isPreviewableImage(item) {
  return item.type === "-" && /\.(png|jpe?g|gif|bmp|webp)$/i.test(item.name);
}

function isPkgPackage(item) {
  return item.type === "-" && /\.pkg$/i.test(item.name);
}

function isElfFile(item) {
  return item.type === "-" && /\.elf$/i.test(item.name);
}

function isSupportedArchive(item) {
  if (item.type !== "-") return false;
  const part = /\.part(\d+)\.rar$/i.exec(item.name);
  if (part) return Number(part[1]) === 1;
  return ARCHIVE_FILE_RE.test(item.name);
}

function archiveFolderName(item) {
  let name = displayName(item);
  name = name.replace(/\.part\d+\.rar$/i, "");
  name = name.replace(/\.001$/i, "");
  name = name.replace(ARCHIVE_COMPOUND_SUFFIX_RE, "");
  name = name.replace(ARCHIVE_SIMPLE_SUFFIX_RE, "");
  return name || displayName(item);
}

function isSpecialDirectory(item) {
  if (item.type !== "d") return false;
  if (item.path === "/data") return true;
  if (item.path === "/mnt") return hasSpecialMntStorage;
  return Boolean(specialStoragePaths[item.path]);
}

function itemTypeLabel(item) {
  if (item.type === "d") return t("dir");
  if (isPkgPackage(item)) return "PKG";
  if (isElfFile(item)) return "ELF";
  if (isSupportedArchive(item)) return t("archiveFile");
  if (isPreviewableImage(item)) return t("image");
  if (isEditableText(item)) return t("textFile");
  return t("file");
}

async function queuePkgInstall(items) {
  if (busy || loadingPath || !items.length) return;
  try {
    localActionBusy = true;
    setBusy(true);
    setStatus(t("pkgInstalling", { name: itemTitle(items) }));
    await apiForm("/api/install-pkg", {
      paths: items.map(item => item.path).join("\n")
    });
    clearSelection();
    setStatus(t("pkgInstallStarted", { name: itemTitle(items) }));
    await pollTasks();
  } catch (err) {
    showActionFailed(t("installPackage"), err.message);
  } finally {
    localActionBusy = false;
    if (overlayEl.hidden && !taskOverlayTimer) setBusy(false);
  }
}

function installPkg(item) {
  return queuePkgInstall([item]);
}

function actionInstallSelectedPkgs() {
  return queuePkgInstall(selectedEntries().filter(isPkgPackage));
}

function launchElf(item) {
  if (!confirm(t("elfLaunchConfirm", { name: displayName(item) }))) return;
  runImmediateAction(t("launchElf"), () => apiForm("/api/launch-elf", {
    path: item.path
  }));
}

function openImagePreview(item) {
  if (busy) return;
  setModalBackgroundLocked(true);
  imagePreviewNameEl.textContent = displayName(item);
  imagePreviewNameEl.title = displayName(item);
  imagePreviewEl.src = "/fs?path=" + encodeURIComponent(item.path);
  imagePreviewOverlayEl.hidden = false;
  imagePreviewCloseBtn.focus();
}

function closeImagePreview() {
  imagePreviewOverlayEl.hidden = true;
  imagePreviewNameEl.textContent = "";
  imagePreviewNameEl.title = "";
  imagePreviewEl.removeAttribute("src");
  setModalBackgroundLocked(false);
}

function addPkgInfoRow(name, value) {
  if (value === undefined || value === null || String(value) === "") return;
  const row = document.createElement("div");
  row.className = "pkg-info-row";
  const key = document.createElement("div");
  key.className = "pkg-info-key";
  key.textContent = name;
  const text = document.createElement("div");
  text.className = "pkg-info-value";
  text.textContent = String(value);
  appendChildren(row, key, text);
  pkgInfoFieldsEl.appendChild(row);
}

function pkgHex(value) {
  return "0x" + ("00000000" + (Number(value || 0) >>> 0).toString(16).toUpperCase()).slice(-8);
}

async function openPkgInfo(item) {
  if (busy || loadingPath) return;
  const requestId = ++pkgInfoRequestId;
  pkgInfoItem = item;
  setModalBackgroundLocked(true);
  pkgInfoFieldsEl.innerHTML = "";
  pkgInfoImageEl.src = "/icon-pkg.png";
  pkgInfoInstallBtn.disabled = true;
  setStatus(t("pkgInfoLoading"));
  try {
    const data = await api("/api/pkg-info", { path: item.path }, { method: "GET" });
    if (requestId !== pkgInfoRequestId || pkgInfoItem !== item) return;
    pkgInfoFieldsEl.innerHTML = "";
    const platform = data.platform === "PS5" ? "PS5" : "PS4";
    pkgInfoTitleEl.textContent = platform + " " + t("pkgInfoTitle");
    const fields = data.fields || [];
    const priority = [
      "TITLE", "titleName", "TITLE_ID", "titleId",
      "VERSION", "masterVersion", "APP_VER", "contentVersion"
    ];
    const shown = {};
    for (const name of priority) {
      const field = fields.find(item => item.name === name);
      if (field) {
        addPkgInfoRow(field.name, field.value);
        shown[name] = true;
      }
    }
    addPkgInfoRow("PKG_SIZE", formatBytes(data.size, true));
    for (const field of fields) {
      if (!shown[field.name]) addPkgInfoRow(field.name, field.value);
    }
    if (!fields.some(field => field.name === "CONTENT_ID" || field.name === "contentId")) {
      addPkgInfoRow("PKG_CONTENT_ID", data.content_id);
    }
    addPkgInfoRow("PKG_CONTENT_TYPE", pkgHex(data.content_type));
    addPkgInfoRow("PKG_CONTENT_FLAGS", pkgHex(data.content_flags));
    if (data.has_icon) {
      pkgInfoImageEl.src = "/api/pkg-icon?path=" + encodeURIComponent(item.path) +
        "&mtime=" + encodeURIComponent(item.mtime || 0);
    }
    pkgInfoInstallBtn.disabled = false;
    pkgInfoOverlayEl.hidden = false;
    pkgInfoFieldsEl.scrollTop = 0;
    pkgInfoInstallBtn.focus();
    await nextPaint();
    if (requestId === pkgInfoRequestId) pkgInfoFieldsEl.scrollTop = 0;
  } catch (err) {
    if (requestId !== pkgInfoRequestId) return;
    pkgInfoItem = null;
    pkgInfoFieldsEl.innerHTML = "";
    setModalBackgroundLocked(false);
    showActionFailed(t("pkgInfoTitle"), err.message);
  }
}

function closePkgInfo() {
  pkgInfoRequestId++;
  pkgInfoOverlayEl.hidden = true;
  pkgInfoItem = null;
  pkgInfoTitleEl.textContent = t("pkgInfoTitle");
  pkgInfoFieldsEl.innerHTML = "";
  pkgInfoImageEl.src = "/icon-pkg.png";
  pkgInfoInstallBtn.disabled = true;
  setModalBackgroundLocked(false);
}

function installPkgFromInfo() {
  const item = pkgInfoItem;
  if (!item || pkgInfoInstallBtn.disabled) return;
  closePkgInfo();
  installPkg(item);
}

function permissionModeText(mode) {
  const octal = (Number(mode || 0) & 0x1ff).toString(8);
  return "0" + ("000" + octal).slice(-3);
}

function validPermissionMode(value) {
  return /^0[0-7]{3}$/.test(String(value || ""));
}

function syncPermissionChecks(value) {
  if (!validPermissionMode(value)) return;
  const mode = parseInt(value, 8);
  const bits = [0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004, 0x002, 0x001];
  for (let i = 0; i < permissionChecks.length; i++) {
    permissionChecks[i].checked = Boolean(mode & bits[i]);
  }
}

function syncPermissionMode() {
  const bits = [0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004, 0x002, 0x001];
  let mode = 0;
  for (let i = 0; i < permissionChecks.length; i++) {
    if (permissionChecks[i].checked) mode |= bits[i];
  }
  permissionModeEl.value = permissionModeText(mode);
}

function validatePermissionMode() {
  if (validPermissionMode(permissionModeEl.value)) {
    syncPermissionChecks(permissionModeEl.value);
    return true;
  }
  alert(t("permissionInvalid"));
  permissionModeEl.value = permissionModeText(permissionItems.length ? permissionItems[0].mode : 0);
  syncPermissionChecks(permissionModeEl.value);
  permissionModeEl.focus();
  permissionModeEl.select();
  return false;
}

function setPermissionBusy(value) {
  permissionBusy = value;
  permissionModeEl.disabled = value;
  permissionRecursiveEl.disabled = value;
  permissionCancelBtn.disabled = value;
  permissionApplyBtn.disabled = value;
  for (const checkbox of permissionChecks) checkbox.disabled = value;
}

function openPermissionDialog(item) {
  if (busy || loadingPath || permissionBusy) return;
  const items = selected.has(item.path) ? selectedEntries() : [item];
  setModalBackgroundLocked(true);
  permissionItems = items;
  renderDialogItems(permissionPathEl, items);
  permissionModeEl.value = permissionModeText(item.mode);
  permissionOriginal = permissionModeEl.value;
  permissionRecursiveOptionEl.hidden = !items.some(entry => entry.type === "d");
  permissionRecursiveEl.checked = true;
  syncPermissionChecks(permissionModeEl.value);
  setPermissionBusy(false);
  permissionOverlayEl.hidden = false;
  renderSinglePermissionPath();
  permissionModeEl.focus();
  permissionModeEl.select();
}

function closePermissionDialog() {
  if (permissionBusy) return;
  permissionOverlayEl.hidden = true;
  permissionItems = [];
  permissionOriginal = "";
  permissionPathEl.textContent = "";
  permissionPathEl.title = "";
  permissionRecursiveOptionEl.hidden = true;
  setModalBackgroundLocked(false);
}

function requestClosePermissionDialog() {
  if (permissionBusy) return;
  if (validPermissionMode(permissionModeEl.value) &&
      (permissionModeEl.value !== permissionOriginal ||
       (!permissionRecursiveOptionEl.hidden && !permissionRecursiveEl.checked)) &&
      !confirm(t("unsavedPermissionConfirm"))) return;
  closePermissionDialog();
}

function extractFolderDisplayName(item) {
  const name = archiveFolderName(item);
  return name.length <= EXTRACT_NAME_LIMIT ? name :
    name.slice(0, EXTRACT_NAME_LIMIT - 3) + "...";
}

function extractFolderList(items) {
  const names = items.map(archiveFolderName).join(", ");
  if (names.length <= EXTRACT_NAME_LIMIT) return names;
  return t("extractFolderListMore", {
    folders: names.slice(0, EXTRACT_NAME_LIMIT)
  });
}

function syncExtractDestination(focusCustom) {
  extractCustomPathEl.disabled = !extractCustomEl.checked;
  if (focusCustom && extractCustomEl.checked) {
    extractCustomPathEl.focus();
    extractCustomPathEl.select();
  }
}

function renderSingleExtractPath() {
  if (!extractOverlayEl.hidden) renderSingleDialogPath(extractPathEl, extractItems);
}

function openExtractDialogForItems(items) {
  if (busy || loadingPath || !items.length) return;
  if (cwd.startsWith("smb://") || items.some(item => item.path.startsWith("smb://"))) {
    showActionFailed(t("extract"), t("smbLocalOperation"));
    return;
  }

  setModalBackgroundLocked(true);
  extractItems = items.slice();
  renderDialogItems(extractPathEl, items);
  if (items.length === 1) {
    extractSeparateTextEl.textContent = t("extractToFolder", {
      name: extractFolderDisplayName(items[0])
    });
    extractCustomTextEl.textContent = t("extractToCustom");
  } else {
    extractSeparateTextEl.textContent = t("extractSeparateFolders", {
      folders: extractFolderList(items)
    });
    extractCustomTextEl.textContent = t("extractToCustomSeparate");
  }
  extractSeparateEl.checked = true;
  extractCustomPathEl.value = items.length === 1 ?
    pathJoin(displayPath(cwd), archiveFolderName(items[0])) : displayPath(cwd);
  syncExtractDestination(false);
  extractOverlayEl.hidden = false;
  renderSingleExtractPath();
  extractApplyBtn.focus();
}

function actionExtractSelected() {
  openExtractDialogForItems(selectedEntries().filter(isSupportedArchive));
}

function closeExtractDialog() {
  extractOverlayEl.hidden = true;
  extractItems = [];
  extractPathEl.textContent = "";
  extractPathEl.title = "";
  extractCustomPathEl.value = "";
  setModalBackgroundLocked(false);
}

async function startExtractRequest(request, password, overwrite, passwordPrompted) {
  const payload = Object.assign({}, request);
  if (password !== undefined) payload.password = password;
  payload.overwrite = overwrite ? "1" : "0";
  setBusy(true);
  setStatus(t("extractStarting"));
  taskRefreshPath = cwd;
  try {
    const data = await apiForm("/api/extract", payload);
    trackTask(data.task_id, "extract", false);
    trackedTask.extractRequest = request;
    trackedTask.extractPassword = password;
    trackedTask.extractOverwrite = Boolean(overwrite);
    trackedTask.extractPasswordPrompted = Boolean(passwordPrompted);
    clearSelection(false);
    setStatus(t("taskCreated", { label: t("extract") }));
    await pollTasks();
  } catch (err) {
    showActionFailed(t("extract"), err.message);
    setBusy(false);
  }
}

function submitExtractDialog() {
  if (!extractItems.length) return;
  if (extractCustomEl.checked && !extractCustomPathEl.value.trim()) {
    extractCustomPathEl.focus();
    return;
  }
  const items = extractItems.slice();
  const useCustom = extractCustomEl.checked;
  const request = {
    paths: items.map(item => item.path).join("\n"),
    destination: useCustom ? extractCustomPathEl.value.trim() : cwd,
    separate: extractSeparateEl.checked || (useCustom && items.length > 1) ? "1" : "0"
  };
  if (request.destination.startsWith("smb://")) {
    showActionFailed(t("extract"), t("smbLocalOperation"));
    return;
  }
  closeExtractDialog();
  startExtractRequest(request);
}

async function applyPermissionMode() {
  if (permissionBusy || !permissionItems.length || !validatePermissionMode()) return;
  const items = permissionItems.slice();
  const mode = permissionModeEl.value;
  setPermissionBusy(true);
  setStatus(t("permissionChanging"));
  taskRefreshPath = cwd;
  try {
    const data = await apiForm("/api/chmod", {
      paths: items.map(item => item.path).join("\n"),
      mode,
      recursive: items.some(item => item.type === "d") && permissionRecursiveEl.checked ? "1" : "0"
    });
    setPermissionBusy(false);
    closePermissionDialog();
    setBusy(true);
    trackTask(data.task_id, "chmod", false);
    clearSelection(false);
    setStatus(t("taskCreated", { label: t("permissionsTitle") }));
    await pollTasks();
  } catch (err) {
    setPermissionBusy(false);
    const message = t("permissionChangeFailed", { error: err.message });
    setStatus(message);
    alert(message);
    permissionModeEl.focus();
  }
}

function setTextEditorBusy(value, status) {
  textEditorBusy = value;
  textEditorEl.disabled = value;
  textEditorCloseBtn.disabled = value;
  textEditorSaveBtn.disabled = value;
  textEditorStatusEl.textContent = status || "";
}

function closeTextEditor() {
  if (textEditorBusy) return;
  textEditorOverlayEl.hidden = true;
  setModalBackgroundLocked(false);
  textEditorPath = null;
  textEditorVersion = null;
  textEditorOriginal = "";
  textEditorEl.value = "";
  textEditorStatusEl.textContent = "";
  return load(cwd, false, true);
}

function requestCloseTextEditor() {
  if (textEditorBusy) return;
  if (textEditorEl.value !== textEditorOriginal &&
      !confirm(t("unsavedSaveConfirm"))) return;
  return closeTextEditor();
}

function showTextEditor(item, text, version) {
  setModalBackgroundLocked(true);
  textEditorPath = item.path;
  textEditorVersion = version;
  textEditorPathEl.textContent = displayPath(item.path);
  textEditorPathEl.title = displayPath(item.path);
  textEditorEl.value = text;
  textEditorOriginal = textEditorEl.value;
  textEditorOverlayEl.hidden = false;
  setTextEditorBusy(false, "");
  textEditorEl.focus();
}

async function openTextEditor(item) {
  if (busy || textEditorBusy) return;
  setModalBackgroundLocked(true);
  textEditorPath = item.path;
  textEditorVersion = null;
  textEditorOriginal = "";
  textEditorPathEl.textContent = displayPath(item.path);
  textEditorPathEl.title = displayPath(item.path);
  textEditorEl.value = "";
  textEditorOverlayEl.hidden = false;
  setTextEditorBusy(true, t("loadingText"));
  try {
    const data = await fetchText("/api/text", { path: item.path });
    showTextEditor(item, data.text, data.version);
  } catch (err) {
    setTextEditorBusy(false, "");
    textEditorOverlayEl.hidden = true;
    setModalBackgroundLocked(false);
    textEditorPath = null;
    const message = t("openTextFailed", { error: err.message });
    setStatus(message);
    alert(message);
    await load(cwd, false, true);
  }
}

async function saveTextEditor() {
  if (textEditorBusy || !textEditorPath || !textEditorVersion) return;
  if (textEditorEl.value === textEditorOriginal) {
    await closeTextEditor();
    return;
  }
  const path = textEditorPath;
  setTextEditorBusy(true, t("savingText"));
  try {
    await api("/api/text/save", { path, version: textEditorVersion }, {
      body: textEditorEl.value,
      headers: { "Content-Type": "text/plain; charset=utf-8" }
    });
    setTextEditorBusy(false, "");
    await closeTextEditor();
    setStatus(t("textSaved"));
  } catch (err) {
    setTextEditorBusy(false, "");
    const message = t("saveTextFailed", { error: err.message });
    setStatus(message);
    alert(message);
  }
}

async function actionNewText() {
  if (busy || loadingPath) return;
  const dir = cwd;
  const input = prompt(t("newTextPrompt"));
  if (input === null) return;
  const name = input.trim();
  if (!name) return;
  const item = { name, path: pathJoin(dir, name), type: "-" };

  try {
    setBusy(true);
    setStatus(t("creatingText"));
    const data = await api("/api/text/create", { path: dir, name });
    setBusy(false);
    showTextEditor(item, "", data.version);
  } catch (err) {
    setBusy(false);
    const message = t("createTextFailed", { error: err.message });
    setStatus(message);
    alert(message);
  }
}

function resetScrollTop() {
  const apply = () => {
    contentEl.scrollTop = 0;
    document.documentElement.scrollTop = 0;
    document.body.scrollTop = 0;
    window.scrollTo(0, 0);
  };
  apply();
  setTimeout(apply, 0);
  if (window.requestAnimationFrame) requestAnimationFrame(apply);
}

function revealPathInList(path) {
  const row = filesEl.querySelector("tr[data-path=\"" + cssEscape(path) + "\"]");
  if (!row) return;

  const top = row.offsetTop;
  const maxScroll = contentEl.scrollHeight - contentEl.clientHeight;
  if (maxScroll <= 0) return;
  const target = top - Math.floor((contentEl.clientHeight - row.offsetHeight) / 2);
  contentEl.scrollTop = Math.max(0, Math.min(maxScroll, target));
}

function clipboardTitle() {
  if (!clipboard || clipboard.items.length === 0) return "";
  return itemTitle(clipboard.items);
}

function itemTitle(items) {
  if (!items.length) return "";
  return items.length === 1 ? displayName(items[0]) : t("selectedItems", { name: displayName(items[0]), count: items.length });
}

function itemListTitle(items, limit) {
  if (!items.length) return "";
  if (items.length === 1) return displayName(items[0]);
  const shown = items.slice(0, limit).map(displayName).join(", ");
  return items.length > limit ? t("selectedItems", { name: shown, count: items.length }) : shown;
}

function renderSingleDialogPath(container, items) {
  if (items.length !== 1) return;
  const namesEl = container.querySelector(".permission-path-names");
  if (namesEl) renderEndOfPath(namesEl, displayPath(items[0].path));
}

function renderDialogItems(container, items) {
  const names = items.length === 1 ? displayPath(items[0].path) :
    items.map(displayName).join(", ");
  const count = items.length > 1 ? t("permissionObjectCount", { count: items.length }) : "";
  const namesEl = document.createElement("span");
  namesEl.className = "permission-path-names";
  namesEl.textContent = names;
  container.innerHTML = "";
  container.appendChild(namesEl);
  if (count) {
    const countEl = document.createElement("span");
    countEl.className = "permission-path-count";
    countEl.textContent = count;
    container.appendChild(countEl);
  }
  container.title = count ? names + " " + count : names;
}

function renderSinglePermissionPath() {
  if (!permissionOverlayEl.hidden) renderSingleDialogPath(permissionPathEl, permissionItems);
}

function compareText(a, b) {
  return String(a || "").localeCompare(String(b || ""));
}

function compareEntries(a, b) {
  if (sortDir === "none") {
    if (a.type === "d" && b.type !== "d") return -1;
    if (a.type !== "d" && b.type === "d") return 1;
    let defaultResult = compareText(displayName(a), displayName(b));
    if (!defaultResult) defaultResult = compareText(a.path, b.path);
    return defaultResult;
  }

  let result = 0;
  if (sortKey === "name") result = compareText(displayName(a), displayName(b));
  else if (sortKey === "type") result = compareText(itemTypeLabel(a), itemTypeLabel(b));
  else if (sortKey === "size") result = (a.type === "d" ? 0 : Number(a.size || 0)) -
    (b.type === "d" ? 0 : Number(b.size || 0));
  else if (sortKey === "mtime") result = Number(a.mtime || 0) - Number(b.mtime || 0);
  else if (sortKey === "mode") result = Number(a.mode || 0) - Number(b.mode || 0);

  if (!result) result = compareText(displayName(a), displayName(b));
  if (!result) result = compareText(a.path, b.path);
  return sortDir === "desc" ? -result : result;
}

function itemCountSuffix(items) {
  if (items.length <= 1) return "";
  const text = t("selectedItems", { name: "", count: items.length });
  return text.replace(/^\s+/, "");
}

function pathName(path) {
  const clean = String(path || "").replace(/\/+$/, "");
  const pos = clean.lastIndexOf("/");
  return displayPath(pos >= 0 ? clean.slice(pos + 1) || "/" : clean);
}

function taskSubject(task) {
  const count = Number(task.src_count || 0);
  return count > 1 ? t("countItems", { count }) : pathName(task.src);
}

function renderTaskName(el, task) {
  const target = taskSubject(task);
  const op = document.createElement("span");
  op.className = "task-name-op";
  op.textContent = taskOpLabel(task.op);
  const subject = document.createElement("span");
  subject.className = "task-name-subject";
  subject.textContent = target;
  subject.title = target;
  appendChildren(el, op, subject);
}

function renderClipboard() {
  const hasClipboard = Boolean(clipboard && clipboard.items.length);
  const locked = backgroundInteractionLocked();
  pasteBtn.hidden = !hasClipboard;
  clearClipboardBtn.hidden = !hasClipboard;
  if (!hasClipboard) {
    pasteVerbEl.textContent = t("paste");
    pasteNameEl.textContent = "";
    pasteCountEl.textContent = "";
    pasteBtn.title = "";
    pasteBtn.disabled = true;
    clearClipboardBtn.disabled = true;
    return;
  }

  const title = clipboardTitle();
  pasteVerbEl.textContent = clipboard.op === "move" ? t("move") : t("paste");
  pasteNameEl.textContent = clipboard.items.length === 1 ? title : displayName(clipboard.items[0]);
  pasteCountEl.textContent = itemCountSuffix(clipboard.items);
  pasteBtn.title = t("pasteTitle", { label: clipboard.op === "move" ? t("move") : t("paste"), name: title, path: cwd });
  pasteBtn.disabled = locked;
  clearClipboardBtn.disabled = locked;
}

function renderInstallPkgButton(items, locked) {
  const pkgs = items.filter(isPkgPackage);
  installPkgBtn.hidden = pkgs.length === 0;
  if (!pkgs.length) {
    installPkgBtn.title = "";
    installPkgBtn.disabled = true;
    return;
  }
  const title = itemTitle(pkgs);
  installPkgBtn.title = t("installPackage") + ": " + title;
  installPkgBtn.disabled = locked;
}

function renderExtractButton(items, locked) {
  const archives = items.filter(isSupportedArchive);
  extractBtn.hidden = archives.length === 0;
  if (!archives.length) {
    extractBtn.title = "";
    extractBtn.disabled = true;
    return;
  }
  extractBtn.title = t("extract") + ": " + itemTitle(archives);
  extractBtn.disabled = locked;
}

function singleSelected() {
  const items = selectedEntries();
  return items.length === 1 ? items[0] : null;
}

function updateButtons() {
  const items = selectedEntries();
  const locked = backgroundInteractionLocked();
  const shell = document.querySelector(".shell");
  shell.classList.toggle("interaction-locked", locked);
  shell.classList.toggle("modal-open", modalBackgroundLocked || !overlayEl.hidden);
  versionEl.disabled = locked;
  manageSmbBtn.disabled = spaceNavigationLocked();
  smbManagerAddBtn.disabled = smbManagerBusy();
  smbManagerCloseBtn.disabled = localActionBusy;
  for (const button of smbConnectionListEl.querySelectorAll("button")) {
    button.disabled = smbManagerBusy();
  }
  for (const select of spaceInfoEl.querySelectorAll(".space-select")) {
    select.disabled = spaceNavigationLocked();
  }
  document.getElementById("copyBtn").disabled = locked || items.length === 0;
  document.getElementById("moveBtn").disabled = locked || items.length === 0;
  document.getElementById("renameBtn").disabled = locked || items.length !== 1;
  document.getElementById("deleteBtn").disabled = locked || items.length === 0;
  downloadBtn.disabled = locked || items.length === 0;
  document.getElementById("refreshBtn").disabled = locked;
  uploadBtn.disabled = locked;
  uploadMenuBtn.disabled = locked;
  uploadFolderBtn.disabled = locked;
  uploadFilesEl.disabled = locked;
  uploadFolderEl.disabled = locked;
  document.getElementById("mkdirBtn").disabled = locked;
  newTextBtn.disabled = locked;
  for (const button of filesEl.querySelectorAll(".row-action, .mode-action, .row-tool")) {
    button.disabled = locked || (/^smb:\/\//.test(cwd) && button.classList.contains("mode-action"));
  }
  for (const checkbox of filesEl.querySelectorAll(".select-cell input")) checkbox.disabled = locked;
  selectAllEl.disabled = locked;
  nameFilterBtn.disabled = locked;
  parentBtn.disabled = locked || cwd === "/";
  exitBtn.disabled = locked;
  const visibleCount = visibleEntries().length;
  selectAllEl.checked = visibleCount > 0 && items.length === visibleCount;
  selectAllEl.indeterminate = items.length > 0 && items.length < visibleCount;
  renderInstallPkgButton(items, locked);
  renderExtractButton(items, locked);
  renderClipboard();
}

function updateSortHeaders() {
  const headers = document.querySelectorAll("th[data-sort]");
  for (const th of headers) {
    const key = th.getAttribute("data-sort");
    th.classList.remove("sorted-asc");
    th.classList.remove("sorted-desc");
    th.setAttribute("aria-sort", key === sortKey && sortDir !== "none" ?
      (sortDir === "asc" ? "ascending" : "descending") : "none");
    if (key === sortKey && sortDir !== "none") th.classList.add(sortDir === "asc" ? "sorted-asc" : "sorted-desc");
  }
}

async function setSort(key) {
  if (busy || loadingPath) return;
  if (!SORT_KEYS[key]) return;
  if (sortKey !== key || sortDir === "none") {
    sortKey = key;
    sortDir = "asc";
  } else if (sortDir === "asc") {
    sortDir = "desc";
  } else {
    sortKey = "";
    sortDir = "none";
  }
  saveSort();
  updateSortHeaders();
  const useLoading = entries.length >= SELECT_ALL_LOADING_THRESHOLD;
  if (useLoading) {
    showContentLoading(t("processing"));
    await nextPaint();
  }
  entries.sort(compareEntries);
  render();
  if (useLoading) hideContentLoading();
}

function focusPath(path) {
  if (focusedPath === path) return;
  focusedPath = path;
  const old = filesEl.querySelector("tr.focused");
  const next = filesEl.querySelector("tr[data-path=\"" + cssEscape(path) + "\"]");
  if (old) old.classList.remove("focused");
  if (next) next.classList.add("focused");
}

function hoverRow(row) {
  if (hoverPaused || backgroundInteractionLocked()) return;
  if (hoveredRow === row) return;
  clearHoverPath();
  hoveredRow = row;
  if (hoveredRow) hoveredRow.classList.add("hovered");
}

function clearHoverPath() {
  if (hoveredRow) hoveredRow.classList.remove("hovered");
  hoveredRow = null;
}

function cssEscape(value) {
  if (window.CSS && CSS.escape) return CSS.escape(value);
  return String(value).replace(/["\\]/g, "\\$&");
}

function appendChildren(parent) {
  for (let i = 1; i < arguments.length; i++) {
    parent.appendChild(arguments[i]);
  }
}

function bindPress(el, fn) {
  let handled = false;
  const run = event => {
    if (handled) {
      if (event && event.preventDefault) event.preventDefault();
      return;
    }
    handled = true;
    if (event && event.preventDefault) event.preventDefault();
    fn();
    setTimeout(() => {
      handled = false;
    }, 350);
  };
  el.addEventListener("mousedown", run);
  el.addEventListener("click", run);
}

function findRowFromTarget(target) {
  while (target && target !== contentEl) {
    if (target.tagName === "TR" && target.getAttribute("data-path") !== null) return target;
    target = target.parentNode;
  }
  return null;
}

function togglePath(path, checked) {
  if (checked) selected.add(path);
  else selected.delete(path);
  const row = filesEl.querySelector("tr[data-path=\"" + cssEscape(path) + "\"]");
  if (row) {
    if (checked) row.classList.add("selected");
    else row.classList.remove("selected");
    const checkbox = row.querySelector("input[type=\"checkbox\"]");
    if (checkbox) checkbox.checked = checked;
  }
  updateButtons();
}

function clearSelection(updateVisibleRows) {
  if (selected.size === 0) {
    updateButtons();
    return;
  }
  if (updateVisibleRows === false) {
    selected.clear();
    updateButtons();
    return;
  }
  selected.forEach(path => {
    const row = filesEl.querySelector("tr[data-path=\"" + cssEscape(path) + "\"]");
    if (!row) return;
    row.classList.remove("selected");
    const checkbox = row.querySelector("input[type=\"checkbox\"]");
    if (checkbox) checkbox.checked = false;
  });
  selected.clear();
  updateButtons();
}

const FILE_ACTIONS = { download: actionDownload, copy: actionCopy, move: actionMove, rename: actionRename, delete: actionDelete };

function runRowAction(item, action) {
  if (backgroundInteractionLocked()) return;
  clearSelection();
  togglePath(item.path, true);
  focusPath(item.path);
  action();
}

function render() {
  hoveredRow = null;
  filesEl.innerHTML = "";
  const visible = visibleEntries();
  emptyEl.hidden = visible.length !== 0;
  emptyEl.textContent = entries.length ? t("noMatches") : t("empty");
  const fragment = document.createDocumentFragment();
  const actions = Object.keys(FILE_ACTIONS).filter(op => op !== "download" || !isPlayStationBrowser());

  for (const item of visible) {
    const tr = document.createElement("tr");
    tr.dataset.path = item.path;
    tr.dataset.name = item.name;
    tr.dataset.type = item.type;
    tr.className = (selected.has(item.path) ? "selected " : "") +
      (focusedPath === item.path ? "focused" : "");

    const selectTd = document.createElement("td");
    selectTd.className = "select-cell";
    const label = document.createElement("label");
    label.className = "select-hit";
    const checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.checked = selected.has(item.path);
    label.appendChild(checkbox);
    selectTd.appendChild(label);

    const nameTd = document.createElement("td");
    nameTd.className = "name-col";
    const nameBtn = document.createElement("button");
    nameBtn.className = "row-action";
    nameBtn.type = "button";
    const nameText = document.createElement("span");
    nameText.className = "name-text";
    nameText.textContent = displayName(item);
    nameBtn.title = displayPath(item.path);
    const iconImg = document.createElement("img");
    iconImg.className = "icon" + (isSpecialDirectory(item) ? " special-folder-icon" : "");
    iconImg.src = item.type === "d" ? "/icon-folder.png" :
      isPkgPackage(item) ? "/icon-pkg.png" :
      isElfFile(item) ? "/icon-elf.png" :
      isSupportedArchive(item) ? "/icon-archive.png" :
      isPreviewableImage(item) ? "/icon-image.png" :
      isEditableText(item) ? "/icon-file.png" : "/icon-generic.png";
    iconImg.alt = "";
    nameBtn.appendChild(iconImg);
    nameBtn.appendChild(nameText);
    const nameCell = document.createElement("div");
    nameCell.className = "file-name-cell";
    nameCell.appendChild(nameBtn);
    const tools = document.createElement("span");
    tools.className = "row-tools";
    for (const op of actions) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "row-tool";
      button.dataset.action = op;
      button.title = t(op);
      button.setAttribute("aria-label", t(op) + ": " + displayName(item));
      const icon = document.createElement("img");
      icon.src = "/icon-" + op + ".png";
      icon.alt = "";
      button.appendChild(icon);
      tools.appendChild(button);
    }
    nameCell.appendChild(tools);
    nameTd.appendChild(nameCell);

    const modeTd = document.createElement("td");
    modeTd.className = "mode-col";
    const modeBtn = document.createElement("button");
    modeBtn.className = "mode-action";
    modeBtn.type = "button";
    const remote = /^smb:\/\//.test(item.path);
    modeBtn.textContent = remote ? "—" : permissionModeText(item.mode);
    modeBtn.title = remote ? t("smbPermissions") : t("permissionChangeTitle", { name: displayName(item) });
    modeBtn.setAttribute("aria-label", modeBtn.title);
    modeBtn.disabled = remote || busy || Boolean(loadingPath);
    modeBtn.addEventListener("click", event => {
      event.preventDefault();
      event.stopPropagation();
      focusPath(item.path);
      openPermissionDialog(item);
    });
    modeTd.appendChild(modeBtn);

    appendChildren(
      tr,
      selectTd,
      nameTd,
      classCell("type-col", itemTypeLabel(item)),
      classCell("size-col", formatSize(item.size, item.type)),
      classCell("time-col", formatTime(item.mtime)),
      modeTd
    );
    fragment.appendChild(tr);
  }
  filesEl.appendChild(fragment);
  updateButtons();
}

function parentPath(path) {
  if (path === "/") return "/";
  if (/^smb:\/\/[^/]+\/[^/]+\/?$/.test(path)) return "/";
  const parts = path.replace(/\/+$/, "").split("/");
  parts.pop();
  return parts.join("/") || "/";
}

function actionParentDirectory(event) {
  if (event) {
    event.preventDefault();
    event.stopPropagation();
  }
  if (busy || loadingPath || cwd === "/") return;
  if (window.history && history.back && historyPath() === cwd) {
    history.back();
  } else {
    load(parentPath(cwd), undefined, false, true, "push");
  }
}

function cell(text) {
  const td = document.createElement("td");
  td.textContent = text;
  td.title = text;
  return td;
}

function classCell(className, text) {
  const td = cell(text);
  td.className = className;
  return td;
}

async function load(path, scrollTop, force, alertOnError, historyMode) {
  const shouldScrollTop = scrollTop !== false;
  if ((busy && !force) || loadingPath) return;
  loadingPath = path;
  updateButtons();
  startContentLoadingTimer();
  try {
    setStatus(t("readDir"));
    const data = await api("/api/list", { path });
    if (contentLoadingEl.hidden && data.entries && data.entries.length > 80) {
      await showContentLoadingAfterDelay();
    }
    if (!contentLoadingEl.hidden) await nextPaint();
    if (cwd !== data.path && nameFilter) {
      nameFilter = "";
      updateNameFilterButton();
    }
    cwd = data.path;
    renderAddressPath();
    savePath(cwd);
    if (historyMode === "seed") seedHistoryPath(cwd);
    else if (historyMode) writeHistoryPath(cwd, historyMode === "replace");
    refreshSpaces();
    entries = data.entries.sort(compareEntries);
    selected.clear();
    focusedPath = null;
    render();
    if (shouldScrollTop) resetScrollTop();
    setStatus(t("totalItems", { count: entries.length }));
    return true;
  } catch (err) {
    setStatus(err.message);
    if (alertOnError && err.code !== "smb_password_canceled") alert(err.message);
    return false;
  } finally {
    loadingPath = null;
    hideContentLoading();
  }
}

async function loadAndReveal(path, scrollTop, force, alertOnError, historyMode, revealPath) {
  const ok = await load(path, scrollTop, force, alertOnError, historyMode);
  if (ok && revealPath) revealPathInList(revealPath);
  return ok;
}

async function runAction(label, fn, options) {
  try {
    taskRefreshPath = cwd;
    setBusy(true);
    setStatus(t("actionBusy", { label }));
    const data = await fn();
    setStatus(t("taskCreated", { label }));
    trackTask(data.task_id, "delete", false, options && options.clearClipboardOnDone);
    clearSelection(false);
    await pollTasks();
  } catch (err) {
    setBusy(false);
    showActionFailed(label, err.message);
  }
}

async function runImmediateAction(label, fn) {
  const refreshPath = cwd;
  try {
    setBusy(true);
    setStatus(t("actionBusy", { label }));
    await fn();
    clearSelection();
    await load(refreshPath, false, true);
    setStatus(t("actionDone", { label }));
  } catch (err) {
    showActionFailed(label, err.message);
  } finally {
    setBusy(false);
  }
}

function setClipboard(op) {
  if (busy || loadingPath || selected.size === 0) return;
  const items = selectedEntries().map(item => ({ path: item.path, name: item.name, type: item.type }));
  if (items.length === 0) return;
  clipboard = { op, items };
  renderClipboard();
  setStatus(t("selectedForPaste", { name: itemTitle(items), verb: op === "move" ? t("move") : t("paste") }));
}

function clearClipboard() {
  if (busy || loadingPath) return;
  clipboard = null;
  renderClipboard();
  setStatus(t("clipboardCleared"));
}

function pathsTouchClipboard(paths) {
  if (!clipboard || !clipboard.items.length) return false;
  for (const path of paths) {
    for (const item of clipboard.items) {
      if (pathIsSameOrChild(path, item.path)) return true;
    }
  }
  return false;
}

function validatePasteTarget() {
  const byName = new Map(entries.map(item => [item.name, item]));
  const sameTargets = [];
  const sameFiles = [];
  const sameDirs = [];

  for (const item of clipboard.items) {
    const targetPath = pathJoin(cwd, item.name);
    const existing = byName.get(item.name);
    if (item.path === targetPath) {
      sameTargets.push(item.name);
      continue;
    }
    if (item.type === "d" && pathIsSameOrChild(item.path, targetPath)) {
      return {
        ok: false,
        message: t("err_destination_inside_source")
      };
    }
    if (!existing) continue;

    if ((item.type === "d") !== (existing.type === "d")) {
      return {
        ok: false,
        message: t("removeConflictFirst", {
          existingType: itemTypeLabel(existing),
          name: item.name,
          label: clipboard.op === "move" ? t("move") : t("copy"),
          sourceType: itemTypeLabel(item)
        })
      };
    }
    if (item.type === "d") sameDirs.push(item.name);
    else sameFiles.push(item.name);
  }
  if (sameTargets.length) {
    return {
      ok: false,
      message: t("sameSourceTarget", {
        label: clipboard.op === "move" ? t("move") : t("copy"),
        name: conflictText(sameTargets)
      })
    };
  }

  return { ok: true, sameFiles, sameDirs };
}

function conflictText(names) {
  if (!names.length) return "";
  const shown = names.slice(0, 4).map(decodeFsText).join(", ");
  return names.length > 4 ? t("selectedItems", { name: shown, count: names.length }) : shown;
}

function renderPendingOverlay(text, label) {
  tasksEl.innerHTML = "";
  const div = document.createElement("div");
  div.className = "task running";

  const head = document.createElement("div");
  head.className = "task-head has-amount";
  const name = document.createElement("div");
  name.className = "task-name";
  name.textContent = label || t("preparingTask");
  const amount = document.createElement("div");
  amount.className = "task-amount";
  amount.textContent = t("pleaseWait");
  appendChildren(head, name, amount);

  const current = document.createElement("div");
  current.className = "task-current";
  current.textContent = displayPath(text);

  const progress = document.createElement("div");
  progress.className = "progress";
  const progressText = document.createElement("div");
  progressText.className = "progress-text";
  progressText.textContent = t("checking");
  progress.appendChild(progressText);

  const cancel = document.createElement("button");
  cancel.className = "danger";
  cancel.textContent = t("cancel");
  bindPress(cancel, () => {
    if (pendingAbortController) pendingAbortController.abort();
    pendingAbortController = null;
    pendingOverlayText = "";
    pendingOverlayLabel = "";
    hideTaskOverlay();
    setStatus(t("cancelPrepare"));
    setBusy(false);
  });

  appendChildren(div, head, current, progress, cancel);
  tasksEl.appendChild(div);
}

function showPendingOverlay(text, label) {
  pendingOverlayText = text;
  pendingOverlayLabel = label || "";
  renderPendingOverlay(text, pendingOverlayLabel);
  showTaskOverlay();
}

function renderTaskPath(element, path) {
  path = displayPath(path);
  element.title = path;
  element.textContent = path;
  if (element.scrollWidth <= element.clientWidth) return;

  let low = 0;
  let high = path.length;
  while (low < high) {
    const count = Math.ceil((low + high) / 2);
    const left = Math.ceil(count / 2);
    const right = Math.floor(count / 2);
    element.textContent = path.slice(0, left) + "..." +
      (right ? path.slice(-right) : "");
    if (element.scrollWidth <= element.clientWidth) low = count;
    else high = count - 1;
  }

  const left = Math.ceil(low / 2);
  const right = Math.floor(low / 2);
  element.textContent = path.slice(0, left) + "..." +
    (right ? path.slice(-right) : "");
}

async function actionPaste() {
  if (busy || loadingPath || !clipboard || clipboard.items.length === 0) return;
  const op = clipboard.op;
  const label = clipboard.op === "move" ? t("move") : t("copy");
  const check = validatePasteTarget();
  if (!check.ok) {
    alert(check.message);
    setStatus(check.message);
    return;
  }

  const hasFileConflicts = check.sameFiles.length > 0;
  const hasDirConflicts = check.sameDirs.length > 0;
  if (hasFileConflicts || hasDirConflicts) {
    const lines = [];
    if (hasFileConflicts) lines.push(t("overwriteFiles", { names: conflictText(check.sameFiles) }));
    if (hasDirConflicts) lines.push(t("mergeDirs", { names: conflictText(check.sameDirs) }));
    if (!confirm(lines.join("\n") + "\n\n" + t("continueConfirm"))) return;
  }

  try {
    pendingAbortController = new AbortController();
    taskRefreshPath = cwd;
    showPendingOverlay(t("preparingPaste", { label }), label);
    setBusy(true);
    setStatus(t("preparingStatus", { label }));
    await nextPaint();
    const payload = {
      paths: clipboard.items.map(item => item.path).join("\n"),
      dst: cwd,
      overwrite: hasFileConflicts || hasDirConflicts ? "1" : "0"
    };
    const data = await apiForm("/api/" + op, payload, { signal: pendingAbortController.signal });
    pendingAbortController = null;
    pendingOverlayText = "";
    pendingOverlayLabel = "";
    setStatus(t("taskCreated", { label }));
    trackTask(data.task_id, op, true);
    clearSelection(false);
    await pollTasks();
  } catch (err) {
    const aborted = err && err.name === "AbortError";
    pendingAbortController = null;
    pendingOverlayText = "";
    pendingOverlayLabel = "";
    hideTaskOverlay();
    if (aborted) setStatus(t("cancelPrepare"));
    else showActionFailed(label, err.message);
    renderClipboard();
    setBusy(false);
  }
}

function renderTasks(tasks) {
  const fileTasks = tasks.filter(task => task.op !== "pkg_install");
  const active = fileTasks.find(task => task.state === "queued" || task.state === "running");
  if (!active && pendingOverlayText) {
    renderPendingOverlay(pendingOverlayText, pendingOverlayLabel);
    showTaskOverlay();
    return;
  }

  tasksEl.innerHTML = "";
  const hasActive = Boolean(active);
  if (hasActive) showTaskOverlay();
  else {
    hideTaskOverlay();
    if (!localActionBusy) setBusy(false);
  }

  if (!hasActive) return;

  const task = active;
  const total = Number(task.total || 0);
  const done = Number(task.done || 0);
  const speed = Number(task.speed || 0);
  const isDelete = task.op === "delete";
  const isDownload = task.op === "download";
  const isChmod = task.op === "chmod";
  const isExtract = task.op === "extract";
  const isPreparing = (task.op === "copy" || task.op === "move" ||
    isChmod || isExtract) && task.state === "running" && done === 0;
  const isFinishing = !isDelete && !isDownload && !isExtract &&
    task.state === "running" && total > 0 && done >= total;
  let pct = total > 0 ? Math.min(100, Math.floor(done * 100 / total)) : 0;
  if (!isDownload && task.state === "running" && pct >= 100) pct = 99;
  const div = document.createElement("div");
  div.className = "task " + task.state;

  const head = document.createElement("div");
  head.className = "task-head";
  const name = document.createElement("div");
  name.className = "task-name";
  renderTaskName(name, task);
  head.appendChild(name);
  const elapsed = document.createElement("div");
  elapsed.className = "task-elapsed";
  elapsed.textContent = t("elapsedLabel") + ": " + taskElapsed(task);
  head.appendChild(elapsed);

  const current = document.createElement("div");
  current.className = "task-current";
  if (task.error) {
    current.className += " task-current-plain";
    current.textContent = task.error;
  }
  else current.textContent = displayPath(task.current || task.src);

  const meta = document.createElement("div");
  meta.className = "task-meta";
  const speedItem = document.createElement("div");
  speedItem.textContent = t("speedLabel") + ": " + (isChmod ?
    t("itemsPerSecond", { count: Math.round(speed) }) : formatSpeed(speed));
  const progressItem = document.createElement("div");
  progressItem.textContent = t("progressLabel") + ": " + (isChmod ?
    t("permissionProgress", { done, total }) : formatSize(done) + " / " + formatSize(total));
  const etaItem = document.createElement("div");
  etaItem.textContent = t("etaLabel") + ": " + averageEta(task, done, total);
  appendChildren(meta, speedItem, progressItem, etaItem);

  const progress = document.createElement("div");
  progress.className = "progress";
  const bar = document.createElement("div");
  bar.className = "progress-bar";
  bar.style.width = isDelete ? "0" : pct + "%";
  const progressText = document.createElement("div");
  progressText.className = "progress-text";
  progressText.textContent = isDownload ? (total ? pct + "%" : t("preparing")) :
    isPreparing ? t("preparing") : isFinishing ? t("finishing") :
    total ? pct + "%" : stateLabel(task.state);
  appendChildren(progress, bar, progressText);

  const cancel = document.createElement("button");
  cancel.className = "danger";
  cancel.textContent = task.cancel_requested ? t("canceling") : t("cancel");
  cancel.disabled = task.cancel_requested;
  bindPress(cancel, () => {
    if (confirm(t("cancelTaskConfirm", { label: opLabel(task.op) }))) {
      requestTaskCancel(task.id);
      api("/api/cancel", { id: task.id }).then(() => {
        if (task.op !== "upload") return pollTasks();
        if (uploadXhr) uploadXhr.abort();
        return api("/api/upload/finish", { task_id: task.id })
          .catch(() => {})
          .then(pollTasks);
      }).catch(err => {
        if (trackedTask && trackedTask.id === task.id) trackedTask.cancelRequested = false;
        setStatus(t("cancelFailed", { error: err.message }));
      });
    }
  });

  if (isDelete) appendChildren(div, head, current, cancel);
  else if (total && !isFinishing) appendChildren(div, head, current, meta, progress, cancel);
  else appendChildren(div, head, current, progress, cancel);
  tasksEl.appendChild(div);
  if (!task.error) renderTaskPath(current, task.current || task.src);
}

async function pollTasksOnce() {
  const trackedIdAtStart = trackedTask ? trackedTask.id : 0;
  try {
    const acknowledgeDownload = downloadAck;
    const data = await api("/api/tasks", acknowledgeDownload ? { download_ack: acknowledgeDownload } : undefined);
    if (downloadAck === acknowledgeDownload) downloadAck = 0;
    if ((trackedTask ? trackedTask.id : 0) !== trackedIdAtStart) {
      setTimeout(pollTasks, 0);
      return;
    }
    taskPollFailedAlertShown = false;
    const tasks = data.tasks || [];
    const fileTasks = tasks.filter(task => task.op !== "pkg_install");
    const completion = data.completion;
    if (lastCompletionId === null) {
      lastCompletionId = completion ? completion.id : 0;
    } else if (completion && completion.id !== lastCompletionId) {
      lastCompletionId = completion.id;
      alert(t(completion.op === "extract" ? "extractComplete" :
        completion.file_count ? "transferCompleteFiles" : "transferComplete", {
        label: opLabel(completion.op),
        name: taskSubject(completion),
        duration: formatDuration(Number(completion.elapsed || 0)),
        size: formatBytes(completion.total, true),
        count: completion.file_count,
        archives: completion.src_count
      }));
    }
    const wasBusy = busy;
    const wasTaskBusy = Boolean(trackedTask || pendingOverlayText ||
      !overlayEl.hidden || taskOverlayTimer);
    renderTasks(tasks);
    const becameIdle = wasBusy && !busy;
    const taskBecameIdle = wasTaskBusy && !busy;
    let shouldRefresh = false;
    let sawTrackedTask = false;
    let completedDownload = false;
    for (const task of tasks) {
      if ((task.op === "download" || task.op === "upload") && isTerminalTask(task) &&
          (!trackedTask || trackedTask.id !== task.id)) continue;
      if (trackedTask && trackedTask.id === task.id) {
        sawTrackedTask = true;
        if (task.cancel_requested) trackedTask.cancelRequested = true;
        if (trackedTask.state === task.state) continue;
        trackedTask.state = task.state;
        completedDownload = task.op === "download" && task.state === "done";
      }
      if (isTerminalTask(task)) {
        if (task.op === "download") downloadAck = task.id;
        const retryingPassword = handleTerminalTask(task);
        if (task.op !== "pkg_install" && !retryingPassword) shouldRefresh = true;
      }
    }
    if (trackedTask && trackedTask.id === trackedIdAtStart &&
        becameIdle && !sawTrackedTask) {
      if ((trackedTask.fromClipboard || trackedTask.clearClipboardOnDone) &&
          !trackedTask.cancelRequested) clearClipboardAfterPaste();
      shouldRefresh = true;
      if (trackedTask.op === "download") {
        downloadQueue = [];
        showActionFailed(t("download"), t("downloadInterrupted"));
      }
      clearTrackedTask();
    }
    if (taskBecameIdle && !fileTasks.length) shouldRefresh = true;
    if (completedDownload && downloadQueue.length && !trackedTask) {
      await startDownload(downloadQueue.shift());
      return;
    }
    if (shouldRefresh) {
      if (taskBecameIdle) startContentLoadingTimer();
      clearSelection(false);
      await load(taskRefreshPath || cwd, false);
      await refreshSpaces();
    }
  } catch (err) {
    const message = t("tasksPollFailed", { error: err.message });
    setStatus(message);
  }
}

function pollTasks() {
  if (taskPollPromise) return taskPollPromise;
  const request = pollTasksOnce();
  taskPollPromise = request;
  request.then(() => {
    if (taskPollPromise === request) taskPollPromise = null;
  }, () => {
    if (taskPollPromise === request) taskPollPromise = null;
  });
  return request;
}

function actionCopy() {
  setClipboard("copy");
}

function actionMove() {
  setClipboard("move");
}

function actionRename() {
  if (busy || loadingPath) return;
  const item = singleSelected();
  if (!item) return;
  const name = (prompt(t("renamePrompt"), displayName(item)) || "").trim();
  if (!name || name === displayName(item)) return;
  runImmediateAction(t("rename"), () => api("/api/rename", { path: item.path, name }));
}

function actionDelete() {
  if (busy || loadingPath || selected.size === 0) return;
  const items = selectedEntries();
  const paths = items.map(item => item.path);
  const target = itemListTitle(items, 4);
  const confirmKey = items.some(item => item.type === "d") ?
    "deleteConfirmRecursive" : "deleteConfirm";
  if (!confirm(t(confirmKey, { name: target }))) return;
  runAction(t("delete"), () => apiForm("/api/delete", { paths: paths.join("\n") }), {
    clearClipboardOnDone: pathsTouchClipboard(paths)
  });
}

async function actionDownload() {
  if (busy || loadingPath || selected.size === 0) return;
  const items = selectedEntries();
  if (!items.length) return;
  downloadQueue = items.slice(1);
  await startDownload(items[0]);
  await pollTasks();
}

async function startDownload(item) {
  const label = t("download");
  try {
    localActionBusy = true;
    setBusy(true);
    taskRefreshPath = cwd;
    setStatus(t("actionBusy", { label }));
    const data = await apiForm("/api/download/prepare", {
      paths: item.path
    });
    trackTask(data.task_id, "download", false);
    if (downloadFrame) downloadFrame.parentNode.removeChild(downloadFrame);
    downloadFrame = document.createElement("iframe");
    downloadFrame.hidden = true;
    downloadFrame.src = "/api/download?id=" + encodeURIComponent(data.task_id);
    document.body.appendChild(downloadFrame);
    setStatus(t("downloadStarted", { name: displayName(item) }));
  } catch (err) {
    downloadQueue = [];
    setBusy(false);
    showActionFailed(label, err.message);
  } finally {
    localActionBusy = false;
  }
}

function uploadRelativeName(file) {
  return file.webkitRelativePath || file.name;
}

function uploadConflicts(rels) {
  const names = {};
  const conflicts = [];
  for (const item of entries) names[item.name] = true;
  for (const rel of rels) {
    const top = rel.split("/")[0];
    if (names[top] && conflicts.indexOf(top) < 0) conflicts.push(top);
  }
  return conflicts;
}

function uploadFileRequest(taskId, file, rel, overwrite, index) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    uploadXhr = xhr;
    xhr.open("POST", "/api/upload-file", true);
    xhr.setRequestHeader("Content-Type", "application/octet-stream");
    xhr.setRequestHeader("X-WFM-Task-ID", String(taskId));
    xhr.setRequestHeader("X-WFM-Path", encodeURIComponent(cwd));
    xhr.setRequestHeader("X-WFM-Rel", encodeURIComponent(rel));
    xhr.setRequestHeader("X-WFM-Size", String(file.size));
    xhr.setRequestHeader("X-WFM-Overwrite", overwrite ? "1" : "0");
    xhr.onload = () => {
      uploadXhr = null;
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve();
        return;
      }
      try {
        const data = JSON.parse(xhr.responseText || "{}");
        reject(new Error(backendErrorText(data.error_code, data.error_arg, data.error)));
      } catch (err) {
        reject(new Error(xhr.statusText || String(xhr.status)));
      }
    };
    xhr.onerror = () => {
      uploadXhr = null;
      const error = new Error(xhr.statusText || t("backendError"));
      api("/api/tasks").then(data => {
        const task = (data.tasks || []).find(item => item.id === taskId);
        if (task && Number(task.completed_count || 0) >= index) {
          resolve();
        } else if (task && task.state === "failed") {
          reject(new Error(backendErrorText(task.error_code, task.error_arg,
            task.error || task.current)));
        } else {
          reject(error);
        }
      }).catch(() => reject(error));
    };
    xhr.onabort = () => {
      uploadXhr = null;
      const terminal = uploadTerminalAbort;
      uploadTerminalAbort = false;
      const err = new Error("aborted");
      err.name = terminal ? "TerminalTaskAbort" : "AbortError";
      reject(err);
    };
    xhr.send(file);
  });
}

async function uploadFiles(files, relativeNames) {
  if (backgroundInteractionLocked() || !files.length) return;
  const useLoading = files.length >= SELECT_ALL_LOADING_THRESHOLD;
  let list;
  let rels = [];
  let sizes = [];
  let total = 0;
  let conflicts = [];

  if (useLoading) {
    showContentLoading(t("processing"));
    await nextPaint();
  }
  try {
    list = Array.prototype.slice.call(files);
    for (let i = 0; i < list.length; i++) {
      const file = list[i];
      rels.push(relativeNames ? relativeNames[i] : uploadRelativeName(file));
      sizes.push(String(file.size || 0));
      total += Number(file.size || 0);
    }
    conflicts = uploadConflicts(rels);
  } finally {
    if (useLoading) hideContentLoading();
  }

  const overwrite = conflicts.length &&
    confirm(t("uploadOverwriteConfirm", { names: conflictText(conflicts) }));
  if (conflicts.length && !overwrite) return;

  let taskId = 0;

  try {
    setBusy(true);
    taskRefreshPath = cwd;
    const task = await apiForm("/api/upload/prepare", {
      path: cwd,
      src: rels[0],
      total,
      count: list.length,
      rels: rels.join("\n"),
      sizes: sizes.join("\n"),
      overwrite: overwrite ? "1" : "0"
    });
    taskId = task.task_id;
    trackTask(taskId, "upload", false);
    await pollTasks();
    for (let i = 0; i < list.length; i++) {
      const file = list[i];
      const rel = rels[i];
      setStatus(t("uploadingStatus", { index: i + 1, count: list.length, name: rel }));
      await uploadFileRequest(taskId, file, rel, overwrite, i + 1);
    }
    await api("/api/upload/finish", { task_id: taskId });
    await pollTasks();
    setStatus(t("uploadDone", { count: list.length }));
  } catch (err) {
    const terminalAbort = err && err.name === "TerminalTaskAbort";
    const canceled = err && err.name === "AbortError";
    if (!terminalAbort) {
      if (taskId) {
        try {
          await api("/api/cancel", { id: taskId });
          await api("/api/upload/finish", { task_id: taskId });
        } catch (cancelErr) {
          /* The server may already have moved a failed upload to a terminal state. */
        }
      }
      await pollTasks();
      if (canceled) {
        setStatus(t("taskCanceled", { label: opLabel("upload") }));
      } else {
        const message = t("uploadFailed", { error: err.message });
        setStatus(message);
        alert(message);
      }
    }
  } finally {
    uploadXhr = null;
    uploadFilesEl.value = "";
    uploadFolderEl.value = "";
    if (!trackedTask) setBusy(false);
  }
}

async function readDroppedDirectory(directory) {
  const reader = directory.createReader();
  const entries = [];
  for (;;) {
    const batch = await new Promise((resolve, reject) => reader.readEntries(resolve, reject));
    if (!batch.length) return entries;
    for (const entry of batch) entries.push(entry);
  }
}

async function collectDroppedEntry(entry, prefix, files, relativeNames) {
  if (entry.isFile) {
    const file = await new Promise((resolve, reject) => entry.file(resolve, reject));
    files.push(file);
    relativeNames.push(prefix + file.name);
    return;
  }
  if (!entry.isDirectory) return;
  const entries = await readDroppedDirectory(entry);
  const childPrefix = prefix + entry.name + "/";
  for (let i = 0; i < entries.length; i++) {
    await collectDroppedEntry(entries[i], childPrefix, files, relativeNames);
  }
}

async function uploadDroppedItems(dataTransfer) {
  const files = [];
  const relativeNames = [];
  const entries = [];
  const items = dataTransfer.items;

  if (items && items.length) {
    for (let i = 0; i < items.length; i++) {
      if (items[i].kind !== "file") continue;
      const entry = items[i].webkitGetAsEntry ? items[i].webkitGetAsEntry() : null;
      if (entry) entries.push(entry);
      else {
        const file = items[i].getAsFile();
        if (file) {
          files.push(file);
          relativeNames.push(file.name);
        }
      }
    }
  } else {
    for (let i = 0; i < dataTransfer.files.length; i++) {
      files.push(dataTransfer.files[i]);
      relativeNames.push(uploadRelativeName(dataTransfer.files[i]));
    }
  }
  for (let i = 0; i < entries.length; i++) {
    await collectDroppedEntry(entries[i], "", files, relativeNames);
  }
  await uploadFiles(files, relativeNames);
}

function dataTransferHasFiles(dataTransfer) {
  const types = dataTransfer && dataTransfer.types;
  if (!types) return false;
  for (let i = 0; i < types.length; i++) {
    if (types[i] === "Files") return true;
  }
  return false;
}

function setupDropUpload() {
  let dragDepth = 0;
  window.addEventListener("dragenter", event => {
    if (!dataTransferHasFiles(event.dataTransfer)) return;
    event.preventDefault();
    dragDepth++;
    if (!backgroundInteractionLocked()) dropUploadOverlayEl.hidden = false;
  });
  window.addEventListener("dragover", event => {
    if (!dataTransferHasFiles(event.dataTransfer)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = backgroundInteractionLocked() ? "none" : "copy";
  });
  window.addEventListener("dragleave", () => {
    dragDepth = Math.max(0, dragDepth - 1);
    if (!dragDepth) dropUploadOverlayEl.hidden = true;
  });
  window.addEventListener("drop", event => {
    if (!dataTransferHasFiles(event.dataTransfer)) return;
    event.preventDefault();
    dragDepth = 0;
    dropUploadOverlayEl.hidden = true;
    if (backgroundInteractionLocked()) return;
    uploadDroppedItems(event.dataTransfer).catch(err => {
      const message = t("uploadFailed", { error: err.message || t("backendError") });
      setStatus(message);
      alert(message);
    });
  });
}

function actionUploadFiles() {
  if (busy || loadingPath) return;
  uploadMenuEl.classList.remove("open");
  uploadFilesEl.click();
}

function actionUploadFolder() {
  if (busy || loadingPath) return;
  uploadMenuEl.classList.remove("open");
  uploadFolderEl.click();
}

function toggleUploadMenu() {
  if (busy || loadingPath) return;
  uploadMenuEl.classList.toggle("open");
}

function actionExit() {
  if (!confirm(t("exitConfirm"))) return;
  setStatus(t("exiting"));
  exitBtn.disabled = true;
  api("/api/exit").catch(err => {
    exitBtn.disabled = false;
    showActionFailed(t("exit"), err.message);
  });
}

document.getElementById("refreshBtn").addEventListener("click", () => load(cwd, false));
document.getElementById("mkdirBtn").addEventListener("click", () => {
  if (busy || loadingPath) return;
  const path = cwd;
  const name = (prompt(t("mkdirPrompt")) || "").trim();
  if (!name) return;
  runImmediateAction(t("mkdir"), () => api("/api/mkdir", { path, name }));
});
for (const op of Object.keys(FILE_ACTIONS)) {
  const button = document.getElementById(op + "Btn");
  if (button) button.addEventListener("click", FILE_ACTIONS[op]);
}
pasteBtn.addEventListener("click", actionPaste);
installPkgBtn.addEventListener("click", actionInstallSelectedPkgs);
extractBtn.addEventListener("click", actionExtractSelected);
clearClipboardBtn.addEventListener("click", clearClipboard);
uploadBtn.addEventListener("click", actionUploadFiles);
uploadMenuBtn.addEventListener("click", toggleUploadMenu);
uploadFolderBtn.addEventListener("click", actionUploadFolder);
uploadFilesEl.addEventListener("change", () => uploadFiles(uploadFilesEl.files));
uploadFolderEl.addEventListener("change", () => uploadFiles(uploadFolderEl.files));
if (!isPlayStationBrowser()) setupDropUpload();
exitBtn.addEventListener("click", actionExit);
versionEl.addEventListener("click", () => {
  alert(`PS5 Web File Manager ${APP_VERSION}

A file manager for PS5 with a web UI. It is primarily intended for quickly and safely copying game dump folders from USB storage to internal storage.

Credits:
ps5-payload-dev/websrv, ps5-payload-dev/ftpsrv,
seregonwar/zftpd, itsPLK/ps5-payload-manager,
itsPLK/ps5-pkg-manager, libmicrohttpd, libsmb2,
ps5-payload-dev/sdk, etaHEN, ezremote.

License:
GPLv3 or later. Third-party licenses apply; libmicrohttpd and libsmb2 are LGPL.`);
});
parentBtn.addEventListener("click", actionParentDirectory);
nameFilterBtn.addEventListener("click", event => {
  event.stopPropagation();
  if (busy || loadingPath) return;
  if (nameFilter) nameFilter = "";
  else {
    const value = prompt(t("nameFilterPrompt"));
    if (!value) return;
    nameFilter = value;
  }
  selected.clear();
  updateNameFilterButton();
  render();
  resetScrollTop();
});
textEditorCloseBtn.addEventListener("click", requestCloseTextEditor);
textEditorSaveBtn.addEventListener("click", saveTextEditor);
newTextBtn.addEventListener("click", actionNewText);
imagePreviewCloseBtn.addEventListener("click", closeImagePreview);
pkgInfoCloseBtn.addEventListener("click", closePkgInfo);
pkgInfoInstallBtn.addEventListener("click", installPkgFromInfo);
pkgInfoImageEl.addEventListener("error", () => {
  if (pkgInfoImageEl.getAttribute("src") !== "/icon-pkg.png") {
    pkgInfoImageEl.src = "/icon-pkg.png";
  }
});
manageSmbBtn.addEventListener("click", openSmbManager);
smbManagerAddBtn.addEventListener("click", addSmbConnection);
smbManagerCloseBtn.addEventListener("click", closeSmbManager);
document.getElementById("smbShareConnectBtn").addEventListener("click", () => {
  if (smbShareChoice) smbShareChoice(smbShareSelectEl.value);
});
document.getElementById("smbShareCancelBtn").addEventListener("click", () => {
  if (smbShareChoice) smbShareChoice(null);
});
smbManagerOverlayEl.addEventListener("click", event => {
  if (event.target === smbManagerOverlayEl) closeSmbManager();
});
permissionCancelBtn.addEventListener("click", requestClosePermissionDialog);
permissionApplyBtn.addEventListener("click", applyPermissionMode);
extractCancelBtn.addEventListener("click", closeExtractDialog);
extractApplyBtn.addEventListener("click", submitExtractDialog);
extractSeparateEl.addEventListener("change", () => syncExtractDestination(false));
extractCurrentEl.addEventListener("change", () => syncExtractDestination(false));
extractCustomEl.addEventListener("change", () => syncExtractDestination(true));
permissionModeEl.addEventListener("input", () => {
  if (validPermissionMode(permissionModeEl.value)) syncPermissionChecks(permissionModeEl.value);
});
permissionModeEl.addEventListener("change", validatePermissionMode);
for (const checkbox of permissionChecks) checkbox.addEventListener("change", syncPermissionMode);
window.addEventListener("resize", () => {
  fitSpaceOptions();
  renderAddressPath();
  renderSinglePermissionPath();
  renderSingleExtractPath();
});
window.addEventListener("beforeunload", event => {
  if (!historyBlocked()) return;
  event.preventDefault();
  event.returnValue = "";
});
window.addEventListener("popstate", event => {
  if (historyBlocked()) {
    writeHistoryPath(cwd, false);
    if (!textEditorOverlayEl.hidden) {
      requestCloseTextEditor();
      return;
    }
    if (!permissionOverlayEl.hidden) {
      requestClosePermissionDialog();
      return;
    }
    if (!extractOverlayEl.hidden) {
      closeExtractDialog();
      return;
    }
    if (!pkgInfoOverlayEl.hidden) {
      closePkgInfo();
      return;
    }
    setStatus(t("activeTask"));
    return;
  }
  const path = event.state && event.state.path ? event.state.path : historyPath();
  if (path && path !== cwd) {
    const revealPath = parentPath(cwd) === path ? cwd : "";
    loadAndReveal(path, false, false, true, null, revealPath);
  }
});
document.addEventListener("click", event => {
  if (!uploadMenuEl || uploadMenuEl.contains(event.target)) return;
  uploadMenuEl.classList.remove("open");
});
document.addEventListener("mousedown", () => {
  document.body.classList.remove("keyboard-navigation");
}, true);
document.addEventListener("keydown", event => {
  if (event.key === "Tab" || event.keyCode === 9 ||
      (event.keyCode >= 37 && event.keyCode <= 40)) {
    document.body.classList.add("keyboard-navigation");
  }
  if (!smbManagerOverlayEl.hidden && (event.key === "Tab" || event.keyCode === 9)) {
    const buttons = Array.from(smbManagerOverlayEl.querySelectorAll("select, button:not(:disabled)"))
      .filter(button => button.offsetParent !== null);
    if (!buttons.length) { event.preventDefault(); return; }
    const first = buttons[0];
    const last = buttons[buttons.length - 1];
    if (event.shiftKey && (document.activeElement === first || !smbManagerOverlayEl.contains(document.activeElement))) {
      first.blur();
      last.focus();
      event.preventDefault();
    } else if (!event.shiftKey && (document.activeElement === last || !smbManagerOverlayEl.contains(document.activeElement))) {
      first.focus();
      event.preventDefault();
    }
    return;
  }
  if (event.key !== "Escape" && event.keyCode !== 27) return;
  let handled = true;
  if (!smbManagerOverlayEl.hidden) closeSmbManager();
  else if (!extractOverlayEl.hidden) closeExtractDialog();
  else if (!permissionOverlayEl.hidden) requestClosePermissionDialog();
  else if (!pkgInfoOverlayEl.hidden) closePkgInfo();
  else if (!imagePreviewOverlayEl.hidden) closeImagePreview();
  else if (!textEditorOverlayEl.hidden) requestCloseTextEditor();
  else handled = false;
  if (!handled) return;
  event.preventDefault();
  event.stopPropagation();
}, true);
document.querySelector("thead").addEventListener("click", event => {
  if (busy || loadingPath) return;
  if (parentBtn.contains(event.target)) return;
  let target = event.target;
  while (target && target.tagName !== "TH") target = target.parentNode;
  if (target && target.getAttribute("data-sort")) setSort(target.getAttribute("data-sort"));
});
textEditorOverlayEl.addEventListener("click", event => {
  if (event.target === textEditorOverlayEl) requestCloseTextEditor();
});
imagePreviewOverlayEl.addEventListener("click", event => {
  if (event.target === imagePreviewOverlayEl) closeImagePreview();
});
pkgInfoOverlayEl.addEventListener("click", event => {
  if (event.target === pkgInfoOverlayEl) closePkgInfo();
});
permissionOverlayEl.addEventListener("click", event => {
  if (event.target === permissionOverlayEl) requestClosePermissionDialog();
});
extractOverlayEl.addEventListener("click", event => {
  if (event.target === extractOverlayEl) closeExtractDialog();
});
document.querySelector(".shell").addEventListener("click", event => {
  if (!backgroundInteractionLocked()) return;
  event.preventDefault();
  event.stopPropagation();
}, true);
filesEl.addEventListener("focusin", event => {
  const row = findRowFromTarget(event.target);
  if (row) focusPath(row.dataset.path);
});
filesEl.addEventListener("change", event => {
  if (backgroundInteractionLocked()) {
    event.preventDefault();
    return;
  }
  const row = findRowFromTarget(event.target);
  if (row && event.target.type === "checkbox") togglePath(row.dataset.path, event.target.checked);
});
filesEl.addEventListener("click", event => {
  if (busy || loadingPath) {
    event.preventDefault();
    return;
  }
  const row = findRowFromTarget(event.target);
  if (!row) return;
  let target = event.target;
  while (target && target !== row && !target.classList.contains("row-action") &&
      !target.classList.contains("row-tool")) target = target.parentNode;
  if (target === row) return;
  const item = {
    path: row.dataset.path,
    name: row.dataset.name,
    type: row.dataset.type,
    displayName: decodeFsText(row.dataset.name)
  };
  if (target.classList.contains("row-tool")) {
    event.preventDefault();
    event.stopPropagation();
    runRowAction(item, FILE_ACTIONS[target.dataset.action]);
    return;
  }
  focusPath(item.path);
  if (item.type === "d") {
    setTimeout(async () => {
      if (!await load(item.path, undefined, false, true, "push")) {
        await load(cwd, false, true);
      }
    }, 0);
  }
  else if (isPkgPackage(item)) openPkgInfo(item);
  else if (isElfFile(item)) launchElf(item);
  else if (isSupportedArchive(item)) openExtractDialogForItems([item]);
  else if (isPreviewableImage(item)) openImagePreview(item);
  else if (isEditableText(item)) openTextEditor(item);
  else togglePath(item.path, !selected.has(item.path));
});
selectAllEl.addEventListener("change", async () => {
  if (busy || loadingPath) {
    selectAllEl.checked = selected.size > 0 && selected.size === visibleEntries().length;
    return;
  }
  const checked = selectAllEl.checked;
  const visible = visibleEntries();
  const useLoading = visible.length >= SELECT_ALL_LOADING_THRESHOLD;
  if (useLoading) {
    showContentLoading(t("processing"));
    await nextPaint();
  }
  selected.clear();
  if (checked) visible.forEach(item => selected.add(item.path));
  render();
  if (useLoading) hideContentLoading();
});
contentEl.addEventListener("mousemove", event => {
  document.body.classList.remove("keyboard-navigation");
  hoverRow(findRowFromTarget(event.target));
});
contentEl.addEventListener("mouseleave", () => hoverRow(null));
contentEl.addEventListener("scroll", () => {
  hoverPaused = true;
  clearHoverPath();
  clearTimeout(hoverResumeTimer);
  hoverResumeTimer = setTimeout(() => {
    hoverPaused = false;
  }, 160);
});

async function init() {
  await loadLanguage();
  applyStaticText();
  readSavedSort();
  updateSortHeaders();
  const savedPath = historyPath() || readSavedPath();
  cwd = savedPath;
  renderAddressPath();
  taskRefreshPath = savedPath;
  seedHistoryPath(cwd);
  await refreshSpaces();
  if (initLoadingEl) initLoadingEl.hidden = true;
  await pollTasks();
  if (busy) {
    setStatus(t("activeTask"));
    return;
  }
  if (!await load(savedPath || "/", undefined, false, false) &&
      savedPath !== "/" && await load("/", undefined, false, false)) {
    seedHistoryPath(cwd);
  }
}

init();
setInterval(pollTasks, 1000);
setInterval(refreshSpaces, 10000);
