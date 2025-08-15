## Implementation Plan (Updated for udp2tcp migration)

Updated 15 Aug 2025. Migration to `udp2tcp` is complete; this plan now tracks post‑migration hardening & follow‑ups. Historical phases (1–4) remain in `docs_legacy/`.

### TL;DR Current State (Post Legacy Purge)
| Area | Status | Comment |
|------|--------|---------|
| Legacy Phases 1–2 | DONE | Historical (archived in docs_legacy) |
| Legacy Phases 3–4 | DONE | Legacy code fully removed; flag eliminated |
| Migration strategy selection | DONE | Vendored udp2tcp (mandatory) |
| Adapter `udp2tcp_client_adapter` creation | DONE | Start/stop, advanced init, library stats |
| README / Tech Spec updates | DONE | Reflect udp2tcp default path |
| Legacy source cleanup | DONE | Files deleted; build hard-fails if submodule missing |
| E2E udp2tcp test | PENDING | Echo validation script not implemented |
| CI update (exclude legacy) | DONE | Workflows enforce submodule + no legacy checks |
| Documentation update (final) | PARTIAL | Tech spec & migration done; FAQ/comparison pending |

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
1. (Done) Integrate `udp2tcp` client (NDK) with minimal glue.
2. (Done) Reuse existing SSH port forwarding as transport.
3. (Done) Provide transparent local UDP port for apps.
4. (In Progress) Extend stats beyond basic frame/byte counters (latency, errors) if upstream exposes.
5. (Done) Remove legacy code & tests.
6. (In Progress) UI refinements / multi-forward support.

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
Interface (current active):
```c
int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port);
int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port);
int udp2tcp_init_advanced(const char* remote_host, int remote_port, int local_udp_port, const char* dst_ip, int dst_port);
int udp2tcp_start(void);
int udp2tcp_stop(void);
void udp2tcp_cleanup(void);
int udp2tcp_get_library_stats(uint64_t* tx_frames, uint64_t* rx_frames, uint64_t* tx_bytes, uint64_t* rx_bytes);
int udp2tcp_is_running(void);
```
- [x] Header / struct scaffolding
- [x] Basic init/start/stop using udp2tcp C API
- [x] JNI methods + Java wrapper refinement (legacy natives removed)
- [ ] Multi-forward context support (`A1-next`)
- [ ] Enhanced stats (latency, errors) when upstream exposes (`A2-next`)
- [ ] Structured JSON stats JNI (optional UI improvement) (`A2b`)

### 3.4 SSH Integration
- [x] Forwarding established (reuses existing logic)
- [ ] Parameterize server port in UI
- [ ] Verify teardown via automated test

### 3.5 Testing
Categories:
1. Unit (adapter, simple local packet transfer).
2. Integration (local udp client -> adapter -> tcp (via ssh) -> udp2tcp server -> echo target).
3. E2E script (shell) for CI.
4. Load (N * 1000 packets + loss measurement).

Tasks:
- [ ] `test_udp2tcp_basic.sh` — implement echo validation
- [ ] Add `--udp2tcp` mode to `run_full_e2e_test.sh`
- [ ] Load scenario (10k packets, avg size 200B, capture latency)
- [ ] Log analysis script (grep udp2tcp_adapter markers)

### 3.6 Legacy Cleanup (Completed)
- [x] Remove all legacy sources & headers
- [x] Eliminate feature flags / auto-detect
- [x] Update workflows to hard-require submodule
- [ ] Archive legacy test scripts list in LEGACY_REMOVAL_CHANGELOG (`C3`)

### 3.7 CI/CD Update
- [x] Drop legacy file checks
- [x] Enforce submodule presence
- [ ] Add E2E udp2tcp smoke test job (`CI2`)
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
| Adapter | Multi-forward support | A1-next | ☐ |
| Adapter | Enhanced statistics | A2-next | ☐ |
| Adapter | JNI methods / Java wrapper cleanup | A3 | ☑ |
| SSH | Forward binding & teardown test | F1 | ☐ |
| Testing | test_udp2tcp_basic.sh (echo) | T1 | ☐ |
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
| Release | Stats extension deployed | R3 | ☐ |

Legend: ☐ not started, ◐ in progress, ☑ done.

---
## 5. Risks & Mitigation (Updated)

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|-----------|
| Upstream API change | Medium | Medium | Pin commit; add compatibility shim |
| Latency regression | Medium | Medium | Benchmark + tune TCP options |
| Adapter resource leak | Low | High | Add stress test + valgrind (host) run |
| Missing richer metrics | Medium | Low | Extend C API / upstream PR |
| Lack of multi-forward | Medium | Medium | Design scalable context list early |

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
- Create `docs/LEGACY_REMOVAL_CHANGELOG.md` with deleted artifacts + final legacy commit SHA.
- README already points to `docs_legacy/`.

---
## 8. Immediate Next Steps (Focus)
1. Implement echo test script (T1)
2. Add CI smoke job (CI2)
3. Draft LEGACY_REMOVAL_CHANGELOG (C3/D4)
4. Expand stats (A2-next) if upstream ready
5. Add multi-forward design note (A1-next)

---
## 9. References
- Migration: `MIGRATION_UDP2TCP.md`
- Technical spec: `TECH_SPEC_NEW_ARCHITECTURE.md`
- Historical reports: `../docs_legacy/`

Maintained until migration complete; then will be converted into a short `Roadmap.md`.
