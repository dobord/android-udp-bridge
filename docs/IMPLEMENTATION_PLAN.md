## Implementation Plan (Updated for udp2tcp migration)

Updated 14 Aug 2025. This document reflects the transition from the custom "UDP Bridge" (protocol, listener, client manager, TCP Connection Manager) to using external `udp2tcp` over SSH port forwarding. Historical phases (1–4) are preserved in the Legacy Summary and detailed in reports in `docs_legacy/`.

### TL;DR Current State
| Area | Status | Comment |
|------|--------|---------|
| Legacy Phases 1–2 | DONE | Server + Android protocol infra implemented (now legacy) |
| Legacy Phases 3–4 | PARTIAL | Docs / CI partly done; remainder becomes irrelevant post-udp2tcp |
| Migration strategy selection | DONE | Chosen: vendored udp2tcp source (NDK) |
| Adapter `udp2tcp_client_adapter` creation | IN PROGRESS | Stubs / interface exist; full I/O loops needed |
| README / Tech Spec updates | DONE | Legacy marked; udp2tcp sections added |
| Legacy source cleanup | PENDING | After udp2tcp e2e confirmation |
| E2E udp2tcp test | PENDING | Script + docker scenario |
| CI update (exclude legacy) | PENDING | After file removal |
| Documentation update (final) | PENDING | After tests + cleanup |

---
## 1. Historical Context (Legacy Summary)

Original plan focused on building a custom binary protocol and server. Completed phases:
1. Server UDP Bridge: protocol, client table, UDP forwarder, select() server.
2. Android integration: protocol port, client manager, UDP listener, TCP connection manager, Java interface.
3. Partial e2e & functional testing, CI/CD preparation.
4. Documentation & reports (see `docs_legacy/PHASE_*`, `TASK_*`, `PROTOCOL_IMPLEMENTATION_REPORT.md`).

Reasons to abandon custom stack: reduce complexity, shrink JNI code, drop checksum/ping/client_table maintenance in favor of streaming encapsulation via `udp2tcp`.

---
## 2. Goals (Current Stage)
1. Integrate `udp2tcp` client into Android (NDK) with minimal glue.
2. Reuse existing SSH port forwarding as transport.
3. Provide a transparent local UDP port for apps.
4. Maintain / update stats (packets, bytes, uptime, errors).
5. Remove (or conditionally disable) legacy code & tests after validation.
6. Simplify UI (remove hidden legacy toggles — partly done).

---
## 3. New Plan Structure

### 3.1 Preparation
- [x] Create / update migration doc (`MIGRATION_UDP2TCP.md`).
- [x] Update technical spec (`TECH_SPEC_NEW_ARCHITECTURE.md`).
- [x] Update README (docs index / legacy marking).
- [ ] Add lightweight udp2tcp flow diagram (optional / later).

### 3.2 udp2tcp Source Integration
Approach A: vendor sources under `third_party/udp2tcp/`.
- [ ] Import current udp2tcp tag/commit (README_IMPORT or git subtree / vendor).
- [ ] Add Android.mk / CMakeLists fragment (arch-agnostic).
- [ ] Determine minimal source set (client portion only, exclude tools).
- [ ] Build verification for all ABIs (arm64-v8a / armeabi-v7a / x86_64 / x86).

### 3.3 Adapter (JNI layer)
Interface (draft):
```c
int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port);
int udp2tcp_start(void);        // создает потоки RX/TX
int udp2tcp_stop(void);
void udp2tcp_get_stats(uint64_t* rx_pkts, uint64_t* tx_pkts, uint64_t* rx_bytes, uint64_t* tx_bytes);
int udp2tcp_is_running(void);
```
- [x] Header / struct scaffolding.
- [ ] UDP receive loop (local) -> send over tcp (udp2tcp API).
- [ ] TCP read loop -> inject into local UDP socket.
- [ ] Error handling / reconnect (configurable intervals).
- [ ] Atomic stats counters.
- [ ] JNI methods + Java wrapper (replacing legacy service layer).

### 3.4 SSH Integration
- [ ] Ensure libssh forwarding binds local TCP on <local_forward_port> to <udp2tcp_server_host:udp2tcp_server_port>.
- [ ] Parameterize port via UI / config.
- [ ] Verify forward teardown on stop.

### 3.5 Testing
Categories:
1. Unit (adapter, simple local packet transfer).
2. Integration (local udp client -> adapter -> tcp (via ssh) -> udp2tcp server -> echo target).
3. E2E script (shell) for CI.
4. Load (N * 1000 packets + loss measurement).

Tasks:
- [ ] `test_udp2tcp_basic.sh` — send one UDP packet and verify reply.
- [ ] Extend `run_full_e2e_test.sh` with `--udp2tcp` mode.
- [ ] Load scenario (10k packets, avg size 200B, measure latency).
- [ ] Log analysis (grep markers udp2tcp_adapter).

