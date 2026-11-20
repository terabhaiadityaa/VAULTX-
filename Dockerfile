# =========================================================================
# Multi-Stage Production Dockerfile for NOIR ATM Simulation Platform
# C++17 Backend + SQLite Database + Embedded Static Web Frontend
# =========================================================================

# --- STAGE 1: Build Stage ---
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    libsqlite3-dev \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy CMake files and project structure
COPY CMakeLists.txt ./
COPY backend/ ./backend/
COPY database/ ./database/
COPY tests/ ./tests/

# Configure and compile with Ninja
RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENSSL_ROOT_DIR=/usr \
    && ninja -C build atm_backend

# --- STAGE 2: Minimal Runtime Stage ---
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 \
    libssl3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Create persistent storage directory for SQLite database
RUN mkdir -p /data database

# Copy compiled backend binary
COPY --from=builder /app/build/backend/atm_backend /app/atm_backend

# Copy database schema and static frontend assets
COPY database/schema.sql /app/database/schema.sql
COPY frontend /app/frontend

# Environment variables
ENV HOST=0.0.0.0
ENV PORT=8080
ENV DATABASE_PATH=/data/atm.db
ENV FRONTEND_DIR=/app/frontend

VOLUME ["/data"]

EXPOSE 8080

CMD ["/app/atm_backend"]
