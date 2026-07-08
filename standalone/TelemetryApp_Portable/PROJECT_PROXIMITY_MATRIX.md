# WINAPP Project Proximity Matrix

## Purpose

This is the perpetual project proximity matrix for the native WINAPP TelemetryApp product.

It records where TelemetryApp sits relative to adjacent commercial, open-source, and platform-native tools. It should be updated during major remediation passes, release-candidate reviews, and after any meaningful change to metric catalog coverage, process capture, dashboard editing, API surface, logging, installer behavior, or accelerator telemetry.

The goal is not to copy rival tools. The goal is to keep TelemetryApp's product identity clear:

```text
Local Windows-native telemetry, process-run capture, scriptable API access, lightweight desktop monitoring, maintenance/degradation evidence, and diagnosable logs in one simple testable package.
```

## Update Rules

- Owner: project maintainer.
- Location: root `docs` folder, outside `WINAPP`.
- Update cadence: each release candidate, major UX remediation pass, API contract change, metric-catalog expansion, or installer/package change.
- Evidence rule: compare against vendor docs, official docs, direct product testing, or checked local code. Mark assumptions clearly.
- DevOpsAgent rule: use the read-only DevOpsAgent guidance as a consultant only. Do not write to `C:\Users\demit\Documents\Antigrav\DevOpsAgent`.
- Decision rule: proximity should drive roadmap priority only when it improves TelemetryApp's intended use case.

## Reference Set

The current competitor and comparator set is:

| Tool | Category | Reference |
|---|---|---|
| HWiNFO | Hardware sensor monitoring and shared-memory ecosystem | https://www.hwinfo.com/forum/threads/shared-memory-support.18/ |
| AIDA64 | Hardware diagnostics, logging, reports, stability testing | https://www.aida64.com/user-manual/hardware-monitoring/logging?language_content_entity=en |
| MSI Afterburner | GPU tuning and on-screen display | https://www.msi.com/Landing/afterburner |
| NVIDIA Nsight Systems | GPU/CPU performance profiling | https://developer.nvidia.com/nsight-systems |
| Windows Performance Recorder / WPA | ETW system and application tracing | https://learn.microsoft.com/en-us/windows-hardware/test/wpt/windows-performance-recorder |
| Sysinternals Process Monitor | File, registry, process, and thread activity tracing | https://learn.microsoft.com/en-us/sysinternals/downloads/procmon |
| Prometheus windows_exporter | Windows metrics exporter | https://github.com/prometheus-community/windows_exporter |
| Datadog Live Processes / Process Check | Fleet process monitoring and cloud observability | https://docs.datadoghq.com/infrastructure/process/ |

## Scoring Model

Scores use a 0 to 5 scale:

| Score | Meaning |
|---|---|
| 0 | Not present. |
| 1 | Prototype only or not reliable enough for normal use. |
| 2 | Present but incomplete, hard to use, or not well documented. |
| 3 | Usable for intended workflows with known gaps. |
| 4 | Strong implementation, close to commercial expectations. |
| 5 | Best-in-class or clearly differentiated. |

Proximity means how close the rival is to TelemetryApp's target job. A high-proximity rival is important even if it is not a direct clone.

| Proximity | Meaning |
|---|---|
| High | Directly overlaps TelemetryApp's target use case. |
| Medium | Overlaps one important capability or user workflow. |
| Low | Useful benchmark, but not a direct product substitute. |

## Product Identity

TelemetryApp is closest to a hybrid of:

- HWiNFO/AIDA64-style local sensor visibility.
- Prometheus/windows_exporter-style metrics access.
- Datadog-style process resource monitoring, but local-first.
- MSI Afterburner-style lightweight HUD/bar viewing.
- A small slice of Nsight/WPR-style run-focused diagnostic capture, without trying to replace deep profilers.
- Technician-oriented workstation maintenance evidence, without trying to replace full enterprise endpoint management.

The strongest product wedge is:

