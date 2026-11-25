# =========================================================================
# Multi-Stage Production Dockerfile for VAULTX Banking Simulation Platform
# C++17 Backend + Supabase PostgreSQL (libpqxx) + Embedded Static Web Frontend
# =========================================================================

# --- STAGE 1: Build Stage ---
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libpq-dev \
    libpqxx-dev \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && ninja -C build atm_backend

# --- STAGE 2: Minimal Hardened Runtime Stage (Non-Root) ---
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    libpqxx-6.4 \
    libssl3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system vaultx \
    && useradd --system --gid vaultx --home-dir /app --no-create-home --shell /usr/sbin/nologin vaultx

WORKDIR /app

COPY --from=builder --chown=vaultx:vaultx /app/build/backend/atm_backend /app/atm_backend
COPY --chown=vaultx:vaultx frontend /app/frontend

RUN chmod 0755 /app/atm_backend \
    && chmod -R u=rwX,g=rX,o= /app/frontend \
    && chown -R vaultx:vaultx /app

ENV HOST=0.0.0.0
ENV PORT=8080
ENV FRONTEND_DIR=/app/frontend

USER vaultx

EXPOSE 8080

CMD ["/app/atm_backend"]
