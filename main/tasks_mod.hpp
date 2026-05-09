#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

namespace tasks
{
  // ── constants ───────────────────────────────────────────────────────────────
  inline constexpr uint32_t MEASURE_WINDOW_MS = 1000;

  static constexpr const char *STATE_NAMES[] = {
      "Running", "Ready", "Blocked", "Suspended", "Deleted",
  };
  static constexpr const char *STATE_SHORT[] = {
      "RUN", "RDY", "BLK", "SUS", "DEL",
  };

  inline const char *state_name(UBaseType_t s)  { return s < 5 ? STATE_NAMES[s] : "Unknown"; }
  inline const char *state_short(UBaseType_t s) { return s < 5 ? STATE_SHORT[s] : "???"; }

  // ── types ────────────────────────────────────────────────────────────────────
  struct TaskMetrics
  {
    char         name[configMAX_TASK_NAME_LEN];
    UBaseType_t  priority;
    UBaseType_t  base_priority;
    eTaskState   state;
    int          core_id;
    uint32_t     stack_free_min;
    float        cpu_percent;   // CPU usage over the last MEASURE_WINDOW_MS
    TaskHandle_t handle;
  };

  // Map of handle -> runtime counter from the previous snapshot
  using RuntimeMap = std::unordered_map<TaskHandle_t, uint32_t>;

  // ── raw snapshot ─────────────────────────────────────────────────────────────
  inline std::vector<TaskStatus_t> raw_snapshot(uint32_t &total_runtime)
  {
    UBaseType_t count = uxTaskGetNumberOfTasks();
    std::vector<TaskStatus_t> buf(count);
    count = uxTaskGetSystemState(buf.data(), count, &total_runtime);
    buf.resize(count);
    return buf;
  }

  // ── windowed snapshot ────────────────────────────────────────────────────────
  inline std::vector<TaskMetrics> snapshot()
  {
    // ── first sample ──
    uint32_t total_a = 0;
    auto buf_a = raw_snapshot(total_a);

    // Build lookup: handle -> runtime at T0
    RuntimeMap rt_a;
    rt_a.reserve(buf_a.size());
    for (auto &t : buf_a)
      rt_a[t.xHandle] = t.ulRunTimeCounter;

    // ── wait one window ──
    vTaskDelay(pdMS_TO_TICKS(MEASURE_WINDOW_MS));

    // ── second sample ──
    uint32_t total_b = 0;
    auto buf_b = raw_snapshot(total_b);

    const uint32_t total_delta = (total_b > total_a) ? (total_b - total_a) : 1;

    // ── compute metrics from T1, with delta CPU% ──
    std::vector<TaskMetrics> out;
    out.reserve(buf_b.size());

    for (auto &t : buf_b)
    {
      TaskMetrics m{};
      snprintf(m.name, sizeof(m.name), "%s", t.pcTaskName);
      m.priority      = t.uxCurrentPriority;
      m.base_priority = t.uxBasePriority;
      m.state         = t.eCurrentState;
      m.handle        = t.xHandle;
      m.stack_free_min = t.usStackHighWaterMark;

      // Delta CPU over the measurement window
      auto it = rt_a.find(t.xHandle);
      if (it != rt_a.end() && t.ulRunTimeCounter >= it->second)
      {
        uint32_t task_delta = t.ulRunTimeCounter - it->second;
        m.cpu_percent = task_delta * 100.0f / total_delta;
      }
      else
      {
        // Task was created during the window — treat as 0% for this sample
        m.cpu_percent = 0.0f;
      }

      BaseType_t core = xTaskGetCoreID(t.xHandle);
      m.core_id = (core == tskNO_AFFINITY) ? -1 : static_cast<int>(core);

      out.push_back(m);
    }

    std::sort(out.begin(), out.end(),
              [](const TaskMetrics &a, const TaskMetrics &b) {
                return a.cpu_percent > b.cpu_percent;
              });

    return out;
  }

  // ── stack pressure ───────────────────────────────────────────────────────────
  inline const char *stack_pressure(uint32_t free_min)
  {
    if (free_min <  512) return "CRITICAL";
    if (free_min < 1024) return "HIGH";
    if (free_min < 2048) return "MODERATE";
    return "OK";
  }

  // ── JSON output ──────────────────────────────────────────────────────────────
  inline cJSON *to_json()
  {
    auto tasks = snapshot();
    cJSON *root  = cJSON_CreateObject();
    cJSON *array = cJSON_CreateArray();

    for (const auto &m : tasks)
    {
      cJSON *t = cJSON_CreateObject();
      cJSON_AddStringToObject(t, "name",           m.name);
      cJSON_AddStringToObject(t, "state",          state_name(m.state));
      cJSON_AddNumberToObject(t, "priority",       m.priority);
      cJSON_AddNumberToObject(t, "base_priority",  m.base_priority);
      cJSON_AddNumberToObject(t, "core",           m.core_id);
      cJSON_AddNumberToObject(t, "stack_free_min", m.stack_free_min);
      cJSON_AddStringToObject(t, "stack_pressure", stack_pressure(m.stack_free_min));
      cJSON_AddNumberToObject(t, "cpu_percent",    static_cast<double>(m.cpu_percent));
      cJSON_AddItemToArray(array, t);
    }

    cJSON_AddNumberToObject(root, "task_count",    tasks.size());
    cJSON_AddNumberToObject(root, "window_ms",     MEASURE_WINDOW_MS);
    cJSON_AddItemToObject(root,   "tasks",         array);
    return root;
  }

  // ── text table ───────────────────────────────────────────────────────────────
  inline std::string to_table()
  {
    auto tasks = snapshot();
    if (tasks.empty()) return "(no tasks)\n";

    std::string out;
    out.reserve(tasks.size() * 80);

    char line[96];
    snprintf(line, sizeof(line),
             "%-16s %-5s %4s %4s %5s %9s %-10s %7s\n",
             "Name", "State", "Pri", "Core", "BasP", "Free(mn)", "Pressure", "CPU%");
    out += line;
    out += std::string(76, '-') + '\n';

    for (const auto &m : tasks)
    {
      const char *core_str = (m.core_id == -1) ? "any" :
                             (m.core_id == 0)  ? "C0"  : "C1";

      snprintf(line, sizeof(line),
               "%-16s %-5s %4u %4s %5u %7u B   %-10s %5.1f%%\n",
               m.name,
               state_short(m.state),
               static_cast<unsigned>(m.priority),
               core_str,
               static_cast<unsigned>(m.base_priority),
               static_cast<unsigned>(m.stack_free_min),
               stack_pressure(m.stack_free_min),
               m.cpu_percent);
      out += line;
    }

    out += std::string(76, '-') + '\n';
    out += "CPU% measured over last 1000 ms window\n";
    return out;
  }

} // namespace tasks
