import { getBaseUrl } from './config.js';
const signalText = document.getElementById('signalText');
const tempText = document.getElementById('tempText');

let signalInterval = null;

function signalBars(dbm) {
  if (dbm >= -40) return '█████ Excellent';
  if (dbm >= -50) return '▄████ Very good';
  if (dbm >= -57) return '▄███░ Good';
  if (dbm >= -63) return '▄██░░ Fair';
  if (dbm >= -70) return '▄█░░░ Weak';
  if (dbm >= -78) return '▄░░░░ Poor';
  if (dbm >= -85) return '░░░░░ Very poor';
  return '░░░░░ No signal';
}

async function pollSignal() {
  try {
    const signalUrl = `${getBaseUrl()}/api/wifi/info`;
    const res = await fetch(signalUrl, { signal: AbortSignal.timeout(500) });
    const { wifi_signal_strength: s, temperature_sensor_get_celsius: t } = await res.json();
    signalText.innerHTML = `Signal: ${s} dBm<br><span class="font-monospace">${signalBars(s)}</span>`;
    tempText.textContent = t != null ? `Temp: ${t.toFixed(1)} °C` : 'Temp: N/A';
  } catch (e) {
    // console.error(e);
    signalText.textContent = e.name === 'TimeoutError' ? 'Signal: timeout' : 'Signal: N/A';
  }
}

export function scheduleSignalPoll(delay) {
  clearTimeout(signalInterval);
  clearInterval(signalInterval);

  signalInterval = setTimeout(() => {
    const run = () => pollSignal();

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

export function stopSignalPoll() {
  clearTimeout(signalInterval);
  clearInterval(signalInterval);
  signalInterval = null;
  signalText.textContent = 'Signal: --';
  tempText.textContent = 'Temp: --';
}
