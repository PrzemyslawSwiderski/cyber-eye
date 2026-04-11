(() => {
  'use strict';

  const btnReset = document.getElementById('btnReset');
  const btnFetchTasks = document.getElementById('btnFetchTasks');
  const tasksLog = document.getElementById('tasksLog');
  const inputBase = document.getElementById('inputBaseUrl');

  function baseUrl() {
    return inputBase.value.replace(/\/$/, '');
  }

  // ── Reset ────────────────────────────────────────────────────────────────────
  btnReset.addEventListener('click', async () => {
    if (!confirm('Reset the ESP now?')) return;
    btnReset.disabled = true;
    try {
      await fetch(`${baseUrl()}/api/system/reset`);
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
      const res = await fetch(`${baseUrl()}/api/system/tasks`);
      const json = await res.json();
      if (!res.ok) throw new Error(json.error ?? `HTTP ${res.status}`);

      const header = `Tasks: ${json.task_count}\n${'─'.repeat(52)}\n` +
        `${'Name'.padEnd(16)} ${'Pri'.padStart(3)}  ${'HWM'.padStart(6)}  State\n` +
        `${'─'.repeat(52)}\n`;

      const STATE = ['Running', 'Ready', 'Blocked', 'Suspended', 'Deleted'];
      const rows = json.tasks
        .sort((a, b) => b.priority - a.priority)
        .map(t =>
          `${t.name.padEnd(16)} ${String(t.priority).padStart(3)}  ${String(t.stack_hwm).padStart(6)}  ${STATE[t.state] ?? t.state}`
        ).join('\n');

      tasksLog.value = header + rows;
    } catch (err) {
      tasksLog.value = `Error: ${err.message}`;
    } finally {
      btnFetchTasks.disabled = false;
    }
  });
})();
