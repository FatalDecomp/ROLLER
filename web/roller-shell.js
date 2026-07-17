"use strict";

// Keep pre-main browser orchestration here. Asset selection and phone-mode
// setup extend this gate in E4 and E5 without changing the generated loader.
var Module = (() => {
  const canvas = document.getElementById("canvas");
  const gameFrame = document.querySelector(".game-frame");
  const startGate = document.getElementById("start-gate");
  const gateTitle = document.getElementById("gate-title");
  const status = document.getElementById("status");
  const phoneStatus = document.getElementById("phone-status");
  const progress = document.getElementById("loading-progress");
  const gateActions = document.getElementById("gate-actions");
  const playButton = document.getElementById("play-button");
  const importButton = document.getElementById("import-button");
  const retailControls = document.getElementById("retail-controls");
  const resetDemoButton = document.getElementById("reset-demo-button");
  const reimportButton = document.getElementById("reimport-button");
  const cdImageInput = document.getElementById("cd-image-input");
  const importWarningActions = document.getElementById("import-warning-actions");
  const continueImportButton = document.getElementById("continue-import-button");
  const cancelImportButton = document.getElementById("cancel-import-button");
  const scrollKeys = new Set(["ArrowDown", "ArrowLeft", "ArrowRight", "ArrowUp"]);
  const progressPattern = /\((\d+(?:\.\d+)?)\s*\/\s*(\d+(?:\.\d+)?)\)/;
  const persistentFatdataPath = "/persist/fatdata";
  const extractedFatdataPath = "/persist/FATDATA";
  const previousFatdataPath = "/persist/fatdata.previous";
  const persistentAudioPath = "/persist/audio";
  const previousAudioPath = "/persist/audio.previous";
  const importPath = "/import";
  const persistentDemoConfigPaths = ["/persist/FATAL.INI", "/persist/ROLLER.INI"];
  const importChunkBytes = 8 * 1024 * 1024;
  const importSizeWarningBytes = 800 * 1024 * 1024;
  const phoneControlsTiltTurn = 1;
  const phoneControlsTouchTurn = 2;
  const motionSampleIntervalMs = 1000 / 60;
  const motionSampleTimeoutMs = 2000;
  const extractionFailureMessage =
    "No game data could be extracted from the files you selected.\n\n" +
    "For a CUE/BIN image you must select the .CUE file AND all of its " +
    ".BIN/audio files together - selecting only the .CUE or only the .BIN " +
    "will not work, because the other files cannot be read on their own.\n\n" +
    "Please try again and select the .CUE and every file it references.";
  const phoneModeDecision = detectPhoneMode();
  let runtimeReady = false;
  let runtimeStarted = false;
  let phoneModeApplied = false;
  let motionPermissionState = "not-requested";
  let motionListening = false;
  let motionSampleReceived = false;
  let motionLastSampleMs = Number.NEGATIVE_INFINITY;
  let motionSampleTimer = 0;
  let phoneStatusTimer = 0;
  let gateBusy = true;
  let retailFatdataReady = false;
  let pendingImportFiles = null;
  let filesystemPreparationError = null;
  const persistenceDependency = "roller-idbfs-restore";
  const requiredDemoFiles = [
    "/demo/fatdata/FATAL.INI",
    "/demo/fatdata/FRONTEND.BM",
    "/demo/fatdata/TRACK5.TRK",
    "/demo/fatdata/WHIPTIT.BM"
  ];

  function detectPhoneMode() {
    const override = new URLSearchParams(window.location.search).get("phone");
    if (override === "0" || override === "1") {
      return {
        active: override === "1",
        source: "query",
        touchPoints: Number(navigator.maxTouchPoints) || 0,
        coarsePointer: window.matchMedia("(pointer: coarse)").matches
      };
    }

    const touchPoints = Number(navigator.maxTouchPoints) || 0;
    const coarsePointer = window.matchMedia("(pointer: coarse)").matches;
    return {
      active: touchPoints > 0 && coarsePointer,
      source: "capability",
      touchPoints,
      coarsePointer
    };
  }

  function applyPhoneMode() {
    if (phoneModeApplied) {
      return;
    }
    if (typeof Module["_ROLLERWebSetPhoneMode"] !== "function") {
      throw new Error("This browser bundle does not contain phone-mode support.");
    }

    Module["_ROLLERWebSetPhoneMode"](phoneModeDecision.active ? 1 : 0);
    Module["rollerPhoneMode"] = phoneModeDecision.active;
    Module["rollerPhoneModeSource"] = phoneModeDecision.source;
    Module["rollerPhoneTouchPoints"] = phoneModeDecision.touchPoints;
    Module["rollerPhoneCoarsePointer"] = phoneModeDecision.coarsePointer;
    phoneModeApplied = true;
    console.log(
      `ROLLER: phone mode ${phoneModeDecision.active ? "enabled" : "disabled"} ` +
      `(${phoneModeDecision.source}, touch points ${phoneModeDecision.touchPoints}, ` +
      `coarse pointer ${phoneModeDecision.coarsePointer ? "yes" : "no"})`
    );
  }

  function updateMotionDiagnostics() {
    Module["rollerMotionPermission"] = motionPermissionState;
    Module["rollerMotionListening"] = motionListening;
    Module["rollerMotionSampleReceived"] = motionSampleReceived;
  }

  function showPhoneStatus(message) {
    if (phoneStatusTimer) {
      clearTimeout(phoneStatusTimer);
      phoneStatusTimer = 0;
    }
    phoneStatus.textContent = message;
    phoneStatus.hidden = false;
    if (!runtimeStarted)
      status.textContent = message;
    phoneStatusTimer = setTimeout(() => {
      phoneStatus.hidden = true;
      phoneStatusTimer = 0;
    }, 5000);
  }

  function phoneControlsScheme() {
    if (typeof Module["_ROLLERWebGetPhoneControls"] !== "function")
      return phoneControlsTiltTurn;
    return Module["_ROLLERWebGetPhoneControls"]();
  }

  function clearMotionSampleTimer() {
    if (motionSampleTimer) {
      clearTimeout(motionSampleTimer);
      motionSampleTimer = 0;
    }
  }

  function setWebAccel(fX, fY, fZ) {
    if (typeof Module["_ROLLERWebSetAccel"] === "function")
      Module["_ROLLERWebSetAccel"](fX, fY, fZ);
  }

  function handleDeviceMotion(event) {
    if (!motionListening || document.hidden)
      return;

    const accel = event.accelerationIncludingGravity;
    const fX = Number(accel?.x);
    const fY = Number(accel?.y);
    const fZ = Number(accel?.z);
    if (!Number.isFinite(fX) || !Number.isFinite(fY) || !Number.isFinite(fZ))
      return;

    const now = performance.now();
    if (now - motionLastSampleMs < motionSampleIntervalMs)
      return;
    motionLastSampleMs = now;

    // DeviceMotion and SDL's Android accelerometer both use device-relative
    // X-right, Y-top, Z-out axes. The shared C path applies the portrait or
    // landscape orientation sign/swap before calculating steering.
    setWebAccel(fX, fY, fZ);
    motionSampleReceived = true;
    Module["rollerMotionLastAccel"] = [fX, fY, fZ];
    clearMotionSampleTimer();
    updateMotionDiagnostics();
  }

  function stopPhoneMotionSubscription() {
    if (motionListening) {
      window.removeEventListener("devicemotion", handleDeviceMotion);
      motionListening = false;
    }
    clearMotionSampleTimer();
    motionSampleReceived = false;
    motionLastSampleMs = Number.NEGATIVE_INFINITY;
    if (phoneModeDecision.active)
      setWebAccel(0.0, 0.0, 0.0);
    updateMotionDiagnostics();
  }

  function useTouchSteeringFallback(message, state) {
    stopPhoneMotionSubscription();
    motionPermissionState = state;
    if (typeof Module["_ROLLERWebSetPhoneControls"] === "function")
      Module["_ROLLERWebSetPhoneControls"](phoneControlsTouchTurn);
    Module["rollerMotionFallback"] = true;
    showPhoneStatus(message);
    updateMotionDiagnostics();
  }

  function scheduleMotionSampleTimeout() {
    clearMotionSampleTimer();
    if (!motionListening || motionSampleReceived)
      return;

    motionSampleTimer = setTimeout(() => {
      motionSampleTimer = 0;
      if (document.hidden || phoneControlsScheme() !== phoneControlsTiltTurn)
        return;
      useTouchSteeringFallback(
        "Motion data is unavailable; using touch steering.",
        "unavailable"
      );
    }, motionSampleTimeoutMs);
  }

  function startPhoneMotionSubscription() {
    if (!phoneModeDecision.active || motionPermissionState !== "granted" ||
        document.hidden || phoneControlsScheme() !== phoneControlsTiltTurn) {
      return;
    }
    if (!motionListening) {
      window.addEventListener("devicemotion", handleDeviceMotion, { passive: true });
      motionListening = true;
      motionSampleReceived = false;
      motionLastSampleMs = Number.NEGATIVE_INFINITY;
      scheduleMotionSampleTimeout();
    } else if (!motionSampleReceived) {
      scheduleMotionSampleTimeout();
    }
    updateMotionDiagnostics();
  }

  function refreshPhoneMotionSubscription() {
    if (motionPermissionState === "granted" &&
        phoneControlsScheme() === phoneControlsTiltTurn) {
      startPhoneMotionSubscription();
    } else {
      stopPhoneMotionSubscription();
    }
  }

  async function requestPhoneMotionFromGesture() {
    if (!phoneModeDecision.active) {
      motionPermissionState = "not-applicable";
      updateMotionDiagnostics();
      return;
    }
    if (phoneControlsScheme() !== phoneControlsTiltTurn) {
      updateMotionDiagnostics();
      return;
    }
    if (!("DeviceMotionEvent" in window)) {
      useTouchSteeringFallback(
        "Motion controls are unavailable; using touch steering.",
        "unavailable"
      );
      return;
    }

    const requestPermission = window.DeviceMotionEvent?.requestPermission;
    if (typeof requestPermission === "function") {
      motionPermissionState = "requesting";
      status.textContent = "Requesting tilt steering permission...";
      updateMotionDiagnostics();
      try {
        const result = await requestPermission.call(window.DeviceMotionEvent);
        if (result !== "granted") {
          useTouchSteeringFallback(
            "Tilt permission was denied; using touch steering.",
            "denied"
          );
          return;
        }
      } catch (error) {
        console.warn("ROLLER: motion permission request failed", error);
        useTouchSteeringFallback(
          "Tilt permission could not be granted; using touch steering.",
          "error"
        );
        return;
      }
    }

    motionPermissionState = "granted";
    Module["rollerMotionFallback"] = false;
    updateMotionDiagnostics();
    startPhoneMotionSubscription();
  }

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

  function setGateBusy(busy) {
    gateBusy = busy;
    startGate.setAttribute("aria-busy", String(busy));
    playButton.disabled = busy || !runtimeReady;
    importButton.disabled = busy || !runtimeReady;
    resetDemoButton.disabled = busy || !runtimeReady;
    reimportButton.disabled = busy || !runtimeReady;
    continueImportButton.disabled = busy || !runtimeReady;
    cancelImportButton.disabled = busy || !runtimeReady;
  }

  async function startRuntime() {
    if (!runtimeReady || runtimeStarted || gateBusy) {
      return;
    }

    setGateBusy(true);
    try {
      applyPhoneMode();
      await requestPhoneMotionFromGesture();
      runtimeStarted = true;
      startGate.hidden = true;
      focusCanvas();
      setTimeout(refreshPhoneMotionSubscription, 0);
      Module.callMain(["--no-crash-handler"]);
    } catch (error) {
      stopPhoneMotionSubscription();
      runtimeStarted = false;
      startGate.hidden = false;
      failStartup(error);
    }
  }

  function failStartup(error) {
    stopPhoneMotionSubscription();
    const message = error instanceof Error ? error.message : String(error);
    runtimeReady = false;
    gateTitle.textContent = "ROLLER could not start";
    status.textContent = message;
    progress.hidden = true;
    gateActions.hidden = true;
    importWarningActions.hidden = true;
    setGateBusy(true);
    console.error("ROLLER: browser startup failed", error);
  }

  function filesystemNode(path) {
    try {
      return FS.lstat(path);
    } catch (error) {
      return null;
    }
  }

  function ensureFilesystemLink(target, linkPath) {
    if (!filesystemNode(linkPath))
      FS.symlink(target, linkPath);
  }

  function removeFilesystemTree(path) {
    const entry = filesystemNode(path);
    if (!entry) {
      return;
    }

    if (FS.isDir(entry.mode) && !FS.isLink(entry.mode)) {
      for (const name of FS.readdir(path)) {
        if (name !== "." && name !== "..") {
          removeFilesystemTree(`${path}/${name}`);
        }
      }
      FS.rmdir(path);
      return;
    }

    FS.unlink(path);
  }

  function persistentFatdataSource() {
    const entry = filesystemNode(persistentFatdataPath);
    if (!entry) {
      return "missing";
    }
    if (FS.isLink(entry.mode)) {
      return "demo";
    }
    if (FS.isDir(entry.mode)) {
      return "retail";
    }
    throw new Error(`${persistentFatdataPath} is not a directory or symlink`);
  }

  function updateAssetGate() {
    const source = persistentFatdataSource();
    pendingImportFiles = null;
    retailFatdataReady = source === "retail";
    Module["rollerRetailReady"] = retailFatdataReady;
    Module["rollerAssetSource"] = retailFatdataReady ? "retail" : "demo";
    playButton.textContent = retailFatdataReady ? "PLAY FULL GAME" : "PLAY DEMO";
    retailControls.hidden = !retailFatdataReady;
    gateTitle.textContent = "ROLLER is ready";
    status.textContent = retailFatdataReady
      ? "Saved retail game data restored"
      : "Freeware demo loaded";
    progress.hidden = true;
    importWarningActions.hidden = true;
    gateActions.hidden = false;
    setGateBusy(false);
    playButton.focus({ preventScroll: true });
  }

  function syncPersistentFilesystem() {
    // SaveDefaultFatalIni schedules the engine's normal debounced sync. An
    // import performs its own awaited pre-main sync, so prevent both IDBFS
    // transactions from racing once E4.S2 calls that writer.
    if (Module["rollerPersistSyncTimer"]) {
      clearTimeout(Module["rollerPersistSyncTimer"]);
      Module["rollerPersistSyncTimer"] = 0;
    }

    return new Promise((resolve, reject) => {
      FS.syncfs(false, (error) => {
        if (error) {
          reject(error);
        } else {
          resolve();
        }
      });
    });
  }

  function yieldForOverlayPaint() {
    return new Promise((resolve) => {
      requestAnimationFrame(() => requestAnimationFrame(resolve));
    });
  }

  function yieldForProgressPaint() {
    return new Promise((resolve) => {
      requestAnimationFrame(() => setTimeout(resolve, 0));
    });
  }

  function formatBytes(bytes) {
    if (bytes >= 1024 * 1024) {
      return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`;
    }
    if (bytes >= 1024) {
      return `${(bytes / 1024).toFixed(1)} KiB`;
    }
    return `${bytes} bytes`;
  }

  function importFilename(file) {
    const name = String(file.name || "");
    if (!name || name === "." || name === ".." ||
        name.includes("/") || name.includes("\\") || name.includes("\0")) {
      throw new Error("A selected CD image has an invalid filename.");
    }
    return name;
  }

  function compareImportFiles(left, right) {
    const leftName = importFilename(left).toLowerCase();
    const rightName = importFilename(right).toLowerCase();
    if (leftName < rightName) return -1;
    if (leftName > rightName) return 1;
    return 0;
  }

  function chooseCdImageEntry(files) {
    const sortedFiles = Array.from(files).sort(compareImportFiles);
    const cue = sortedFiles.find((file) => importFilename(file).toLowerCase().endsWith(".cue"));
    if (cue) {
      return cue;
    }

    const iso = sortedFiles.find((file) => importFilename(file).toLowerCase().endsWith(".iso"));
    if (iso) {
      return iso;
    }

    throw new Error(extractionFailureMessage);
  }

  function assertUniqueImportFilenames(files) {
    const names = new Set();
    for (const file of files) {
      const name = importFilename(file).toLowerCase();
      if (names.has(name)) {
        throw new Error(`More than one selected file is named ${importFilename(file)}.`);
      }
      names.add(name);
    }
  }

  function assertCueHasBinSelection(files, entryFile) {
    if (!importFilename(entryFile).toLowerCase().endsWith(".cue")) {
      return;
    }
    if (!files.some((file) => importFilename(file).toLowerCase().endsWith(".bin"))) {
      throw new Error(extractionFailureMessage);
    }
  }

  function showLargeImportWarning(files, totalBytes) {
    pendingImportFiles = files;
    gateTitle.textContent = "Large CD image selected";
    status.textContent =
      `The selected files total ${formatBytes(totalBytes)}. Browser imports above ` +
      `${formatBytes(importSizeWarningBytes)} can exhaust the WebAssembly memory ` +
      "limit and crash this tab. Continue only if this browser has enough memory.";
    progress.hidden = true;
    gateActions.hidden = true;
    importWarningActions.hidden = false;
    setGateBusy(false);
    continueImportButton.focus({ preventScroll: true });
  }

  function cancelLargeImport() {
    pendingImportFiles = null;
    cdImageInput.value = "";
    updateAssetGate();
    status.textContent = "CD image import cancelled";
  }

  function continueLargeImport() {
    const files = pendingImportFiles;
    pendingImportFiles = null;
    importWarningActions.hidden = true;
    gateActions.hidden = false;
    if (files) {
      void importCdImage(files, true);
    }
  }

  async function requestPersistentStorage() {
    Module["rollerStoragePersistRequested"] = true;
    if (!navigator.storage || typeof navigator.storage.persist !== "function") {
      Module["rollerStoragePersistent"] = false;
      console.warn("ROLLER: persistent browser storage is not supported");
      return;
    }

    try {
      const granted = await navigator.storage.persist();
      Module["rollerStoragePersistent"] = granted;
      if (!granted) {
        console.warn("ROLLER: persistent browser storage was not granted; retail data may be evicted");
      }
    } catch (error) {
      Module["rollerStoragePersistent"] = false;
      console.warn("ROLLER: persistent browser storage request failed", error);
    }
  }

  async function reloadAfterSourceSwitch(source) {
    retailFatdataReady = source === "retail";
    Module["rollerRetailReady"] = retailFatdataReady;
    Module["rollerAssetSource"] = source;
    Module["rollerSourceSwitchReloading"] = source;
    gateTitle.textContent = source === "retail"
      ? "Retail game data is ready"
      : "Demo game data is ready";
    status.textContent = "Reloading ROLLER...";
    progress.hidden = true;
    await yieldForOverlayPaint();
    window.location.reload();
  }

  async function stageImportFile(file, destination, stagingProgress) {
    const stream = FS.open(destination, "w");
    try {
      for (let offset = 0; offset < file.size; offset += importChunkBytes) {
        const end = Math.min(offset + importChunkBytes, file.size);
        const content = new Uint8Array(await file.slice(offset, end).arrayBuffer());
        let written = 0;
        while (written < content.length) {
          const count = FS.write(
            stream,
            content,
            written,
            content.length - written,
            offset + written
          );
          if (count <= 0) {
            throw new Error(`Could not stage ${importFilename(file)}.`);
          }
          written += count;
        }

        stagingProgress.completed += content.length;
        progress.value = stagingProgress.completed;
        const percent = Math.floor(
          (stagingProgress.completed * 100) / Math.max(stagingProgress.total, 1)
        );
        status.textContent =
          `Staging ${importFilename(file)}... ${formatBytes(stagingProgress.completed)} / ` +
          `${formatBytes(stagingProgress.total)} (${percent}%)`;
        await yieldForProgressPaint();
      }
    } finally {
      FS.close(stream);
    }
  }

  function openCdImagePicker() {
    if (!runtimeReady || runtimeStarted || gateBusy) {
      return;
    }
    cdImageInput.value = "";
    cdImageInput.click();
  }

  async function importCdImage(fileList, largeImportConfirmed = false) {
    const files = Array.from(fileList || []);
    let fatdataSwapStarted = false;
    let audioSwapStarted = false;
    if (!files.length || !runtimeReady || runtimeStarted || gateBusy) {
      return;
    }

    try {
      assertUniqueImportFilenames(files);
      const entryFile = chooseCdImageEntry(files);
      assertCueHasBinSelection(files, entryFile);
      if (typeof Module["_ROLLERWebExtractFATDATA"] !== "function") {
        throw new Error("Retail CD extraction is not available in this build yet.");
      }

      const totalBytes = files.reduce((total, file) => total + file.size, 0);
      if (totalBytes > importSizeWarningBytes && !largeImportConfirmed) {
        showLargeImportWarning(files, totalBytes);
        return;
      }

      setGateBusy(true);
      gateTitle.textContent = "Importing retail game data";
      status.textContent = "Requesting persistent browser storage...";
      await requestPersistentStorage();

      status.textContent = "Staging selected CD image files...";
      progress.max = totalBytes || 1;
      progress.value = 0;
      progress.hidden = false;

      removeFilesystemTree(importPath);
      FS.mkdirTree(importPath);
      const stagingProgress = { completed: 0, total: totalBytes };
      for (const file of files) {
        const name = importFilename(file);
        status.textContent = `Staging ${name}...`;
        await stageImportFile(file, `${importPath}/${name}`, stagingProgress);
      }

      const currentSource = persistentFatdataSource();
      removeFilesystemTree(extractedFatdataPath);
      removeFilesystemTree(previousFatdataPath);
      if (currentSource === "demo") {
        FS.unlink(persistentFatdataPath);
      } else {
        FS.rename(persistentFatdataPath, previousFatdataPath);
      }
      fatdataSwapStarted = true;
      removeFilesystemTree(previousAudioPath);
      if (filesystemNode(persistentAudioPath)) {
        FS.rename(persistentAudioPath, previousAudioPath);
      }
      audioSwapStarted = true;

      status.textContent = "Extracting game data...";
      progress.removeAttribute("value");
      await yieldForOverlayPaint();

      const entryPath = `${importPath}/${importFilename(entryFile)}`;
      const extracted = Module.ccall(
        "ROLLERWebExtractFATDATA",
        "number",
        ["string", "string"],
        [entryPath, "/persist"]
      );
      const extractedEntry = filesystemNode(extractedFatdataPath);
      if (!extracted || !extractedEntry || !FS.isDir(extractedEntry.mode)) {
        throw new Error(extractionFailureMessage);
      }

      FS.rename(extractedFatdataPath, persistentFatdataPath);
      fatdataSwapStarted = false;
      audioSwapStarted = false;
      removeFilesystemTree(previousFatdataPath);
      removeFilesystemTree(previousAudioPath);
      removeFilesystemTree(importPath);
      cdImageInput.value = "";

      status.textContent = "Saving retail game data...";
      await syncPersistentFilesystem();
      await reloadAfterSourceSwitch("retail");
    } catch (error) {
      pendingImportFiles = null;
      cdImageInput.value = "";
      removeFilesystemTree(importPath);
      removeFilesystemTree(extractedFatdataPath);
      if (fatdataSwapStarted) {
        removeFilesystemTree(persistentFatdataPath);
        if (filesystemNode(previousFatdataPath)) {
          FS.rename(previousFatdataPath, persistentFatdataPath);
        }
      }
      if (audioSwapStarted) {
        removeFilesystemTree(persistentAudioPath);
        if (filesystemNode(previousAudioPath)) {
          FS.rename(previousAudioPath, persistentAudioPath);
        }
      }
      if (persistentFatdataSource() === "missing") {
        ensureFilesystemLink("/demo/fatdata", persistentFatdataPath);
      }
      updateAssetGate();
      status.textContent = error instanceof Error ? error.message : String(error);
      console.error("ROLLER: retail CD import failed", error);
    }
  }

  async function resetToDemo() {
    if (!retailFatdataReady || !runtimeReady || runtimeStarted || gateBusy) {
      return;
    }

    setGateBusy(true);
    gateTitle.textContent = "Switching to demo";
    status.textContent = "Removing saved retail game data...";
    progress.removeAttribute("value");
    progress.hidden = false;
    try {
      removeFilesystemTree(persistentFatdataPath);
      removeFilesystemTree(extractedFatdataPath);
      removeFilesystemTree(previousFatdataPath);
      removeFilesystemTree(persistentAudioPath);
      removeFilesystemTree(previousAudioPath);
      // Config is source-dependent: the retail pair leaves with its tree above.
      // Clear the persistent pair together so the web first-run policy creates
      // consistent demo-safe settings when the demo next starts.
      for (const path of persistentDemoConfigPaths) {
        removeFilesystemTree(path);
      }
      await syncPersistentFilesystem();
      ensureFilesystemLink("/demo/fatdata", persistentFatdataPath);
      await reloadAfterSourceSwitch("demo");
    } catch (error) {
      if (persistentFatdataSource() === "missing") {
        ensureFilesystemLink("/demo/fatdata", persistentFatdataPath);
      }
      updateAssetGate();
      status.textContent = error instanceof Error ? error.message : String(error);
      console.error("ROLLER: failed to reset retail game data", error);
    }
  }

  function assertDemoPackage() {
    let directory;
    try {
      directory = FS.stat("/demo/fatdata");
    } catch (error) {
      throw new Error("Demo package is missing /demo/fatdata");
    }
    if (!FS.isDir(directory.mode)) {
      throw new Error("Demo package path /demo/fatdata is not a directory");
    }

    for (const path of requiredDemoFiles) {
      let entry;
      try {
        entry = FS.stat(path);
      } catch (error) {
        throw new Error(`Demo package is missing required file ${path}`);
      }
      if (!FS.isFile(entry.mode)) {
        throw new Error(`Demo package entry is not a file: ${path}`);
      }
    }
  }

  function preparePersistentFilesystem() {
    setStatus("Restoring saved data...");
    FS.mkdirTree("/persist");
    // The preload owns /demo/fatdata. Creating it here would mask a missing
    // or failed roller-<hash>.data request until the engine opened a file.
    FS.mkdirTree("/demo");
    FS.mount(IDBFS, {}, "/persist");

    addRunDependency(persistenceDependency);
    FS.syncfs(true, (restoreError) => {
      if (restoreError)
        console.error("ROLLER: failed to restore browser filesystem", restoreError);

      try {
        // IDBFS cannot serialize symlinks. Recreate the demo layout after
        // every restore and keep these links out of the persisted snapshot.
        // A restored retail FATDATA directory takes precedence over the demo.
        ensureFilesystemLink("/demo/fatdata", persistentFatdataPath);

        // Emscripten resolves chdir through a symlink to its target. These
        // links keep the game's ../REPLAYS and ../TRACKS paths in IDBFS while
        // its effective cwd is /demo/fatdata.
        ensureFilesystemLink("/persist/REPLAYS", "/demo/REPLAYS");
        ensureFilesystemLink("/persist/TRACKS", "/demo/TRACKS");
      } catch (error) {
        filesystemPreparationError = error;
        console.error("ROLLER: failed to prepare browser filesystem layout", error);
      }

      Module["rollerPersistenceReady"] = true;
      removeRunDependency(persistenceDependency);
    });
  }

  playButton.addEventListener("click", () => void startRuntime());
  importButton.addEventListener("click", openCdImagePicker);
  reimportButton.addEventListener("click", openCdImagePicker);
  resetDemoButton.addEventListener("click", () => void resetToDemo());
  cdImageInput.addEventListener("change", () => void importCdImage(cdImageInput.files));
  continueImportButton.addEventListener("click", continueLargeImport);
  cancelImportButton.addEventListener("click", cancelLargeImport);

  // SDL's Emscripten keyboard target is #canvas. Keep it focused when mouse
  // input returns to the game or the browser tab/window becomes active again.
  // pointerdown runs before SDL's mousedown callback, so the click still
  // reaches the menu after focus is restored.
  canvas.addEventListener("pointerdown", () => {
    if (runtimeStarted) {
      focusCanvas();
    }
  });
  gameFrame.addEventListener("contextmenu", (event) => {
    event.preventDefault();
  });
  gameFrame.addEventListener("dragstart", (event) => {
    event.preventDefault();
  });
  for (const eventName of ["gesturestart", "gesturechange", "gestureend"]) {
    window.addEventListener(eventName, (event) => {
      event.preventDefault();
    }, { passive: false });
  }
  window.addEventListener("focus", () => {
    if (runtimeStarted) {
      focusCanvas();
    }
  });
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
      stopPhoneMotionSubscription();
    } else if (runtimeStarted) {
      refreshPhoneMotionSubscription();
      focusCanvas();
    }
  });

  window.addEventListener("keydown", (event) => {
    // Keep the gate's native keyboard activation intact. Once the game has
    // focus, Space is suppressed with the other browser scrolling keys.
    if (event.code === "Space" && event.target?.tagName === "BUTTON") {
      return;
    }
    if (scrollKeys.has(event.key) || event.code === "Space") {
      event.preventDefault();
    }
  }, { passive: false });

  return {
    canvas,
    rollerPhoneMode: phoneModeDecision.active,
    rollerPhoneModeSource: phoneModeDecision.source,
    rollerPhoneTouchPoints: phoneModeDecision.touchPoints,
    rollerPhoneCoarsePointer: phoneModeDecision.coarsePointer,
    rollerMotionPermission: motionPermissionState,
    rollerMotionListening: motionListening,
    rollerMotionSampleReceived: motionSampleReceived,
    rollerMotionFallback: false,
    preRun: [preparePersistentFilesystem],
    setStatus,
    onRuntimeInitialized() {
      try {
        applyPhoneMode();
        updateMotionDiagnostics();
        if (filesystemPreparationError) {
          throw filesystemPreparationError;
        }
        // Emscripten preload run dependencies delay this callback until the
        // hashed data file has downloaded and populated MEMFS.
        assertDemoPackage();
        runtimeReady = true;
        Module["rollerDemoReady"] = true;
        updateAssetGate();
      } catch (error) {
        failStartup(error);
      }
    },
    onAbort(reason) {
      failStartup(new Error(`Runtime aborted while loading: ${String(reason)}`));
    }
  };
})();
