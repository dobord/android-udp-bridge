# Technical Specification: New UDP-over-SSH Architecture

## 1. Architecture Overview

> NOTE: This document was updated to reflect the transition to `udp2tcp`. Sections describing the custom "UDP Bridge" protocol are marked Legacy and will be removed after migration completion (see `MIGRATION_UDP2TCP.md`).

### 1.1 Current architecture (udp2tcp)
Current implementation uses:
- SSH (libssh) to establish a secure TCP channel (port forwarding)
- `udp2tcp` protocol/client to encapsulate UDP in a single TCP stream without custom header or client-side multiplexing logic

```
[UDP Client] ⇄ (UDP localhost:<local_udp_port>) ⇄ [udp2tcp client (Android)] ⇄ (TCP via SSH forward) ⇄ [udp2tcp server] ⇄ (UDP) ⇄ [Target UDP Server]
```

Characteristics:
- No internal `udp_listener` / client_table in JNI — simplified
- Statistics collected inside udp2tcp adapter (packets/bytes)
- Reliability & maintenance improvements via external component reuse

### 1.2 Legacy architecture (custom UDP Bridge protocol)
Historically a client-server model with proprietary binary header & multiplexing:
```
[UDP Client] ←→ [Android UDP Bridge] ←→ [SSH TCP Tunnel] ←→ [Server UDP Bridge] ←→ [Target UDP Server]
```
This path included `udp_bridge_protocol.[ch]`, `udp_listener.[ch]`, `client_manager.[ch]`, PING/PONG, CRC32 etc — now marked Legacy.

## 2. System Components

### 2.1 Android UDP Bridge (Current)

Minimal layer:
- SSH connection (password/key auth)
- Local UDP port setup
- Start udp2tcp client forwarding UDP into SSH-forwarded TCP port
- Collect simple statistics

### 2.1L (Legacy) UDP Listener / Protocol Handler / TCP Connection Manager
The following subsections are preserved for historical context and will be removed after migration.

#### 2.1.1L UDP Listener (Legacy)
**Functions:**
- Listen on UDP ports for incoming client packets
- Identify clients by source address & port
- Assign unique `client_id`

**Client data structure:**
```c
typedef struct {
		uint32_t client_id;         // Unique client ID
		struct sockaddr_in addr;    // Client IP address and port
		time_t last_activity;       // Time of last activity
		uint32_t packet_count;      // Packet counter
} udp_client_t;
```

**Flow:**
1. Receive UDP packet
2. Extract sender address (IP:Port)
3. Find/create client entry
4. Assign/get `client_id`
5. Pack protocol message
6. Send via TCP tunnel

#### 2.1.2L Protocol Handler (Legacy)
**Message header:**
```c
typedef struct {
		uint8_t  magic[4];          // "UDPB" - magic bytes
		uint8_t  version;           // Protocol version (1)
		uint8_t  message_type;      // Message type
		uint16_t flags;             // Flags
		uint32_t client_id;         // Client ID
		uint32_t payload_size;      // Payload size
		uint32_t checksum;          // CRC32 of header
} __attribute__((packed)) udp_bridge_header_t;
```

**Message types:**
- `MSG_DATA` (0x01) - UDP packet data
- `MSG_CLIENT_REGISTER` (0x02) - register new client
- `MSG_CLIENT_TIMEOUT` (0x03) - client timeout
- `MSG_PING` (0x04) - connectivity check
- `MSG_PONG` (0x05) - ping reply

#### 2.1.3L TCP Connection Manager (Legacy)
**Functions:**
- Manage SSH TCP connection
- Send protocol messages to server
- Receive responses and route back to UDP clients
- Reconnect on failure

### 2.2 Server Side

#### 2.2.1 Current (udp2tcp server)
- A lightweight udp2tcp server accepts TCP (via SSH forward) and forwards packets to the target UDP endpoint.
- Can be deployed as a standalone service or container next to the target application.

#### 2.2.2L Legacy Server UDP Bridge
(Describes the old server with protocol.c, client_table.c, udp_forwarder.c)

#### 2.2.1 Docker Environment
**Container structure:**
```yaml
# docker-compose.yml
version: '3.8'
services:
	udp-bridge-server:
		build: .
		ports:
			- "22:22"           # SSH port
			- "9999:9999/udp"   # UDP forwarding port (exposed)
		environment:
			- TARGET_UDP_HOST=target-server.example.com
			- TARGET_UDP_PORT=5060
			- BRIDGE_TCP_PORT=8080
		volumes:
			- ./ssh_keys:/etc/ssh/keys:ro
```

