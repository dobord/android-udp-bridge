# Migration to udp2tcp (full document)

## Status
- Current branch: `feature/udp2tcp`
- Goal: Replace custom UDP Bridge protocol & listener with the `udp2tcp` project (git@github.com:dobord/udp2tcp.git) while preserving SSH port forwarding.
- Current progress: minimal "embed" mode of udp2tcp on Android without coroutine scheduler / networking / TLS (`UDP2TCP_EMBED_NO_SCHEDULER`). C API builds; client/server start returns exit_code = -100 (Not Implemented) as a temporary stub.

## Rationale
| Aspect | Legacy UDP Bridge | udp2tcp |
|--------|-------------------|---------|
| Multiplexing | Custom binary protocol w/ header | Stream encapsulation of UDP in TCP (minimal) |
| Code base | ~hundreds of lines JNI + protocol | External reusable code |
| Reliability | Supported & tested inside project | Separate project simplifies maintenance |
| Complexity | Client tables, ping/pong, CRC | Transparent forwarding w/out extra layers |

## Scope of change
1. Removal / deprecation:
	- `udp_listener.c/h`
	- `udp_bridge_protocol.c/h`
	- client manager (JNI part)
	- Docs: mark Legacy (`UDP_BRIDGE_SCHEMA.md`, PHASE_2.x reports, PROTOCOL_IMPLEMENTATION_REPORT.md)
2. Addition:
	- Embed `udp2tcp` client (static library or binary via JNI/Java wrapper)
	- Configuration layer: map local UDP port -> remote target (host:port)
	- Minimal stats (packets/bytes) gathered locally via socket
3. SSH:
	- Retain existing SSH setup workflow (password / key)
	- Use `ssh -L localhost:<bridge_port>:127.0.0.1:<udp2tcp_server_port>` equivalent inside libssh (already forwarding 8080 — retarget to udp2tcp server port)

## Step-by-step plan
### Phase 1 — Preparation (done/ongoing)
- [x] Created `MIGRATION_UDP2TCP.md`
- [x] Updated `README.md` (added link + legacy notes)
- [x] Updated `TECH_SPEC_NEW_ARCHITECTURE.md` (added udp2tcp, marked legacy)

### Phase 2 — udp2tcp client integration
Options:
A. Embed sources as NDK module
B. Build as external library & link
C. Separate process (least preferred)

Chosen: A (sources under `third_party/udp2tcp/` + Android.mk / CMakeLists). If blocked by licensing/structure fallback B.

Tasks:
- [x] Imported udp2tcp code into `third_party/`
- [x] Created wrapper `udp2tcp_client_adapter.c/h` (init, start, stop, stats) — stub + single-client reply
- [x] Replaced JNI methods with adapter calls (start/stop/stats/isRunning)
- [x] Updated Gradle / Android.mk for build (Android.mk done; Gradle later if needed)

### Phase 3 — Replace JNI logic
- [x] In `ssh_tunnel.c` conditionally exclude legacy blocks (`USE_UDP2TCP`) leaving SSH + udp2tcp (added to `CMakeLists.txt`)
- [x] Implemented simple local UDP socket listener inside adapter (stub C version to be removed later)
- [x] Added thread-safe atomic counters (std::atomic in C++ adapter)

### Phase 4 — Cleanup & legacy marking
- [ ] Move legacy files to `legacy/` or delete after successful tests (some docs already marked)
- [ ] Remove `udp2tcp_client_adapter.c` (legacy stub) after confirming C++ adapter works on target ABIs
- [ ] Update CI: skip `udp_listener.c` & protocol checks when `USE_UDP2TCP` active
- [ ] Update `IMPLEMENTATION_PLAN.md` (add migration section)

### Phase 5 — Testing
- [ ] Adapt `run_full_e2e_test.sh` for udp2tcp end-to-end scenario
- [x] Add script `test_udp2tcp_basic.sh` (placeholder)
- [ ] Load test: send 10k UDP packets -> integrity check

### Phase 6 — Documentation final
- [ ] Extend README with "Architecture comparison"
- [ ] Short migration FAQ
- [ ] Update diagrams (remove custom header, simplify diagram)

## Adapter interface (draft)
```c
// udp2tcp_client_adapter.h
int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port);
int udp2tcp_start(void);   // non-blocking background thread start
int udp2tcp_stop(void);
void udp2tcp_get_stats(uint64_t* rx_packets, uint64_t* tx_packets, uint64_t* rx_bytes, uint64_t* tx_bytes);
```

## Risks
| Risk | Mitigation |
|------|-----------|
| API incompatibility (udp2tcp vs Android NDK) | Minimal patch/fork if needed |
| No scheduler during embed phase | Use -100 stub; later enable real client/server paths |
| Performance under high RTT | Enable TCP_NODELAY, tune buffers |
| Loss of ping/pong | Use socket activity stats / optional keepalive |

## Decisions needing confirmation
- Remove legacy code immediately or keep feature flag? (short flag period recommended)
- Statistics format — keep previous JNI interface or simplify
- Strategy: initial MVP without real traffic (embed stub -100), then gradually enable networking (libcoro FEATURE_NETWORKING) and TLS if required.

## Minimal embed mode (Android)
Intermediate step to achieve a green build & JNI integration:

| Aspect | Value |
|--------|-------|
| Macro | `UDP2TCP_EMBED_NO_SCHEDULER` |
| Disabled | `LIBCORO_FEATURE_NETWORKING=OFF`, `LIBCORO_FEATURE_TLS=OFF` |
| Excluded sources | `server_impl.cpp`, `client_impl.cpp` |
| Logging | Synchronous (rewritten `src/common/log.cpp`) |
| C API start functions | Create handle, set `exit_code=-100` |
| Goal | Bring up JNI, stabilize build, then progressively restore functionality |

Exit code `-100` means "not implemented in minimal embed" and is not treated as an infrastructure failure.

### Exit plan from embed mode
1. Enable `LIBCORO_FEATURE_NETWORKING=ON` only after OpenSSL headers/binaries exist for all ABIs.
2. Restore coroutine logger when `<stop_token>` available (future NDK) or add std::jthread compatible layer.
3. Remove `UDP2TCP_EMBED_NO_SCHEDULER`; restore real `run_client` / `run_server` calls.
4. Add e2e UDP encapsulation tests (generate & receive packets) under feature flag.

## Completion criteria
- App successfully forwards UDP via udp2tcp + SSH
- Legacy code removed or isolated; CI no longer references it
- E2E tests pass (green)
- Documentation updated; user does not require knowledge of old protocol

--
Update status as tasks progress.
