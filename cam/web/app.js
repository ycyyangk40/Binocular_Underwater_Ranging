const lastCommand = document.getElementById("last-command");
const runStatus = document.getElementById("run-status");
const nativeReply = document.getElementById("native-reply");
const grayStatus = document.getElementById("gray-status");
const enhanceStatus = document.getElementById("enhance-status");

const grayToggle = document.getElementById("gray-toggle");
const enhanceToggle = document.getElementById("enhance-toggle");
const showLeftToggle = document.getElementById("show-left");
const showRightToggle = document.getElementById("show-right");
const showDispToggle = document.getElementById("show-disp");
const showDepthToggle = document.getElementById("show-depth");
const statusPanel = document.getElementById("status-panel");
const statusTitle = document.getElementById("status-title");
const statusDesc = document.getElementById("status-desc");
const statusHint = document.getElementById("status-hint");
const statusLog = document.getElementById("status-log");

const setGrayStatus = () => {
  grayStatus.textContent = grayToggle.checked ? "on" : "off";
};

const setEnhanceStatus = () => {
  enhanceStatus.textContent = enhanceToggle.checked ? "on" : "off";
};


const setRunStatus = (state) => {
  runStatus.textContent = state;
  runStatus.className = `status-pill status-${state}`;
};

setGrayStatus();
setEnhanceStatus();
enhanceToggle.addEventListener("change", setEnhanceStatus);

let lastPayload = null;

const sendPayload = (payload, label) => {
  if (label) {
    lastCommand.textContent = label;
  }
  setGrayStatus();
  setEnhanceStatus();
  setRunStatus(payload.type === "stop" ? "stopped" : "starting");

  if (window.chrome && window.chrome.webview) {
    window.chrome.webview.postMessage(payload);
  } else {
    nativeReply.textContent = "WebView2 not available";
  }
};

const appendStatusLine = (text) => {
  const line = document.createElement("div");
  line.className = "status-line";
  line.textContent = text;
  statusLog.appendChild(line);
  statusLog.scrollTop = statusLog.scrollHeight;
};

const resetStatusLog = (mode) => {
  statusLog.innerHTML = "";
  if (mode === "calib") {
    statusTitle.textContent = "Calibration Status";
    statusDesc.textContent = "Capture and solve feedback will appear here while calibration is running.";
    statusHint.textContent = "Shortcut tips: C = capture, E = solve, Q = quit";
    appendStatusLine("Calibration started...");
  } else if (mode === "depth") {
    statusTitle.textContent = "Depth Status";
    statusDesc.textContent = "Click on the depth window to measure distance.";
    statusHint.textContent = "Tip: click a valid point to print distance.";
    appendStatusLine("Depth stream started...");
  } else if (mode === "yolo") {
    statusTitle.textContent = "YOLO Status";
    statusDesc.textContent = "Left camera detection will be fused with stereo depth once the runtime is configured.";
    statusHint.textContent = "Next step: load the ONNX model and enable the GPU execution provider.";
    appendStatusLine("YOLO mode prepared...");
  }
};

const showStatusPanel = (mode) => {
  statusPanel.classList.remove("is-hidden");
  resetStatusLog(mode);
};

const hideStatusPanel = () => {
  statusPanel.classList.add("is-hidden");
  statusLog.innerHTML = "";
};

const isStatusVisible = () => !statusPanel.classList.contains("is-hidden");

const actionButtons = document.querySelectorAll("button[data-action]");
actionButtons.forEach((button) => {
  button.addEventListener("click", () => {
    const action = button.dataset.action;
    if (action === "live") {
      const payload = {
        type: "mode",
        mode: "depth",
        view: "both",
        showLeft: showLeftToggle.checked,
        showRight: showRightToggle.checked,
        showDisp: showDispToggle.checked,
        showDepth: showDepthToggle.checked,
        gray: grayToggle.checked,
        enhance: enhanceToggle.checked,
      };
      lastPayload = payload;
      if (showDepthToggle.checked) {
        showStatusPanel("depth");
      } else {
        hideStatusPanel();
      }
      sendPayload(payload, "Start Live View");
    } else if (action === "yolo") {
      const payload = {
        type: "mode",
        mode: "yolo",
        showLeft: showLeftToggle.checked,
        showRight: showRightToggle.checked,
        showDepth: showDepthToggle.checked,
        gray: grayToggle.checked,
        enhance: enhanceToggle.checked,
      };
      lastPayload = payload;
      showStatusPanel("yolo");
      sendPayload(payload, "Start YOLO");
    } else if (action === "calib") {
      const payload = {
        type: "mode",
        mode: "calib",
        gray: grayToggle.checked,
        enhance: enhanceToggle.checked,
      };
      lastPayload = payload;
      showStatusPanel("calib");
      sendPayload(payload, "Open Calibration");
    } else if (action === "stop") {
      hideStatusPanel();
      sendPayload({ type: "stop" }, "Stop Stream");
    } else if (action === "restart" && lastPayload) {
      if (lastPayload.mode === "calib") {
        showStatusPanel("calib");
      } else if (lastPayload.mode === "depth") {
        if (lastPayload.showDepth) {
          showStatusPanel("depth");
        } else {
          hideStatusPanel();
        }
      }
      sendPayload(lastPayload, "Restart Last");
    }
  });
});

if (window.chrome && window.chrome.webview) {
  window.chrome.webview.addEventListener("message", (event) => {
    const message = event.data;
    if (typeof message === "string" && message.startsWith("status:")) {
      const state = message.substring("status:".length).trim();
      setRunStatus(state);
      return;
    }
    if (typeof message === "string" && message.startsWith("calib:")) {
      if (!isStatusVisible()) {
        showStatusPanel("calib");
      }
      appendStatusLine(message.substring("calib:".length).trim());
      return;
    }
    if (typeof message === "string" && message.startsWith("depth:")) {
      if (!isStatusVisible() && lastPayload && lastPayload.showDepth) {
        showStatusPanel("depth");
      }
      if (isStatusVisible()) {
        appendStatusLine(message.substring("depth:".length).trim());
      }
      return;
    }
    if (typeof message === "string" && message.startsWith("yolo:")) {
      if (!isStatusVisible() && lastPayload && lastPayload.mode === "yolo") {
        showStatusPanel("yolo");
      }
      if (isStatusVisible()) {
        appendStatusLine(message.substring("yolo:".length).trim());
      }
      return;
    }
    nativeReply.textContent = message;
    if (typeof message === "string" && message.startsWith("CLI launched")) {
      setRunStatus("running");
    }
    if (typeof message === "string" && message.startsWith("CLI failed")) {
      setRunStatus("failed");
    }
    if (typeof message === "string" && message.startsWith("CLI stopped")) {
      setRunStatus("stopped");
      hideStatusPanel();
    }
  });
}
