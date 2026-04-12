import { VideoStreamHandler } from './video_stream.js';
import { scheduleSignalPoll, stopSignalPoll } from './signal_info.js';

// ── DOM refs ──────────────────────────────────────────────────────────────────
const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
const statusText = document.getElementById('statusText');
const fpsText = document.getElementById('fpsText');
const btnConnect = document.getElementById('btnConnect');
const btnDisconnect = document.getElementById('btnDisconnect');
const inputBaseUrl = document.getElementById('inputBaseUrl');
const canvasWrap = document.getElementById('canvasWrap');

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

// ── Video Stream Handler ──────────────────────────────────────────────────────
let streamHandler = null;

// ── Connect / disconnect ──────────────────────────────────────────────────────
function connect() {
  btnConnect.disabled = true;
  btnDisconnect.disabled = false;
  inputBaseUrl.disabled = true;
  statusText.textContent = 'Connecting...';
  fpsText.textContent = 'FPS: --';

  // Create stream handler
  streamHandler = new VideoStreamHandler(
    canvas,
    ctx,
    (status) => { statusText.textContent = status; },
    (fps) => { fpsText.textContent = `FPS: ${fps}`; }
  );

  // Start stream
  streamHandler.start();

  // scheduleSignalPoll(1000); // defined in signal_info.js
}

function disconnect() {
  if (streamHandler) {
    streamHandler.stop();
  }
  cleanup();
}

function cleanup() {
  stopSignalPoll();

  if (streamHandler) {
    streamHandler.cleanup();
    streamHandler = null;
  }

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
