(() => {
  'use strict';

  const btnPlay = document.getElementById('btnMusicPlay');
  const btnStop = document.getElementById('btnMusicStop');
  const btnVolume = document.getElementById('btnVolumeSet');
  const inputFile = document.getElementById('inputMusicFile');
  const inputVol = document.getElementById('inputVolume');
  const musicStatus = document.getElementById('musicStatus');

  function musicUrl(path) {
    return `${getBaseUrl()}${path}`;
  }

  function setStatus(msg, type = 'secondary') {
    musicStatus.textContent = msg;
    musicStatus.className = `small ms-1 text-${type}`;
  }

  btnPlay.addEventListener('click', async () => {
    const file = inputFile.value.trim();
    if (!file) { setStatus('filename required', 'danger'); inputFile.focus(); return; }
    try {
      const res = await fetch(musicUrl(`/api/music/play?file=${file}`));
      const json = await res.json();
      if (!res.ok) throw new Error(json.error ?? `HTTP ${res.status}`);
      setStatus(json.status, 'success');
    } catch (err) {
      setStatus(err.message, 'danger');
    }
  });

  btnStop.addEventListener('click', async () => {
    try {
      const res = await fetch(musicUrl('/api/music/stop'));
      const json = await res.json();
      if (!res.ok) throw new Error(json.error ?? `HTTP ${res.status}`);
      setStatus(json.status, 'secondary');
    } catch (err) {
      setStatus(err.message, 'danger');
    }
  });

  btnVolume.addEventListener('click', async () => {
    const vol = Number(inputVol.value);
    if (vol < 0 || vol > 100) { setStatus('volume must be 0 - 100 ', 'danger'); return; }
    try {
      const res = await fetch(musicUrl(`/api/music/volume?value=${vol}`));
      const json = await res.json();
      if (!res.ok) throw new Error(json.error ?? `HTTP ${res.status}`);
      setStatus(`vol ${vol}%`, 'success');
    } catch (err) {
      setStatus(err.message, 'danger');
    }
  });

  inputFile.addEventListener('keydown', e => { if (e.key === 'Enter') btnPlay.click(); });
  inputVol.addEventListener('keydown', e => { if (e.key === 'Enter') btnVolume.click(); });
})();
