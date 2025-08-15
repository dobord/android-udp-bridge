## Implementation Plan (Updated for udp2tcp migration)

Updated 14 Aug 2025. This document reflects the transition from the custom "UDP Bridge" (protocol, listener, client manager, TCP Connection Manager) to using external `udp2tcp` over SSH port forwarding. Historical phases (1–4) are preserved in the Legacy Summary and detailed in reports in `docs_legacy/`.

### TL;DR Current State (Post Legacy Purge)
| Area | Status | Comment |
|------|--------|---------|
| Legacy Phases 1–2 | DONE | Historical (archived in docs_legacy) |
| Legacy Phases 3–4 | DONE | Legacy code fully removed; flag eliminated |
| Migration strategy selection | DONE | Vendored udp2tcp (mandatory) |
| Adapter `udp2tcp_client_adapter` creation | PARTIAL | Basic start/stop, stats passthrough pending enhancement |
| README / Tech Spec updates | DONE | Reflect udp2tcp default path |
| Legacy source cleanup | DONE | Files deleted; build hard-fails if submodule missing |
| E2E udp2tcp test | PENDING | Need real client/server echo path |
| CI update (exclude legacy) | DONE | Workflows enforce submodule + no legacy checks |
| Documentation update (final) | PENDING | FAQ / comparison / latency metrics |

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
Approach A implemented (vendored sources under `third_party/udp2tcp/`).
- [x] Import current udp2tcp commit
- [x] Android CMake integration (mandatory path)
- [x] Minimal source set (client + dependencies)
- [x] Multi-ABI build verification via CI

### 3.3 Adapter (JNI layer)
Interface (current):
```c
int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port);
int udp2tcp_start(void);        // creates RX/TX threads
int udp2tcp_stop(void);
void udp2tcp_get_stats(uint64_t* rx_pkts, uint64_t* tx_pkts, uint64_t* rx_bytes, uint64_t* tx_bytes);
int udp2tcp_is_running(void);
```
- [x] Header / struct scaffolding
- [x] Basic init/start/stop using udp2tcp C API
- [ ] UDP receive loop (local) -> send (`A1`)
- [ ] TCP read loop -> inject into local UDP socket (`A1`)
- [ ] Error handling / reconnect (backoff) (`A1`)
- [ ] Enhanced stats (poll udp2tcp_client_get_stats + deltas) (`A2`)
- [ ] JNI methods + Java wrapper refinement (remove legacy remnants) (`A3`)

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

### 3.6 Legacy Cleanup (Completed)
- [x] Remove all legacy sources & headers
- [x] Eliminate feature flags / auto-detect
- [x] Update workflows to hard-require submodule
- [ ] Archive legacy test scripts list in LEGACY_REMOVAL_CHANGELOG (`C3`)

### 3.7 CI/CD Update
- [x] Drop legacy file checks
- [x] Enforce submodule presence
- [ ] Add E2E udp2tcp smoke test container (`CI2`)
- [ ] Publish udp2tcp test log artifact (`CI3`)

### 3.8 Documentation (Final Round)
- [ ] Update QUICKSTART (udp2tcp steps) (`D1`)
- [ ] Expanded comparison table (`D2`)
- [ ] FAQ (ping/pong removal, stats differences) (`D3`)
- [ ] LEGACY_REMOVAL_CHANGELOG with deleted files + commit hash (`D4`)

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
| Source Import | Import udp2tcp into third_party | S1 | ☑ |
| Build | CMake integration (mandatory) | B1 | ☑ |
| Adapter | TX/RX loop implementation | A1 | ◐ |
| Adapter | Enhanced statistics | A2 | ◐ |
| Adapter | JNI methods / Java wrapper cleanup | A3 | ☐ |
| SSH | Forward binding & teardown | F1 | ☐ |
| Testing | test_udp2tcp_basic.sh (apk symbol check) | T1 | ◐ |
| Testing | E2E integration in run_full_e2e_test.sh | T2 | ☐ |
| Testing | Load test 10k packets | T3 | ☐ |
| Cleanup | Legacy removal (code) | C2 | ☑ |
| Cleanup | Legacy changelog archive | C3 | ☐ |
| CI/CD | Enforce submodule & build | CI1 | ☑ |
| CI/CD | E2E smoke test job | CI2 | ☐ |
| CI/CD | Test log artifact | CI3 | ☐ |
| Docs | QUICKSTART udp2tcp update | D1 | ☐ |
| Docs | FAQ / Comparison table | D2 | ☐ |
| Docs | Legacy removal changelog | D4 | ☐ |
| Release | p95 latency verification | R1 | ☐ |
| Release | No legacy references (grep) | R2 | ☑ |

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
4. Minimal `test_udp2tcp_basic.sh` (T1) — extend to verify symbol presence & basic client start.
5. Manual e2e via SSH port forward (pre-automation).

After validation — parallelize CI (CI1) + cleanup (C1).

---
## 9. References
- Migration: `MIGRATION_UDP2TCP.md`
- Technical spec: `TECH_SPEC_NEW_ARCHITECTURE.md`
- Historical reports: `../docs_legacy/`

Maintained until migration complete; then will be converted into a short `Roadmap.md`.
