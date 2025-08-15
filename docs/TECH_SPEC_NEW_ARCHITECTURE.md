# Technical Specification: UDP-over-SSH Architecture (udp2tcp Only)

## 1. Architecture Overview

The project has fully migrated to `udp2tcp`. All legacy custom "UDP Bridge" protocol components (listener, protocol header, client table, TCP connection manager) have been removed from the build and codebase (see `MIGRATION_UDP2TCP.md`). This document describes only the active udp2tcp-based architecture. A brief historical appendix is retained for context.

### 1.1 Active architecture
Runtime stack:
- SSH (libssh) establishes a secure TCP transport (local port forward → server)
- `udp2tcp` client embeds into the Android JNI library and encapsulates UDP datagrams into a framed TCP stream (handled entirely inside the upstream library)
- `udp2tcp` server forwards to the target UDP endpoint

Key characteristics:
- No in-process UDP listener / client table logic inside JNI
- Simplified stat collection: Java polls `udp2tcp_get_library_stats()` periodically
- Reduced maintenance surface by reusing an external, actively maintained component

### 1.2 Removed legacy architecture (summary)
Previously: custom framing + client multiplexing layer. Entire stack eliminated; details moved to Appendix A.

## 2. System Components

### 2.1 Android component

Responsibilities:
- Establish SSH session (password / key auth)
- Configure SSH local → remote TCP forward for the udp2tcp server port
- Initialize and start `udp2tcp` client (one or more UDP forward definitions in future)
- Expose lifecycle JNI (start / stop / stats)
- Periodically poll native stats from Java (ScheduledExecutorService)

### 2.2 Future enhancements (planned)
| Area | Planned Improvement |
|------|---------------------|
| Multiple forwards | Support configuring multiple UDP listen → remote destination mappings via JNI/Java |
| TLS | Optional TLS enablement for udp2tcp TCP leg (already build‑enabled via libcoro feature flags) |
| Extended metrics | Surface latency percentiles & error counters when upstream library exports them |
| Reconnect strategy | Structured backoff / retry wrappers if upstream reconnection is delegated to consumer |

### 2.3 Server side
- `udp2tcp` server binary (external project) bound locally behind SSH remote port forward
- Deployment via systemd / container

### 2.3.1 Docker example
See server-udp-bridge directory (retained for container scaffolding) or upstream examples.

## 3. Data Encapsulation

### 3.1 udp2tcp framing
Upstream library applies length-prefix framing over a persistent TCP connection. No application-level multiplexing layer exists in this project; reliability (ordering, loss) handled by TCP. Keepalive delegated to SSH / TCP stack.

### 3.2 Removed legacy protocol
Legacy custom binary protocol (magic bytes "UDPB", client registration, timeout messages) removed — see Appendix A for historical outline.

## 4. Configuration

### 4.1 udp2tcp Java configuration
```java
public class Udp2TcpConfig {
	private String sshHost;
	private int sshPort = 22;
	private String sshUsername;
	private String sshPassword; // or key
	private int localUdpPort = 5060;      // Local UDP listen
	private int remoteUdpPort = 5060;     // Target final UDP port
	private String remoteUdpHost;         // Target host
	private int udp2tcpServerPort = 8080; // udp2tcp server port (TCP) used for SSH forward
}
```

### 4.2 (Removed legacy Android configuration – retained below for contrast)
```java
public class UdpBridgeConfig {
		// SSH settings
		private String sshHost;
		private int sshPort = 22;
		private String sshUsername;
		private String sshPassword;
		private String sshPrivateKey;
    
		// Bridge settings
		private int localUdpPort = 5060;        // Local UDP port
		private int bridgeTcpPort = 8080;       // TCP port on server
		private int clientTimeout = 300;        // Client timeout (sec)
		private int maxClients = 1000;          // Max clients
    
		// Target server (configured on server side)
		// private String targetHost;  // Not needed in Android
		// private int targetPort;     // Configured on server
}
```

### 4.3 UI
UI hides legacy client-timeout/max-client fields. Visible: SSH auth, local UDP port, remote UDP host/port.

## 5. Implementation Status

Completed:
1. Embedded udp2tcp client (C API) in JNI shared library
2. Purged legacy protocol / listener / TCP manager sources & JNI
3. Updated CI workflows to enforce mandatory submodule & remove feature flag
4. Added ScheduledExecutorService polling in service layer for stats

In Progress / Next:
1. Multi-port forward support (adapter extension)
2. Reconnect/backoff policy surface (if not handled upstream)
3. E2E automated test harness (basic echo validation)
4. Performance benchmarks (packet loss, p95 latency)
5. Extended metrics & UI surfacing

### 5.2 File structure (Active)
```
ssh-tunnel-android-app/
	app/src/main/jni/
		ssh_tunnel.c (simplified logic)
		udp2tcp_client_adapter.[ch]
third_party/udp2tcp/ (sources)
```

### Appendix A: Legacy (Removed) Overview
Legacy code (udp_listener, protocol framing, TCP connection manager, server bridge) was replaced by udp2tcp. See repository history prior to the removal commit for full details if forensic review is needed.

## 6. Advantages of udp2tcp
- Less custom code => lower risk of bugs
- No need to maintain own binary protocol and client tables
- Easier debugging (tcpdump, standard tools)
- Enables reuse of udp2tcp in other projects

## 6A Legacy advantages (removed) – see Appendix A

## 7. Compatibility & Migration
Migration complete (documented in `MIGRATION_UDP2TCP.md`). No backward compatibility requirements remain for legacy protocol.

## 8. Monitoring & Metrics
### 8.1 Metrics
Initial exposed stats are aggregate packet/byte counters from udp2tcp library (queried via JNI). Future metrics (latency, errors) will depend on upstream exports or added instrumentation.

This technical specification reflects the finalized udp2tcp-only design.