```text
An app or script can call TelemetryApp, tell it which process or executable to watch, where to write logs, which metrics to capture, and how long to run, while a normal Windows user can also see the same system state in a lightweight desktop UI.
```

## Core Capability Matrix

| Capability | TelemetryApp Current | Grade | Closest Rival Strength | Competitive Position | Remediation Priority |
|---|---:|---:|---|---|---|
| Single local Windows executable entry point | Implemented through `WINAPP\bin\TelemetryApp.exe` launcher and installed `TelemetryApp.exe` root entry point | 4 | Consumer tools are generally simple to launch | Competitive and easy to test | Preserve |
| Hardware sensor breadth | CPU, memory, disk, network, GPU/NVML path, thermal, service health, process metrics | 3 | HWiNFO and AIDA64 have broader sensor databases | Behind specialist sensor tools | Medium-high |
| Stable metric catalog | Shared model exists and persists `data\metric_catalog.json` | 3 | windows_exporter has mature collector taxonomy | Promising, needs final UX/API polish | High |
| Dashboard customization | Editor supports hide/show, visualization choice, sizing, save/cancel/default, scrolling, and left-packed visible tiles | 3 | AIDA64 and Afterburner provide mature visual workflows | Usable, still needs live user validation and regression hardening | High |
| HUD/minimized bar mode | Implemented with selectable metric routing, hover explanations, context menu, orientation choices, and appbar work-area reservation | 4 | Afterburner OSD is mature for gaming/GPU overlays | Strong for general system telemetry, not a game overlay replacement | Preserve |
| Process-run capture | API sessions, PID/exe watch, custom log folder, duration/exit policies | 4 | Datadog process monitoring is fleet-scale, AIDA64 has logging options | Strong local differentiator | High |
| Third-party script/API usage | REST, API keys, snapshot/history/sessions/watch/diagnostics, Prometheus, SSE | 4 | windows_exporter and Datadog are stronger in ecosystem integration | Strong for local scripted capture | High |
| Custom log folder per API call | Implemented in sessions | 5 | Not consistently central in rival local desktop tools | Clear differentiator | Preserve |
| Diagnostic/crash logging | Overwritten launcher/client/service logs beside executables | 4 | Enterprise tools often have stronger incident pipelines | Strong for local remediation | Preserve |
| API security | API key hashing; HTTPS/TLS and encrypted client-side key cache deferred | 3 | Datadog/cloud tools stronger; local tools often vary | Acceptable for local dev, not broad release | High before external distribution |
| GPU accelerator awareness | NVML path exposes CUDA compute capability and tensor-core inference fields where available | 3 | Nsight is far deeper; HWiNFO sensor breadth is broader | Good for telemetry, not profiling | Medium |
| GPU kernel/API profiling | Not a goal today | 0 | Nsight Systems and Nsight Compute dominate | Do not chase unless scope changes | Low |
| ETW/deep OS trace | Not implemented | 0 | WPR/WPA dominate ETW traces | Possible future optional capture mode | Low-medium |
| File/registry activity tracing | Not implemented | 0 | Process Monitor dominates | Out of scope unless process diagnostics expands | Low |
| Prometheus export | Implemented | 3 | windows_exporter is mature and broadly adopted | Good adjunct, not full replacement | Medium |
| Fleet/local network monitoring | Fleet navigation, readiness/manual add, explicit lab enrollment, durable identity, heartbeat call-home, address history, and fleet logging job wizard exist; TLS/mTLS and enterprise token lifecycle remain pending | 3 | Datadog dominates hosted fleet workflows; PRTG/Zabbix are mature network monitoring systems | Useful local lab fleet lane, not yet enterprise-grade | High after local UX/security gates |
| Remote real-time device dashboard | Fleet Manager can switch to enrolled device snapshots and preserve devices across IP changes by stable identity; richer remote streaming and credentialed dispatch remain next hardening | 2 | Datadog, PRTG, Zabbix, and Grafana are mature for remote live views | Important differentiator if kept local-first and simple | High for fleet release |
| User-facing simplicity | Improved through consolidated menus, dashboard editor controls, folder-prompt logging, and wizard-guided fleet logging; still needs live testing | 3 | Consumer tools are simpler for their narrower tasks | Main adoption risk is regression in new controls | High |
| Installer/package readiness | Installer and portable package build, plant README/API/LICENSE in root, support repair/update/modify/uninstall, and stop running processes before replacement | 3 | Commercial tools have polished installers | Usable release-candidate state; still needs clean-machine validation | High |

