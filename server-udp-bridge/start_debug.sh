#!/bin/bash

# Debug startup script for UDP Bridge Server
# This script starts the server with debug logging and useful defaults

set -e

# Default configuration
DEFAULT_PORT=8080
DEFAULT_TARGET_HOST="localhost"
DEFAULT_TARGET_PORT=5060
DEFAULT_MAX_CLIENTS=100
DEFAULT_TIMEOUT=300

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_usage() {
    cat << EOF
Usage: $0 [options]

Debug startup script for UDP Bridge Server

Options:
    -p, --port <port>          TCP listen port (default: $DEFAULT_PORT)
    -t, --target <host:port>   Target UDP server (default: $DEFAULT_TARGET_HOST:$DEFAULT_TARGET_PORT)
    -m, --max-clients <num>    Maximum clients (default: $DEFAULT_MAX_CLIENTS)
    -T, --timeout <seconds>    Client timeout (default: $DEFAULT_TIMEOUT)
    -d, --daemon               Run as daemon (background)
    -v, --verbose              Verbose logging
    -h, --help                 Show this help

Environment variables:
    BRIDGE_TCP_PORT           Override TCP listen port
    TARGET_UDP_HOST           Override target UDP host
    TARGET_UDP_PORT           Override target UDP port
    MAX_CLIENTS               Override max clients
    CLIENT_TIMEOUT            Override client timeout

Examples:
    $0                                    # Start with defaults
    $0 -p 9090 -t example.com:5060      # Custom port and target
    $0 -d -v                             # Run as daemon with verbose logging
    
EOF
}

# Parse command line arguments
PORT=$DEFAULT_PORT
TARGET_HOST=$DEFAULT_TARGET_HOST
TARGET_PORT=$DEFAULT_TARGET_PORT
MAX_CLIENTS=$DEFAULT_MAX_CLIENTS
TIMEOUT=$DEFAULT_TIMEOUT
DAEMON=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -t|--target)
            if [[ "$2" =~ ^([^:]+):([0-9]+)$ ]]; then
                TARGET_HOST="${BASH_REMATCH[1]}"
                TARGET_PORT="${BASH_REMATCH[2]}"
            else
                log_error "Invalid target format. Use host:port"
                exit 1
            fi
            shift 2
            ;;
        -m|--max-clients)
            MAX_CLIENTS="$2"
            shift 2
            ;;
        -T|--timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        -d|--daemon)
            DAEMON=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Check if server binary exists
SERVER_BINARY="./udp-bridge-server"
if [ ! -f "$SERVER_BINARY" ]; then
    log_warn "Server binary not found at $SERVER_BINARY"
    
    # Try to build it
    if [ -f "Makefile" ]; then
        log_info "Attempting to build server..."
        if make server; then
            log_success "Server built successfully"
        else
            log_error "Failed to build server"
            exit 1
        fi
    else
        log_error "Server binary not found and no Makefile available"
        log_info "Please build the server first with: make server"
        exit 1
    fi
fi

# Set environment variables
export BRIDGE_TCP_PORT=$PORT
export TARGET_UDP_HOST=$TARGET_HOST
export TARGET_UDP_PORT=$TARGET_PORT
export MAX_CLIENTS=$MAX_CLIENTS
export CLIENT_TIMEOUT=$TIMEOUT

if [ "$VERBOSE" = true ]; then
    export LOG_LEVEL=DEBUG
fi

# Create logs directory
mkdir -p logs

# Print configuration
echo "=== UDP Bridge Server Debug Startup ==="
echo "Configuration:"
echo "  Listen Port: $PORT"
echo "  Target: $TARGET_HOST:$TARGET_PORT"
echo "  Max Clients: $MAX_CLIENTS"
echo "  Timeout: $TIMEOUT seconds"
echo "  Daemon Mode: $DAEMON"
echo "  Verbose: $VERBOSE"
echo "  Working Directory: $(pwd)"
echo "  Binary: $SERVER_BINARY"
echo "========================================="
echo ""

# Check if port is already in use
if netstat -ln 2>/dev/null | grep -q ":$PORT "; then
    log_warn "Port $PORT appears to be in use"
    log_info "You can check what's using it with: lsof -i :$PORT"
    echo ""
fi

# Start server
log_info "Starting UDP Bridge Server..."

if [ "$DAEMON" = true ]; then
    # Run as daemon
    nohup $SERVER_BINARY -p $PORT -t $TARGET_HOST:$TARGET_PORT -m $MAX_CLIENTS -T $TIMEOUT > logs/server.log 2>&1 &
    SERVER_PID=$!
    echo $SERVER_PID > logs/server.pid
    
    log_success "Server started as daemon with PID $SERVER_PID"
    log_info "Logs: tail -f logs/server.log"
    log_info "Stop: kill $SERVER_PID or kill \$(cat logs/server.pid)"
else
    # Run in foreground
    log_info "Starting server in foreground (Ctrl+C to stop)..."
    exec $SERVER_BINARY -p $PORT -t $TARGET_HOST:$TARGET_PORT -m $MAX_CLIENTS -T $TIMEOUT
fi
