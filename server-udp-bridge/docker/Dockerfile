# Simple udp2tcp server image
FROM debian:stable-slim
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl iproute2 iputils-ping tcpdump build-essential \
    && rm -rf /var/lib/apt/lists/*
# Copy prebuilt server binary at runtime via volume or build arg
# For fallback, allow downloading from local build context path
WORKDIR /app
COPY udp2tcp_srv /usr/local/bin/udp2tcp_srv
COPY udp2tcp_srv.yaml /etc/udp2tcp/udp2tcp_srv.yaml
COPY udp_echo.c /app/udp_echo.c
RUN gcc -O2 -s -o /usr/local/bin/udp_echo /app/udp_echo.c
ENV SRV_CFG=/etc/udp2tcp/udp2tcp_srv.yaml
EXPOSE 38080/tcp
EXPOSE 40000/udp
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