## Rival Proximity Matrix

| Rival | Proximity | What They Do Best | What TelemetryApp Does Better | What TelemetryApp Must Not Attempt To Replace |
|---|---|---|---|---|
| HWiNFO | High | Deep hardware sensor discovery, rich sensor labels, broad motherboard/GPU coverage, shared-memory ecosystem | API-driven process-run capture, custom per-session log folders, single local app/service workflow | Full sensor database maturity on day one |
| AIDA64 | High | Hardware inventory, diagnostics, reports, stability tests, configurable sensor logging | Script/API initiated session capture and local automation-first process tracking | Full benchmark/stability-test suite |
| MSI Afterburner | Medium | GPU tuning, real-time OSD, user familiarity, low-friction gaming overlay | Whole-system/process/API telemetry, custom logs, service-backed capture | GPU overclocking and fan-control domain |
| NVIDIA Nsight Systems | Medium | GPU/CPU timeline profiling, CUDA/NVIDIA workload analysis, performance optimization | Lightweight always-on resource telemetry and simple process-run logs | Kernel-level profiler, CUDA timeline analysis, deep GPU trace |
| Windows Performance Recorder / WPA | Medium | ETW-based Windows internals trace, deep post-run analysis | Simple user-facing API capture and readable telemetry logs | ETW trace authority |
| Sysinternals Process Monitor | Low-medium | Real-time file, registry, process/thread activity with powerful filters | Numeric resource consumption capture, dashboard/HUD/API sessions | File/registry event tracing |
| Prometheus windows_exporter | High for API/export | Mature Windows metrics export for Prometheus collectors | Desktop UI, process-run sessions, custom local log folder, script-triggered capture | Full Prometheus ecosystem collector maturity |
| Datadog Live Processes | Medium-high for fleet/process views | Fleet monitoring, cloud dashboards, alerting, tag-based infrastructure views | Local-first no-cloud process capture, custom local log folders, desktop visibility | Cloud observability platform |
| PRTG / Zabbix class NMS tools | Medium for fleet/network monitoring | Mature remote device inventory, polling, alerting, dashboards, SNMP/agent workflows | Easier local Windows process-run telemetry, richer desktop workflow, no central server required for local use | Full network management suite |

## Current Overall Grade

Current product grade: `B`.

Reasoning:

- The technical architecture is stronger than the visible product polish.
- The process-run API, custom log folder, diagnostic logging, and local-first service/client model are strong differentiators.
- The dashboard editor, metric-selection workflow, installer polish, API transport security, and long-run validation still determine whether the app feels commercial-grade.
- Fleet logging and enrollment-readiness workflows improve product direction, but current fleet maturity is still early because remote device inventory, secure enrollment, remote collector, and real-time remote dashboard telemetry are not yet implemented.

Current score split:

