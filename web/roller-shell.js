"use strict";

// Keep pre-main browser orchestration here. Asset selection and phone-mode
// setup extend this gate in E4 and E5 without changing the generated loader.
var Module = (() => {
  const canvas = document.getElementById("canvas");
  const startGate = document.getElementById("start-gate");
  const gateTitle = document.getElementById("gate-title");
  const status = document.getElementById("status");
  const progress = document.getElementById("loading-progress");
  const startInstruction = document.getElementById("start-instruction");
  const scrollKeys = new Set(["ArrowDown", "ArrowLeft", "ArrowRight", "ArrowUp"]);
  const progressPattern = /\((\d+(?:\.\d+)?)\s*\/\s*(\d+(?:\.\d+)?)\)/;
  let runtimeReady = false;
  let runtimeStarted = false;
  const persistenceDependency = "roller-idbfs-restore";

  function focusCanvas() {
    if (document.visibilityState === "visible" && document.activeElement !== canvas) {
      canvas.focus({ preventScroll: true });
    }
  }

  function setStatus(text) {
    const message = String(text ?? "");

    // Emscripten clears its status after onRuntimeInitialized. Preserve the
    // ready prompt until the user starts the runtime.
    if (!message && runtimeReady) {
      return;
    }

    status.textContent = message || "Loading...";
    const match = message.match(progressPattern);
    if (match) {
      progress.max = Number(match[2]);
      progress.value = Number(match[1]);
      progress.hidden = false;
    } else if (!runtimeReady) {
      progress.removeAttribute("value");
      progress.hidden = false;
    }
  }

  function startRuntime() {
    if (!runtimeReady || runtimeStarted) {
      return;
    }

    runtimeStarted = true;
    startGate.hidden = true;
    focusCanvas();
    Module.callMain(["--no-crash-handler"]);
  }

  function filesystemNodeExists(path) {
    try {
      FS.lstat(path);
      return true;
    } catch (error) {
      return false;
    }
  }

  function ensureFilesystemLink(target, linkPath) {
    if (!filesystemNodeExists(linkPath))
      FS.symlink(target, linkPath);
  }

  function preparePersistentFilesystem() {
    setStatus("Restoring saved data...");
    FS.mkdirTree("/persist");
    FS.mkdirTree("/demo/fatdata");
    FS.mount(IDBFS, {}, "/persist");

    addRunDependency(persistenceDependency);
    FS.syncfs(true, (restoreError) => {
      if (restoreError)
        console.error("ROLLER: failed to restore browser filesystem", restoreError);

      try {
        // IDBFS cannot serialize symlinks. Recreate the demo layout after
        // every restore and keep these links out of the persisted snapshot.
        // A restored retail FATDATA directory takes precedence over the demo.
        ensureFilesystemLink("/demo/fatdata", "/persist/fatdata");

        // Emscripten resolves chdir through a symlink to its target. These
        // links keep the game's ../REPLAYS and ../TRACKS paths in IDBFS while
        // its effective cwd is /demo/fatdata.
        ensureFilesystemLink("/persist/REPLAYS", "/demo/REPLAYS");
        ensureFilesystemLink("/persist/TRACKS", "/demo/TRACKS");
      } catch (error) {
        console.error("ROLLER: failed to prepare browser filesystem layout", error);
      }

      Module["rollerPersistenceReady"] = true;
      removeRunDependency(persistenceDependency);
    });
  }

  startGate.addEventListener("click", startRuntime);
  startGate.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.code === "Space") {
      event.preventDefault();
      startRuntime();
    }
  });

  // SDL's Emscripten keyboard target is #canvas. Keep it focused when mouse
  // input returns to the game or the browser tab/window becomes active again.
  // pointerdown runs before SDL's mousedown callback, so the click still
  // reaches the menu after focus is restored.
  canvas.addEventListener("pointerdown", () => {
    if (runtimeStarted) {
      focusCanvas();
    }
  });
  canvas.addEventListener("contextmenu", (event) => {
    event.preventDefault();
  });
  window.addEventListener("focus", () => {
    if (runtimeStarted) {
      focusCanvas();
    }
  });
  document.addEventListener("visibilitychange", () => {
    if (runtimeStarted) {
      focusCanvas();
    }
  });

  window.addEventListener("keydown", (event) => {
    if (scrollKeys.has(event.key) || event.code === "Space") {
      event.preventDefault();
    }
  }, { passive: false });

  return {
    canvas,
    preRun: [preparePersistentFilesystem],
    setStatus,
    onRuntimeInitialized() {
      runtimeReady = true;
      gateTitle.textContent = "ROLLER is ready";
      status.textContent = "Runtime loaded";
      progress.hidden = true;
      startInstruction.hidden = false;
      startGate.setAttribute("aria-disabled", "false");
      startGate.focus({ preventScroll: true });
    }
  };
})();
