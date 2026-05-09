import { getBaseUrl } from './config.js';

const btnReset = document.getElementById('btnReset');
const btnFetchTasks = document.getElementById('btnFetchTasks');
const tasksLog = document.getElementById('tasksLog');

// ── Reset ────────────────────────────────────────────────────────────────────
btnReset.addEventListener('click', async () => {
  if (!confirm('Reset the ESP now?')) return;
  btnReset.disabled = true;
  try {
    await fetch(`${getBaseUrl()}/api/system/reset`);
  } catch {
    // esp_restart() kills the connection before a response arrives — that's fine
  } finally {
    btnReset.disabled = false;
  }
});

// ── Fetch Tasks ──────────────────────────────────────────────────────────────
btnFetchTasks.addEventListener('click', async () => {
  btnFetchTasks.disabled = true;
  tasksLog.value = 'Fetching…';
  try {
    const res = await fetch(`${getBaseUrl()}/api/system/tasks`);
    const json = await res.json();
    if (!res.ok) throw new Error(json.error ?? `HTTP ${res.status}`);

    const windowMs = json.window_ms;
    const header = `Tasks: ${json.task_count} (measured over ${windowMs}ms)\n` +
      `${'─'.repeat(77)}\n` +
      `${'Name'.padEnd(17)} ${'State'.padEnd(11)} ${'Pri'.padEnd(4)} ${'Core'.padEnd(5)} ${'Base'.padEnd(5)} ${'Free'.padEnd(8)} ${'Pressure'.padEnd(10)} ${'CPU%'.padEnd(6)}\n` +
      `${'─'.repeat(77)}\n`;

    const rows = json.tasks
      .sort((a, b) => b.cpu_percent - a.cpu_percent)
      .map(t => {
        const coreDisplay = t.core === -1 ? 'any' : `C${t.core}`;
        const freeKb = (t.stack_free_min / 1024).toFixed(1);
        return `${t.name.padEnd(17)} ${t.state.padEnd(11)} ${String(t.priority).padEnd(4)} ${coreDisplay.padEnd(5)} ${String(t.base_priority).padEnd(5)} ${(freeKb + 'K').padEnd(8)} ${t.stack_pressure.padEnd(10)} ${(t.cpu_percent.toFixed(1) + '%').padEnd(6)}`;
      }).join('\n');

    // Calculate summary statistics
    const totalCpu = json.tasks.reduce((sum, t) => sum + t.cpu_percent, 0).toFixed(1);
    const criticalTasks = json.tasks.filter(t => t.stack_pressure === 'CRITICAL').length;
    const highPressureTasks = json.tasks.filter(t => t.stack_pressure === 'HIGH').length;

    const summary = `\n${'─'.repeat(77)}\n` +
      `Total CPU: ${totalCpu}% | Critical stack: ${criticalTasks} | High pressure: ${highPressureTasks}\n` +
      `Free stack shown in KB (1KB = 1024 bytes)`;

    tasksLog.value = header + rows + summary;
  } catch (err) {
    tasksLog.value = `Error: ${err.message}`;
  } finally {
    btnFetchTasks.disabled = false;
  }
});
