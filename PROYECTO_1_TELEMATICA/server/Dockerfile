# ──────────────────────────────────────────────────────────
#  Dockerfile — IoT Equipment Monitoring Server
#  Multi-stage build: compile in builder, run in slim image
# ──────────────────────────────────────────────────────────

# ── Stage 1: Builder ──
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    make \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN make clean && make

# ── Stage 2: Runtime ──
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libc6 \
    && rm -rf /var/lib/apt/lists/*

# Non-root user for security
RUN useradd -m -s /bin/bash iotserver

WORKDIR /app

# Copy only the compiled binary from builder
COPY --from=builder /src/server /app/server

# Create logs directory with correct permissions
RUN mkdir -p /app/logs && chown -R iotserver:iotserver /app

USER iotserver

EXPOSE 9000

VOLUME ["/app/logs"]

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD nc -z localhost 9000 || exit 1

ENTRYPOINT ["./server"]
CMD ["9000", "/app/logs/server.log"]