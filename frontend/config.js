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
