/*
 * Dynalogic Hyperion (1983) WebAssembly Frontend
 */

(() => {
  const SCREEN_W = 640;
  const SCREEN_H = 250;

  // Map standard browser KeyboardEvent.code to IBM PC/XT Set 1 Scan Codes (0x01 - 0x53)
  const CODE_TO_XT_SCANCODE = {
    Escape: 0x01,
    Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05, Digit5: 0x06,
    Digit6: 0x07, Digit7: 0x08, Digit8: 0x09, Digit9: 0x0A, Digit0: 0x0B,
    Minus: 0x0C, Equal: 0x0D, Backspace: 0x0E,
    Tab: 0x0F,
    KeyQ: 0x10, KeyW: 0x11, KeyE: 0x12, KeyR: 0x13, KeyT: 0x14,
    KeyY: 0x15, KeyU: 0x16, KeyI: 0x17, KeyO: 0x18, KeyP: 0x19,
    BracketLeft: 0x1A, BracketRight: 0x1B, Enter: 0x1C, NumpadEnter: 0x1C,
    ControlLeft: 0x1D, ControlRight: 0x1D,
    KeyA: 0x1E, KeyS: 0x1F, KeyD: 0x20, KeyF: 0x21, KeyG: 0x22,
    KeyH: 0x23, KeyJ: 0x24, KeyK: 0x25, KeyL: 0x26,
    Semicolon: 0x27, Quote: 0x28, Backquote: 0x29,
    ShiftLeft: 0x2A, Backslash: 0x2B,
    KeyZ: 0x2C, KeyX: 0x2D, KeyC: 0x2E, KeyV: 0x2F, KeyB: 0x30,
    KeyN: 0x31, KeyM: 0x32, Comma: 0x33, Period: 0x34, Slash: 0x35, NumpadDivide: 0x35,
    ShiftRight: 0x36, NumpadMultiply: 0x37,
    AltLeft: 0x38, AltRight: 0x38, Space: 0x39, CapsLock: 0x3A,
    F1: 0x3B, F2: 0x3C, F3: 0x3D, F4: 0x3E, F5: 0x3F,
    F6: 0x40, F7: 0x41, F8: 0x42, F9: 0x43, F10: 0x44,
    NumLock: 0x45, ScrollLock: 0x46,
    Home: 0x47, Numpad7: 0x47,
    ArrowUp: 0x48, Numpad8: 0x48,
    PageUp: 0x49, Numpad9: 0x49,
    NumpadSubtract: 0x4A,
    ArrowLeft: 0x4B, Numpad4: 0x4B,
    Numpad5: 0x4C,
    ArrowRight: 0x4D, Numpad6: 0x4D,
    NumpadAdd: 0x4E,
    End: 0x4F, Numpad1: 0x4F,
    ArrowDown: 0x50, Numpad2: 0x50,
    PageDown: 0x51, Numpad3: 0x51,
    Insert: 0x52, Numpad0: 0x52,
    Delete: 0x53, NumpadDecimal: 0x53
  };

  const VIDEO_MODE_NAMES = {
    0x00: "00h (40x25 Text)",
    0x01: "01h (40x25 Text)",
    0x02: "02h (80x25 IBM Text)",
    0x03: "03h (80x25 IBM Text)",
    0x04: "04h (320x200 CGA Graph)",
    0x05: "05h (320x200 CGA Graph)",
    0x06: "06h (640x200 CGA Graph)",
    0x07: "07h (80x25 Mono Text)",
    0x80: "80h (40x25 Hyperion Text)",
    0x81: "81h (40x25 Hyperion Text)",
    0x82: "82h (80x25 Hyperion Text)",
    0x83: "83h (80x25 Hyperion Text)",
    0x84: "84h (320x250 4-Shade Amber)",
    0x85: "85h (320x250 4-Shade Amber)",
    0x86: "86h (640x250 Hi-Res Amber)"
  };

  const VRAM_MAP_NAMES = [
    "0 (Disabled)",
    "1 (B000h + B800h)",
    "2 (B800h CGA)",
    "3 (B000h Mono)"
  ];

  // DOM Elements
  const canvas = document.getElementById("crt-canvas");
  const ctx = canvas.getContext("2d", { alpha: false });
  const imgData = ctx.createImageData(SCREEN_W, SCREEN_H);
  const crtWrap = document.getElementById("crt-wrap");

  const biosSelect = document.getElementById("bios-select");
  const ramSelect = document.getElementById("ram-select");
  const btnWarmBoot = document.getElementById("btn-warm-boot");
  const btnColdReset = document.getElementById("btn-cold-reset");
  const btnAudio = document.getElementById("btn-audio");
  const btnCrtFx = document.getElementById("btn-crt-fx");
  const btnFullscreen = document.getElementById("btn-fullscreen");

  const diskSelects = [
    document.getElementById("disk-select-0"),
    document.getElementById("disk-select-1")
  ];
  const diskUploads = [
    document.getElementById("disk-upload-0"),
    document.getElementById("disk-upload-1")
  ];
  const diskWps = [
    document.getElementById("disk-wp-0"),
    document.getElementById("disk-wp-1")
  ];
  const speedSelect = document.getElementById("speed-select");
  const btnPcKbd = document.getElementById("btn-pc-kbd");
  let pcKbdPort = false;
  const diskStatuses = [
    document.getElementById("disk-status-0"),
    document.getElementById("disk-status-1")
  ];
  const driveLeds = [
    document.getElementById("led-drive-0"),
    document.getElementById("led-drive-1")
  ];

  const diskSelectHd = document.getElementById("disk-select-hd");
  const diskUploadHd = document.getElementById("disk-upload-hd");
  const diskStatusHd = document.getElementById("disk-status-hd");
  const ledDriveHd = document.getElementById("led-drive-hd");
  const btnDownloadHd = document.getElementById("btn-download-hd");
  const presetHd = document.getElementById("preset-hd");

  const teleCpu = document.getElementById("tele-cpu");
  const teleRegs = document.getElementById("tele-regs");
  const teleSpeed = document.getElementById("tele-speed");
  const teleVideo = document.getElementById("tele-video");
  const teleVmap = document.getElementById("tele-vmap");
  const teleStatus = document.getElementById("tele-status");

  let emuFramesExecuted = 0;
  let lastSpeedMeasureTime = performance.now();
  let lastFrameTime = performance.now();
  let frameAccumulator = 0;
  const TARGET_FRAME_MS = 1000 / 60; // 16.666 ms
  let speedMultiplier = 1;
  let speedMode = "1";

  let wasmMemory = null;
  let ex = null;
  let audioCtx = null;
  let oscNode = null;
  let gainNode = null;
  let audioEnabled = false;

  function syncRtcToHost() {
    if (!ex) return;
    const now = new Date();
    ex.emu_set_rtc(
      now.getFullYear(),
      now.getMonth() + 1,
      now.getDate(),
      now.getDay(),
      now.getHours(),
      now.getMinutes(),
      now.getSeconds()
    );
  }

  async function fetchDiskImage(filename) {
    const candidates = [
      `disks/${filename}`,
      `../archive/software/${filename}`,
      `../archive/software/Multimate/${filename}`
    ];
    for (const url of candidates) {
      try {
        const resp = await fetch(url);
        if (resp.ok) {
          return new Uint8Array(await resp.arrayBuffer());
        }
      } catch (_) {
        // try next candidate
      }
    }
    throw new Error(`Could not fetch diskette ${filename}`);
  }

  function mountDiskBytes(drive, bytes, label) {
    if (!ex) return;
    const ptr = ex.emu_get_disk_upload_buffer(bytes.length);
    if (!ptr) {
      diskStatuses[drive].textContent = "Too large";
      return;
    }
    new Uint8Array(wasmMemory.buffer, ptr, bytes.length).set(bytes);
    const tracks = ex.emu_load_disk(drive, ptr, bytes.length);
    if (tracks > 0) {
      diskStatuses[drive].textContent = `${tracks} trk (${(bytes.length / 1024).toFixed(0)} KB)`;
      teleStatus.textContent = `Mounted ${label} in Drive ${drive === 0 ? "A:" : "B:"}`;
    } else {
      diskStatuses[drive].textContent = "Load error";
    }
  }

  async function loadSelectedDisk(drive) {
    const name = diskSelects[drive].value;
    if (!name) {
      ex.emu_eject_disk(drive);
      diskStatuses[drive].textContent = "Ejected";
      return;
    }
    diskStatuses[drive].textContent = "Loading...";
    try {
      const bytes = await fetchDiskImage(name);
      mountDiskBytes(drive, bytes, name);
    } catch (err) {
      diskStatuses[drive].textContent = "Missing file";
      teleStatus.textContent = err.message;
    }
  }

  function mountHdBytes(bytes, label) {
    if (!ex) return;
    const ptr = ex.emu_get_hd_ptr();
    if (!ptr || bytes.length > 22 * 1024 * 1024) {
      if (diskStatusHd) diskStatusHd.textContent = "Too large";
      return;
    }
    new Uint8Array(wasmMemory.buffer, ptr, bytes.length).set(bytes);
    const res = ex.emu_load_hd(ptr, bytes.length);
    if (res === 0) {
      const mb = (bytes.length / (1024 * 1024)).toFixed(0);
      const cyls = bytes.length >= 20000000 ? 615 : 306;
      if (diskStatusHd) diskStatusHd.textContent = `${mb} MB (${cyls} cyls)`;
      teleStatus.textContent = `Mounted ${label} as Hard Disk C:`;
    } else {
      if (diskStatusHd) diskStatusHd.textContent = "Load error";
    }
  }

  async function loadSelectedHd() {
    if (!diskSelectHd || !ex) return;
    const val = diskSelectHd.value;
    if (!val) {
      ex.emu_eject_hd();
      if (diskStatusHd) diskStatusHd.textContent = "Ejected";
      teleStatus.textContent = "Hard Disk C: ejected";
      return;
    }
    if (val === "blank10") {
      ex.emu_create_blank_hd(10);
      if (diskStatusHd) diskStatusHd.textContent = "10 MB (306 cyls)";
      teleStatus.textContent = "Created blank 10MB Hard Disk (ST-412)";
      return;
    }
    if (val === "blank20") {
      ex.emu_create_blank_hd(20);
      if (diskStatusHd) diskStatusHd.textContent = "20 MB (615 cyls)";
      teleStatus.textContent = "Created blank 20MB Hard Disk (ST-225)";
      return;
    }
    if (diskStatusHd) diskStatusHd.textContent = "Loading...";
    try {
      const bytes = await fetchDiskImage(val);
      mountHdBytes(bytes, val);
    } catch (err) {
      if (diskStatusHd) diskStatusHd.textContent = "Missing file";
      teleStatus.textContent = err.message;
    }
  }

  function downloadHdImage() {
    if (!ex) return;
    const size = ex.emu_get_hd_size();
    if (size <= 0) {
      teleStatus.textContent = "No Hard Disk mounted to save";
      return;
    }
    const ptr = ex.emu_get_hd_ptr();
    const data = new Uint8Array(wasmMemory.buffer, ptr, size);
    const copy = new Uint8Array(size);
    copy.set(data);
    const blob = new Blob([copy], { type: "application/octet-stream" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    const val = diskSelectHd ? diskSelectHd.value : "";
    a.download = (val && val.toUpperCase().endsWith(".IMG"))
      ? val
      : `HYPERION_${(size / (1024 * 1024)).toFixed(0)}MB.IMG`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    teleStatus.textContent = `Saved ${a.download} (${(size / (1024 * 1024)).toFixed(1)} MB)`;
  }

  function resetMachine(warm = false) {
    if (!ex) return;
    if (warm) {
      syncRtcToHost();
      ex.emu_reset(1);
      teleStatus.textContent = "Warm Reboot (Ctrl+Alt+Del)";
    } else {
      const biosRev = parseInt(biosSelect.value, 10) || 0;
      const ramKb = parseInt(ramSelect.value, 10) || 256;
      ex.emu_init(biosRev, ramKb);
      if (ex.emu_set_cpu_speed) ex.emu_set_cpu_speed(speedMultiplier);
      syncRtcToHost();
      teleStatus.textContent = `Cold Reset (BIOS ${biosRev === 1 ? "C1" : "C7"}, ${ramKb} KB RAM)`;
    }
  }

  function fastForwardBoot(maxFrames = 1000) {
    if (!ex) return;
    const vramPtr = ex.emu_get_vram();
    const ramPtr = ex.emu_get_ram();
    for (let i = 0; i < maxFrames; i++) {
      ex.emu_run_frame();
      const ram = new Uint8Array(wasmMemory.buffer, ramPtr, 0x500);
      const pageOff = ram[0x44E] | (ram[0x44F] << 8);
      const vram = new Uint8Array(wasmMemory.buffer, vramPtr + pageOff, 4000);
      let found = false;
      for (let offset = 0; offset < 240 * 2; offset += 2) {
        const b0 = vram[offset], b1 = vram[offset + 2], b2 = vram[offset + 4], b3 = vram[offset + 6];
        if (b0 === 68 && b1 === 65 && b2 === 84 && b3 === 69) { // DATE
          found = true;
          break;
        }
        if ((b0 === 65 || b0 === 67) && b1 === 62) { // A> or C>
          found = true;
          break;
        }
        if (b0 === 72 && b1 === 121 && b2 === 112 && b3 === 101) { // Hype
          found = true;
          break;
        }
      }
      if (found) {
        for (let extra = 0; extra < 25; extra++) ex.emu_run_frame();
        break;
      }
    }
  }

  function updateSoftKeyLabelsFromVram() {
    if (!ex) return;
    const vramPtr = ex.emu_get_vram();
    const vram = new Uint8Array(wasmMemory.buffer, vramPtr, 4000);
    const spans = [
      [0, 6], [7, 13], [14, 20], [21, 27], [28, 34],
      [45, 51], [52, 58], [59, 65], [66, 72], [73, 79]
    ];
    for (let i = 0; i < 10; i++) {
      const [c0, c1] = spans[i];
      let s = "";
      for (let c = c0; c <= c1; c++) {
        const ch = vram[(24 * 80 + c) * 2];
        s += (ch >= 32 && ch < 127) ? String.fromCharCode(ch) : " ";
      }
      const trimmed = s.trim();
      const el = document.getElementById(`sk-lbl-${i}`);
      if (el) el.textContent = trimmed || "\u2014";
    }
  }

  function updateSpeakerAudio() {
    if (!audioEnabled || !audioCtx || !oscNode || !gainNode || !ex) return;
    const freq = ex.emu_get_speaker_freq();
    if (freq >= 40 && freq <= 12000) {
      oscNode.frequency.setValueAtTime(freq, audioCtx.currentTime);
      gainNode.gain.setTargetAtTime(0.08, audioCtx.currentTime, 0.005);
    } else {
      gainNode.gain.setTargetAtTime(0.0, audioCtx.currentTime, 0.005);
    }
  }

  function hex16(val) {
    return (val & 0xFFFF).toString(16).toUpperCase().padStart(4, "0");
  }

  function renderFrame() {
    if (ex) {
      const now = performance.now();
      let delta = now - lastFrameTime;
      if (delta > 250) delta = 250;
      lastFrameTime = now;

      if (speedMode === "max") {
        const start = performance.now();
        let ran = 0;
        while (performance.now() - start < 12 && ran < 20) {
          ex.emu_run_frame();
          ran++;
        }
        emuFramesExecuted += ran;
      } else {
        frameAccumulator += delta;
        let framesToRun = Math.floor(frameAccumulator / TARGET_FRAME_MS);
        if (framesToRun > 10) framesToRun = 10;
        if (framesToRun > 0) {
          frameAccumulator -= framesToRun * TARGET_FRAME_MS;
          for (let i = 0; i < framesToRun; i++) {
            ex.emu_run_frame();
          }
          emuFramesExecuted += framesToRun;
        }
      }

      // Copy 640x250 RGBA framebuffer to Canvas
      const fbPtr = ex.emu_get_framebuffer();
      const fbBytes = new Uint8ClampedArray(wasmMemory.buffer, fbPtr, SCREEN_W * SCREEN_H * 4);
      imgData.data.set(fbBytes);
      ctx.putImageData(imgData, 0, 0);

      // Update Drive LEDs
      driveLeds[0].classList.toggle("on", ex.emu_get_drive_led(0) !== 0);
      driveLeds[1].classList.toggle("on", ex.emu_get_drive_led(1) !== 0);
      if (ledDriveHd && ex.emu_get_hd_led) {
        ledDriveHd.classList.toggle("on", ex.emu_get_hd_led() !== 0);
      }

      // Update Soft Key labels from Row 24
      updateSoftKeyLabelsFromVram();

      // Update PC Speaker
      updateSpeakerAudio();

      // Measure & update speed telemetry every 500ms
      const elapsed = now - lastSpeedMeasureTime;
      if (elapsed >= 500) {
        const fps = (emuFramesExecuted / elapsed) * 1000;
        if (teleSpeed) {
          teleSpeed.textContent = speedMode === "max"
            ? `Max (${Math.round(fps)} fps)`
            : `${speedMultiplier}x (${Math.round(fps)} fps, ${(speedMultiplier * 4.77).toFixed(1)} MHz)`;
        }
        emuFramesExecuted = 0;
        lastSpeedMeasureTime = now;
      }

      // Update Telemetry
      const cs = hex16(ex.emu_get_reg(9));
      const ip = hex16(ex.emu_get_ip());
      const ax = hex16(ex.emu_get_reg(0));
      const bx = hex16(ex.emu_get_reg(3));
      const cx = hex16(ex.emu_get_reg(1));
      const dx = hex16(ex.emu_get_reg(2));
      const vmode = ex.emu_get_video_mode();
      const vmap = ex.emu_get_vram_map() & 3;

      teleCpu.textContent = `CS:${cs} IP:${ip}`;
      teleRegs.textContent = `AX:${ax} BX:${bx} CX:${cx} DX:${dx}`;
      teleVideo.textContent = VIDEO_MODE_NAMES[vmode] || `${vmode.toString(16).toUpperCase()}h`;
      teleVmap.textContent = VRAM_MAP_NAMES[vmap];
    }
    requestAnimationFrame(renderFrame);
  }

  function sendAsciiString(str, delayMs = 25) {
    let idx = 0;
    const timer = setInterval(() => {
      if (idx >= str.length) {
        clearInterval(timer);
        return;
      }
      ex.emu_ascii_keypress(str.charCodeAt(idx++));
    }, delayMs);
  }

  function tapXtKey(xtCode) {
    if (!ex) return;
    ex.emu_xt_key_event(xtCode, 1);
    const delay = speedMode === "max" ? 10 : Math.max(15, Math.round(40 / Math.min(speedMultiplier, 4)));
    setTimeout(() => ex.emu_xt_key_event(xtCode, 0), delay);
  }

  // Event Listeners (Capture keyboard globally unless typing in an input/textarea)
  window.addEventListener("keydown", (e) => {
    if (!ex) return;
    const active = document.activeElement;
    if (active && (active.tagName === "INPUT" || active.tagName === "TEXTAREA")) return;
    // Allow browser paste (Ctrl+V / Cmd+V)
    if ((e.ctrlKey || e.metaKey) && e.code === "KeyV") return;
    if (e.repeat) return;
    const xt = CODE_TO_XT_SCANCODE[e.code];
    if (xt) {
      e.preventDefault();
      ex.emu_xt_key_event(xt, 1);
    }
  });

  window.addEventListener("keyup", (e) => {
    if (!ex) return;
    const active = document.activeElement;
    if (active && (active.tagName === "INPUT" || active.tagName === "TEXTAREA")) return;
    if ((e.ctrlKey || e.metaKey) && e.code === "KeyV") return;
    const xt = CODE_TO_XT_SCANCODE[e.code];
    if (xt) {
      e.preventDefault();
      ex.emu_xt_key_event(xt, 0);
    }
  });

  // Clipboard Paste support (like Dorado)
  window.addEventListener("paste", (e) => {
    if (!ex || document.activeElement === document.getElementById("type-input")) return;
    const text = e.clipboardData ? e.clipboardData.getData("text") : "";
    if (text) {
      e.preventDefault();
      const normalized = text.replace(/\r?\n/g, "\r");
      sendAsciiString(normalized, 20);
      teleStatus.textContent = `Pasted ${normalized.length} chars`;
    }
  });

  // Drag-and-drop diskette onto CRT screen (like Dorado)
  crtWrap.addEventListener("dragover", (e) => {
    e.preventDefault();
    crtWrap.classList.add("drop");
  });
  crtWrap.addEventListener("dragleave", () => {
    crtWrap.classList.remove("drop");
  });
  crtWrap.addEventListener("drop", async (e) => {
    e.preventDefault();
    crtWrap.classList.remove("drop");
    const file = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
    if (!file || !ex) return;
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (bytes.length >= 2 * 1024 * 1024) {
      // Large raw sector image -> Mount as Hard Disk C:
      mountHdBytes(bytes, file.name);
      if (diskSelectHd) {
        let opt = Array.from(diskSelectHd.options).find(o => o.value === file.name);
        if (!opt) {
          opt = document.createElement("option");
          opt.value = file.name;
          opt.textContent = `Custom: ${file.name}`;
          diskSelectHd.insertBefore(opt, diskSelectHd.firstChild);
        }
        diskSelectHd.value = file.name;
      }
      resetMachine(false);
    } else {
      // Standard floppy diskette -> Mount in Drive A:
      mountDiskBytes(0, bytes, file.name);
      resetMachine(false);
    }
    crtWrap.focus();
  });

  document.querySelectorAll(".softkey").forEach((btn) => {
    btn.addEventListener("click", () => {
      const xt = parseInt(btn.dataset.xt, 16);
      tapXtKey(xt);
      crtWrap.focus();
    });
  });

  biosSelect.addEventListener("change", () => resetMachine(false));
  ramSelect.addEventListener("change", () => resetMachine(false));
  btnWarmBoot.addEventListener("click", () => resetMachine(true));
  btnColdReset.addEventListener("click", () => resetMachine(false));

  btnCrtFx.addEventListener("click", () => {
    const active = crtWrap.classList.toggle("scanlines");
    btnCrtFx.classList.toggle("active", active);
    btnCrtFx.setAttribute("aria-pressed", String(active));
  });

  function toggleFullscreen() {
    if (!document.fullscreenElement && !document.webkitFullscreenElement) {
      if (crtWrap.requestFullscreen) {
        crtWrap.requestFullscreen();
      } else if (crtWrap.webkitRequestFullscreen) {
        crtWrap.webkitRequestFullscreen();
      }
    } else {
      if (document.exitFullscreen) {
        document.exitFullscreen();
      } else if (document.webkitExitFullscreen) {
        document.webkitExitFullscreen();
      }
    }
  }

  if (btnFullscreen) {
    btnFullscreen.addEventListener("click", toggleFullscreen);
  }
  crtWrap.addEventListener("dblclick", toggleFullscreen);

  function onFullscreenChange() {
    const isFs = !!(document.fullscreenElement || document.webkitFullscreenElement);
    if (btnFullscreen) {
      btnFullscreen.classList.toggle("active", isFs);
      btnFullscreen.setAttribute("aria-pressed", String(isFs));
      btnFullscreen.textContent = isFs ? "Exit Fullscreen" : "Fullscreen";
    }
  }
  document.addEventListener("fullscreenchange", onFullscreenChange);
  document.addEventListener("webkitfullscreenchange", onFullscreenChange);

  btnAudio.addEventListener("click", () => {
    if (!audioCtx) {
      audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      oscNode = audioCtx.createOscillator();
      gainNode = audioCtx.createGain();
      oscNode.type = "square";
      gainNode.gain.value = 0;
      oscNode.connect(gainNode);
      gainNode.connect(audioCtx.destination);
      oscNode.start();
    }
    audioEnabled = !audioEnabled;
    if (audioEnabled && audioCtx.state === "suspended") {
      audioCtx.resume();
    }
    if (!audioEnabled && gainNode) {
      gainNode.gain.setTargetAtTime(0, audioCtx.currentTime, 0.005);
    }
    btnAudio.textContent = `Speaker: ${audioEnabled ? "On" : "Off"}`;
    btnAudio.classList.toggle("active", audioEnabled);
    btnAudio.setAttribute("aria-pressed", String(audioEnabled));
  });

  btnPcKbd.addEventListener("click", () => {
    pcKbdPort = !pcKbdPort;
    if (ex) ex.emu_set_pc_keyboard_port(pcKbdPort ? 1 : 0);
    btnPcKbd.textContent = `PC Kbd 60h: ${pcKbdPort ? "On" : "Off"}`;
    btnPcKbd.classList.toggle("active", pcKbdPort);
    btnPcKbd.setAttribute("aria-pressed", String(pcKbdPort));
    teleStatus.textContent = pcKbdPort
      ? "PC keyboard port 60h on (reset if a PC game's keyboard is already stuck)"
      : "PC keyboard port 60h off";
  });

  function setSpeed(val) {
    speedMode = String(val);
    if (speedMode === "max") {
      speedMultiplier = 16;
      if (speedSelect) speedSelect.value = "max";
      teleStatus.textContent = "Speed: Max (Uncapped Warp)";
    } else {
      speedMultiplier = Math.max(1, parseInt(val, 10) || 1);
      if (speedSelect) speedSelect.value = String(speedMultiplier);
      teleStatus.textContent = speedMultiplier === 1
        ? "Speed: 1x (4.77 MHz Authentic 8088)"
        : `Speed: ${speedMultiplier}x (${(speedMultiplier * 4.77).toFixed(1)} MHz)`;
    }
    if (ex && ex.emu_set_cpu_speed) {
      ex.emu_set_cpu_speed(speedMultiplier);
    }
  }

  if (speedSelect) {
    speedSelect.addEventListener("change", (e) => {
      setSpeed(e.target.value);
      speedSelect.blur();
      crtWrap.focus();
    });
  }

  [0, 1].forEach((drive) => {
    diskWps[drive].addEventListener("change", () => {
      if (ex) ex.emu_set_write_protect(drive, diskWps[drive].checked ? 1 : 0);
    });
    diskSelects[drive].addEventListener("change", () => loadSelectedDisk(drive));
    diskUploads[drive].addEventListener("change", async (e) => {
      const file = e.target.files[0];
      if (!file) return;
      const buf = new Uint8Array(await file.arrayBuffer());
      mountDiskBytes(drive, buf, file.name);
    });
  });

  if (diskSelectHd) {
    diskSelectHd.addEventListener("change", () => loadSelectedHd());
  }
  if (diskUploadHd) {
    diskUploadHd.addEventListener("change", async (e) => {
      const file = e.target.files && e.target.files[0];
      if (!file) return;
      const buf = new Uint8Array(await file.arrayBuffer());
      mountHdBytes(buf, file.name);
      if (diskSelectHd) {
        let opt = Array.from(diskSelectHd.options).find(o => o.value === file.name);
        if (!opt) {
          opt = document.createElement("option");
          opt.value = file.name;
          opt.textContent = `Custom: ${file.name}`;
          diskSelectHd.insertBefore(opt, diskSelectHd.firstChild);
        }
        diskSelectHd.value = file.name;
      }
    });
  }
  if (btnDownloadHd) {
    btnDownloadHd.addEventListener("click", downloadHdImage);
  }

  async function bootPreset(presetName) {
    if (presetName === "dos125") {
      diskSelects[0].value = "HYPMSTRU.IMD";
      diskSelects[1].value = "HYPMSPRG.IMD";
      await Promise.all([loadSelectedDisk(0), loadSelectedDisk(1), loadSelectedHd()]);
      resetMachine(false);
      fastForwardBoot();
      sendAsciiString("\r\r", 25);
      teleStatus.textContent = "Booted Hyperion H-DOS 1.25H";
    } else if (presetName === "diag") {
      diskSelects[0].value = "HYPDIAG.IMD";
      await loadSelectedDisk(0);
      resetMachine(false);
      fastForwardBoot();
      teleStatus.textContent = "Booted Master Diagnostic Diskette";
    } else if (presetName === "multiplan") {
      diskSelects[0].value = "HYPDOS21.IMD";
      diskSelects[1].value = "MULTIPLN.IMD";
      await Promise.all([loadSelectedDisk(0), loadSelectedDisk(1), loadSelectedHd()]);
      resetMachine(false);
      fastForwardBoot();
      sendAsciiString("\r\rB:\rMP\r", 25);
      teleStatus.textContent = "Launching Microsoft Multiplan (B:MP)...";
    } else if (presetName === "hd") {
      diskSelects[0].value = "";
      diskSelects[1].value = "";
      if (!diskSelectHd.value) diskSelectHd.value = "HYP10MB.IMG";
      await Promise.all([loadSelectedDisk(0), loadSelectedDisk(1), loadSelectedHd()]);
      resetMachine(false);
      fastForwardBoot();
      sendAsciiString("\r\rDIR\r", 25);
      teleStatus.textContent = "Self-booted MS-DOS 2.11 from Hard Disk C:";
    } else {
      // Default: Hyperion MS-DOS 2.11 System Diskette
      diskSelects[0].value = "HYPDOS21.IMD";
      diskSelects[1].value = "MULTIPLN.IMD";
      await Promise.all([loadSelectedDisk(0), loadSelectedDisk(1), loadSelectedHd()]);
      resetMachine(false);
      fastForwardBoot();
      sendAsciiString("\r\r", 25);
      teleStatus.textContent = "Booted Hyperion MS-DOS 2.11";
    }
    crtWrap.focus();
  }

  document.getElementById("preset-dos125").addEventListener("click", () => bootPreset("dos125"));
  document.getElementById("preset-dos211").addEventListener("click", () => bootPreset("dos211"));
  if (presetHd) {
    presetHd.addEventListener("click", () => bootPreset("hd"));
  }
  document.getElementById("preset-diag").addEventListener("click", () => bootPreset("diag"));
  document.getElementById("macro-multiplan").addEventListener("click", async () => {
    if (diskSelects[1].value !== "MULTIPLN.IMD") {
      diskSelects[1].value = "MULTIPLN.IMD";
      await loadSelectedDisk(1);
    }
    sendAsciiString("B:\rMP\r", 35);
    teleStatus.textContent = "Launching Microsoft Multiplan (B:MP)...";
    crtWrap.focus();
  });

  // Dorado-style "Try it" buttons in #guide
  document.querySelectorAll("#guide button.try").forEach((btn) => {
    btn.addEventListener("click", async () => {
      if (!ex) return;
      const cmd = btn.dataset.cmd;
      const action = btn.dataset.action;
      if (cmd) {
        sendAsciiString(cmd + "\r", 25);
        teleStatus.textContent = `Running: ${cmd}`;
      } else if (action === "multiplan") {
        if (diskSelects[1].value !== "MULTIPLN.IMD") {
          diskSelects[1].value = "MULTIPLN.IMD";
          await loadSelectedDisk(1);
        }
        sendAsciiString("B:\rMP\r", 35);
        teleStatus.textContent = "Launching Microsoft Multiplan (B:MP)...";
      } else if (action === "diag-f6" || action === "diag-f3") {
        diskSelects[0].value = "HYPDIAG.IMD";
        await loadSelectedDisk(0);
        resetMachine(false);
        fastForwardBoot();
        tapXtKey(action === "diag-f6" ? 0x40 : 0x3D);
        teleStatus.textContent = action === "diag-f6"
          ? "Running Diagnostics F6 (Dynalogic CRT Attributes)"
          : "Running Diagnostics F3 (320x250 4-Shade Amber Low-Res Graphics)";
      }
      crtWrap.focus();
    });
  });

  document.getElementById("type-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const input = document.getElementById("type-input");
    if (input.value && ex) {
      sendAsciiString(input.value + "\r", 25);
      input.value = "";
      crtWrap.focus();
    }
  });

  // Boot Wasm Emulator
  async function initEmulator() {
    try {
      wasmMemory = new WebAssembly.Memory({ initial: 512, maximum: 1024 });
      const stackPtr = new WebAssembly.Global({ value: "i32", mutable: true }, 6 * 1024 * 1024);
      const fnTable = new WebAssembly.Table({ initial: 0, element: "anyfunc" });

      const resp = await fetch("hyperion.wasm");
      const wasmBytes = await resp.arrayBuffer();
      const { instance } = await WebAssembly.instantiate(wasmBytes, {
        env: {
          __linear_memory: wasmMemory,
          __stack_pointer: stackPtr,
          __indirect_function_table: fnTable
        }
      });

      ex = instance.exports;
      const urlParams = new URLSearchParams(window.location.search);
      const speedParam = urlParams.get("speed");
      if (speedParam) setSpeed(speedParam);
      const bootParam = urlParams.get("boot");
      await bootPreset(bootParam || "dos211");
      teleStatus.textContent = "Running (Click CRT to focus keyboard)";
      requestAnimationFrame(renderFrame);
    } catch (err) {
      console.error(err);
      teleStatus.textContent = `Wasm Init Error: ${err.message}`;
    }
  }

  initEmulator();
})();
