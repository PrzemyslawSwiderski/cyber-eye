const DEFAULT_BASE_URL = 'http://192.168.1.17:8080';

// ── DOM refs ──────────────────────────────────────────────────────────────────
const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
const statusText = document.getElementById('statusText');
const fpsText = document.getElementById('fpsText');
const btnConnect = document.getElementById('btnConnect');
const btnDisconnect = document.getElementById('btnDisconnect');
const inputBaseUrl = document.getElementById('inputBaseUrl');
const canvasWrap = document.getElementById('canvasWrap');

// ── URL helpers (used by signal_info.js too, so defined here) ─────────────────
function getBaseUrl() { return inputBaseUrl.value.replace(/\/+$/, '') || DEFAULT_BASE_URL; }
function getStreamUrl() { return `${getBaseUrl()}/stream.h264`; }
function getSignalUrl() { return `${getBaseUrl()}/api/wifi/info`; }

document.getElementById('btnApUrl').addEventListener('click', () => {
  inputBaseUrl.value = 'http://192.168.4.1:8080';
});

// ── Canvas resize ─────────────────────────────────────────────────────────────
new ResizeObserver(entries => {
  for (const entry of entries) {
    const { inlineSize: w, blockSize: h } = entry.contentBoxSize[0];
    const rw = Math.round(w), rh = Math.round(h);
    if (canvas.width !== rw || canvas.height !== rh) {
      canvas.width = rw;
      canvas.height = rh;
    }
  }
}).observe(canvasWrap);

// ── Worker + frame rendering ──────────────────────────────────────────────────
let worker = null;
let pendingBitmap = null;
let rafScheduled = false;

function renderFrame() {
  rafScheduled = false;
  if (!pendingBitmap) return;

  const cw = canvas.width, ch = canvas.height;
  const vw = pendingBitmap.width, vh = pendingBitmap.height;
  const scale = Math.min(cw / vw, ch / vh);
  const dw = vw * scale, dh = vh * scale;
  const dx = (cw - dw) / 2, dy = (ch - dh) / 2;

  ctx.clearRect(0, 0, cw, ch);
  ctx.drawImage(pendingBitmap, dx, dy, dw, dh);
  pendingBitmap.close();
  pendingBitmap = null;
}

function onWorkerMessage({ data: msg }) {
  switch (msg.type) {
    case 'frame':
      if (pendingBitmap) pendingBitmap.close();
      pendingBitmap = msg.bitmap;
      if (!rafScheduled) { rafScheduled = true; requestAnimationFrame(renderFrame); }
      break;
    case 'fps': fpsText.textContent = `FPS: ${msg.fps}`; break;
    case 'status': statusText.textContent = msg.text; break;
    case 'error': statusText.textContent = `Error: ${msg.message}`; break;
    case 'done': cleanup(); break;
  }
}

// ── Connect / disconnect ──────────────────────────────────────────────────────
function connect() {
  btnConnect.disabled = true;
  btnDisconnect.disabled = false;
  inputBaseUrl.disabled = true;
  statusText.textContent = 'Connecting...';
  fpsText.textContent = 'FPS: --';

  if (pendingBitmap) { pendingBitmap.close(); pendingBitmap = null; }

  worker = new Worker('worker.js');
  worker.onmessage = onWorkerMessage;
  worker.postMessage({ type: 'start', url: getStreamUrl() });

  scheduleSignalPoll(1000); // defined in signal_info.js
}

function disconnect() {
  if (worker) worker.postMessage({ type: 'stop' });
}

function cleanup() {
  stopSignalPoll(); // defined in signal_info.js

  if (pendingBitmap) { pendingBitmap.close(); pendingBitmap = null; }
  if (worker) { worker.terminate(); worker = null; }

  fpsText.textContent = 'FPS: --';
  btnConnect.disabled = false;
  btnDisconnect.disabled = true;
  inputBaseUrl.disabled = false;
}

btnConnect.addEventListener('click', connect);
btnDisconnect.addEventListener('click', disconnect);

// ── Top bar collapse ──────────────────────────────────────────────────────────
const topBar = document.getElementById('topBar');
const topBarToggle = document.getElementById('topBarToggle');

topBarToggle.addEventListener('click', () => {
  const collapsed = topBar.classList.toggle('collapsed');
  topBarToggle.classList.toggle('collapsed', collapsed);
  topBarToggle.textContent = collapsed ? '▼' : '▲';
  topBarToggle.title = collapsed ? 'Expand toolbar' : 'Collapse toolbar';
});