#### 2.2.2 TCP Protocol Handler
**Functions:**
- Listen for TCP connections from Android clients
- Parse protocol messages
- Manage active client table
- Route UDP packets

**Server structure:**
```c
typedef struct {
		int tcp_socket;             // TCP socket for Android clients
		int udp_socket;             // UDP socket for the target server
		struct sockaddr_in target;  // Target UDP server address
		pthread_t tcp_thread;       // TCP handling thread
		pthread_t udp_thread;       // UDP response handling thread
		client_table_t* clients;    // Client table
} udp_bridge_server_t;
```

#### 2.2.3 UDP Forwarder
**Functions:**
- Convert protocol messages to UDP packets
- Send UDP packets to target server
- Receive responses from target server
- Map responses back to clients and send

## 3. Interaction Protocol

### 3.1 Current (udp2tcp)
A streaming approach is used: each UDP datagram is placed into the TCP stream with minimal framing logic from the udp2tcp project (no custom "UDPB" magic header). Ping/pong can be handled via standard TCP keepalive or an optional heartbeat (not implemented at migration time).

### 3.2L Legacy protocol

### 3.2.1L Client registration
```
Android → Server: MSG_CLIENT_REGISTER
	client_id: 0 (new client)
	payload: client_address_info

Server → Android: MSG_CLIENT_REGISTER
	client_id: [assigned_id]
	payload: success/error
```

### 3.2.2L Data transfer
```
Android → Server: MSG_DATA
	client_id: [assigned_id]
	payload: [UDP packet data]

Server → Target: UDP packet to target_host:target_port

Target → Server: UDP response

Server → Android: MSG_DATA
	client_id: [assigned_id]
	payload: [UDP response data]

Android → Client: UDP response to original client
```

### 3.2.3L Timeout management
```
Android → Server: MSG_CLIENT_TIMEOUT
	client_id: [expired_id]
	payload: empty

Server: Cleanup client entry
```

## 4. Configuration

### 4.1 Current (udp2tcp)
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

### 4.2L Legacy configuration

### 4.1 Android application
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

### 4.2L Server Bridge (Legacy)
```bash
# Environment variables
UDP_BRIDGE_TCP_PORT=8080
TARGET_UDP_HOST=sip-server.example.com
TARGET_UDP_PORT=5060
CLIENT_TIMEOUT=300
MAX_CLIENTS=1000
LOG_LEVEL=INFO
```

### 4.3 UI (Current)
UI simplified: client timeout/max clients fields hidden (not applicable to udp2tcp); remains SSH + local/remote UDP ports.

### 4.3L User Interface (Legacy)

#### 4.3.1 Main screen
**Interface structure:**
- **Connections list** - main area
- **"+" button** - top of screen to add new configuration
- **ON/OFF switch** - right side of each list item

#### 4.3.2 Connection list item
**Displayed info:**
```
┌─────────────────────────────────────────┬──────┐
│ [Connection Name]                       │ [ON] │
│ ssh://user@server.com:22 → 192.168.1.1 │ OFF  │
│ UDP: 5060 → 5060                        │      │
└─────────────────────────────────────────┴──────┘
```

**Elements:**
- **Connection name** - user-friendly config label
- **SSH connection string** - format "ssh://user@host:port"
- **Target server** - IP or domain of target UDP server
- **UDP ports** - local port → remote port
- **Switch** - enable/disable tunnel

#### 4.3.3 User actions
**Primary actions:**
- **Click "+" button** → open create configuration screen
- **Click list item** → open edit screen
- **ON/OFF switch** → toggle tunnel
- **Long press** → context menu (delete, duplicate, export)

#### 4.3.4 Connection settings screen
**Configuration fields:**
```
┌─────────────────────────────────────────┐
│ Connection Name                         │
│ [Office SIP server                   ]  │
│                                         │
│ SSH Connection                          │
│ Host: [server.company.com            ]  │
│ Port: [22                            ]  │
│ User: [username                      ]  │
│ Password: [••••••••                 ]  │
│ ☐ Use SSH key                          │
│                                         │
│ UDP Settings                            │
│ Local port: [5060                   ]  │
│ Remote host: [192.168.1.100         ]  │
│ Remote port: [5060                  ]  │
│                                         │
│ Additional settings                     │
│ Client timeout: [300] s                │
│ Max clients: [1000                   ]  │
│                                         │
│ [ Save ] [ Connection Test ]           │
└─────────────────────────────────────────┘
```

