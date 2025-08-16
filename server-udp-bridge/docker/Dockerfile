# Simple udp2tcp server image
FROM debian:stable-slim
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl iproute2 iputils-ping tcpdump \
    && rm -rf /var/lib/apt/lists/*
# Copy prebuilt server binary at runtime via volume or build arg
# For fallback, allow downloading from local build context path
WORKDIR /app
COPY udp2tcp_srv /usr/local/bin/udp2tcp_srv
COPY udp2tcp_srv.yaml /etc/udp2tcp/udp2tcp_srv.yaml
ENV SRV_CFG=/etc/udp2tcp/udp2tcp_srv.yaml
EXPOSE 38080/tcp
EXPOSE 40000/udp
ENTRYPOINT ["/usr/local/bin/udp2tcp_srv", "/etc/udp2tcp/udp2tcp_srv.yaml"]