| Track | Grade | Reason |
|---|---:|---|
| Local telemetry engine | `B+` | Native service, SHM, REST, Prometheus, SSE, JSONL, process watches, and diagnostics are real and useful. |
| Local desktop UX | `B-` | Dashboard/HUD/metrics flows are usable and better documented; live regression testing remains important. |
| Script/API process-run capture | `A-` | Strongest differentiator: process/session/log-folder control is substantially ahead of normal desktop sensor tools. |
| Hardware sensor breadth | `C+` | Good general coverage; behind HWiNFO/AIDA64 sensor depth and motherboard-specific coverage. |
| Power/electrical telemetry | `C-` | Source-qualified direction is correct; comprehensive multi-vendor electrical accounting remains early. |
| Fleet/remote monitoring | `C+` | Fleet UI, readiness/manual add, lab enrollment, remote snapshot/logging hooks, and heartbeat identity reconciliation now exist; TLS/mTLS, one-time tokens, and enterprise inventory policy still gate production use. |
| Installer/enterprise lifecycle | `B-` | Registry/env/service groundwork, maintenance mode, role modification, and install-root docs exist; TLS/enrollment and clean-machine validation still gate enterprise acceptance. |

Near-term target grade: `B+`.

Requirements for `B+`:

- Dashboard editor scroll, save, cancel, default, hide, and left-pack behavior survive live testing.
- Metrics page becomes the clear catalog/profile manager instead of a dense checklist.
- Logging/capture workflow is obvious: folder, process, duration, metrics/profile, start.
- README and generated API docs match implemented routes, UI menu paths, and honest limitations.
- App logs capture enough failure detail on every run.
- Live test path is simple: run `WINAPP\bin\TelemetryApp.exe`.

Release-candidate target grade: `A-`.

Requirements for `A-`:

- Installer and portable package are validated.
- API security plan is implemented or explicitly gated for local-only release.
- Long-run telemetry validation confirms low overhead and no handle/thread/log growth.
- Metric catalog clearly separates supported, unavailable, inferred, and future metrics.
- GPU/CUDA/tensor accelerator detection clearly reports detection source and confidence.

## Differentiator Scorecard

| Differentiator | Strength | Why It Matters | Preserve/Improve |
|---|---:|---|---|
| API-initiated process session capture | 5 | This is the clearest reason for another app/script to use TelemetryApp. | Preserve and document heavily |
| Custom log folder per run | 5 | Makes automation, benchmark runs, support bundles, and reproducible testing practical. | Preserve |
| Desktop UI plus service API | 4 | Bridges normal-user monitoring and automation workflows. | Improve UI simplicity |
| Local-first/no-cloud telemetry | 4 | Useful for private workloads, AI/ML runs, local benchmarking, and diagnostics. | Preserve |
| Overwritten executable-folder diagnostics | 4 | Makes crash remediation easy during current test phase. | Preserve for debug mode; add retention option later |
| Accelerator fields | 3 | Valuable for AI/ML workloads, but must be labeled clearly by source/confidence. | Improve |
| Metric catalog/profile model | 3 | Right architecture, but needs final UX. | Critical improvement |
| Fleet page information architecture | 3 | Correct place for inventory, discovery candidates, and remote telemetry drill-in. | Implement API-backed device views |
| Remote real-time dashboard reuse | 1 | Not yet implemented; must let a selected trusted device show Dashboard-equivalent live telemetry. | High for fleet maturity |

## Risk Matrix

| Risk | Impact | Likelihood | Current Mitigation | Next Action |
|---|---|---:|---|---|
| Dashboard editor controls appear but do not interact reliably | High | High | Recent fixes widened controls and added header actions | Validate manually, then add regression notes/tests |
| Metric selection feels too complex | High | Medium | Shared catalog model exists | Build searchable catalog/profile manager |
| API docs drift from implementation | High | Medium | API docs generated/updated | Re-run docs generation after every API change |
| Hardware accelerator detection is misinterpreted | Medium | Medium | API exposes fields when available | Add source/confidence labels and unavailable reasons |
| API key storage/security not ready for broad distribution | High | Medium | Server-side hash store exists | Implement TLS/API encryption and DPAPI client cache before broad release |
| Installer/package confusion | High | Medium | Unified executable path established | Validate portable and installer paths |
| Long-run memory/handle/log growth | High | Unknown | Diagnostic logs and service metrics exist | Run 8-hour and 24-hour validation passes |
| Remote telemetry mixed with local telemetry without provenance | High | Medium once fleet backend starts | Remote plan requires `source_device` on every row | Enforce source-qualified snapshots and dashboard context |
| Remote dashboard causes UI stalls under packet loss | High | Medium | Plan requires timeouts, stale states, backoff | Build collector cache and render from last-known snapshot, not blocking UI calls |

