# UDP Forwarder Implementation

## Overview

UDP Forwarder is a component of the UDP Bridge server responsible for forwarding UDP packets from clients to the target server and back.

## Architecture

### Core components

1. **udp_forwarder_t** - main forwarder structure
2. **Receiver Thread** - thread receiving responses from target server
3. **Send Mutex** - mutex for thread-safe send
4. **Statistics** - runtime statistics collection

### Files

- `src/udp_forwarder.h` - header with definitions
- `src/udp_forwarder.c` - function implementation
- `test_udp_forwarder.c` - test program

## Main functions

### udp_forwarder_create()
Creates a new UDP forwarder instance.

**Parameters:**
- `target_host` - target UDP server host
- `target_port` - target UDP server port  
- `clients` - client table

### udp_forwarder_start()
Starts forwarder threads.

### udp_forwarder_send()
Sends a UDP packet to the target server on behalf of a client.

### udp_forwarder_receiver_thread()
Thread that receives responses from the target server and relays them to clients.

## Implementation details

### Error handling
- Retry mechanism for sends
- Graceful handling of network errors
- Automatic reconnect when needed

### Thread safety
- Mutex for critical sections
- Safe access to client table
- Proper thread shutdown

### Statistics
- Count of sent/received packets
- Count of transferred bytes
- Uptime

## Current limitations

1. **Broadcast responses** - currently responses from the target server are sent to all active clients. Future: map UDP request -> client.

2. **Simple retry logic** - basic retry, could be improved with exponential backoff.

3. **No connection pooling** - each forwarder uses a single UDP socket.

## Testing

### Build
```bash
make udp-forwarder-test
```

### Run tests
```bash
./run_udp_forwarder_test.sh
```

### Manual testing
```bash
# Start UDP echo server
./test_udp_echo_server.sh &

# Run forwarder test
./test_udp_forwarder localhost 5060
```

## Integration

UDP Forwarder integrates with:
- **Client Table** - client management
- **Protocol** - building protocol messages
- **Main Server** - part of main server

## Next steps

1. Implement mapping from UDP requests to specific clients
2. Add connection pooling for multiple target servers
3. Improve retry logic
4. Add performance metrics
5. Integrate into main server (task 1.5)
