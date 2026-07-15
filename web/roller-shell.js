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
    canvas.focus({ preventScroll: true });
    Module.callMain(["--no-crash-handler"]);
  }

  startGate.addEventListener("click", startRuntime);
  startGate.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.code === "Space") {
      event.preventDefault();
      startRuntime();
    }
  });

  window.addEventListener("keydown", (event) => {
    if (scrollKeys.has(event.key) || event.code === "Space") {
      event.preventDefault();
    }
  }, { passive: false });

  return {
    canvas,
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
