"use strict";

// Keep pre-main browser orchestration here. Asset selection and phone-mode
// setup extend this gate in E4 and E5 without changing the generated loader.
var Module = (() => {
  const canvas = document.getElementById("canvas");
  const startGate = document.getElementById("start-gate");
  const gateTitle = document.getElementById("gate-title");
  const status = document.getElementById("status");
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
  const importPath = "/import";
  const importChunkBytes = 8 * 1024 * 1024;
  const importSizeWarningBytes = 800 * 1024 * 1024;
  const extractionFailureMessage =
    "No game data could be extracted from the files you selected.\n\n" +
    "For a CUE/BIN image you must select the .CUE file AND all of its " +
    ".BIN/audio files together - selecting only the .CUE or only the .BIN " +
    "will not work, because the other files cannot be read on their own.\n\n" +
    "Please try again and select the .CUE and every file it references.";
  let runtimeReady = false;
  let runtimeStarted = false;
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

  function startRuntime() {
    if (!runtimeReady || runtimeStarted || gateBusy) {
      return;
    }

    setGateBusy(true);
    runtimeStarted = true;
    startGate.hidden = true;
    focusCanvas();
    try {
      Module.callMain(["--no-crash-handler"]);
    } catch (error) {
      runtimeStarted = false;
      startGate.hidden = false;
      failStartup(error);
    }
  }

  function failStartup(error) {
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
      if (currentSource === "demo") {
        FS.unlink(persistentFatdataPath);
      }
      removeFilesystemTree(extractedFatdataPath);

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

      const previousRetailPath = "/persist/fatdata.previous";
      removeFilesystemTree(previousRetailPath);
      if (filesystemNode(persistentFatdataPath)) {
        FS.rename(persistentFatdataPath, previousRetailPath);
      }
      try {
        FS.rename(extractedFatdataPath, persistentFatdataPath);
      } catch (error) {
        if (filesystemNode(previousRetailPath) && !filesystemNode(persistentFatdataPath)) {
          FS.rename(previousRetailPath, persistentFatdataPath);
        }
        throw error;
      }
      removeFilesystemTree(previousRetailPath);
      removeFilesystemTree(importPath);
      cdImageInput.value = "";

      status.textContent = "Saving retail game data...";
      await syncPersistentFilesystem();
      retailFatdataReady = true;
      updateAssetGate();
      startRuntime();
    } catch (error) {
      pendingImportFiles = null;
      cdImageInput.value = "";
      removeFilesystemTree(importPath);
      removeFilesystemTree(extractedFatdataPath);
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
      await syncPersistentFilesystem();
      ensureFilesystemLink("/demo/fatdata", persistentFatdataPath);
      updateAssetGate();
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

  playButton.addEventListener("click", startRuntime);
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
    preRun: [preparePersistentFilesystem],
    setStatus,
    onRuntimeInitialized() {
      try {
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