## Remediation Map By Competitive Pressure

| Pressure | Source | Required TelemetryApp Response |
|---|---|---|
| HWiNFO/AIDA64 sensor breadth | Users expect accurate sensor names and availability | Clearly label detected, unavailable, inferred, and unsupported metrics; avoid fake completeness |
| Afterburner overlay simplicity | Users expect quick glanceable HUD behavior | Keep HUD/bar customization simple and hover explanations concise |
| windows_exporter ecosystem maturity | Developers expect stable metric IDs and scrape behavior | Preserve stable IDs, document metric catalog and Prometheus naming |
| Datadog process visibility | Engineers expect process filtering and repeatable process watches | Keep PID/exe watch APIs stable; add scheduled/repeated capture profiles |
| Datadog/PRTG/Zabbix fleet views | Operators expect device list, health, last seen, live charts, stale states, and drill-down | Fleet page must show enrolled devices and open Dashboard-equivalent remote telemetry without unsafe auto-trust |
| Nsight/WPR depth | Power users may expect deep tracing | State boundaries clearly; consider optional ETW/Nsight handoff rather than replacement |

## Next Completion Gates

1. Stabilize dashboard editor interaction.
   - Scrollbar must respond to wheel, rail click, thumb drag, and touchpad scroll.
   - Save, Cancel, and Save Default must be visible and reachable.
   - Hidden dashboard panels must pack left without holes.

2. Finalize metric catalog UX.
   - Search, filter, group, display all, restore default, route to Dashboard/HUD/Logging/API.
   - Save named profiles.
   - Preserve the current default display as the starter profile.

3. Finalize capture workflow.
   - Folder prompt.
   - Process/PID/exe selection.
   - Duration and stop policy.
   - Metric profile selection.
   - Live status and failure explanation.

4. Validate API completeness.
   - Snapshot, catalog, capabilities, diagnostics, sessions, watch, keys, Prometheus, SSE.
   - Confirm examples in `WINAPP\README.md` and `WINAPP\bin\API.md`.

5. Validate package behavior.
   - Run only `WINAPP\bin\TelemetryApp.exe`.
   - Confirm logs are overwritten beside executables.
   - Confirm tray icon is always present.
   - Confirm start minimized and hide-to-tray behavior.

6. Prepare release security gate.
   - HTTPS/TLS transport.
   - DPAPI-protected client API key cache.
   - Key rotation and revocation verification.
   - Local-only mode explicitly documented if TLS is deferred.

7. Implement Fleet real-time dashboard path.
   - Trusted device inventory in `%PROGRAMDATA%\TelemetryApp\remote\trusted_devices.json`.
   - `GET /api/v1/devices` and per-device health/snapshot/capabilities routes.
   - Remote collector cache with timeout, jitter, backoff, and stale-device state.
   - Dashboard context selector: Local Device vs enrolled remote device.
   - Same visualization/profile model as local Dashboard, with visible remote source identity.
   - Session/log rows must include `source_device`.

## Current Position Summary

TelemetryApp should not be marketed or designed as a replacement for HWiNFO, AIDA64, Afterburner, Nsight, WPR, Process Monitor, windows_exporter, or Datadog.

It should be positioned as the local Windows process-run telemetry bridge between those categories:

- Easier for scripts and third-party apps than traditional desktop sensor tools.
- More user-visible than headless exporters.
- More focused and lightweight than deep profilers.
- More private and local than cloud observability platforms.

That is the product lane. The final remediation work should protect that lane by making the app simpler, more reliable, and more explicit about what each metric and output surface does.
