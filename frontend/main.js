const DEFAULT_BASE_URL = 'http://192.168.1.17:8080';

// ---- DOM refs ----

const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
const statusText = document.getElementById('statusText');
const fpsText = document.getElementById('fpsText');
const signalText = document.getElementById('signalText');
const tempText = document.getElementById('tempText');
const btnConnect = document.getElementById('btnConnect');
const btnDisconnect = document.getElementById('btnDisconnect');
const inputBaseUrl = document.getElementById('inputBaseUrl');

function getBaseUrl() {
  return inputBaseUrl.value.replace(/\/+$/, '') || DEFAULT_BASE_URL;
}

function getStreamUrl() { return `${getBaseUrl()}/stream.h264`; }
function getSignalUrl() { return `${getBaseUrl()}/api/signal/info`; }

// Keep canvas pixel dimensions matched to its container size
const canvasWrap = document.getElementById('canvasWrap');
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
    const { wifi_signal_strength: s, temperature_sensor_get_celsius: t } = await res.json();
    signalText.textContent = `Signal: ${s} dBm ${signalBars(s)}`;
    tempText.textContent = t != null ? `Temp: ${t.toFixed(1)} °C` : 'Temp: N/A';
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

// ---- Event listeners ----

btnConnect.addEventListener('click', connect);
btnDisconnect.addEventListener('click', disconnect);

// ---- Top bar collapse ----

const topBar = document.getElementById('topBar');
const topBarToggle = document.getElementById('topBarToggle');

topBarToggle.addEventListener('click', () => {
  const collapsed = topBar.classList.toggle('collapsed');
  topBarToggle.classList.toggle('collapsed', collapsed);
  topBarToggle.textContent = collapsed ? '▼' : '▲';
  topBarToggle.title = collapsed ? 'Expand toolbar' : 'Collapse toolbar';
});

const settingsToggle = document.getElementById('settingsToggle');
const settingsPanel = document.getElementById('settingsPanel');
const sliderBright = document.getElementById('brightness');
const sliderContrast = document.getElementById('contrast');
const sliderExposure = document.getElementById('exposure');
const valBright = document.getElementById('brightnessVal');
const valContrast = document.getElementById('contrastVal');
const valExposure = document.getElementById('exposureVal');
const btnReset = document.getElementById('settingsReset');

settingsToggle.addEventListener('click', () => {
  settingsPanel.classList.toggle('open');
});

function applyFilters() {
  const b = sliderBright.value;
  const c = sliderContrast.value;
  const ev = sliderExposure.value;

  valBright.textContent = `${b}%`;
  valContrast.textContent = `${c}%`;
  valExposure.textContent = ev > 0 ? `+${ev}` : `${ev}`;

  // Exposure is simulated via an extra brightness layer (1ev ≈ 2x brightness)
  const exposureFactor = Math.pow(2, ev / 100) * 100;
  canvasWrap.style.filter = `brightness(${exposureFactor}%) contrast(${c}%) brightness(${b}%)`;
}

sliderBright.addEventListener('input', applyFilters);
sliderContrast.addEventListener('input', applyFilters);
sliderExposure.addEventListener('input', applyFilters);

btnReset.addEventListener('click', () => {
  sliderBright.value = 100;
  sliderContrast.value = 100;
  sliderExposure.value = 0;
  applyFilters();
});
