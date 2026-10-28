(function () {
  "use strict";

  var strings = {
    starting: {{notify.selector.starting}},
    serviceStatus: {{notify.selector.service_status}},
    alreadySelected: {{notify.selector.already_selected}},
    ready: {{notify.selector.ready}},
    unreachable: {{notify.selector.unreachable}},
    timeout: {{notify.selector.timeout}},
    coreUnavailable: {{notify.selector.core_unavailable}}
  };

  function format(message, value) {
    return message.replace(/%s/g, function () { return String(value); });
  }

  var modes = {
    {{selector.first_token}}: { label: {{selector.first_name}} },
    hypercore: { label: "HyperCore" }
  };
  var firmwareFamily = {{selector.family}};
  var coreAvailable = {{selector.core_available}} === "true";
  var selectorSupported = {{selector.supported}} === "true";

  var cards = Array.prototype.slice.call(document.querySelectorAll(".engine-card"));
  var launchScreen = document.getElementById("launchScreen");
  var launchMode = document.getElementById("launchMode");
  var firmwareValue = document.getElementById("firmwareValue");
  var liveStatus = document.getElementById("liveStatus");
  var activeIndex = 0;
  var navigating = false;
  var pressedGamepadButtons = [];

  function toggleClass(node, name, enabled) {
    if (enabled) node.classList.add(name);
    else node.classList.remove(name);
  }

  function setStatus(message, kind) {
    liveStatus.textContent = message;
    toggleClass(liveStatus, "is-ready", kind === "ready");
    toggleClass(liveStatus, "is-error", kind === "error");
  }

  function displayFirmware(value) {
    if (typeof value === "string" && /^\d{1,2}\.\d{2}$/.test(value)) return value;
    if (typeof value === "string" && /^\d{1,2}\.\d$/.test(value)) return value + "0";

    var raw = typeof value === "string" && /^0x/i.test(value)
      ? parseInt(value.slice(2), 16)
      : Number(value);
    if (!isFinite(raw) || raw <= 0) return firmwareFamily;

    var unsigned = raw >>> 0;
    var major = ((unsigned >>> 24) & 0xff).toString(16);
    var minor = ((unsigned >>> 16) & 0xff).toString(16);
    if (minor.length < 2) minor = "0" + minor;
    return unsigned >>> 24 ? major + "." + minor : String(value);
  }

  function removeCursorSnapOverlays() {
    var overlay = document.getElementsByClassName("websrv-cursor-snap-overlay")[0];
    while (overlay) {
      overlay.parentNode.removeChild(overlay);
      overlay = document.getElementsByClassName("websrv-cursor-snap-overlay")[0];
    }
  }

  /* websrv 0.34 PS5 cursor-snap workaround. */
  function generateCursorSnapOverlay() {
    var card;
    var rect;
    var overlay;

    removeCursorSnapOverlays();
    card = cards[activeIndex];
    if (!card || !card.getBoundingClientRect) return;
    rect = card.getBoundingClientRect();

    overlay = document.createElement("a");
    overlay.className = "websrv-cursor-snap-overlay";
    overlay.tabIndex = 0;
    overlay.style.position = "fixed";
    overlay.style.top = Math.round(rect.top + rect.height * (3 / 5)) + "px";
    overlay.style.left = "0";
    overlay.style.right = "0";
    overlay.style.height = "1px";
    overlay.style.pointerEvents = "none";
    (document.getElementById("content") || document.body).appendChild(overlay);
  }

  function selectCard(index) {
    var position;
    activeIndex = (index + cards.length) % cards.length;
    for (position = 0; position < cards.length; position += 1) {
      toggleClass(cards[position], "is-focused", position === activeIndex);
    }
    generateCursorSnapOverlay();
  }

  function showLaunch(mode) {
    launchMode.textContent = modes[mode] ? modes[mode].label : "HyperHEN";
    launchScreen.hidden = false;
    document.body.style.overflow = "hidden";
  }

  function activateCard(card) {
    var mode;
    var target;
    if (navigating || !card) return;

    mode = card.getAttribute("data-mode");
    target = card.getAttribute("data-native-href");
    if (!selectorSupported || card.getAttribute("aria-disabled") === "true" ||
        (mode === "hypercore" && !coreAvailable)) {
      setStatus(strings.coreUnavailable, "error");
      return;
    }
    if (!modes[mode] || !target) return;

    navigating = true;
    setStatus(format(strings.starting, modes[mode].label), "ready");
    showLaunch(mode);
    window.location.href = target;
  }

  function requestSession() {
    var request = new XMLHttpRequest();
    request.open("GET", "/api/boot-mode", true);
    request.timeout = 10000;
    request.setRequestHeader("Accept", "application/json");
    request.onload = function () {
      var payload = {};
      var firmware;
      var selected;
      try {
        if (request.responseText) payload = JSON.parse(request.responseText);
      } catch (_) {
        payload = {};
      }

      if (request.status < 200 || request.status >= 300) {
        setStatus(format(strings.serviceStatus, request.status), "error");
        return;
      }

      firmware = payload.firmwareLabel != null
        ? payload.firmwareLabel
        : payload.firmware != null ? payload.firmware : payload.firmware_version;
      firmwareValue.textContent = displayFirmware(firmware);

      selected = payload.selected != null ? payload.selected : payload.mode;
      if (selected && modes[selected]) {
        if (!selectorSupported || (selected === "hypercore" && !coreAvailable)) {
          setStatus(strings.coreUnavailable, "error");
          return;
        }
        navigating = true;
        setStatus(format(strings.alreadySelected, modes[selected].label), "ready");
        showLaunch(selected);
        return;
      }

      setStatus(strings.ready, "ready");
    };
    request.onerror = function () {
      setStatus(strings.unreachable, "error");
    };
    request.ontimeout = function () {
      setStatus(strings.timeout, "error");
    };
    request.send(null);
  }

  function attachCard(card, index) {
    card.onclick = function () {
      selectCard(index);
      activateCard(card);
    };
    card.onmouseover = function () {
      selectCard(index);
    };
  }

  cards.forEach(attachCard);

  document.addEventListener("keydown", function (event) {
    var keyCode = event.keyCode || event.which || 0;
    if (navigating) return;

    if (keyCode === 116) {
      event.preventDefault();
      selectCard(0);
    } else if (keyCode === 117) {
      event.preventDefault();
      selectCard(cards.length - 1);
    } else if (keyCode === 37 || keyCode === 38) {
      event.preventDefault();
      selectCard(activeIndex - 1);
    } else if (keyCode === 39 || keyCode === 40) {
      event.preventDefault();
      selectCard(activeIndex + 1);
    } else if (keyCode === 13 || keyCode === 32) {
      event.preventDefault();
      activateCard(cards[activeIndex]);
    }
  });

  function onGamepadPress(button) {
    if (navigating) return;
    if (button === 12 || button === 14) selectCard(activeIndex - 1);
    if (button === 13 || button === 15) selectCard(activeIndex + 1);
    if (button === 0) activateCard(cards[activeIndex]);
  }

  function pollGamepad() {
    var pads = [];
    var pad = null;
    var index;
    var pressed;

    try {
      pads = navigator.getGamepads ? navigator.getGamepads() : [];
    } catch (_) {
      pads = [];
    }

    for (index = 0; pads && index < pads.length; index += 1) {
      if (pads[index] && pads[index].connected !== false) {
        pad = pads[index];
        break;
      }
    }

    if (pad) {
      for (index = 0; index < pad.buttons.length; index += 1) {
        pressed = pad.buttons[index].pressed || pad.buttons[index].value > 0.65;
        if (pressed && !pressedGamepadButtons[index]) onGamepadPress(index);
        pressedGamepadButtons[index] = pressed;
      }
    } else {
      pressedGamepadButtons = [];
    }

    window.setTimeout(pollGamepad, document.hidden ? 300 : 50);
  }

  window.addEventListener("resize", generateCursorSnapOverlay);
  selectCard(0);
  requestSession();
  pollGamepad();
}());
