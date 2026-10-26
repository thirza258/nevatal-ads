# Multi-stage build for C++ Backend
FROM debian:bookworm AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    cmake \
    make \
    libsqlite3-dev \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy CMake and source files
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/
COPY tests/ ./tests/

# Configure and compile
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release -j$(nproc)

# Run tests during build
RUN ./build/adcontrol-tests

# Minimal Runtime Stage
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 \
    libssl3 \
    ca-certificates \
    curl \
    sqlite3 \
    gzip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Create persistent database folder
RUN mkdir -p /data

# Copy built binary from builder
COPY --from=builder /app/build/adcontrol-api /usr/local/bin/adcontrol-api

ENV PORT=8088
ENV HOST=0.0.0.0
ENV DB_PATH=/data/adcontrol.db
ENV SDK_PATH=/app/sdk/adcontrol.js

EXPOSE 8088

HEALTHCHECK --interval=15s --timeout=3s --retries=3 \
    CMD curl -f http://localhost:8088/healthz || exit 1

CMD ["adcontrol-api"]
