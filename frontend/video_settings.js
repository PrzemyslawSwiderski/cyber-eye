const settingsToggle = document.getElementById('settingsToggle');
const settingsPanel = document.getElementById('settingsPanel');
const sliderBright = document.getElementById('brightness');
const sliderContrast = document.getElementById('contrast');
const sliderExposure = document.getElementById('exposure');
const valBright = document.getElementById('brightnessVal');
const valContrast = document.getElementById('contrastVal');
const valExposure = document.getElementById('exposureVal');
const btnSettingsReset = document.getElementById('settingsReset');

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
  document.getElementById('canvasWrap').style.filter =
    `brightness(${exposureFactor}%) contrast(${c}%) brightness(${b}%)`;
}

sliderBright.addEventListener('input', applyFilters);
sliderContrast.addEventListener('input', applyFilters);
sliderExposure.addEventListener('input', applyFilters);

btnSettingsReset.addEventListener('click', () => {
  sliderBright.value = 100;
  sliderContrast.value = 100;
  sliderExposure.value = 0;
  applyFilters();
});
