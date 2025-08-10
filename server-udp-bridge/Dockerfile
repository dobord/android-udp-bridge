FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    openssh-server \
    build-essential \
    supervisor \
    && rm -rf /var/lib/apt/lists/*

# Setup SSH
RUN mkdir /var/run/sshd
RUN echo 'root:password' | chpasswd
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config
RUN sed -i 's/#PasswordAuthentication yes/PasswordAuthentication yes/' /etc/ssh/sshd_config

# Create user for SSH connections
RUN useradd -m -s /bin/bash sshuser
RUN echo 'sshuser:sshpassword' | chpasswd

# Create directories
RUN mkdir -p /opt/udp-bridge/logs
RUN mkdir -p /etc/ssh/keys

# Copy bridge server code
COPY src/ /opt/udp-bridge/src/
COPY config/ /opt/udp-bridge/config/
COPY scripts/ /opt/udp-bridge/scripts/
COPY ssh_keys/ /etc/ssh/keys/

# Set working directory
WORKDIR /opt/udp-bridge

# Compile bridge server
RUN gcc -o udp-bridge-server \
    src/main.c \
    src/protocol.c \
    src/client_table.c \
    src/udp_forwarder.c \
    -pthread \
    -Wall \
    -Wextra \
    -std=c99

# Copy supervisor config
COPY config/supervisord.conf /etc/supervisor/conf.d/supervisord.conf

# Make scripts executable
RUN chmod +x scripts/*.sh

# Setup SSH keys if provided
RUN if [ -f /etc/ssh/keys/authorized_keys ]; then \
        mkdir -p /home/sshuser/.ssh && \
        cp /etc/ssh/keys/authorized_keys /home/sshuser/.ssh/ && \
        chown -R sshuser:sshuser /home/sshuser/.ssh && \
        chmod 700 /home/sshuser/.ssh && \
        chmod 600 /home/sshuser/.ssh/authorized_keys; \
    fi

# Expose ports
EXPOSE 22 8080

# Start supervisor
CMD ["/usr/bin/supervisord", "-c", "/etc/supervisor/conf.d/supervisord.conf"]
