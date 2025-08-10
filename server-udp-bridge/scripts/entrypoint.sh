#!/bin/bash
set -e

echo "Starting UDP Bridge Server..."
echo "Target UDP Host: ${TARGET_UDP_HOST:-127.0.0.1}"
echo "Target UDP Port: ${TARGET_UDP_PORT:-5060}"
echo "Bridge TCP Port: ${BRIDGE_TCP_PORT:-8080}"

# Setup SSH host keys if they don't exist
if [ ! -f /etc/ssh/ssh_host_rsa_key ]; then
    echo "Generating SSH host keys..."
    ssh-keygen -A
fi

# Copy custom SSH config if provided
if [ -f /opt/udp-bridge/config/sshd_config ]; then
    echo "Using custom SSH configuration..."
    cp /opt/udp-bridge/config/sshd_config /etc/ssh/sshd_config
fi

# Setup SSH user
if [ ! -d /home/sshuser/.ssh ]; then
    mkdir -p /home/sshuser/.ssh
    chown sshuser:sshuser /home/sshuser/.ssh
    chmod 700 /home/sshuser/.ssh
fi

# Copy authorized keys if provided
if [ -f /etc/ssh/keys/authorized_keys ]; then
    echo "Setting up SSH key authentication..."
    cp /etc/ssh/keys/authorized_keys /home/sshuser/.ssh/
    chown sshuser:sshuser /home/sshuser/.ssh/authorized_keys
    chmod 600 /home/sshuser/.ssh/authorized_keys
fi

# Create log directory
mkdir -p /opt/udp-bridge/logs
chown root:root /opt/udp-bridge/logs

# Test UDP bridge binary
if [ ! -f /opt/udp-bridge/udp-bridge-server ]; then
    echo "ERROR: UDP bridge server binary not found!"
    exit 1
fi

echo "Testing UDP bridge server..."
/opt/udp-bridge/udp-bridge-server --version || echo "Version check failed (expected for initial build)"

echo "Starting services via supervisor..."
exec /usr/bin/supervisord -c /etc/supervisor/conf.d/supervisord.conf
