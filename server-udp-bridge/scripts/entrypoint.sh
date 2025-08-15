#!/bin/bash
set -e

echo "Starting udp2tcp Server (C++ coroutine version)..."
echo "Config: /opt/udp-bridge/udp2tcp_srv.yaml (mounted)"

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

if [ ! -f /usr/local/bin/udp2tcp_srv ]; then
    echo "ERROR: udp2tcp_srv binary not found!"
    ls -l /usr/local/bin
    exit 1
fi

echo "Binary version (if --help prints version):"
/usr/local/bin/udp2tcp_srv --help | head -n 3 || true

echo "Starting services via supervisor..."
exec /usr/bin/supervisord -c /etc/supervisor/conf.d/supervisord.conf
