# Migration to udp2tcp (Completed)

## Status
- Branch: `feature/udp2tcp` (ready for merge)
- Goal (Replace custom UDP Bridge protocol & listener with upstream `udp2tcp` over SSH) – ACHIEVED
- Legacy sources & JNI removed from repository and build
- Active build always enables: `USE_UDP2TCP=ON`, `UDP2TCP_ENABLE_C_API=ON`, networking + TLS features

The document now serves as a historical record and post‑migration follow‑up tracker.

## Rationale
| Aspect | Legacy UDP Bridge | udp2tcp |
|--------|-------------------|---------|
| Multiplexing | Custom binary protocol w/ header | Stream encapsulation of UDP in TCP (minimal) |
| Code base | ~hundreds of lines JNI + protocol | External reusable code |
| Reliability | Supported & tested inside project | Separate project simplifies maintenance |
| Complexity | Client tables, ping/pong, CRC | Transparent forwarding w/out extra layers |

## Scope of change (Executed)
1. Removed: `udp_listener.*`, `udp_bridge_protocol.*`, client manager, TCP connection manager, all related JNI & Java entry points
2. Added: Embedded `udp2tcp` client via C API wrapper (`udp2tcp_client_adapter.[ch]/.cpp`) with advanced init variant
3. Simplified stats: single JNI method exposes library aggregate counters (frames/bytes tx/rx) polled from Java scheduler
4. SSH logic retained (port forwarding) – reused for tcp leg of udp2tcp
5. CI: feature flag & legacy dual-path removed; submodule mandatory and validated

## Step-by-step plan
### Phased Execution (Historical Summary)
1. Preparation – docs & submodule wiring (DONE)
2. Client integration – C API adapter + JNI surface (DONE)
3. Dual path w/ feature flag (SHORT) then full removal (DONE)
4. Stats simplification & Java polling (DONE)
5. Legacy file purge & CI cleanup (DONE)
6. Documentation refresh (IN PROGRESS – remaining minor README tweaks)
7. Extended testing & benchmarks (PENDING)

### Remaining Follow-ups
- Update `IMPLEMENTATION_PLAN.md` with final migration synopsis
- Flesh out `test_udp2tcp_basic.sh` into real echo validation + packet counter assertions
- Add high-volume load test (10k+ packets, measure loss & latency)
- README: concise architecture comparison + FAQ
- Optional: multi-forward support & advanced metrics

## Adapter interface (Current)
```c
int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port);
int udp2tcp_init_advanced(const char* remote_host, int remote_port, int local_udp_port,
						  const char* dst_ip, int dst_port);
int udp2tcp_start(void);
int udp2tcp_stop(void);
void udp2tcp_cleanup(void);
int udp2tcp_get_library_stats(uint64_t* tx_frames, uint64_t* rx_frames,
							  uint64_t* tx_bytes, uint64_t* rx_bytes);
int udp2tcp_is_running(void);
```

## Current Risks / Considerations
| Risk | Mitigation |
|------|------------|
| Upstream API changes | Pin submodule commit; periodic sync & regression tests |
| Performance under high RTT | Tune TCP buffers, consider enabling TCP_NODELAY (measure first) |
| Lack of granular latency metrics | Extend C API or add timing hooks around send/recv paths |
| Multi-forward requirement emerges | Generalize adapter: maintain vector of forward contexts |
| TLS handshake latency | Session reuse / persistent SSH forward; optional TLS offload |

## Decisions (Final)
- Legacy code removed (no feature flag maintained)
- Stats format simplified to single poll method returning formatted string (may evolve to structured JSON if UI needs)
- Real networking + TLS enabled directly; skipped prolonged stub phase

## Removed Interim Embed Mode
The temporary "embed no scheduler" mode was superseded by enabling full networking & TLS early. References retained in git history only.

## Completion Criteria (Met)
- UDP forwarding via udp2tcp + SSH operational
- Legacy code purged; CI enforces submodule presence
- Documentation updated (TECH_SPEC migrated to udp2tcp-only)
- User workflows do not expose or require legacy protocol concepts

--
Update status as tasks progress.

## Build Configuration
Always ON: `USE_UDP2TCP`, `UDP2TCP_ENABLE_C_API`, networking & TLS features.

| Scenario | Action |
|----------|--------|
| Standard build | `./gradlew assembleDebug` (fails fast if submodule missing) |
| Fresh clone | `git submodule update --init --recursive` |
| CI | Verifies submodule + runs build & basic test script |

Fast failure prevents accidental silent fallback. Simplifies maintenance and testing effort.