#### 4.3.5 Status indicators
**Visual indicators:**
- **Green** - tunnel active
- **Red** - connection error
- **Yellow** - connecting
- **Gray** - tunnel disabled

**Additional info:**
- Active client count
- Tunnel uptime
- Packet statistics (sent/received)

## 5. Implementation

### 5.1 Current focus
1. Integrate udp2tcp (NDK module)
2. Remove/encapsulate legacy JNI layers
3. Update CI/CD (exclude legacy file checks)
4. E2E testing with udp2tcp

### 5.2L Legacy plan (historical)

### 5.1 Implementation plan

#### Phase 1: Infrastructure preparation (1-2 days)
1. ✓ Create technical specification
2. Create basic project structure
3. Configure development environment

#### Phase 2: Server UDP Bridge - Basic functionality (3-4 days)
1. Create Docker image with SSH server
2. Implement message protocol (protocol.h/c)
3. Implement client table (client_table.h/c)
4. Basic TCP server accepting connections
5. Basic UDP forwarding

#### Phase 3: Server UDP Bridge - Full functionality (2-3 days)
1. Handle all protocol message types
2. Manage client lifecycle
3. Error handling and reconnection
4. Logging and monitoring

#### Phase 4: Android UDP Bridge - Protocol (2-3 days)
1. Implement protocol structures
2. Serialization/deserialization
3. client_id management
4. Basic TCP client

#### Phase 5: Android UDP Bridge - Integration (2-3 days)
1. Modify UDP listener
2. Integrate with SSH tunnel
3. Update user interface
4. Configuration management

#### Phase 6: Testing and debugging (3-4 days)
1. Unit tests for protocol
2. End-to-end integration tests
3. Performance testing
4. Debugging and optimization

#### Phase 7: Documentation and deployment (1-2 days)
1. Deployment documentation
2. User instructions
3. Final testing
4. Release preparation

**Total implementation time: 14-21 days**

### 5.2 File structure (Target)
```
ssh-tunnel-android-app/
	app/src/main/jni/
		ssh_tunnel.c (simplified logic)
		udp2tcp_client_adapter.[ch]
third_party/udp2tcp/ (sources)
```

### 5.2L File structure (Legacy)

```
server-udp-bridge/
├── Dockerfile
├── docker-compose.yml
├── src/
│   ├── main.c
│   ├── protocol.h
│   ├── protocol.c
│   ├── client_table.h
│   ├── client_table.c
│   ├── udp_forwarder.h
│   └── udp_forwarder.c
├── config/
│   ├── sshd_config
│   └── supervisord.conf
└── scripts/
		├── entrypoint.sh
		└── setup_ssh.sh

ssh-tunnel-android-app/
├── app/src/main/jni/
│   ├── udp_bridge_protocol.h      # New
│   ├── udp_bridge_protocol.c      # New
│   ├── client_manager.h           # New
│   ├── client_manager.c           # New
│   └── ssh_tunnel_bridge.c        # Modified
└── app/src/main/java/
		└── com/example/sshtunnel/
				├── UdpBridgeConfig.java   # New
				└── UdpBridgeService.java  # Modified
```

## 6. Advantages of udp2tcp
- Less custom code => lower risk of bugs
- No need to maintain own binary protocol and client tables
- Easier debugging (tcpdump, standard tools)
- Enables reuse of udp2tcp in other projects

## 6L Legacy advantages (historical)

### 6.1 Performance

### 6.2 Scalability

### 6.3 Reliability

### 6.4 Flexibility

## 7. Compatibility & Migration
Current migration described in `MIGRATION_UDP2TCP.md`.

### 7.1 Backward compatibility

### 7.2L Migration plan (old)
1. Deploy server bridge in test environment
2. Add feature toggle in Android app
3. Test with real users
4. Gradual transition to new architecture
5. Remove old code after stabilization

## 8. Monitoring & Metrics
### 8.1 Server metrics
- Active client count
- UDP packet throughput
- Processing latency
- Protocol errors

### 8.2 Android metrics
- Connection time
- Packet loss rate
- Reconnection frequency
- Memory usage

This technical specification provides the foundation for implementing the new efficient UDP-over-SSH architecture with improved performance and scalability.
