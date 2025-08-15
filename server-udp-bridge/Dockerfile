FROM ubuntu:22.04

# --- Base packages (SSH + build toolchain + cmake for udp2tcp) ---
RUN apt-get update && apt-get install -y \
    openssh-server \
    build-essential \
    cmake \
    ninja-build \
    git \
    supervisor \
    ca-certificates \
    pkg-config \
    libssl-dev \
    openssl \
    iproute2 \
    net-tools \
    && rm -rf /var/lib/apt/lists/*

# --- SSH setup ---
RUN mkdir /var/run/sshd \
 && echo 'root:password' | chpasswd \
 && sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config \
 && sed -i 's/#PasswordAuthentication yes/PasswordAuthentication yes/' /etc/ssh/sshd_config

# Create non-root user for SSH
RUN useradd -m -s /bin/bash sshuser && echo 'sshuser:sshpassword' | chpasswd

# Directories
RUN mkdir -p /opt/udp-bridge/logs /etc/ssh/keys

# Copy legacy configs / scripts (some scripts may still be useful)
COPY server-udp-bridge/config/ /opt/udp-bridge/config/
COPY server-udp-bridge/scripts/ /opt/udp-bridge/scripts/
COPY server-udp-bridge/ssh_keys/ /etc/ssh/keys/

# Copy udp2tcp sources (C++20 coroutine server) from monorepo root
COPY third_party/udp2tcp /opt/udp-bridge/third_party/udp2tcp

WORKDIR /opt/udp-bridge/third_party/udp2tcp

# Build udp2tcp server (static build by default)
# Build udp2tcp server (static build by default). If you want to disable TLS to shrink deps add: -DLIBCORO_FEATURE_TLS=OFF
RUN cmake -S . -B build -D CMAKE_BUILD_TYPE=Release \
    && cmake --build build -j

# Place symlink for convenience into PATH
RUN ln -s /opt/udp-bridge/third_party/udp2tcp/build/udp2tcp_srv /usr/local/bin/udp2tcp_srv

# Supervisor config (updated to launch udp2tcp_srv)
COPY server-udp-bridge/config/supervisord.conf /etc/supervisor/conf.d/supervisord.conf

# Permissions for scripts
RUN chmod +x /opt/udp-bridge/scripts/*.sh || true

# SSH authorized keys (optional)
RUN if [ -f /etc/ssh/keys/authorized_keys ]; then \
        mkdir -p /home/sshuser/.ssh && \
        cp /etc/ssh/keys/authorized_keys /home/sshuser/.ssh/ && \
        chown -R sshuser:sshuser /home/sshuser/.ssh && \
        chmod 700 /home/sshuser/.ssh && \
        chmod 600 /home/sshuser/.ssh/authorized_keys; \
    fi

# Expose SSH (22) and udp2tcp server port (47000) + optional metrics (9108)
EXPOSE 22 47000 9108

WORKDIR /opt/udp-bridge

CMD ["/usr/bin/supervisord", "-c", "/etc/supervisor/conf.d/supervisord.conf"]
