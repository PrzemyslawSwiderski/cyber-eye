const DEFAULT_BASE_URL = 'http://192.168.1.17:8080';
const inputBaseUrl = document.getElementById('inputBaseUrl');

export function getBaseUrl() {
  return inputBaseUrl.value.replace(/\/+$/, '') || DEFAULT_BASE_URL;
}

export function setBaseUrl(url) {
  inputBaseUrl.value = url;
}

document.getElementById('btnApUrl').addEventListener('click', () => {
  setBaseUrl('http://192.168.4.1:8080');
});

document.getElementById('btnSta1Url').addEventListener('click', () => {
  setBaseUrl('http://192.168.1.17:8080');
});

document.getElementById('btnHsUrl').addEventListener('click', () => {
  setBaseUrl('http://10.67.45.134:8080');
});
