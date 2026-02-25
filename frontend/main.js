const DEFAULT_BASE_URL = 'http://192.168.1.17:8080';

// ---- DOM refs ----

const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('bitmaprenderer');
const statusText = document.getElementById('statusText');
const fpsText = document.getElementById('fpsText');
const signalText = document.getElementById('signalText');
const btnConnect = document.getElementById('btnConnect');
const btnDisconnect = document.getElementById('btnDisconnect');
const btnFullscreen = document.getElementById('btnFullscreen');
const inputBaseUrl = document.getElementById('inputBaseUrl');

function getBaseUrl() {
  return inputBaseUrl.value.replace(/\/+$/, '') || DEFAULT_BASE_URL;
}

function getStreamUrl() { return `${getBaseUrl()}/stream.h264`; }
function getSignalUrl() { return `${getBaseUrl()}/api/signal/info`; }

// ---- Worker management ----

let worker = null;
let pendingBitmap = null;
let rafScheduled = false;

function createWorker() {
  return new Worker('worker.js');
}

function renderFrame() {
  rafScheduled = false;
  if (!pendingBitmap) return;
  ctx.transferFromImageBitmap(pendingBitmap);
  pendingBitmap = null;
}

function onWorkerMessage({ data: msg }) {
  switch (msg.type) {
    case 'frame':
      if (msg.bitmap.width !== canvas.width || msg.bitmap.height !== canvas.height) {
        canvas.width = msg.bitmap.width;
        canvas.height = msg.bitmap.height;
      }
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

// ---- Signal polling ----

let signalInterval = null;

function signalBars(dbm) {
  if (dbm >= -50) return '▂▄▆█';
  if (dbm >= -60) return '▂▄▆_';
  if (dbm >= -70) return '▂▄__';
  if (dbm >= -80) return '▂___';
  return '____';
}

async function pollSignal(deadline) {
  if (pendingBitmap || (deadline && deadline.timeRemaining() < 10)) {
    scheduleSignalPoll(500);
    return;
  }

  try {
    const res = await fetch(getSignalUrl(), { signal: AbortSignal.timeout(500) });
    const { wifi_signal_strength: s } = await res.json();
    signalText.textContent = `Signal: ${s} dBm ${signalBars(s)}`;
  } catch (e) {
    signalText.textContent = e.name === 'TimeoutError' ? 'Signal: timeout' : 'Signal: N/A';
  }
}

function scheduleSignalPoll(delay = 3000) {
  clearTimeout(signalInterval);
  clearInterval(signalInterval);

  signalInterval = setTimeout(() => {
    const run = (deadline) => pollSignal(deadline);

    if (typeof scheduler !== 'undefined' && scheduler.postTask)
      scheduler.postTask(() => run(null), { priority: 'background' });
    else if (typeof requestIdleCallback !== 'undefined')
      requestIdleCallback(run, { timeout: 2000 });
    else
      run(null);

    signalInterval = setInterval(() => {
      if (typeof scheduler !== 'undefined' && scheduler.postTask)
        scheduler.postTask(() => pollSignal(null), { priority: 'background' });
      else if (typeof requestIdleCallback !== 'undefined')
        requestIdleCallback(pollSignal, { timeout: 2000 });
      else
        pollSignal(null);
    }, 3000);
  }, delay);
}

// ---- Connect / disconnect ----

function connect() {
  btnConnect.disabled = true;
  btnDisconnect.disabled = false;
  inputBaseUrl.disabled = true;
  statusText.textContent = 'Connecting...';
  fpsText.textContent = 'FPS: --';

  if (pendingBitmap) { pendingBitmap.close(); pendingBitmap = null; }

  worker = createWorker();
  worker.onmessage = onWorkerMessage;
  worker.postMessage({ type: 'start', url: getStreamUrl() });

  scheduleSignalPoll(1000);
}

function disconnect() {
  if (worker) worker.postMessage({ type: 'stop' });
}

function cleanup() {
  clearTimeout(signalInterval);
  clearInterval(signalInterval);
  signalInterval = null;

  if (pendingBitmap) { pendingBitmap.close(); pendingBitmap = null; }
  if (worker) { worker.terminate(); worker = null; }

  fpsText.textContent = 'FPS: --';
  signalText.textContent = 'Signal: --';
  btnConnect.disabled = false;
  btnDisconnect.disabled = true;
  inputBaseUrl.disabled = false;
}

// ---- Fullscreen ----

function toggleFullscreen() {
  const wrapper = document.getElementById('wrapper');
  if (!document.fullscreenElement && !document.webkitFullscreenElement) {
    (wrapper.requestFullscreen || wrapper.webkitRequestFullscreen).call(wrapper);
  } else {
    (document.exitFullscreen || document.webkitExitFullscreen).call(document);
  }
}

// ---- Event listeners ----

btnConnect.addEventListener('click', connect);
btnDisconnect.addEventListener('click', disconnect);
btnFullscreen.addEventListener('click', toggleFullscreen);
