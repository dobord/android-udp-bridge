#!/bin/bash
set -e

echo "Setting up SSH keys for UDP Bridge..."

# Create SSH keys directory if it doesn't exist
mkdir -p /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys

# Generate SSH key pair for UDP bridge if it doesn't exist
if [ ! -f /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/udp_bridge_key ]; then
    echo "Generating SSH key pair..."
    ssh-keygen -t rsa -b 4096 -f /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/udp_bridge_key -N "" -C "udp-bridge-key"
    echo "SSH key pair generated successfully!"
else
    echo "SSH key pair already exists."
fi

# Create authorized_keys file
if [ ! -f /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/authorized_keys ]; then
    echo "Creating authorized_keys file..."
    cp /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/udp_bridge_key.pub /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/authorized_keys
    echo "authorized_keys file created!"
else
    echo "authorized_keys file already exists."
fi

# Set proper permissions
chmod 600 /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/udp_bridge_key
chmod 644 /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/udp_bridge_key.pub
chmod 644 /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/authorized_keys

echo "SSH setup completed!"
echo ""
echo "Private key: /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/udp_bridge_key"
echo "Public key: /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/udp_bridge_key.pub"
echo "Authorized keys: /workspaces/android-udp-bridge/server-udp-bridge/ssh_keys/authorized_keys"
echo ""
echo "You can now use the private key to connect to the SSH server as 'sshuser'."
