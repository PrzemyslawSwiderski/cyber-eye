import { getBaseUrl } from './config.js';

const btnAp = document.getElementById('btnApMode');
const btnSta = document.getElementById('btnStaConnect');
const inputSsid = document.getElementById('inputSsid');
const inputPwd = document.getElementById('inputPassword');
const wifiStatus = document.getElementById('wifiStatus');

function setStatus(msg, type = 'secondary') {
  wifiStatus.textContent = msg;
  wifiStatus.className = `small ms-2 text-${type}`;
}

// ── AP mode ──────────────────────────────────────────────────────────────────
btnAp.addEventListener('click', () => {
  fetch(`${getBaseUrl()}/api/wifi/ap`).catch(() => { });
  setStatus('AP mode triggered — connection will drop', 'warning');
});

// ── STA mode ─────────────────────────────────────────────────────────────────
btnSta.addEventListener('click', () => {
  const ssid = inputSsid.value.trim();
  if (!ssid) {
    setStatus('SSID is required', 'danger');
    inputSsid.focus();
    return;
  }

  const params = new URLSearchParams({ ssid });
  const pwd = inputPwd.value;
  if (pwd) params.set('password', pwd);

  fetch(`${getBaseUrl()}/api/wifi/sta?${params}`).catch(() => { });
  setStatus(`STA mode triggered — connecting to "${ssid}"…`, 'info');
  inputPwd.value = '';
});

// ── Enter key shortcut ───────────────────────────────────────────────────────
[inputSsid, inputPwd].forEach(el =>
  el.addEventListener('keydown', e => { if (e.key === 'Enter') btnSta.click(); })
);
