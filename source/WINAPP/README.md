# TelemetryApp — Native Windows System Telemetry Service

> **Version: 1.0.0** | Build: 2026-07-07

**A high-performance, low-overhead Windows telemetry service and real-time client built in native C++.**
Designed for local lab monitoring, AI/ML training observation, data-processing workload capture, workstation maintenance, and structured telemetry export.

**Repository:** https://github.com/dpevzner1/TelemetryApp  
**Contact:** demitri.pevzner@gmail.com  
**License:** MIT License

---

## Table of Contents

1. [Overview](#1-overview)
   - 1.1 [Practical Uses](#11-practical-uses)
2. [Why Native C++ and Windows APIs](#2-why-native-c-and-windows-apis)
3. [Architecture](#3-architecture)
   - 3.1 [Component Map](#31-component-map)
   - 3.2 [Shared Memory Design](#32-shared-memory-design)
   - 3.3 [Seqlock Protocol](#33-seqlock-protocol)
   - 3.4 [Named Pipe IPC](#34-named-pipe-ipc)
4. [Sensor Coverage](#4-sensor-coverage)
   - 4.1 [CPU](#41-cpu)
   - 4.2 [Memory](#42-memory)
   - 4.3 [GPU — Unified Vendor Dispatch](#43-gpu--unified-vendor-dispatch)
   - 4.4 [Disk I/O](#44-disk-io)
   - 4.5 [Network](#45-network)
   - 4.6 [Temperatures](#46-temperatures)
   - 4.7 [Process Watcher](#47-process-watcher)
   - 4.8 [Self-Monitoring](#48-self-monitoring)
5. [Metric ID Schema](#5-metric-id-schema)
6. [REST API Reference](#6-rest-api-reference)
   - 6.1 [Authentication](#61-authentication)
   - 6.2 [Endpoints](#62-endpoints)
   - 6.3 [Prometheus Scrape](#63-prometheus-scrape)
   - 6.4 [Server-Sent Events Stream](#64-server-sent-events-stream)
7. [GUI Client](#7-gui-client)
   - 7.1 [Direct2D Rendering Pipeline](#71-direct2d-rendering-pipeline)
   - 7.2 [Navigation and Pages](#72-navigation-and-pages)
   - 7.3 [Dashboard Profiles](#73-dashboard-profiles)
   - 7.4 [HUD Compact Overlay](#74-hud-compact-overlay)
   - 7.5 [API Key Management Page](#75-api-key-management-page)
   - 7.6 [Metrics Logging Page](#76-metrics-logging-page)
8. [Windows Service Lifecycle](#8-windows-service-lifecycle)
9. [Validation Checks](#9-validation-checks)
10. [Build Instructions](#10-build-instructions)
11. [Configuration](#11-configuration)
12. [Overhead and Self-Impact](#12-overhead-and-self-impact)
13. [AI Model Training Integration](#13-ai-model-training-integration)
    - 13.1 [Why Telemetry Matters During Training](#131-why-telemetry-matters-during-training)
    - 13.2 [Process Watcher for Training Process Isolation](#132-process-watcher-for-training-process-isolation)
    - 13.3 [Metrics Most Valuable for Training Observability](#133-metrics-most-valuable-for-training-observability)
    - 13.4 [Integration Patterns](#134-integration-patterns)
    - 13.5 [Prometheus + Grafana Stack Integration](#135-prometheus--grafana-stack-integration)
    - 13.6 [Structured Telemetry for Dataset Generation](#136-structured-telemetry-for-dataset-generation)
14. [Extending the Tool](#14-extending-the-tool)
15. [License](#15-license)

---

## AI / Script / Operator Quick Start

This section is the fastest path for another operator, script, or AI agent on a host with TelemetryApp installed.

### Executables and Installed Files

Installed package root:

```text
C:\Program Files\TelemetryApp\
  TelemetryApp.exe          # user-facing launcher / desktop app
  telemetry_client.exe      # GUI client
  telemetry_service.exe     # Windows service / console-mode collector
  README.md                 # app, architecture, UI, install, and workflow guide
  API.md                    # generated REST API reference
  PROJECT_PROXIMITY_MATRIX.md # competitor, maturity, and roadmap proximity matrix
  LICENSE
```

Portable test package:

```text
WINAPP\dist\TelemetryApp_Portable\
  TelemetryApp.exe
  telemetry_client.exe
  telemetry_service.exe
  launch_client.bat
  run_service_console.bat
  README.md
  API.md
  PROJECT_PROXIMITY_MATRIX.md
```

The installer writes `ReadmePath`, `ApiGuidePath`, and `ProjectMatrixPath` under `HKLM\SOFTWARE\TelemetryApp`, and sets `TELEMETRY_APP_DIR`, `TELEMETRY_API_URL`, `TELEMETRY_DATA_DIR`, `TELEMETRY_INSTALL_MODE`, `TELEMETRY_INSTALL_ROLE`, and `TELEMETRY_START_MODE`.

Installer update/repair safety:

- Setup requires elevation and stops `TelemetryService`, `telemetry_service.exe`, `telemetry_client.exe`, and `TelemetryApp.exe` before replacing files.
- The stop/unlock preflight writes `%TEMP%\TelemetryApp_install_preflight.log` on the target device.
- If setup aborts before extraction, inspect that log and `HKLM\SOFTWARE\TelemetryApp\InstallAudit`. Exit `51` means the installer was not elevated, `55` means a service/process remained running, `56` means an executable stayed file-locked, and `57` means an unexpected preflight exception occurred.
- Setup intentionally aborts instead of installing mismatched files when an existing executable cannot be unlocked.

### First Verification

1. Start the service from the installer, Windows Services, the app Settings page, or portable `run_service_console.bat`.
2. Start the app with `TelemetryApp.exe` or portable `launch_client.bat`.
3. Confirm the sidebar says `Service: Online`. When online, the sidebar also shows a green `LAN: <ip>:8765` address that can be copied to a Fleet Host for `Manual Add`.
4. Create or copy an API key from `API Keys -> New Key`.
5. Confirm API health:

```powershell
$base = "http://localhost:8765"
$key = "<tlm-api-key>"
Invoke-RestMethod "$base/api/v1/health"
Invoke-RestMethod "$base/api/v1/snapshot" -Headers @{ "X-API-Key" = $key }
Invoke-RestMethod "$base/api/v1/hardware" -Headers @{ "X-API-Key" = $key }
Invoke-RestMethod "$base/api/v1/metrics/catalog" -Headers @{ "X-API-Key" = $key }
```

Authentication accepts `X-API-Key: <key>`, `Authorization: Bearer <key>`, or the `api_key` query parameter. Prefer `X-API-Key` or `Authorization` for scripts.

### Process Capture Through The API

Use this flow when another app, orchestration tool, or script wants TelemetryApp to record resource use for a process run. The cleanest orchestration pattern is: launch the process, register its PID with `POST /api/v1/watch`, start a session, wait for the process, then stop the session.

1. Add a process watch target if per-process telemetry is needed. Prefer PID when your script launched the process and has the exact process ID:

```powershell
$proc = Start-Process python -ArgumentList "train.py" -PassThru

Invoke-RestMethod "$base/api/v1/watch" -Method Post -Headers @{ "X-API-Key" = $key } `
  -ContentType "application/json" `
  -Body (@{ pid = $proc.Id } | ConvertTo-Json)
```

Use `exe_name` when the process may start later or when several runs of the same executable should be watched:

```powershell
Invoke-RestMethod "$base/api/v1/watch" -Method Post -Headers @{ "X-API-Key" = $key } `
  -ContentType "application/json" `
  -Body '{"exe_name":"python.exe","label":"training"}'
```

2. Start a logging session with a custom destination folder:

```powershell
$body = @{
  label = "training-run-001"
  log_dir = "D:\TelemetryRuns\training-run-001"
  metric_ids = @(0, 64, 96, 100, 101, 224, 288, 400, 402, 409)
} | ConvertTo-Json

$session = Invoke-RestMethod "$base/api/v1/sessions" -Method Post `
  -Headers @{ "X-API-Key" = $key } -ContentType "application/json" -Body $body
$session.log_path
```

3. Run the workload.
4. If the script launched the process, wait for it to exit:

```powershell
$proc.WaitForExit()
```

5. Stop and flush the log:

```powershell
Invoke-RestMethod "$base/api/v1/sessions/$($session.session_id)/stop" `
  -Method Post -Headers @{ "X-API-Key" = $key }
```

Session output is JSONL by default. With no `log_dir`, logs go under `%TELEMETRY_DATA_DIR%\logs\sessions\`. With `log_dir`, the service writes `sess-{id}.jsonl` and `sess-{id}.meta.json` into the requested absolute folder. When `metric_ids` is empty, each row includes full system telemetry plus a `watched_processes` array for active watched targets. In filtered mode, include the watch metric IDs you need.

### Current UI Menu Paths

Dashboard:
- `Dashboard -> Edit Dashboard` opens the visual layout editor.
- The editor can hide/show panels, change visualization type, adjust tile size, save/close, cancel, or save the layout as the default preferred config.
- Hidden dashboard panels collapse and remaining visible tiles align left.
- HUD/bar minimize mode uses Windows appbar reservation with stable, idempotent edge geometry. The HUD must not compute its next position from a work area already modified by its own reservation, and appbar callbacks are coalesced to avoid Explorer reflow loops.

Metrics:
- `Metrics -> Logging On/Off` prompts for a log output folder before starting capture.
- `Metrics -> Display All` selects all dashboard-visible metrics.
- `Metrics -> BAR All` selects all eligible HUD/bar metrics.
- Metric groups have clear scrolling and group-level `ALL` selection.

Fleet:
- `Fleet -> Search LAN` probes local subnet readiness endpoints and creates untrusted candidates.
- `Fleet -> Manual Add` probes a specific `ip:port` shown by a remote sensor.
- `Fleet -> Enroll` makes an operator-approved lab trust record for a candidate.
- Enrolled sensors store the Fleet Host URL and call back with `POST /api/v1/fleet/heartbeat`; the Fleet Host reconciles records by `device_id`/`sensor_hash` and `mac_hash`, updates `last_seen_address`, and keeps an `address_history`.
- Heartbeat never auto-enrolls or auto-trusts a device. A changed IP updates an existing trusted row only when the stable identity matches.
- Sensor Client installs cannot discover, poll, or manage other devices. Fleet navigation and cross-device dashboard switching are Fleet Manager functions.

HUD / minimized bar:
- `Settings -> HUD / Minimize Behavior` selects normal minimize, HUD bar, or tray behavior.
- `Settings -> HUD position` selects above taskbar, top edge, left edge, or right edge.
- The HUD registers as a Windows appbar while visible, so normal maximized windows should respect its reserved screen edge.
- Hovering HUD readings shows a short explanation.
- Right-click the HUD bar for Restore, Start/Stop Capturing Logs / Metrics, HUD Orientation, Fleet Device Metrics, and Exit.

System tray:
- The tray icon is always present while the app is running.
- Right-click the tray icon for Restore, Show HUD Bar, Start/Stop Capturing Logs / Metrics, HUD Orientation, Fleet Device Metrics, and Exit.

Fleet:
- The Fleet navigation item is visible only when this installation is `FleetHost`. `LocalMonitor` and `SensorClient` installs hide Fleet Management entirely.
- `Fleet -> Search LAN` probes the local IPv4 subnet for TelemetryApp `/api/v1/enrollment/readiness` endpoints on TCP 8765 and adds responding devices to the unified `Devices` list as untrusted candidates.
- `Fleet -> Manual Add` opens an IP/port entry dialog, probes `/api/v1/enrollment/readiness`, and adds a responding sensor to `Devices` as an untrusted candidate. On the sensor machine, use the green `LAN: <ip>:8765` address shown under `Service: Online`.
- `Devices -> Enroll` sends an explicit lab-enrollment request to the sensor, validates the public sensor fingerprint, records the sensor-side enrollment acknowledgement, and persists the device in the Fleet Host inventory.
- `Devices` contains all known fleet-visible devices: local host, discovered candidates, pending devices, and trusted/enrolled devices. Status pills determine whether a device is `Candidate`, `Trusted`, `Online`, or `Offline`.
- Public readiness metadata includes hostname and a `mac_hash` for duplicate detection. Raw MAC address display is reserved for trusted/enrolled device inventory because raw MAC is a stable hardware identifier.
- `Fleet -> New Logging Job` opens the fleet logging wizard.
- The wizard covers targets, cadence, calendar-style recurring windows or one-time runs, storage folder, lifecycle, format/verbosity, and final validation.
- Queued jobs show status, selected format, selected folder, current folder size, and free disk space.

### API Surface Summary

Core endpoints:
- `GET /api/v1/health` - no-auth service liveness.
- `GET /api/v1/snapshot` - current full telemetry snapshot.
- `GET /api/v1/metrics/catalog` - stable metric IDs and names.
- `GET /api/v1/capabilities` - device and accelerator capabilities.
- `GET /api/v1/hardware` - hardware identity registry for CPU/GPU make, model, topology, provider preference, and capability status.
- `GET /api/v1/diagnostics` - service counters and diagnostics.
- `GET /api/v1/stream` - server-sent events at roughly 1Hz.
- `GET /metrics` - Prometheus exposition.
- `GET/POST/DELETE /api/v1/watch` - process watch targets.
- `GET/POST/DELETE /api/v1/sessions` and `POST /api/v1/sessions/{id}/stop` - logging lifecycle.
- `GET/POST/DELETE/rotate /api/v1/keys` - API key management.
- `GET /api/v1/enrollment/readiness`, `POST /api/v1/enrollment/request`, and `GET /api/v1/install/audit` - current enterprise sensor enrollment contract.

Read `API.md` in the install root for the generated endpoint table and active key prefixes. Read section 13 of this README for full third-party capture examples.

### Current Limits That Another Tool Must Understand

- Fleet device enrollment is now live for explicit lab use: the host can convert discovered candidates into trusted inventory records by clicking `Enroll`. Lab-enrolled sensors expose lab snapshot and logging-session endpoints so `View`, `Start`, `Pause`, and `Delete` can act on trusted remote sensors. Enterprise TLS/mTLS hardening is still required before describing this as enterprise-grade remote telemetry.
- Sensor-client installs cannot discover, poll, or manage other devices.
- A remote sensor will not appear in Fleet search unless its service is running, LAN/API binding is explicitly enabled during install/modify, and the network/firewall allows inbound TCP 8765 to the sensor. Discovery creates candidates only; it does not grant trust.
- API transport encryption is planned for a later release. Current local builds use HTTP on `localhost` unless remote API binding is explicitly enabled.
- Electrical/power telemetry is source-qualified. GPU watts may be measured where vendor APIs expose it; CPU package, platform, and per-process electrical attribution remain Day 2 work unless a reliable provider is present.
- L1/L2/L3 cache topology and cache-utilization telemetry are Day 2/Day 3 features. Do not assume live cache utilization exists in this release.

---

## 1. Overview

TelemetryApp is a Windows-native telemetry service that collects, stores, and exposes real-time hardware and process metrics with sub-1% CPU overhead. It runs as a Windows Service accessible to local tools by default, and ships a GPU-accelerated Direct2D client for on-machine live dashboards. Remote API access is intentionally gated until TLS/enrollment hardening is complete.

The strongest current product fit is **local-first telemetry for small labs, AI/data-processing workstations, and technician-managed device groups**. It is not trying to replace enterprise observability platforms, ETW profilers, or deep hardware lab instruments. Its value is the practical middle ground: a normal Windows user can see the machine live, while a script or another app can start a precise logging session for a workload and write structured logs to a chosen folder.

For live testing, run the single launcher:

```text
bin\TelemetryApp.exe
```

The launcher starts the backend service hidden, opens the Windows desktop client, and stops the backend instance it started when the GUI closes. All runtime executables and diagnostic logs live together in `bin\`.

**Key design goals:**
- **Surgical precision** — measure what a specific process is actually consuming, isolated from system noise
- **Minimal footprint** — the service itself stays below 5% CPU and ~20MB RAM under full load
- **No third-party monitoring agents** — no LibreHardwareMonitor, no WMI performance counters for the hot path, no PDH queries opened per-poll
- **API-first** — every metric is reachable via REST, Prometheus scrape, or SSE stream from any process or machine on the network
- **AI/ML ready** — designed from the ground up to feed training pipelines, capture anomalies, and provide reproducible hardware baselines for model benchmarking

### 1.1 Practical Uses

Core intended uses:

- **AI training and inference workstation telemetry** - track CPU, memory, GPU, VRAM, thermals, disk, network, and watched-process metrics during model training, fine-tuning, inference batches, local RAG indexing, embedding generation, and GPU-heavy experimentation.
- **Data-processing run capture** - wrap ETL jobs, batch scripts, compression/decompression runs, database imports, video processing, CAD/export jobs, simulations, or scientific workloads with a start/stop logging session.
- **Small local lab visibility** - maintain a simple, local-first view of several Windows machines used for AI experiments, benchmarks, student labs, technician benches, or data-processing stations. Current remote fleet telemetry remains gated until secure enrollment is complete.
- **Device performance maintenance** - compare current baseline behavior against previous known-good runs, including thermal rise, RAM pressure, page faults, disk throughput, GPU utilization, and service overhead.
- **Proactive degradation signals** - detect patterns that suggest a machine needs attention: rising idle temperature, thermal throttling, lower clocks under the same workload, elevated page faults, falling disk throughput, abnormal network throughput, high thermal behavior inferred from temperature curves, or a process consuming more memory/handles over repeated runs.
- **Benchmark and procurement validation** - capture repeatable telemetry for hardware comparisons, driver changes, BIOS changes, memory upgrades, GPU swaps, and storage changes.
- **Run reproducibility and audit logs** - keep structured JSONL/CSV evidence for what happened during a workload, where logs were written, which metrics were selected, and how long the run lasted.
- **Developer and QA regression testing** - record before/after resource profiles for desktop apps, services, CLI tools, installers, games, renderers, and background workers.
- **Training-data generation for observability models** - export telemetry as structured data for anomaly detection, performance modeling, or workload classification.

Helpful edge cases:

- Spotting a cooling issue after dust buildup or a failed fan by comparing thermal curves under the same workload.
- Finding whether a run is GPU-bound, CPU-bound, memory-bound, disk-bound, or network-bound before changing code or hardware.
- Capturing telemetry for intermittent failures by leaving a low-overhead session active during a scheduled test window.
- Verifying that a background service, updater, scanner, or sync tool is interfering with an AI/data workload.
- Checking whether a laptop is power/thermal constrained compared with the same run on AC power, after a driver update, or after a firmware change.

Alerting status: the API and SSE stream are suitable for external alerting scripts today. Built-in proactive degradation alerting is a natural next feature: define baselines, thresholds, trend windows, and notification actions, then surface them in Dashboard/Fleet without pretending to be a full enterprise SIEM or NMS.

---

## 2. Why Native C++ and Windows APIs

The Python predecessor (`collector.py` / `process_watcher.py`) was functionally complete but carried structural overhead:

| Problem | Root cause | C++ fix |
|---|---|---|
| 25ms overhead per PDH poll | `PdhOpenQuery` called every tick | One persistent `HQUERY` opened at init |
| Double PDH sleep (100ms/poll) | GPU system-wide + per-process queries each slept 50ms independently | Single merged query; one 50ms sleep covers both |
| NVML state corruption | `nvmlInit()`/`nvmlShutdown()` called in process_watcher | Ownership moved to collector; single lifetime |
| Mutation during iteration | `record["entries"].remove(e)` inside a loop | List comprehension replace; C++ uses erase-remove idiom |
| GIL contention on poll thread | CPython GIL | Eliminated entirely in native C++ |
| ~15MB Python runtime overhead | Interpreter | Single ~2MB service EXE, no runtime dependency |

Native C++ was chosen over Rust for two reasons: the Windows SDK COM/WMI APIs have C++ bindings as their primary interface (the `comdef.h`, `wbemidl.h`, `d2d1.h` families), and the team's existing proficiency with the Windows internals APIs (`NtQuerySystemInformation`, `DeviceIoControl`, SCM) made C++ the path of least resistance for a correct, auditable implementation. The current build targets C++17 compatibility because the validated local compiler path is Visual Studio 18's scoped VC15 SDK.

---

## 3. Architecture

### 3.1 Component Map

```
┌─────────────────────────────────────────────────────────────────────┐
│  telemetry_service.exe  (Windows Service — runs as SYSTEM or user)  │
│                                                                     │
│  ┌─────────────┐   ┌──────────────────────────────────────────┐    │
│  │  Poll Loop  │──▶│  Sensor Layer                            │    │
│  │  (1 Hz)     │   │  cpu · memory · gpu · disk · net · temp  │    │
│  └──────┬──────┘   └──────────────────────────────────────────┘    │
│         │                                                           │
│         ▼                                                           │
│  ┌──────────────┐    seqlock write                                  │
│  │  SHM Writer  │──────────────────────▶  Local\TelemetryApp_SHM_v1│
│  └──────┬───────┘                         (≈1.19 MB, 512 rings)    │
│         │                                                           │
│  ┌──────▼───────┐  ┌──────────────────────────────────────────┐    │
│  │  Pipe Server │  │  HTTP Server  (cpp-httplib, port 8765)   │    │
│  │  (\\.\pipe\  │  │  GET /api/v1/snapshot                    │    │
│  │  TelemetryApp│  │  GET /api/v1/history/<id>                │    │
│  │  push, 1Hz)  │  │  GET /metrics  (Prometheus)              │    │
│  └──────────────┘  │  GET /api/v1/stream  (SSE)               │    │
│                    │  GET|POST|DELETE /api/v1/keys             │    │
│                    │  POST /api/v1/keys/{id}/rotate            │    │
│                    │  ApiKeyStore (BCrypt SHA-256, JSON store) │    │
│                    └──────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
              ▲ SHM map (read-only)          ▲ HTTP / named pipe
              │                              │
┌─────────────┴──────────────────────────────────────────────────────┐
│  telemetry_client.exe  (Direct2D GUI, native C++)                   │
│                                                                     │
│  Sidebar ──▶ [Dashboard] [API] [Metrics] [Settings]                │
│                                                                     │
│  Dashboard: 12-col grid, 9 viz types, JSON profile, import/export  │
│  API page:  key table, create/rotate/delete, one-time key banner   │
│  Metrics:   5 functional logging groups, per-metric checkboxes     │
│  Settings:  connection, HUD mode selector, position, service mgmt  │
│                                                                     │
│  ┌───────────────┐  ┌─────────────────────────────────────────┐    │
│  │  CompactOverlay│  │  SystemTray                            │    │
│  │  (HUD bar)    │  │  (tray icon + right-click menu)        │    │
│  │  TOPMOST      │  │  Restore / HUD Mode / Exit             │    │
│  │  TOOLWINDOW   │  └─────────────────────────────────────────┘    │
│  │  36px × screen│                                                  │
│  │  1Hz SHM read │                                                  │
│  └───────────────┘                                                  │
└────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────────┐
│  Any external consumer:                                             │
│  · Python training script    · Grafana (Prometheus scrape)         │
│  · curl / custom REST client · AI benchmark harness                │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 Shared Memory Design

The inter-process communication backbone is a Windows Memory-Mapped File (MMF) named `Local\TelemetryApp_SHM_v1`. It contains a single `ShmBlock` structure:

```
ShmBlock (≈1.19 MB total)
├── ShmHeader (64B cache-line aligned)
│   ├── magic, version
│   ├── write_seq  (atomic<uint64_t>, seqlock counter)
│   ├── ts_poll_ms, poll_duration_ms
│   ├── active_gpu_count, active_disk_count, active_nic_count
│   ├── active_watch_count, active_temp_count, active_cpu_cores
│   ├── service_alive flag
│   └── Label tables (UTF-8 char[]):
│       gpu_name[4][64], disk_name[8][32], nic_name[8][48]
│       watch_label[8][64], temp_name[32][64], service_ver[32]
└── MetricRing[512]   (2432 bytes each = 1,245,184 bytes)
    ├── head, count  (ring buffer position)
    ├── current, min_session, max_session
    └── values[300]  (double, 300-sample rolling history)
```

**Why MMF over a socket or pipe for the GUI?**

The GUI needs the last 300 samples of every metric to render rolling waveforms at 60fps. Sending that over a pipe or socket on every render frame would be wasteful and would couple the render rate to the poll rate. With the MMF, the GUI maps the block once and reads any ring directly at render time — zero copy, zero syscalls in the hot path.

The named pipe is used for push-based delivery to external consumers (Python scripts, CLI tools) that prefer event-driven updates over polling.

### 3.3 Seqlock Protocol

All SHM writes use a seqlock to eliminate the need for a kernel mutex:

```
Writer (service):
  write_seq.fetch_add(1)  // → odd: readers spin
  <write all metric rings>
  write_seq.fetch_add(1)  // → even: stable

Reader (client/GUI):
  do {
    seq0 = write_seq.load(acquire)
    if (seq0 & 1) { spin; continue; }   // writer active
    <read desired metrics>
    seq1 = write_seq.load(acquire)
  } while (seq1 != seq0);               // torn read: retry
```

This gives 1–3µs reader latency with no kernel transitions. The 60fps GUI renderer never blocks behind the 1Hz service writer.

### 3.4 Named Pipe IPC

The service creates `\\.\pipe\TelemetryApp` as a byte-stream pipe with overlapped I/O. Each connected client receives a binary frame ~1Hz:

```
Frame layout:
  uint32_t  magic    (0x54454C4D)
  uint32_t  version  (1)
  uint64_t  ts_ms    (Unix millisecond timestamp)
  uint32_t  n_metrics
  n_metrics × { uint32_t metric_id, double value }
```

Only metrics with non-zero current values are included, keeping frame size small (typically 40–80 metrics × 12 bytes = ~1KB/frame).

---

## 4. Sensor Coverage

### 4.1 CPU

**API used:** `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` via `ntdll.dll`

This is the same single syscall Task Manager uses. It returns `KernelTime`, `UserTime`, and `IdleTime` per logical core in 100ns ticks. Per-core utilization is computed as:

```
usage[core] = (1 - ΔIdle / (ΔKernel + ΔUser)) × 100
```

Note: on Windows, `KernelTime` includes `IdleTime`, so the formula avoids double-counting.

**Actual clock frequency:** PDH `\Processor Information(_Total)\% Processor Performance` multiplied by the rated max frequency from `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0\~MHz`. This gives the real boosted frequency without requiring ACPI or WMI.

**Metrics exposed:**
| Metric | Description |
|---|---|
| `cpu_usage_total_pct` | Aggregate across all logical cores |
| `cpu_freq_actual_mhz` | Boosted actual frequency |
| `cpu_freq_rated_max_mhz` | Base rated max from registry |
| `cpu_core_balance_score` | 0–1; ratio of average core load to peak core load |
| `cpu_hot_core_index` | Index of the most-loaded core |
| `cpu_hot_core_pct` | Utilization of the hottest core |
| `cpu_cold_cores_count` | Cores below 5% utilization |
| `per_core_pct[0..N]` | Per-logical-core utilization |

### 4.2 Memory

**APIs used:** `GlobalMemoryStatusEx`, `NtQuerySystemInformation(SystemMemoryListInformation)`, `GetPerformanceInfo`

`GlobalMemoryStatusEx` gives the standard physical/available RAM and page file totals. The NT information class 80 (`SystemMemoryListInformation`) exposes the standby page list (memory that has been freed but not yet zeroed, and can be reclaimed instantly), which is what Resource Monitor displays as "Standby" memory — unavailable from standard Win32 APIs.

**Metrics exposed:**
| Metric | Description |
|---|---|
| `mem_total_gb` | Physical RAM |
| `mem_used_gb` | Physical RAM in use |
| `mem_available_gb` | Available (free + standby) |
| `mem_percent` | Percent in use (from `MEMORYSTATUSEX.dwMemoryLoad`) |
| `mem_swap_used_gb` | Page file currently in use beyond RAM |
| `mem_standby_gb` | Standby list (can be reclaimed at no cost) |
| `mem_page_fault_rate` | Hard + soft page faults per second |

### 4.3 GPU — Unified Vendor Dispatch

The GPU subsystem dynamically loads vendor DLLs at runtime. The service compiles without any vendor SDK installed — all required types are defined in minimal local headers (`nvml_minimal.h`, `adl_minimal.h`, `igcl_minimal.h`).

**Dispatch priority:**
1. **NVIDIA NVML** (`nvml.dll`) — deepest metrics, used when present
2. **AMD ADL2** (`atiadlxx.dll`) — for Radeon GPUs
3. **Intel IGCL** (`ControlLib.dll`) — for Intel Arc/Xe GPUs
4. **PDH fallback** — `\GPU Engine(*)\Utilization Percentage` wildcard counter always collected for all adapters

The PDH wildcard counter enumerates all GPU engine instances by PID and engine type. The service aggregates:
- **3D engine** utilization → reported as the system-wide display GPU load (matches Task Manager)
- **Compute engine** utilization → included in per-process GPU tracking (catches CUDA/ML workloads)

**NVIDIA-specific metrics (via NVML):**
| Metric | Source |
|---|---|
| SM utilization % | `nvmlDeviceGetUtilizationRates` |
| Memory bandwidth utilization % | `nvmlDeviceGetUtilizationRates` |
| Throttle reasons | `nvmlDeviceGetCurrentClocksThrottleReasons` bitmask |
| Thermal efficiency | `usage_pct / temp_celsius` |
| CUDA compute capability | `nvmlDeviceGetCudaComputeCapability` |
| Tensor core generation | Derived from CC major.minor (Volta=1, Turing=2, Ampere=3, Ada=4) |
| Encoder / decoder % | `nvmlDeviceGetEncoderUtilization`, `nvmlDeviceGetDecoderUtilization` |
| Memory clock P-state transitions | Delta counter on mem clock value |
| Per-process SM util, mem util | `nvmlDeviceGetProcessUtilization` |
| Per-process VRAM | `nvmlDeviceGetComputeRunningProcesses` + `GetGraphicsRunningProcesses`, using `max()` to avoid double-counting compute + graphics contexts |

**VRAM double-count prevention:** A GPU process running CUDA compute and rendering simultaneously will appear in both the Compute and Graphics process lists. Naively summing VRAM from both lists inflates the true value. The service uses `max(compute_vram, graphics_vram)` per PID, which matches NVIDIA's own Task Manager display logic.

### 4.4 Disk I/O

**API used:** `DeviceIoControl(IOCTL_DISK_PERFORMANCE)` per physical drive

This IOCTL returns cumulative `BytesRead`, `BytesWritten`, `ReadCount`, `WriteCount`, and `QueryTime` (busy time in 100ns ticks). Delta calculations over the poll interval yield rates and busy percentage.

`GetDiskFreeSpaceExW` provides per-volume capacity, free space, and used percentage.

**Metrics exposed per physical disk:**
| Metric | Description |
|---|---|
| `read_bytes_s` | Read throughput in bytes/sec |
| `write_bytes_s` | Write throughput in bytes/sec |
| `read_iops` | Read operations per second |
| `write_iops` | Write operations per second |
| `busy_pct` | Fraction of time the disk was busy (0–100%) |

### 4.5 Network

**API used:** `GetIfTable2` (iphlpapi)

`MIB_IF_TABLE2` returns cumulative octet/packet counters per interface. Only Ethernet and 802.11 (Wi-Fi) interfaces are included; loopback and virtual adapters are filtered.

**Metrics exposed per NIC:**
| Metric | Description |
|---|---|
| `recv_bytes_s` | Inbound throughput |
| `sent_bytes_s` | Outbound throughput |
| `recv_pkts_s` | Inbound packets per second |
| `sent_pkts_s` | Outbound packets per second |
| `errors_in`, `errors_out` | Cumulative error counters |
| `drops_in`, `drops_out` | Cumulative drop counters |

### 4.6 Temperatures

**API used:** WMI `ROOT\WMI\MSAcpi_ThermalZoneTemperature`

ACPI thermal zones are the lowest-common-denominator temperature source requiring no drivers or elevated privileges beyond the service's own account. Values are in tenths of Kelvin; converted to Celsius as `(value - 2732) / 10.0`.

For deeper per-core CPU temperatures, fan speeds, and VRM readings, integration with LibreHardwareMonitor's managed DLL or a kernel driver is required. This is noted as a future iteration requiring explicit elevated service configuration.

### 4.7 Process Watcher

The process watcher is the tool's most distinctive capability: it attaches surgical per-process telemetry to any named executable or explicit PID.

**Discovery mechanism:**
- `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` enumerates all running processes each poll tick
- Process names are matched case-insensitively against the configured watch list
- Explicit PID list is also supported (useful for short-lived processes started by a wrapper)
- **PID reuse guard:** `GetProcessTimes` retrieves the process creation time (`FILETIME`) at discovery. On every subsequent poll, the creation time is re-verified. If a new process has the same PID (reuse), the stale entry is evicted and a new one is started — preventing silent cross-process telemetry contamination.

**Per-process metrics:**
| Metric | API | Notes |
|---|---|---|
| CPU % | `GetProcessTimes` delta | Normalized to 100% per core, not 100% total |
| Private memory (MB) | `GetProcessMemoryInfo` `PrivateUsage` | True private committed memory |
| Peak working set (MB) | `GetProcessMemoryInfo` `PeakWorkingSetSize` | High-water mark |
| Page faults/sec | `GetProcessMemoryInfo` `PageFaultCount` delta | Hard + soft faults |
| Disk read bytes/sec | `GetProcessIoCounters` delta | All I/O types |
| Disk write bytes/sec | `GetProcessIoCounters` delta | |
| GPU SM utilization % | `nvmlDeviceGetProcessUtilization` | NVIDIA only |
| GPU VRAM (MB) | NVML process info, `max()` across contexts | |
| GPU mem BW util % | `nvmlDeviceGetProcessUtilization` | NVIDIA only |
| CPU affinity mask | `GetProcessAffinityMask` | Which cores are allowed |
| Priority class | `GetPriorityClass` | Normal, High, Realtime, etc. |
| Uptime (sec) | Creation time delta | |
| Context switches/sec | Tracked via `PROCESS_MEMORY_COUNTERS` | |

Up to 8 processes can be watched simultaneously. Each is assigned a stable SHM slot (slots 0–7) for the duration it is alive. Labels are written to `shm->hdr.watch_label` so the GUI and API can display human-readable names.

### 4.8 Self-Monitoring

The service measures its own resource consumption and exposes it via the `SELF_*` metric range (IDs 384–391) and the `VC-10` validation check:

- `self_cpu_pct` — service process CPU across all cores (target: < 5%)
- `self_rss_mb` — resident working set
- `self_private_mb` — private committed memory
- `self_handles` — open handle count
- `self_poll_ms` — actual wall-clock duration of each poll cycle

This ensures the service's own overhead is never silently mixed into a workload being measured.

---

## 5. Metric ID Schema

All metrics are addressed by a stable 32-bit integer ID that indexes into `ShmBlock.metrics[512]`. The schema is append-only — existing IDs never change, so older consumers remain compatible across service versions.

```
ID Range    Subsystem         Count    Stride
────────────────────────────────────────────────────────
  0–15      CPU aggregate     16       —
 16–47      CPU per-core      32       1 per core
 64–95      Memory            32       —
 96–223     GPU               128      32 per GPU (4 GPUs max)
224–287     Disk              64       8 per device (8 devices max)
288–351     Network           64       8 per NIC (8 NICs max)
352–383     Temperatures      32       1 per sensor (32 sensors max)
384–399     Self-monitoring   16       —
400–511     Process watcher   112      14 per watched process (8 max)
```

Access helpers (defined in `shared/metric_ids.h`):

```cpp
gpu_metric(gpu_idx, GpuOff::USAGE_PCT)     // e.g. gpu_metric(0, 0) = 96
disk_metric(dev_idx, DiskOff::READ_BYTES_S) // e.g. disk_metric(0, 0) = 224
net_metric(nic_idx, NetOff::RECV_BYTES_S)   // e.g. net_metric(0, 0) = 288
watch_metric(slot, WatchOff::CPU_PCT)        // e.g. watch_metric(0, 0) = 400
cpu_core_metric(core_idx)                    // e.g. cpu_core_metric(0) = 16
```

Each `MetricRing` stores:
- `current` — most recent value (what the API returns for `/snapshot`)
- `min_session`, `max_session` — session min/max since service start
- `values[300]` — rolling 300-sample history (5 minutes at 1Hz) for `/history`

---

## 6. REST API Reference

### 6.1 Authentication

All endpoints except `/metrics` and `/api/v1/health` require a Bearer token:

```
Authorization: Bearer <your-key>
```

API keys are created and managed through the GUI's **API page** or via the REST key-management endpoints (below). Keys are stored hashed (BCrypt SHA-256) in `%PROGRAMDATA%\TelemetryApp\api_keys\store.json` — plaintext is shown exactly once at creation.

**Key expiry types:** Permanent, Session (purged on service restart), 7-day, 30-day, or Custom UTC timestamp.

An `API.md` file is auto-generated in the service's executable directory on every key create, delete, or rotate. It contains a complete endpoint reference table and the active key prefix list (never the full key).

**Future release security requirement:** API transport encryption is planned for a later release. The current local test build uses HTTP on `localhost` for simple desktop and script integration. The release-hardening path should add HTTPS/TLS support, optional Windows certificate binding, local certificate generation for loopback testing, certificate thumbprint pinning for desktop client calls, and migration guidance for scripts. API keys should continue to be stored hashed by the service; any client-side plaintext key cache should be protected through Windows user-profile storage or DPAPI before broad distribution.

**REST key management endpoints:**

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/keys` | List all keys (prefix, created, expiry, status) |
| `POST` | `/api/v1/keys` | Create key — returns plaintext once |
| `DELETE` | `/api/v1/keys/{id}` | Delete a key by UUID |
| `POST` | `/api/v1/keys/{id}/rotate` | Rotate key — issues new token, returns once |

### 6.2 Endpoints

#### `GET /api/v1/snapshot`

Returns the current value of every active metric as a structured JSON object.

The snapshot includes a root `power` object. Electrical readings are source-qualified:

- `measured` means a vendor/platform provider reported the value directly.
- `derived` means a value was computed from measured samples.
- `estimated` means a model-based approximation, not direct electrical measurement.
- `unavailable` means the provider is absent, unsupported, or not implemented.

Current power support exposes GPU watts where the GPU provider reports them, marks CPU package power unavailable until a CPU package-power provider is added, marks platform/wall power unavailable unless battery discharge telemetry or an external meter is available, and reserves per-process power for future estimated attribution.

**Response (abbreviated):**
```json
{
  "timestamp": 1751500000000,
  "cpu": {
    "usage_total_pct": 12.4,
    "freq_actual_mhz": 4250.0,
    "per_core_pct": [8.1, 14.3, 22.7, 6.0, ...]
  },
  "memory": {
    "total_gb": 32.0,
    "used_gb": 18.7,
    "available_gb": 13.3,
    "percent": 58.4,
    "standby_gb": 4.1,
    "page_fault_rate": 42.0
  },
  "gpu": [
    {
      "index": 0,
      "name": "NVIDIA GeForce RTX 4070 Laptop GPU",
      "usage_pct": 87.3,
      "vram_used_mb": 6144.0,
      "vram_total_mb": 8192.0,
      "vram_pct": 75.0,
      "temp_c": 71.0,
      "power_w": 115.4,
      "fan_pct": 62.0,
      "clock_core_mhz": 1980.0,
      "clock_mem_mhz": 8001.0,
      "pdh_util_pct": 88.0,
      "encoder_pct": 0.0,
      "decoder_pct": 0.0,
      "sm_util_pct": 87.3,
      "mem_bw_util_pct": 64.1,
      "mem_clk_transitions": 2,
      "has_tensor_cores": true,
      "cuda_cc_major": 8,
      "cuda_cc_minor": 9,
      "tensor_core_gen": 4
    }
  ],
  "disk": [...],
  "network": [...],
  "power": {
    "cpu": {
      "package_power_w": {
        "value": 0.0,
        "unit": "W",
        "source": "CPU package provider",
        "quality": "unavailable",
        "confidence": "none",
        "reason": "CPU package power provider is not implemented or not supported"
      }
    }
  },
  "self": { "cpu_pct": 0.3 }
}
```

#### `GET /api/v1/history/<metric_id>?n=300`

Returns the last `n` samples (default 300, max 300) for any metric by ID.

```bash
# Last 60 seconds of total CPU utilization
curl -H "X-API-Key: $KEY" http://localhost:8765/api/v1/history/0?n=60
# → [12.1, 13.4, 11.9, 14.2, ...]

# Last 300 samples of GPU VRAM for GPU 0 (ID = 96 + 1 = 97)
curl -H "X-API-Key: $KEY" http://localhost:8765/api/v1/history/97
```

This endpoint is particularly useful for training harnesses that want to reconstruct a full utilization timeline for a completed training run without needing to poll continuously.

#### `GET /api/v1/health`

No authentication required. Returns service liveness and last poll duration.

```json
{ "alive": true, "poll_duration_ms": 58 }
```

#### `GET /api/v1/capabilities`

Returns service/device capability metadata, including detected accelerator metadata where available.

```json
{
  "shared_memory": true,
  "service_alive": true,
  "active_gpu_count": 1,
  "accelerators": [
    {
      "index": 0,
      "name": "NVIDIA GeForce RTX 4070 Laptop GPU",
      "has_tensor_cores": true,
      "cuda_cc_major": 8,
      "cuda_cc_minor": 9,
      "tensor_core_gen": 4,
      "sm_util_metric_id": 108,
      "mem_bw_metric_id": 109
    }
  ]
}
```

#### `GET /api/v1/hardware`

Returns the additive hardware identity and capability registry. This endpoint is intended for scripts, dashboards, and future UI surfaces that need to decide which sensors are meaningful on the current host before selecting metrics.

The registry reports CPU identity from CPUID/Windows topology and GPU identity from DXGI plus current provider evidence. It separates make/model/topology from telemetry capability, and it labels capability quality/status instead of treating unsupported sensors as zero.

```json
{
  "schema": "telemetryapp.hardware_inventory.v1",
  "truth_model": "identity first, provider availability second, metric capability third; unsupported is never encoded as a valid zero",
  "cpus": [
    {
      "component_id": "cpu0",
      "class": "cpu",
      "vendor": "GenuineIntel",
      "name": "Intel(R) Core(TM) ...",
      "logical_processors": 32,
      "physical_cores": 16,
      "instruction_sets": {
        "avx2": true,
        "avx512f": false,
        "amx_tile": false
      },
      "capabilities": {
        "usage": { "quality": "measured", "status": "valid" },
        "temperature": { "quality": "unavailable", "status": "unsupported" },
        "package_power": { "quality": "unavailable", "status": "unsupported" }
      }
    }
  ],
  "gpus": [
    {
      "component_id": "gpu0",
      "class": "gpu",
      "vendor": "NVIDIA",
      "vendor_id": "0x10DE",
      "device_id": "0x....",
      "provider_preference": "NVML + DXGI",
      "capabilities": {
        "temperature": { "quality": "measured", "status": "valid" },
        "power_watts": { "quality": "measured", "status": "valid" },
        "nvidia_tensor_cores": { "quality": "inferred", "status": "valid" }
      }
    }
  ]
}
```

#### `GET /api/v1/metrics/catalog`

Returns stable metric IDs and common metric names. Accelerator-related GPU 0 entries include:

| Metric | Meaning |
|---|---|
| `gpu0_sm_util` | SM/compute utilization percent |
| `gpu0_mem_bw_util` | Memory bandwidth utilization percent |
| `gpu0_tensor_cores_present` | Boolean tensor-core presence |
| `gpu0_cuda_cc_major` | CUDA compute capability major |
| `gpu0_cuda_cc_minor` | CUDA compute capability minor |
| `gpu0_tensor_core_gen` | Inferred tensor-core generation |

#### `GET /api/v1/stream`

Server-Sent Events stream. Connects once; receives a snapshot JSON frame approximately every 1 second. Suitable for browser dashboards, real-time alerting scripts, and streaming logging pipelines.

```python
import sseclient, requests
resp = requests.get("http://localhost:8765/api/v1/stream",
                    headers={"X-API-Key": KEY}, stream=True)
for event in sseclient.SSEClient(resp):
    data = json.loads(event.data)
    print(data["cpu"]["usage_total_pct"])
```

### 6.3 Prometheus Scrape

`GET /metrics` returns all active metrics in Prometheus text format (no auth). Point Prometheus at:

```yaml
scrape_configs:
  - job_name: telemetry_app
    static_configs:
      - targets: ['localhost:8765']
```

Sample output:
```
# TYPE sys_cpu_usage_percent gauge
sys_cpu_usage_percent 12.4
# TYPE telemetry_cpu_package_power_watts gauge
telemetry_cpu_package_power_watts{source="CPU package provider",quality="unavailable"} 0
# TYPE telemetry_platform_power_watts gauge
telemetry_platform_power_watts{source="Software-only platform provider",quality="unavailable"} 0
# TYPE sys_gpu_usage_percent gauge
sys_gpu_usage_percent{gpu="0",name="NVIDIA GeForce RTX 4070 Laptop GPU"} 87.3
# TYPE sys_gpu_vram_percent gauge
sys_gpu_vram_percent{gpu="0",name="NVIDIA GeForce RTX 4070 Laptop GPU"} 75.0
# TYPE sys_gpu_power_watts gauge
sys_gpu_power_watts{gpu="0",name="NVIDIA GeForce RTX 4070 Laptop GPU",source="NVML",quality="measured"} 115.4
```

### 6.4 Server-Sent Events Stream

For web frontends or long-running Python scripts, SSE provides an always-on 1Hz push without polling overhead. The event stream format is plain JSON objects matching the `/snapshot` schema, delimited by `data:` lines per the SSE specification.

---

## 7. GUI Client

### 7.1 Direct2D Rendering Pipeline

The GUI client uses a D3D11 swap chain as the DXGI surface, with a D2D1 `ID2D1DeviceContext` targeting it. This setup allows the GPU to composite Direct2D output directly into the swap chain without a CPU-side blit.

**Three-timer architecture:**
- **RENDER_TIMER (16ms / ~60fps)** — `BeginDraw` → sidebar + active page → `EndDraw` → `Present(1, 0)` (VSync)
- **DATA_TIMER (1000ms / 1Hz)** — reads SHM via `ShmReadMetric()`, pushes values to dashboard rings and HUD overlay
- **API_TIMER (5000ms)** — refreshes the API key list from the service

All page rendering uses a retained `ID2D1HwndRenderTarget`. The `br_scratch` pattern (a single `ID2D1SolidColorBrush` with `SetColor()` called per draw) avoids brush allocation in the hot path.

**D2DERR_RECREATE_TARGET handling:** On device loss (sleep/wake, driver update), the render target signals `D2DERR_RECREATE_TARGET`. The client tears down and reinitialises the entire D2D context and all pages in one synchronous block — users see a brief blank frame, then normal rendering resumes.

**Gauge icon:** The app icon is a red circular barometer-style gauge embedded into the native executable resources for the launcher, client window, taskbar, alt-tab entry, and system tray. `CreateGaugeIcon()` remains as a transparent-background fallback if a runtime icon load fails.

### 7.2 Navigation and Pages

The client has a fixed 180px left sidebar and five navigation pages:

| Page | Keyboard | Description |
|---|---|---|
| Dashboard | `1` / click | Live metric panels, visualization controls, and layout editing |
| API | `2` / click | API key table, create/rotate/delete |
| Metrics | `3` / click | Metric selection, logging groups, HUD/bar selection, folder-prompt logging |
| Fleet | click | Trusted local/remote device inventory, discovery candidates, and future remote telemetry drill-in |
| Settings | `4` / click | Connection, HUD mode, service management |

The Fleet page shows the local trusted device and any discovered or enrolled remote devices. Remote discovery can find readiness candidates through explicit LAN probing or manual add. Clicking `Enroll` performs an explicit lab-enrollment request, records acknowledgement on the sensor, and persists the device in the Fleet Host inventory at the app data path as `fleet_devices.json`. This is useful for local lab fleet visibility, but it is not yet enterprise cryptographic trust: TLS/mTLS, one-time pairing tokens, token invalidation, and certificate/thumbprint pinning remain hardening requirements before remote live telemetry is treated as enterprise-grade.

### Fleet Logging Jobs

Fleet Management includes a guided **Fleet Logging Jobs** workflow. The wizard walks operators through:

- target selection: local trusted device, all trusted fleet devices, or selected devices
- cadence selection: manual start/stop, always-on while online, recurring calendar windows, or one-time scheduled runs
- calendar-style schedule controls: recurrence chips for daily/weekly/monthly and bi-daily/bi-weekly/bi-monthly, selectable day chips, all-day toggle, start/end hour controls, and one-time date presets
- storage selection: operator-chosen folder with per-device raw logs and fleet job metadata
- lifecycle shape: daily or monthly per-device folders plus a fleet index
- format and verbosity: balanced JSONL, storage-friendly CSV policy, full diagnostic JSONL, or minimal selected metrics
- final validation before creation
- live status in the Fleet page job list with current selected-folder size and available disk space

Current implementation truth model: **Fleet logging starts the local trusted device capture through the existing session logging API. If the job targets all/selected trusted fleet devices, the host also starts lab remote logging sessions on enrolled sensors through `/api/v1/lab/sessions`.** Explicit lab enrollment enables lab snapshot and logging control, but it is not enterprise cryptographic trust. Sensor-client installs remain unable to discover, poll, or manage other devices.

Recommended storage model:

```text
<chosen log root>\
  fleet_logging_jobs.json        # fleet job policy/status records
  devices\
    <hostname>_<device-id>\
      yyyy\
        mm\
          dd\
            <job-label>_<device-id>_<timestamp>.jsonl
```

Phase 1 persists job definitions in `%TELEMETRY_DATA_DIR%\fleet_logging_jobs.json` and uses the chosen job folder for local session output. Future remote execution should add fleet job API endpoints, secure dispatch, per-device acknowledgements, retry/queue policy, retention enforcement, and remote log transfer or indexing.

**Sidebar status strip:** shows a green/red dot for service connectivity and the active API key count. The sidebar reads `m_shm_open` and `m_key_cache.size()` from the window — no extra HTTP call.

### 7.3 Dashboard Profiles

The dashboard is defined by a `DashboardProfile` — a JSON file containing a list of `MetricPanel` objects. Each panel specifies:

```json
{
  "metric_id": 0,
  "viz_type": "LineGraph",
  "col": 0, "col_span": 6,
  "y_min": 0.0, "y_max": 100.0,
  "unit": "%",
  "color": "#4DCC66",
  "history_samples": 300,
  "cluster": "CPU",
  "visible": true
}
```

**Visualization types:**

| Type | Best for |
|---|---|
| `LineGraph` | Rates, utilization, time-series |
| `ArcGauge` | Percentage saturation (CPU%, RAM%, GPU%) |
| `BarGauge` | Absolute values with a max (VRAM MB used) |
| `Numeral` | Single scalar (freq in MHz, core count) |
| `NumeralTrend` | Scalar + 60-sample sparkline (standby GB, swap) |
| `HeatMap` | Per-core load (32-cell 16×2 grid, green→red) |
| `DualLine` | Two overlaid series on same scale (recv/sent) |
| `LedIndicator` | Boolean state (throttle active) |
| `Badge` | Labelled state (priority class, P-state) |

The default profile (`Default.json`) follows the DevOps engine layout recommendation: CPU cluster (green), Memory (orange), GPU (blue), Disk (purple), Network (cyan), Thermals (red-orange), Self-monitoring (grey).

**Dashboard editing:** The `Edit Dashboard` action opens a right-side editor for visible panels. Operators can show/hide panels, change visualization, and adjust panel dimensions while the live dashboard continues to read from shared memory. The editor header contains visible `Save`, `Cancel`, and `Default` controls, and the drawer scrollbar supports wheel, rail click, and thumb drag interaction. When a panel is hidden, the remaining visible dashboard panels pack left-to-right inside their cluster instead of leaving layout holes.

**Profile import/export:** `Ctrl+E` (export) and `Ctrl+I` (import) open file dialogs. Profiles are saved to `%TELEMETRY_DATA_DIR%\dashboards\`. The active profile name is persisted in `client.json`.

### 7.4 HUD Compact Overlay

When the main window is minimized, the app can display a slim always-on-top bar instead of collapsing to the taskbar. This is called **HUD Mode**.

**Default configuration:**
- Position: **above the Windows taskbar** (`SystemParametersInfo(SPI_GETWORKAREA)` → `y = work_area.bottom - 36`)
- Default metrics: CPU%, RAM%, CPU Temp, GPU%, VRAM%, GPU Temp, Disk R, Disk W, Net↓, Net↑
- Refresh rate: 1Hz (reads SHM directly)
- Thickness: 36px horizontal; same for vertical (Left/Right edge) layouts

**Behavior:**
- The HUD window uses `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED` — it stays available, never appears in alt-tab, and does not steal focus
- While visible, the HUD registers as a Windows shell appbar via `SHAppBarMessage(ABM_NEW / ABM_QUERYPOS / ABM_SETPOS)`. Windows 10/11 then reserves that edge of the work area, similar to the taskbar, so normal maximized windows should use the remaining desktop space instead of overlapping the telemetry bar.
- The appbar reservation is removed when the HUD is hidden or the main window is restored.
- 95% opacity (`SetLayeredWindowAttributes(... 242, LWA_ALPHA)`)
- Left-clicking the bar restores the main window and hides the HUD
- Hovering a metric tile shows a short explanation of the reading
- Each metric tile shows: short label (top), current value (main), thin fill bar (bottom)
- Color coding: neutral grey below warning threshold, amber at warn, red at critical

**Configure in Settings:**

| Setting | Options |
|---|---|
| When minimized | Normal minimize \| **HUD Mode** (default) \| Hide to tray |
| HUD position | **Above taskbar** (default) \| Top of screen \| Left edge \| Right edge |

Changes take effect immediately without restarting.

**System tray:** The tray icon is always present while the app is running. It provides a right-click menu with Restore, Show HUD Bar, Start/Stop Capturing Logs / Metrics, HUD Orientation, Fleet Device Metrics, and Exit. The `Hide to tray` minimize behavior only controls whether the main window hides to the already-present tray icon when minimized.

**HUD and tray context menus:** Right-clicking either the HUD bar or tray icon exposes the same operational controls for capture and orientation. Start/Stop Capturing Logs / Metrics prompts for a destination folder before capture begins. Fleet Device Metrics is gated until secure fleet enrollment and remote telemetry dispatch are active.

### 7.5 API Key Management Page

The API page shows all keys registered in the service's key store:

| Column | Description |
|---|---|
| Name | Human-readable label |
| Prefix | First 8 chars of the key (`tlm-xxxx...`) |
| Created | UTC creation timestamp |
| Expiry | Expiry type + date, or "Permanent" / "Session" |
| Status | Green "Active" badge or red "Expired" badge |
| Actions | Delete (🗑) / Rotate (↻) |

**Create dialog:** Name input + 5 expiry-type buttons + custom date display. On creation, the full key is shown once in a yellow banner — copy it immediately, it is never shown again.

**Rotate:** Issues a new BCryptGenRandom token, hashes and stores it, invalidates the old one. The new plaintext is shown in the same one-time banner.

### 7.6 Metrics Logging Page

Metric logging is organised into five functional groups matching DevOps best practice:

| Group | Default | Interval | Notes |
|---|---|---|---|
| HighFreqNumerics | Enabled | 1s | CPU%, RAM%, GPU%, temps |
| SlowState | Enabled | 10s | Installed RAM, driver versions |
| EventDriven | Enabled | on-change | Throttle transitions, process start/stop |
| ProcessWatcher | Disabled | 1s | Per-process metrics; opt-in |
| SelfMonitoring | Enabled (always) | 1s | Cannot be disabled |

Within each group, individual metrics can be toggled via checkboxes. The "ALL" toggle enables or disables every metric in the group. `Display All` selects every dashboard metric, while `BAR All` selects every eligible minimized HUD/bar metric.

When logging is turned on from the Metrics page, the client prompts for an output folder and starts a backend logging session with the selected metric IDs. Session logs are written by the service to the selected folder in JSONL or CSV format. The selected log folder is persisted in the client configuration.

The final planned simplification is a single Metric Catalog/Profile model shared by Dashboard, HUD/bar mode, logging sessions, and API-oriented capture profiles. The project-level plan lives outside this app folder at `docs\WINAPP_FINAL_UX_COMPLETION_PLAN.md`.

### 7.7 Accelerator Detection

The service has vendor-specific GPU detection paths plus Windows PDH fallback:

- NVIDIA: NVML for utilization, VRAM, clocks, power, thermals, encoder/decoder, per-process GPU data, CUDA compute capability, and tensor-core generation inference.
- AMD: ADL path where available.
- Intel: IGCL path where available.
- Windows fallback: PDH GPU Engine counters for broad GPU utilization when vendor SDKs are unavailable.

NVIDIA CUDA/tensor capability metadata is exposed through the GPU snapshot, capabilities API, and `/api/v1/hardware` when NVML reports an active adapter. Non-NVIDIA accelerator metadata is represented in `/api/v1/hardware` as provider-specific capability candidates: AMD matrix/CDNA/RDNA/XDNA support and Intel XMX/DPAS support must not be labeled as NVIDIA tensor cores, and remain `not_implemented` until AMD SMI/ADLX and Level Zero Sysman/static architecture mapping are added.

---

## 8. Windows Service Lifecycle

Install and manage the service with the built-in CLI options:

```cmd
# Install as auto-start Windows Service (requires elevation)
telemetry_service.exe --install

# Install or convert to automatic startup explicitly
telemetry_service.exe --install-auto

# Install or convert to manual/demand startup
telemetry_service.exe --install-manual

# Remove the service
telemetry_service.exe --uninstall

# Run in foreground console (for development/debugging, no SCM needed)
telemetry_service.exe --console

# Normal SCM start after install
sc start TelemetryService

# Stop
sc stop TelemetryService
```

`--install` is equivalent to `--install-auto`. Manual/demand-start installs are intended for sensor-client devices where an operator or script starts monitoring by running `TelemetryApp.exe` rather than boot-starting the service.

**Automatic recovery:** The service registers `SC_ACTION_RESTART` with a 3-tier escalating delay (5s → 10s → 30s). If the service crashes, the SCM restarts it automatically. Recovery actions reset after 24 hours of uptime.

**Service account:** The service runs under `LocalSystem` by default (set by `--install`). For security-hardened deployments, create a dedicated service account with only these rights: `SeServiceLogonRight`, read access to `HKLM\HARDWARE`, and the ability to open process handles (`PROCESS_QUERY_LIMITED_INFORMATION`) for watched processes.

---

## 9. Validation Checks

Every poll cycle runs 10 internal consistency checks before writing to SHM. Failed checks are logged to the Windows Event Log (source: `TelemetryService`) as warnings. They do not halt the service.

| Check | Assertion | Purpose |
|---|---|---|
| VC-01 | `cpu_total_pct ∈ [0, 100]` | Detects NtQSI overflow or negative delta |
| VC-02 | `mem_pct ∈ (0, 100]` | Detects GlobalMemoryStatusEx failure |
| VC-03 | `gpu_pct[i] ∈ [0, 100]` for all GPUs | Detects vendor driver bad reads |
| VC-04 | `disk_busy[i] ∈ [0, 100]` for all disks | Detects IOCTL counter overflow |
| VC-05 | Network rates ≥ 0 | Detects counter wrap on 64-bit rollover |
| VC-06 | SHM seqlock is even at publish time | Detects re-entrant write (should be impossible) |
| VC-07 | `Σ per_core_pct ≈ cpu_total * core_count ± 15%` | Detects core count mismatch |
| VC-08 | `poll_duration_ms < 200` | Detects service falling behind (sensor latency spike) |
| VC-09 | `temp[i] ∈ [-10, 150]°C` | Detects WMI thermal zone garbage values |
| VC-10 | `self_cpu_pct < 5%` | Service overhead guard |

---

## 10. Build Instructions

### Prerequisites

- **Visual Studio 18.7.3 scoped VC15 SDK fallback** on this device, or a standard Visual Studio native desktop C++ workload
- **CMake 3.24+**
- Local header dependencies in `..\.tools\deps`, or vcpkg for a standard toolchain build
- Windows SDK 10.0.22000+ (for `GetIfTable2`, `PROCESS_MEMORY_COUNTERS_EX`, Direct2D 1.1)
- **NSIS 3.x** (for the Windows installer only — not needed for portable)

### Steps (manual)

Current workspace notes:

- Portable CMake 4.3.4 is available at `..\.tools\cmake-4.3.4-windows-x86_64\bin\cmake.exe`.
- Local fallback headers are available at `..\.tools\deps`.
- `build_release` is the retained build folder for this app.
- `build_scoped_sdk.ps1` builds with the scoped SDK compiler at `C:\Program Files\Microsoft Visual Studio\18\Community\SDK\ScopeCppSDK\vc15`.

```powershell
.\build_scoped_sdk.ps1

# Outputs
#   build_release\service\Release\telemetry_service.exe
#   build_release\client\Release\telemetry_client.exe
#   bin\telemetry_service.exe
#   bin\telemetry_client.exe
```

### One-shot update script

After any code change, run:

```cmd
installer\update.bat
```

This:
1. Builds the CMake Release configuration
2. Assembles `dist\TelemetryApp_Portable\` (run-from-folder, no install)
3. Runs NSIS to produce `dist\TelemetryApp_Setup_1.0.0.exe`
4. Stamps `README.md` with the current version and build date

### Portable package (`installer\build_portable.bat`)

Produces `dist\TelemetryApp_Portable\` containing:
- `TelemetryApp.exe`, `telemetry_service.exe`, `telemetry_client.exe`
- `data\`, `logs\`, `dashboards\` directories (pre-created)
- `run_service_console.bat` — launches service in console mode for testing
- `launch_client.bat` — starts the GUI client
- `README.md`, `LICENSE`, `API.md`, and `PROJECT_PROXIMITY_MATRIX.md` in the package root

Packaging fails if `bin\API.md` is missing, so portable and installer builds do not ship without the API guide.

### Windows Installer (`installer\build_installer.bat`)

Calls `build_portable.bat`, then invokes `makensis.exe installer.nsi`. The installer:
- Copies files to `%ProgramFiles%\TelemetryApp\`
- Plants `TelemetryApp.exe`, `telemetry_client.exe`, `telemetry_service.exe`, `README.md`, `LICENSE`, `API.md`, and `PROJECT_PROXIMITY_MATRIX.md` directly in the install root
- Writes registry keys under `HKLM\SOFTWARE\TelemetryApp`
- Writes `ReadmePath`, `ApiGuidePath`, and `ProjectMatrixPath` under `HKLM\SOFTWARE\TelemetryApp`
- Adds `%ProgramFiles%\TelemetryApp` to the system `PATH`
- Sets environment variables: `TELEMETRY_APP_DIR`, `TELEMETRY_API_URL`, `TELEMETRY_DATA_DIR`, `TELEMETRY_INSTALL_MODE`, `TELEMETRY_INSTALL_ROLE`, `TELEMETRY_START_MODE`, `TELEMETRY_HOST_URL`, and non-secret fleet policy flags
- Shows About and License pages before deployment choices
- Presents separate deployment pages for Local Monitor, Fleet Host, or Sensor Client role and Auto-start vs Manual-start service behavior
- Presents an explicit LAN readiness/API binding checkbox for trusted lab fleet testing. This is required if another TelemetryApp host should probe the sensor over the network before full TLS enrollment is implemented.
- Installs `TelemetryService` as either an auto-start or demand-start Windows service
- Detects an existing install and presents maintenance choices: update/repair, modify role/startup, or uninstall
- Temporarily stops `TelemetryService`, `telemetry_service.exe`, `telemetry_client.exe`, and `TelemetryApp.exe` during install/update/repair/uninstall so locked executables can be replaced cleanly
- The installer role page and running app header identify the active package as `TelemetryApp Local Monitor`, `TelemetryApp Fleet Manager`, or `TelemetryApp Sensor`, including the app version.
- New UI and installer changes are governed by the project UI layout harness in `docs/WINAPP_UI_LAYOUT_HARNESS.md`; clipped, compressed, overlapping, or footer-obstructed text is a release blocker.
- Windows Defender Firewall policy, Microsoft-behavior assumptions, and validation commands are recorded in `docs/WINAPP_FIREWALL_POLICY_AUDIT.md` and bundled into the install root as `WINAPP_FIREWALL_POLICY_AUDIT.md`.
- The installer requests administrator elevation and shows a dedicated Windows Defender Firewall permissions page. When enabled, setup applies grouped `TelemetryApp` firewall allow rules for TelemetryApp client/service outbound traffic on private/domain profiles. When LAN readiness/API binding is also enabled, setup applies an inbound private/domain TCP `8765` rule for `telemetry_service.exe` scoped to `LocalSubnet`; public networks remain closed. Repair/reinstall/modify can enable or disable rule management, and uninstall removes TelemetryApp rules.
- Before replacing files, setup disables `TelemetryService` startup during the replacement window, stops the service, waits for a true stopped state, force-stops `telemetry_service.exe`, `telemetry_client.exe`, and `TelemetryApp.exe`, then verifies the existing executable files are writable. If any process or file handle remains locked, setup aborts before extraction and records `InstallAudit\LastActionResult=BlockedBeforeFileReplace` instead of installing mismatched binaries.
- Creates Start Menu shortcuts and an optional desktop shortcut
- Preserves `%PROGRAMDATA%\TelemetryApp\` on uninstall (key store, dashboards, logs)

Enterprise deployment inputs:

```powershell
TelemetryApp_Setup_1.0.0.exe /MODE=LocalMonitor /START=Auto
TelemetryApp_Setup_1.0.0.exe /MODE=FleetHost /START=Auto
TelemetryApp_Setup_1.0.0.exe /MODE=SensorClient /START=Auto /HOST=https://telemetry-host:8767
TelemetryApp_Setup_1.0.0.exe /MODE=SensorClient /START=Manual /HOST=https://telemetry-host:8767
TelemetryApp_Setup_1.0.0.exe /MODE=SensorClient /START=Auto /REMOTE_API=1 /FIREWALL=1
TelemetryApp_Setup_1.0.0.exe /ACTION=Repair
TelemetryApp_Setup_1.0.0.exe /ACTION=Uninstall
```

`/MODE=` is written to `HKLM\SOFTWARE\TelemetryApp\InstallMode`, `HKLM\SOFTWARE\TelemetryApp\InstallRole`, `TELEMETRY_INSTALL_MODE`, `TELEMETRY_INSTALL_ROLE`, and the service config. `/ROLE=` is also accepted as an alias. `/START=` is written to `HKLM\SOFTWARE\TelemetryApp\ServiceStartMode`, `TELEMETRY_START_MODE`, and the service config. `/HOST=` is written to `HKLM\SOFTWARE\TelemetryApp\HostUrl`, `TELEMETRY_HOST_URL`, and the service config. `/REMOTE_API=1` writes `RemoteApiEnabled=1`, `TELEMETRY_REMOTE_API_ENABLED=1`, and `TELEMETRY_REMOTE_API=1`, causing the service to bind to `0.0.0.0` instead of `127.0.0.1` on the next service start. `/FIREWALL=1` allows setup to manage TelemetryApp Windows Defender Firewall rules; `/FIREWALL=0` removes existing TelemetryApp rules during repair/reinstall/modify and does not recreate them. These values are non-secret. Enrollment tokens and API secrets must not be persisted in registry, environment variables, or diagnostic logs.

Install roles:

- `LocalMonitor` is the default. It installs the desktop app, local service, local dashboard, local API, and logging tools for this computer only. Fleet discovery, remote polling, and remote API binding are disabled.
- `FleetHost` marks the machine as eligible to manage enrolled sensors. It writes `CanManageFleet=1`; discovery remains host-only, and remote collector dispatch remains gated until credentialed telemetry dispatch and TLS hardening are complete.
- `SensorClient` installs the same telemetry service for this device's local measurements and future host enrollment. It writes `CanManageFleet=0`, `DiscoveryEnabled=0`, `RemoteCollectorEnabled=0`, and `RemoteApiEnabled=0`, so a sensor endpoint cannot discover, poll, or manage other devices.
- Only `FleetHost` installs show the Fleet Management navigation item. `LocalMonitor` and `SensorClient` installs keep the app focused on local telemetry and cannot enter the fleet workflow from the sidebar, tray, or HUD menu.
- Legacy `/MODE=FullHost` is accepted for backward compatibility and maps to `LocalMonitor`.

Remote sensor visibility for local lab testing:

- On the sensor device, install or modify with `Allow LAN readiness/API binding for lab fleet testing` checked, and keep `Enable TelemetryApp Windows Defender Firewall rules` checked unless enterprise policy manages firewall rules centrally. Command-line equivalent: `/REMOTE_API=1 /FIREWALL=1`.
- Restart `TelemetryService` after changing this setting.
- Setup applies the Windows Defender Firewall inbound TCP `8765` rule when LAN readiness/API binding and firewall rule management are both enabled. The installer-created inbound rule is program-scoped to `telemetry_service.exe`, profile-scoped to private/domain, and remote-address-scoped to `LocalSubnet`. If the firewall checkbox is off, the sensor is on another routed subnet/VLAN, or enterprise policy blocks local firewall changes, an administrator must allow an equivalent private/domain TCP `8765` rule for `telemetry_service.exe` with the approved remote address scope.
- On the host device, open `Fleet -> Search LAN`, or open `Fleet -> Manual Add` and enter the sensor sidebar's green `LAN: <ip>:8765` value.
- A responding sensor appears as an **untrusted candidate**. This confirms network reachability and sensor identity hash only. Click `Enroll` to perform explicit lab enrollment, persist the host inventory record, and transition the device to `Trusted`. After both host and sensor are updated to the lab-endpoint build, `View` reads `/api/v1/lab/snapshot` and fleet logging starts/stops remote sensor sessions through `/api/v1/lab/sessions`.
- Duplicate detection uses `mac_hash` first, then `sensor_id_hash`, then address. This avoids duplicate rows when the same laptop is found by Search LAN, Manual Add, or future call-home/passive discovery.

### Firewall and Network Discovery Troubleshooting

TelemetryApp setup handles the normal Windows Defender Firewall case, but local or enterprise policy can still prevent a sensor from being found. Microsoft documents these relevant behaviors: inbound traffic is blocked unless an allow rule matches, explicit block rules can override allow rules, firewall profiles matter, and GPO/MDM policy can control whether local rules are merged.

Installer-created rules:

- Rule group: `TelemetryApp`
- `TelemetryApp Client Outbound` - allows `telemetry_client.exe` outbound traffic on private/domain profiles.
- `TelemetryApp Service Outbound` - allows `telemetry_service.exe` outbound traffic on private/domain profiles.
- `TelemetryApp Service API Inbound` - allows inbound TCP `8765` to `telemetry_service.exe` on private/domain profiles only, from `LocalSubnet` only, and only when LAN readiness/API binding is enabled.

These rules are created only when the installer firewall permissions checkbox is enabled. If that checkbox is disabled during repair/reinstall/modify, setup removes existing TelemetryApp firewall rules and leaves firewall policy to the administrator or enterprise management system.

Known blockers to check:

- The active Windows network profile is `Public`. TelemetryApp intentionally does not open public-profile inbound access.
- The host and sensor are separated by routed VLANs/subnets. The installer-created inbound rule uses `LocalSubnet`; cross-subnet access requires an administrator-managed remote address range.
- A local, GPO, MDM, EDR, VPN, or third-party firewall block rule overrides the TelemetryApp allow rule.
- Group Policy disables or ignores local firewall rules, so installer-created rules do not become effective.
- Outbound traffic is blocked by a hardened workstation policy.
- The sensor service is still bound to `127.0.0.1` because LAN readiness/API binding was not enabled or the service was not restarted.
- Host and sensor are on different VLANs/subnets; Search LAN only probes the local `/24`. Use Manual Add with the exact IP/host.
- Router, switch, Wi-Fi client isolation, guest-network isolation, or subnet ACLs block host-to-sensor traffic.
- Port `8765` is already occupied by another process or the service failed to bind.
- DNS resolves the hostname to an unreachable interface; use the green `LAN: <ip>:8765` value shown by the sensor.

Useful administrator checks:

```powershell
Get-NetConnectionProfile
Get-NetFirewallProfile | Select Name,Enabled,DefaultInboundAction,DefaultOutboundAction,AllowLocalFirewallRules
Get-NetFirewallRule -DisplayName "TelemetryApp*" | Format-Table DisplayName,Enabled,Direction,Action,Profile,Group
Get-NetFirewallRule -DisplayName "TelemetryApp Service API Inbound" | Get-NetFirewallAddressFilter
Get-NetFirewallRule -Action Block -Enabled True | Where-Object DisplayName -match "Telemetry|8765|service|client"
Test-NetConnection <sensor-ip> -Port 8765
curl http://<sensor-ip>:8765/api/v1/enrollment/readiness
```

If `Test-NetConnection` fails, fix network/firewall reachability before using Fleet search. If `Test-NetConnection` succeeds but readiness fails, check the service state, API binding mode, and `telemetry_service` logs.

Role promotion and modification:

- To promote a `SensorClient` to a local desktop monitor, rerun the installer, choose `Modify role / startup`, then select `LocalMonitor`.
- To promote a device to the future fleet-manager role, rerun the installer, choose `Modify role / startup`, then select `FleetHost`.
- To demote a device back to a non-manager endpoint, rerun the installer, choose `Modify role / startup`, then select `SensorClient`.
- Modification preserves `%PROGRAMDATA%\TelemetryApp\` data, API keys, dashboards, enrollment material, and logs. It refreshes the executable files, registry values, environment variables, service registration, and startup mode.

Startup behavior:

- `START=Auto` installs or converts `TelemetryService` to automatic startup and starts it during setup.
- `START=Manual` installs or converts `TelemetryService` to demand-start and does not start it during setup. Run `TelemetryApp.exe` to start local monitoring on demand.

Current enterprise sensor contract:

- `GET /api/v1/enrollment/readiness` is public and returns only non-secret candidate metadata, including install mode and a sensor ID hash.
- `GET /api/v1/install/audit` requires API auth and returns the full local sensor/install audit record.
- `POST /api/v1/enrollment/request` accepts explicit lab enrollment when the request sets `accept_lab_enrollment:true` and the submitted public sensor fingerprint matches the sensor. This records `enrolled_lab` on the sensor. A future enterprise release must add one-time token validation, TLS/mTLS, certificate/thumbprint pinning, and token invalidation before remote telemetry is accepted as cryptographic trust.
- `GET /api/v1/lab/snapshot`, `POST /api/v1/lab/sessions`, and `POST /api/v1/lab/sessions/{id}/stop` are lab-only endpoints available after explicit lab enrollment. They are intended for trusted local lab networks, not open enterprise networks.

### No vendor SDKs required

The build does **not** require:
- CUDA Toolkit (nvml.dll loaded at runtime via `nvml_minimal.h`)
- AMD ADL SDK (atiadlxx.dll loaded at runtime via `adl_minimal.h`)
- Intel IGCL SDK (ControlLib.dll loaded at runtime via `igcl_minimal.h`)

If the corresponding GPU is not present or the DLL is not on the system, that vendor path is silently skipped and PDH coverage fills the gap.

---

## 11. Configuration

### Environment Variables

| Variable | Default | Description |
|---|---|---|
| `TELEMETRY_APP_DIR` | exe directory | Installation root |
| `TELEMETRY_DATA_DIR` | `%PROGRAMDATA%\TelemetryApp` | Key store, dashboards, logs |
| `TELEMETRY_API_URL` | `http://localhost:8765` | Service base URL (used by client) |
| `TELEMETRY_INSTALL_MODE` | `LocalMonitor` | Enterprise install role: `LocalMonitor`, `FleetHost`, or `SensorClient` |
| `TELEMETRY_INSTALL_ROLE` | `LocalMonitor` | Alias for the same install role |
| `TELEMETRY_START_MODE` | `Auto` | Service startup behavior: `Auto` or `Manual` |
| `TELEMETRY_HOST_URL` | empty | Optional host enrollment URL for sensor-client deployments |
| `TELEMETRY_CAN_MANAGE_FLEET` | `0` | `1` only for the explicit `FleetHost` role |
| `TELEMETRY_DISCOVERY_ENABLED` | `0` | Discovery gate; remains off by default |
| `TELEMETRY_REMOTE_COLLECTOR_ENABLED` | `0` | Remote collector gate; remains off until trusted enrollment/TLS is implemented |
| `TELEMETRY_REMOTE_API_ENABLED` | `0` | Installer-written remote API policy gate |
| `TELEMETRY_REMOTE_API` | unset/false | When `1`, `true`, `yes`, or `on`, binds the HTTP API to `0.0.0.0`; otherwise binds to `127.0.0.1` |
| `TELEMETRY_FIREWALL_RULES_ENABLED` | `1` | Installer-written firewall rule management preference; `0` means setup removes TelemetryApp rules and does not recreate them |

### Client Config (`%TELEMETRY_DATA_DIR%\client.json`)

```json
{
  "api_url": "http://localhost:8765",
  "api_key": "tlm-...",
  "poll_interval_ms": 1000,
  "minimize_behavior": 1,
  "hud_position": 0,
  "hud_metric_ids": [],
  "active_dashboard_profile": "Default",
  "logging_enabled": false,
  "log_format": "jsonl"
}
```

`minimize_behavior`: `0` = Normal, `1` = HUD Mode (default), `2` = System Tray  
`hud_position`: `0` = Above Taskbar (default), `1` = Top, `2` = Left, `3` = Right  
`hud_metric_ids`: empty array = use the default Core+Thermal+I/O set; otherwise explicit metric IDs in display order

### Watch List (API)

The process watch list is configured at runtime through authenticated API calls. Use `POST /api/v1/watch` with either `exe_name` or `pid`, `GET /api/v1/watch` to inspect configured and live watched processes, and `DELETE /api/v1/watch/<exe_name-or-pid>` to remove a watch target. Watched process metrics appear in snapshots and active logging sessions without rebuilding or restarting the service.

### Poll Interval

Default: 1000ms (1Hz). Change `POLL_INTERVAL_MS` in `service/poll_loop.cpp`. Sub-500ms intervals are possible but increase the risk of PDH counter instability and self-overhead.

---

## 12. Overhead and Self-Impact

The service is designed to be a silent observer. Measured typical overhead on an Intel Ultra 7 155H + RTX 4070 Laptop system:

| Resource | Idle (no watched process) | Under load (8 watched) |
|---|---|---|
| Service CPU | < 0.5% | < 2% |
| Service RAM (private) | ~8 MB | ~12 MB |
| SHM footprint | 1.19 MB (fixed) | 1.19 MB (fixed) |
| Disk I/O | 0 | 0 |
| GPU impact | 0 | 0 |
| PDH sleep per poll | 50ms (unavoidable) | 50ms |

The 50ms PDH sleep is a hard requirement of the PDH rate counter architecture (it requires two samples separated by a measurable interval). It runs on the poll thread and does not block the HTTP server, pipe server, or GUI renderer.

**Self-measurements are excluded from workload metrics** — the `SELF_*` metrics (IDs 384–399) report the service's own consumption separately, and the process watcher explicitly does not track the service's own PID.

---

## 13. Third-Party Integration — Session-Based Logging

This section documents every aspect of how an external tool can drive TelemetryApp to log hardware telemetry for exactly as long as its process runs, control exactly which metrics are captured, write logs to a custom destination, and optionally receive per-process metrics for the watched workload.

---

### 13.1 Logging Session API

A **session** is a named, time-bounded logging window. TelemetryApp writes one JSONL row per second (one per poll tick) for the duration of the session, then closes and flushes the file when stopped. Sessions survive service restarts (existing active sessions are re-opened on start).

**Session lifecycle:**

```
POST /api/v1/sessions           → start logging (returns session_id, log_dir, log_path)
  [your workload runs here]
POST /api/v1/sessions/{id}/stop → stop logging (flushes + closes file, returns final log_path)
  [read the log file at log_path]
```

All session and watch endpoints require `Authorization: Bearer <key>`.

---

#### Start a session — full request schema

```http
POST /api/v1/sessions
Authorization: Bearer tlm-abc...
Content-Type: application/json

{
  "label":      "training-run-001",
  "metric_ids": [],
  "log_dir":    "C:\\MyProject\\telemetry_logs"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `label` | string | no | Human-readable name stored in every row. Defaults to the session ID. |
| `metric_ids` | int[] | no | IDs of metrics to log. **Omit or send `[]` to log ALL standard metrics.** See Section 13.6 for the full ID table. |
| `log_dir` | string | no | Absolute Windows path to write log files to. **Omit or send `""` to use the default location** (`%TELEMETRY_DATA_DIR%\logs\sessions\`). Must be an absolute path (`C:\...` or `\\server\share`). The directory is created automatically if it does not exist. |

**Response 201:**
```json
{
  "session_id":    "sess-3a7fb21c",
  "label":         "training-run-001",
  "started_at_ms": 1751500000000,
  "log_dir":       "C:\\MyProject\\telemetry_logs",
  "log_path":      "C:\\MyProject\\telemetry_logs\\sess-3a7fb21c.jsonl",
  "metric_ids":    []
}
```

`log_path` is the **exact absolute path** to the file being written. It is ready to read (or tail) immediately after this response.

**Error responses:**

| HTTP | `error` code | Cause |
|---|---|---|
| 400 | `ERR_INVALID_BODY` | Request body is not valid JSON |
| 400 | `ERR_INVALID_LOG_DIR` | `log_dir` is not an absolute path |
| 500 | `ERR_SESSION_START_FAILED` | Cannot open the log file (check path write access) |

---

#### Stop a session

```http
POST /api/v1/sessions/sess-3a7fb21c/stop
Authorization: Bearer tlm-abc...
```

Response 200:
```json
{
  "session_id": "sess-3a7fb21c",
  "status":     "stopped",
  "log_path":   "C:\\MyProject\\telemetry_logs\\sess-3a7fb21c.jsonl"
}
```

The file is fully flushed and closed. No further rows will be written.

---

#### Other session endpoints

```http
GET    /api/v1/sessions              → list all sessions (active + stopped), each with log_dir + log_path
GET    /api/v1/sessions/{id}         → full session detail: status, rows_written, log_dir, log_path, metric_ids
DELETE /api/v1/sessions/{id}         → delete session record and its .jsonl + .meta.json files (204 on success)
```

---

### 13.2 Log Destination — Default vs. Custom

**Default (no `log_dir` in request):**
```
%TELEMETRY_DATA_DIR%\logs\sessions\
  sess-3a7fb21c.jsonl        ← session log
  sess-3a7fb21c.meta.json    ← session metadata
  index.jsonl                ← append-only completed session registry
```

`TELEMETRY_DATA_DIR` is set as a system environment variable by the installer (defaults to `%PROGRAMDATA%\TelemetryApp`). The service also exposes it in every session response as `log_dir`.

**Custom destination (provide `log_dir`):**
```json
{ "log_dir": "D:\\experiments\\run042" }
```
- The log file is written as `D:\experiments\run042\sess-{id}.jsonl`
- The meta file is written as `D:\experiments\run042\sess-{id}.meta.json`
- The directory tree is created automatically; no pre-existing path required
- `index.jsonl` is **not** written to the custom dir — it remains in the default location as the service-wide session registry
- The `log_path` in all subsequent API responses reflects the custom location

**Requirements for custom paths:**
- Must be an absolute Windows path: `C:\...` or `\\server\share\...`
- The service account (or the running user in `--console` mode) must have write access
- Relative paths are rejected with `ERR_INVALID_LOG_DIR`

---

### 13.3 Log File Format — Full Reference

Every log file is **JSONL** (JSON Lines) — one JSON object per line, one line per poll tick (~1 second).

#### Unfiltered row (`metric_ids: []` — all standard metrics)

All fields are always present. `watched_processes` is present only when at least one process is being watched via `POST /api/v1/watch`.

```json
{
  "ts_ms":           1751500000123,
  "session_id":      "sess-3a7fb21c",
  "label":           "training-run-001",
  "seq":             1,
  "cpu_pct":         12.4,
  "cpu_freq_mhz":    4250.0,
  "cpu_pkg_temp_c":  71.0,
  "cpu_core_balance": 0.85,
  "mem_pct":         58.4,
  "mem_used_gb":     18.7,
  "mem_avail_gb":    13.3,
  "mem_standby_gb":  4.1,
  "mem_page_faults_s": 42.0,
  "mem_swap_pct":    0.0,
  "gpu": [
    {
      "idx":       0,
      "name":      "NVIDIA GeForce RTX 4070 Laptop GPU",
      "usage_pct": 87.3,
      "vram_pct":  75.0,
      "vram_mb":   6144.0,
      "temp_c":    71.0,
      "power_w":   115.4,
      "clock_mhz": 1980.0
    }
  ],
  "disk": [
    { "idx": 0, "name": "NVMe0", "read_bytes_s": 125829120, "write_bytes_s": 83886080, "busy_pct": 45.0 }
  ],
  "net": [
    { "idx": 0, "name": "Ethernet", "recv_bytes_s": 1258291, "sent_bytes_s": 419430 }
  ],
  "self_cpu_pct": 0.3,
  "watched_processes": [
    {
      "slot":          0,
      "label":         "training-script",
      "cpu_pct":       82.4,
      "mem_private_mb": 4096.0,
      "threads":       16.0,
      "gpu_sm_pct":    87.3,
      "gpu_vram_mb":   6144.0,
      "uptime_sec":    142.0,
      "page_faults_s": 0.0
    }
  ]
}
```

**Field definitions:**

| Field | Unit | Description |
|---|---|---|
| `ts_ms` | Unix ms | Wall-clock timestamp of this poll tick |
| `session_id` | string | Session identifier |
| `label` | string | Label provided when session was started |
| `seq` | int | Monotonic row counter within this session, starting at 1 |
| `cpu_pct` | % | System-wide CPU utilization (all cores combined) |
| `cpu_freq_mhz` | MHz | Current average CPU frequency |
| `cpu_pkg_temp_c` | °C | CPU package temperature |
| `cpu_core_balance` | 0–1 | Load balance score across cores (1.0 = perfectly even) |
| `mem_pct` | % | Physical memory utilization |
| `mem_used_gb` | GB | Physical memory in use |
| `mem_avail_gb` | GB | Physical memory available |
| `mem_standby_gb` | GB | Standby (cached) memory |
| `mem_page_faults_s` | /sec | System-wide hard page fault rate |
| `mem_swap_pct` | % | Page file utilization |
| `gpu[n].usage_pct` | % | GPU compute engine utilization |
| `gpu[n].vram_pct` | % | VRAM utilization |
| `gpu[n].vram_mb` | MB | VRAM in use |
| `gpu[n].temp_c` | °C | GPU die temperature |
| `gpu[n].power_w` | W | GPU power draw |
| `gpu[n].clock_mhz` | MHz | GPU core clock |
| `disk[n].read_bytes_s` | bytes/s | Disk read throughput |
| `disk[n].write_bytes_s` | bytes/s | Disk write throughput |
| `disk[n].busy_pct` | % | Disk busy time |
| `net[n].recv_bytes_s` | bytes/s | NIC receive throughput |
| `net[n].sent_bytes_s` | bytes/s | NIC transmit throughput |
| `self_cpu_pct` | % | TelemetryApp service own CPU usage |
| `watched_processes[n].label` | string | Label given when process was added via `POST /api/v1/watch` |
| `watched_processes[n].cpu_pct` | % | Process CPU utilization |
| `watched_processes[n].mem_private_mb` | MB | Process private working set |
| `watched_processes[n].gpu_sm_pct` | % | GPU SM utilization attributed to this process (NVML) |
| `watched_processes[n].gpu_vram_mb` | MB | GPU VRAM allocated by this process |
| `watched_processes[n].uptime_sec` | sec | Seconds since process was first detected |

#### Filtered row (`metric_ids: [0, 96, 99, ...]`)

Only the requested metric IDs are written. Keys are the numeric IDs as strings. No named fields, no arrays.

```json
{
  "ts_ms":      1751500000123,
  "session_id": "sess-ab12ef34",
  "label":      "gpu-monitor",
  "seq":        1,
  "metrics": {
    "0":   12.4,
    "96":  87.3,
    "99":  75.0,
    "100": 71.0,
    "101": 115.4
  }
}
```

#### Session metadata file (`{id}.meta.json`)

Written immediately when the session starts and overwritten when it stops.

```json
{
  "session_id":    "sess-3a7fb21c",
  "label":         "training-run-001",
  "started_at_ms": 1751500000000,
  "ended_at_ms":   1751503600000,
  "status":        "stopped",
  "rows_written":  3600,
  "log_dir":       "C:\\MyProject\\telemetry_logs",
  "log_path":      "C:\\MyProject\\telemetry_logs\\sess-3a7fb21c.jsonl",
  "metric_ids":    []
}
```

#### Session index (`index.jsonl` — default log dir only)

One line appended per completed (stopped) session.

```json
{"session_id":"sess-3a7fb21c","label":"training-run-001","started_at_ms":1751500000000,"ended_at_ms":1751503600000,"rows_written":3600,"log_path":"C:\\MyProject\\telemetry_logs\\sess-3a7fb21c.jsonl"}
```

---

### 13.4 Runtime Process Watch

Register a process exe name (or PID) to have TelemetryApp track its per-process CPU, memory, GPU, and I/O. Watched process metrics appear automatically in unfiltered session rows as the `watched_processes` array. No service restart is required.

For orchestration tools, CI jobs, training wrappers, and scripts that launch the workload themselves, register by **PID** immediately after process creation. This ties the telemetry row data to that exact process instance instead of every process with the same executable name.

**Register by exe name:**
```http
POST /api/v1/watch
Authorization: Bearer tlm-abc...
Content-Type: application/json

{"exe_name": "python.exe", "label": "training-script"}
```

Response 201:
```json
{"exe_name": "python.exe", "label": "training-script", "status": "added"}
```

`exe_name` is matched case-insensitively against running processes at each poll tick. If the process is not running when the watch is added, TelemetryApp will detect it on the next poll tick after it starts.

**Register by PID** (for processes that share an exe name):
```json
{"pid": 12345}
```

**Remove from watch list:**
```http
DELETE /api/v1/watch/python.exe
Authorization: Bearer tlm-abc...
```
Response: 204 on success, 404 if not in watch list.

**Query current watch config and live telemetry:**
```http
GET /api/v1/watch
```
```json
{
  "exe_names": ["python.exe"],
  "pids": [],
  "live_entries": [
    {
      "exe_name":       "python.exe",
      "label":          "training-script",
      "pid":            12345,
      "alive":          true,
      "cpu_pct":        82.4,
      "mem_private_mb": 4096.0,
      "gpu_sm_pct":     87.3,
      "gpu_vram_mb":    6144.0,
      "uptime_sec":     142.0
    }
  ]
}
```

**How watched data flows into sessions:**

When `metric_ids` is empty (all-metrics mode), every session row automatically includes a `watched_processes` array containing all currently active watched processes. The array is absent if no process is being watched. Each entry is keyed by its watch slot (0–7) and includes the `label` set at registration time, making it fully self-describing without a schema lookup.

If you need watched process metrics in filtered mode, use the IDs from the table in Section 13.6.

---

### 13.5 End-to-End Examples

#### Example A — Full metrics, default log location

```python
import subprocess, os, requests, json

SERVICE = "http://localhost:8765"
HEADERS = {
    "Authorization": f"Bearer {os.environ['TELEMETRY_API_KEY']}",
    "Content-Type":  "application/json"
}

# 1. Register the process to watch (do this before starting the session
#    so the first row already contains watched_processes data)
requests.post(f"{SERVICE}/api/v1/watch", headers=HEADERS,
    json={"exe_name": "python.exe", "label": "train"}).raise_for_status()

# 2. Start a session — logs ALL metrics to the default location
sess = requests.post(f"{SERVICE}/api/v1/sessions", headers=HEADERS,
    json={"label": "training-run-001", "metric_ids": []}).json()
session_id = sess["session_id"]
log_path   = sess["log_path"]
print(f"Logging to: {log_path}")

# 3. Launch the workload (this process is in the watch list so its
#    CPU/memory/GPU will appear as watched_processes in every row)
proc = subprocess.Popen(["python", "train.py", "--epochs", "100"])
proc.wait()

# 4. Stop the session
requests.post(f"{SERVICE}/api/v1/sessions/{session_id}/stop",
    headers=HEADERS).raise_for_status()

# 5. Read and analyse
rows = [json.loads(l) for l in open(log_path)]
gpu       = [r["gpu"][0]["usage_pct"]   for r in rows if r.get("gpu")]
proc_gpu  = [r["watched_processes"][0]["gpu_sm_pct"]
             for r in rows if r.get("watched_processes")]
print(f"System GPU mean:  {sum(gpu)/len(gpu):.1f}%")
print(f"Process GPU mean: {sum(proc_gpu)/len(proc_gpu):.1f}%")
print(f"Duration:         {len(rows)} seconds")
```

#### Example B — Custom log location

```python
# Write logs directly to the experiment directory
sess = requests.post(f"{SERVICE}/api/v1/sessions", headers=HEADERS, json={
    "label":   "run-042",
    "log_dir": r"D:\experiments\run042",
    "metric_ids": []
}).json()

log_path = sess["log_path"]   # D:\experiments\run042\sess-XXXXXXXX.jsonl
log_dir  = sess["log_dir"]    # D:\experiments\run042
```

The directory is created automatically. All subsequent `GET /api/v1/sessions/{id}` responses will return the custom `log_dir` and `log_path`.

#### Example C — Selective metrics only (minimal storage)

```python
# Log only: total CPU %, GPU 0 util, VRAM %, temperature, power draw
sess = requests.post(f"{SERVICE}/api/v1/sessions", headers=HEADERS, json={
    "label":      "gpu-trace",
    "log_dir":    r"C:\traces",
    "metric_ids": [0, 96, 99, 100, 101]
}).json()
```

Rows are minimal:
```json
{"ts_ms":1751500000123,"session_id":"sess-ab12ef34","label":"gpu-trace","seq":1,"metrics":{"0":12.4,"96":87.3,"99":75.0,"100":71.0,"101":115.4}}
```

---

### 13.6 Metric ID Reference

The `metric_ids` array accepts integer metric IDs. Providing an empty array (or omitting the field) logs all standard metrics in named form. The full ID space is defined in `shared/metric_ids.h`; the most commonly used IDs for session logging are listed below.

#### CPU (IDs 0–63)

| ID | Name | Unit | Description |
|---|---|---|---|
| 0 | `CPU_USAGE_TOTAL` | % | All-core CPU utilization |
| 1 | `CPU_FREQ_ACTUAL_MHZ` | MHz | Current average clock frequency |
| 3 | `CPU_CORE_BALANCE_SCORE` | 0–1 | Load balance across cores |
| 9 | `CPU_PACKAGE_TEMP_C` | °C | CPU package temperature |
| 10 | `CPU_PACKAGE_POWER_W` | W | CPU package power draw |
| 16–47 | `CPU_CORE_BASE + n` | % | Per-core utilization (core 0 = ID 16, core 31 = ID 47) |

#### Memory (IDs 64–95)

| ID | Name | Unit | Description |
|---|---|---|---|
| 64 | `MEM_TOTAL_GB` | GB | Total physical RAM |
| 65 | `MEM_USED_GB` | GB | RAM in use |
| 66 | `MEM_AVAILABLE_GB` | GB | RAM available |
| 67 | `MEM_PERCENT` | % | RAM utilization |
| 69 | `MEM_SWAP_USED_GB` | GB | Page file used |
| 70 | `MEM_SWAP_PERCENT` | % | Page file utilization |
| 71 | `MEM_STANDBY_GB` | GB | Standby (cached) memory |
| 75 | `MEM_PAGE_FAULT_RATE` | /sec | Hard page fault rate |

#### GPU (IDs 96–223, 4 GPUs × 32 IDs each)

GPU metric IDs follow this formula: `96 + (gpu_index × 32) + offset`

| Offset | Name | Unit | Description |
|---|---|---|---|
| 0 | `USAGE_PCT` | % | GPU compute utilization |
| 1 | `VRAM_USED_MB` | MB | VRAM used |
| 2 | `VRAM_TOTAL_MB` | MB | VRAM total |
| 3 | `TEMP_C` | °C | GPU temperature |
| 4 | `POWER_W` | W | GPU power draw |
| 5 | `FAN_PCT` | % | Fan speed |
| 6 | `CLOCK_CORE_MHZ` | MHz | Core clock |
| 7 | `CLOCK_MEM_MHZ` | MHz | Memory clock |
| 10 | `ENCODER_PCT` | % | Hardware encoder utilization |
| 11 | `DECODER_PCT` | % | Hardware decoder utilization |
| 17 | `VRAM_PCT` | % | VRAM utilization |

**GPU 0 quick-reference:**

| ID | Metric |
|---|---|
| 96 | GPU 0 utilization % |
| 97 | GPU 0 VRAM used MB |
| 98 | GPU 0 VRAM total MB |
| 99 | GPU 0 VRAM % |
| 100 | GPU 0 temperature °C |
| 101 | GPU 0 power W |
| 113 | GPU 0 VRAM % (alias via offset 17) |

For GPU 1 add 32 (IDs 128–159), GPU 2 add 64 (IDs 160–191), GPU 3 add 96 (IDs 192–223).

#### Disk (IDs 224–287, 8 devices × 8 IDs each)

Formula: `224 + (device_index × 8) + offset`

| Offset | Metric | Unit |
|---|---|---|
| 0 | Read bytes/sec | bytes/s |
| 1 | Write bytes/sec | bytes/s |
| 2 | Read IOPS | /sec |
| 3 | Write IOPS | /sec |
| 4 | Busy % | % |

Disk 0: IDs 224–231 · Disk 1: 232–239 · etc.

#### Network (IDs 288–351, 8 NICs × 8 IDs each)

Formula: `288 + (nic_index × 8) + offset`

| Offset | Metric | Unit |
|---|---|---|
| 0 | Recv bytes/sec | bytes/s |
| 1 | Sent bytes/sec | bytes/s |
| 2 | Recv packets/sec | /sec |
| 3 | Sent packets/sec | /sec |

NIC 0: IDs 288–295 · NIC 1: 296–303 · etc.

#### Watched Processes (IDs 400–511, 8 slots × 14 IDs each)

Formula: `400 + (slot_index × 14) + offset`

Slot assignments are sequential in the order processes are discovered. Query `GET /api/v1/watch` to see the current label-to-slot mapping.

| Offset | Metric | Unit |
|---|---|---|
| 0 | CPU % | % |
| 1 | RSS MB | MB |
| 2 | Private working set MB | MB |
| 3 | Memory % of system | % |
| 4 | Thread count | count |
| 5 | Handle count | count |
| 6 | Disk read bytes/sec | bytes/s |
| 7 | Disk write bytes/sec | bytes/s |
| 8 | GPU VRAM MB (NVML) | MB |
| 9 | GPU SM utilization % (NVML) | % |
| 10 | GPU PDH utilization % | % |
| 11 | Uptime seconds | sec |
| 12 | Page faults/sec | /sec |
| 13 | Context switches/sec | /sec |

Watch slot 0: IDs 400–413 · Slot 1: 414–427 · etc.

**Example — filtered session tracking a specific process:**

```python
# Watch python.exe first, then start a filtered session covering
# system CPU, GPU 0, and the watched process in slot 0
requests.post(f"{SERVICE}/api/v1/watch", headers=HEADERS,
    json={"exe_name": "python.exe", "label": "train"})

sess = requests.post(f"{SERVICE}/api/v1/sessions", headers=HEADERS, json={
    "label":      "proc-trace",
    "log_dir":    r"C:\traces",
    "metric_ids": [
        0,    # CPU total %
        96,   # GPU 0 util %
        100,  # GPU 0 temp °C
        101,  # GPU 0 power W
        400,  # watch slot 0: process CPU %
        402,  # watch slot 0: process private MB
        409,  # watch slot 0: process GPU SM %
        411,  # watch slot 0: process uptime sec
    ]
}).json()
```

Each row:
```json
{"ts_ms":1751500000123,"session_id":"sess-cd34ef12","label":"proc-trace","seq":1,
 "metrics":{"0":12.4,"96":87.3,"100":71.0,"101":115.4,
            "400":82.4,"402":4096.0,"409":87.3,"411":142.0}}
```

#### Self-Monitoring (IDs 384–399)

| ID | Metric | Unit |
|---|---|---|
| 384 | `SELF_CPU_PCT` | % | TelemetryApp service CPU usage |
| 385 | `SELF_RSS_MB` | MB | Service resident set size |
| 388 | `SELF_POLL_MS` | ms | Last poll cycle duration |

---

### 13.7 Why Telemetry Matters During Training

Training large models is the most hardware-intensive operation most ML practitioners run. Without precise telemetry:

- **GPU throttling goes undetected.** A GPU throttling on power or thermal will silently reduce throughput by 15–40% while reporting the same utilization percentage. The throttle reasons bitmask (`VC_10` in NVML, `hw_thermal`, `sw_power_cap`, etc.) is the only way to distinguish a GPU running at full capability from one that is being clock-limited.
- **Memory pressure causes silent swapping.** When training with large batch sizes or long context windows, the model may begin swapping activations to the page file. Page fault rate and `mem_standby_gb` are early warnings before OOM kills the process.
- **VRAM fragmentation is invisible.** A process may appear to use 6GB VRAM, but the pattern of allocations across compute and graphics contexts can consume more pages than the raw bytes suggest. The `max()` aggregation across NVML contexts exposes the true footprint.
- **Stale process telemetry corrupts baselines.** PID reuse without a creation-time guard causes post-mortem telemetry from a dead training process to silently contaminate metrics for a new process that happens to inherit the same PID.

### 13.2 Process Watcher for Training Process Isolation

Configure the watcher to track your training process by executable name or PID. All hardware consumption is attributed exclusively to that process, independent of other activity on the machine.

**Python training script example:**

```python
import subprocess, os, requests, json, time

API = "http://localhost:8765"
KEY = os.environ["TELEMETRY_API_KEY"]
HEADERS = {"X-API-Key": KEY}

# Launch training
proc = subprocess.Popen(["python", "train.py", "--config", "config.yaml"])

# Configure watcher (current version: set exe_names before building;
# future: POST /api/v1/watch {"exe_names": ["python.exe"], "pids": [proc.pid]})

samples = []
while proc.poll() is None:
    snap = requests.get(f"{API}/api/v1/snapshot", headers=HEADERS).json()
    # Find our process in the watch list
    for watch in snap.get("watched", []):
        if watch["pid"] == proc.pid:
            samples.append({
                "ts":          snap["timestamp"],
                "cpu_pct":     watch["cpu_pct"],
                "vram_mb":     watch["gpu_vram_mb"],
                "gpu_sm_pct":  watch["gpu_sm_pct"],
                "mem_priv_mb": watch["mem_private_mb"],
                "pf_rate":     watch["page_faults_per_sec"],
            })
    time.sleep(1)

# Post-run analysis
import statistics
gpu_utils = [s["gpu_sm_pct"] for s in samples]
print(f"Mean GPU SM util:  {statistics.mean(gpu_utils):.1f}%")
print(f"Min  GPU SM util:  {min(gpu_utils):.1f}%  (check for throttle events)")
print(f"Peak VRAM:         {max(s['vram_mb'] for s in samples):.0f} MB")
print(f"Peak page faults:  {max(s['pf_rate'] for s in samples):.0f}/s")
```

### 13.3 Metrics Most Valuable for Training Observability

These are the metrics with the highest signal value for diagnosing training performance issues:

| Metric | What it tells you |
|---|---|
| `gpu_sm_pct` (per-process) | True compute occupancy — if < 80% during a training step, you have a data loading or host-device sync bottleneck |
| `gpu_mem_bw_util_pct` | Memory bandwidth saturation — if 100% while SM util is low, the model is memory-bound |
| `throttle_active` + `throttle_reasons` | Whether the GPU is delivering its rated performance |
| `mem_clk_transitions` (delta) | Frequent P-state transitions indicate the GPU is uncertain about its workload pattern; can reduce throughput by 5–10% |
| `mem_page_fault_rate` (process) | Rising during training = activations or optimizer states being paged out |
| `mem_standby_gb` (system) | If approaching 0, physical RAM is fully committed; next large allocation triggers paging |
| `cpu_core_balance_score` | If near 0 during data loading, all I/O or preprocessing is on one core — your DataLoader is not using multiple workers |
| `disk_read_bytes_s` (process) | Reveals slow data pipelines — if the GPU SM util drops every N steps and disk reads spike, your dataset isn't in RAM/VRAM |
| `self_cpu_pct` | Confirms the monitoring tool itself is not consuming meaningful compute during training |

### 13.4 Integration Patterns

**Pattern 1 — Training step annotations**

Write the current `timestamp` + `gpu_sm_pct` + `vram_mb` at the start and end of each training step. The 300-sample history ring in SHM lets you reconstruct a full 5-minute window after a run without having streamed it live.

**Pattern 2 — Throttle-aware checkpoint selection**

When saving checkpoints, annotate the checkpoint metadata with the current throttle state. During evaluation, skip benchmarking on checkpoints saved during a throttle event — the model may have been trained on an under-clocked GPU and should be re-evaluated at full clock.

**Pattern 3 — Hardware baseline comparison**

Before starting a training run, capture a `/api/v1/snapshot` as the baseline. After the run, diff the `min_session`/`max_session` values in the ring buffer metadata to produce a full hardware utilization report attached to the model card.

**Pattern 4 — Anomaly-triggered alerts**

```python
def check_health(snap):
    gpu = snap["gpu"][0] if snap["gpu"] else {}
    if gpu.get("throttle_active"):
        alert(f"GPU throttle: {gpu['throttle_reasons']}")
    if snap["memory"]["page_fault_rate"] > 1000:
        alert(f"High page fault rate: {snap['memory']['page_fault_rate']:.0f}/s — check batch size")
    if snap["self"]["cpu_pct"] > 3:
        alert(f"TelemetryApp self-overhead elevated: {snap['self']['cpu_pct']:.1f}%")
```

### 13.5 Prometheus + Grafana Stack Integration

The `/metrics` endpoint follows the Prometheus exposition format. Connect a standard Prometheus server and visualize in Grafana with zero custom code:

**Recommended Grafana panels for training runs:**
- Time series: `sys_gpu_sm_util_percent` — compute occupancy over time
- Gauge + history: `sys_gpu_vram_percent` — VRAM fill level
- Alert: `sys_gpu_throttle_active == 1` — immediate notification on throttle
- Time series: `sys_mem_page_fault_rate` — memory pressure trend
- Derived: `sys_gpu_sm_util_percent / sys_gpu_temp_c` — thermal efficiency over the session

### 13.6 Structured Telemetry for Dataset Generation

For use cases where the telemetry data itself is training data (e.g., training a model to predict GPU behavior, anomaly detection, or hardware performance modeling):

**Snapshot schema is stable and versioned.** The SHM version field (`hdr.version`) and the append-only metric ID schema guarantee that data collected across multiple service versions can be merged without schema migrations.

**300-sample ring = 5-minute lookback at 1Hz.** This covers one full training step (for large models) to hundreds of steps (for small ones) — sufficient context window for sequence models learning temporal patterns in hardware utilization.

**Self-measurements as ground truth for overhead correction.** When building a dataset of `(hardware_state, model_behavior)` pairs, the `SELF_*` metrics let you subtract the monitoring tool's own contribution before using the data for regression or correlation analysis.

**Recommended dataset schema for hardware telemetry records:**

```json
{
  "schema_version": 1,
  "run_id": "training-run-2026-07-02-001",
  "timestamp_ms": 1751500000000,
  "hardware": {
    "cpu_usage_pct": 12.4,
    "mem_used_gb": 18.7,
    "mem_pct": 58.4,
    "mem_page_fault_rate": 42.0,
    "gpu": [
      {
        "name": "NVIDIA GeForce RTX 4070 Laptop GPU",
        "sm_util_pct": 87.3,
        "mem_bw_util_pct": 64.1,
        "vram_used_mb": 6144.0,
        "temp_c": 71.0,
        "power_w": 115.4,
        "throttle_active": false,
        "throttle_reasons": []
      }
    ]
  },
  "process": {
    "exe": "python.exe",
    "pid": 12345,
    "cpu_pct": 8.1,
    "mem_private_mb": 4096.0,
    "gpu_sm_pct": 87.3,
    "gpu_vram_mb": 6144.0,
    "page_faults_per_sec": 0.0,
    "disk_read_bytes_s": 0.0
  },
  "tool_overhead": {
    "cpu_pct": 0.3
  }
}
```

This schema separates system-level hardware state, process-specific attribution, and tool overhead — the three axes needed to build a clean training dataset for hardware observability models.

---

## 14. Extending the Tool

### Adding a new metric

1. Assign a new ID in the reserved range within `shared/metric_ids.h` (append only)
2. Add the appropriate offset constant to `GpuOff`, `DiskOff`, `NetOff`, or `WatchOff` if it belongs to an indexed subsystem
3. Call `ShmPush(shm, new_id, value)` in `service/poll_loop.cpp` inside the seqlock write block
4. The metric is immediately accessible via `/api/v1/snapshot`, `/api/v1/history/<id>`, and `/metrics` with no other changes

### Adding a new sensor backend

1. Create `service/sensors/my_sensor.h` and `.cpp` following the `Init / Poll / Shutdown` pattern
2. Call `MyInit()` in `PollLoopInit()`
3. Call `MyPoll(...)` in `PollLoopRun()` and write results to SHM in the existing write block
4. Add the `.cpp` file to `service/CMakeLists.txt` and any required link libraries

### Adding a new API endpoint

Add a `s_svr->Get(...)` or `s_svr->Post(...)` handler in `service/ipc/http_server.cpp`. The `cpp-httplib` server handles threading; handlers run on IOCP worker threads and must be thread-safe (use `std::mutex` or read from SHM via the seqlock pattern).

### Adding a new GUI panel

1. Add a `WaveformGraph` or `GaugePanel` member to `PanelLayout`
2. Assign it a layout rect in `PanelLayout::Layout()` (extend the panel count)
3. Call `Push()` / update `value` in `PushData()`
4. Call `Draw()` in `DrawAll()`

---

## 15. License

MIT License — see [LICENSE](LICENSE).

This software uses the following open-source libraries via vcpkg:
- **cpp-httplib** (MIT) — Yhirose — single-header HTTP/1.1 server
- **nlohmann/json** (MIT) — Niels Lohmann — JSON for Modern C++
- **spdlog** (MIT) — Gabi Melman — fast C++ logging

Vendor GPU APIs are loaded dynamically at runtime from DLLs installed by GPU drivers:
- **NVML** (nvml.dll) — NVIDIA Management Library — NVIDIA proprietary, redistributed by NVIDIA driver installation
- **ADL2** (atiadlxx.dll) — AMD Display Library — AMD proprietary, redistributed by AMD driver installation  
- **IGCL** (ControlLib.dll) — Intel GPU Control Library — Intel proprietary, redistributed by Intel Arc driver installation

None of these vendor libraries are linked statically or redistributed with this software.