### 3.6 Legacy Cleanup
- [ ] Add temporary build flag `ENABLE_LEGACY_BRIDGE=0`.
- [ ] After successful e2e: remove `udp_listener.*`, `client_manager.*`, `udp_bridge_protocol.*`, `tcp_connection_manager.*`.
- [ ] Move remaining reports to `docs_legacy/` (most already migrated).
- [ ] Remove legacy test scripts (archive path in migration log).

### 3.7 CI/CD Update
- [ ] Update workflow: drop legacy file checks.
- [ ] Add udp2tcp adapter build step (ndk-build / cmake).
- [ ] Add E2E udp2tcp smoke test container.
- [ ] Publish artifacts: apk + test log.

### 3.8 Documentation (Final Round)
- [ ] Update QUICKSTART (replace custom protocol section).
- [ ] Add "udp2tcp vs Legacy" comparison (expanded table, README version is short).
- [ ] FAQ: questions on removal of ping/pong / client ids.
- [ ] Archive: list removed files + last commit hash.

### 3.9 Release Criteria (Migration)
All checkboxes must be ticked:
- [ ] Successful udp2tcp e2e (≥3 runs without loss >2%).
- [ ] p95 latency non-degraded (≤ +15% vs legacy baseline, documented).
- [ ] No JNI references to legacy symbols (grep).
- [ ] CI green across all ABI matrix.
- [ ] README / Tech Spec free of "will be removed" notes (converted to past tense).
- [ ] Legacy code removed from repository.

---
## 4. Task Matrix (Summary)

| Category | Task | ID | Status |
|----------|------|----|--------|
| Source Import | Import udp2tcp into third_party | S1 | ☐ |
| Build | Android.mk/CMake integration | B1 | ☐ |
| Adapter | TX/RX loop implementation | A1 | ☐ |
| Adapter | Statistics (atomic counters) | A2 | ☐ |
| Adapter | JNI methods / Java wrapper | A3 | ☐ |
| SSH | Forward binding & teardown | F1 | ☐ |
| Testing | test_udp2tcp_basic.sh | T1 | ☐ |
| Testing | E2E integration in run_full_e2e_test.sh | T2 | ☐ |
| Testing | Load test 10k packets | T3 | ☐ |
| Cleanup | Legacy disable flag | C1 | ☐ |
| Cleanup | Remove legacy files | C2 | ☐ |
| CI/CD | Workflow udp2tcp steps | CI1 | ☐ |
| Docs | QUICKSTART udp2tcp update | D1 | ☐ |
| Docs | FAQ / Comparison table | D2 | ☐ |
| Release | p95 latency verification | R1 | ☐ |
| Release | No legacy references | R2 | ☐ |

Legend: ☐ not started, ◐ in progress, ☑ done.

---
## 5. Risks & Mitigation (Updated)

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|-----------|
| Potential udp2tcp API incompatibility on Android | Medium | Medium | Local fork / socket adaptation; minimize POSIX-specific dependencies |
| Latency increase vs legacy (CRC/client table removed, TCP effects remain) | Medium | Medium | Tune TCP_NODELAY, socket buffers; measure & adjust |
| Resource leaks on adapter stop | Low | High | Structured cleanup, loop stop test cycle |
| Missing ping/pong mechanism | Low | Low | Optional keepalive via TCP or idle timer |
| Insufficient multi-ABI test coverage | Medium | Medium | Add ABI matrix early (before legacy removal) |

---
## 6. Success Metrics (Revised)
| Metric | Target |
|--------|--------|
| Successful udp2tcp e2e test | ≥3 consecutive runs without critical errors |
| Packet loss (load test) | < 1% |
| p95 latency | ≤ +15% over recorded legacy baseline |
| Adapter memory footprint | < 5 MB additional RSS |
| JNI code line count | ≥40% reduction vs legacy set |

---
## 7. Legacy Decommission
- Create `docs/LEGACY_REMOVAL_CHANGELOG.md` (post-removal) listing deleted artifacts.
- Record last commit SHA before deletion.
- Add "Historical Artifacts" section in README → pointing to `docs_legacy/`.

---
## 8. Immediate Next Steps (Focus)
1. Import `udp2tcp` code (S1).
2. Build adapter for one ABI (arm64-v8a) — smoke build (B1).
3. Implement RX/TX loops (A1) + counters (A2).
4. Minimal `test_udp2tcp_basic.sh` (T1).
5. Manual e2e via SSH port forward (pre-automation).

After validation — parallelize CI (CI1) + cleanup (C1).

---
## 9. References
- Migration: `MIGRATION_UDP2TCP.md`
- Technical spec: `TECH_SPEC_NEW_ARCHITECTURE.md`
- Historical reports: `../docs_legacy/`

Maintained until migration complete; then will be converted into a short `Roadmap.md`.
