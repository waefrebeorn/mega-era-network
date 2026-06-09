# F01: Docker container for money-room engine
# Multi-stage build: compile C binaries, then minimal runtime image
#
# Build:  docker build -t money-room .
# Run:    docker run --rm -v /tmp/money-room-data:/data money-room
# Compose: docker compose up -d

FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc make pkg-config libcurl4-openssl-dev libjansson-dev libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY engine/ ./engine/
COPY config/ ./config/

# Build all engine binaries
RUN cd engine && make all 2>&1 | tail -5

# --- Runtime stage ---
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 libjansson4 libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/engine/room_engine /usr/local/bin/
COPY --from=builder /build/engine/room_engine_market /usr/local/bin/
COPY --from=builder /build/engine/data_server /usr/local/bin/
COPY --from=builder /build/engine/health_check /usr/local/bin/
COPY --from=builder /build/engine/accuracy_scorer /usr/local/bin/
COPY --from=builder /build/engine/collector_runner /usr/local/bin/
COPY --from=builder /build/engine/cycle_all_rooms /usr/local/bin/
COPY --from=builder /build/engine/room_watchdog /usr/local/bin/
COPY --from=builder /build/engine/resource_monitor /usr/local/bin/
COPY --from=builder /build/engine/cross_source_check /usr/local/bin/
COPY --from=builder /build/engine/ablation_test /usr/local/bin/
COPY --from=builder /build/scripts/ /usr/local/bin/

# Create non-root user
RUN useradd -r -s /bin/false moneyroom
USER moneyroom

# Health check
HEALTHCHECK --interval=60s --timeout=10s --retries=3 \
    CMD health_check || exit 1

ENTRYPOINT ["room_engine"]
