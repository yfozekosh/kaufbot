# ─────────────────────────────────────────────────────────────────────────────
# Stage 1: Build
# ─────────────────────────────────────────────────────────────────────────────
FROM alpine:3.20 AS builder

ARG CJSON_VERSION=v1.7.18

# Build tools and libraries
RUN apk add --no-cache \
    cmake \
    ninja \
    gcc \
    g++ \
    musl-dev \
    curl-dev \
    sqlite-dev \
    wget

WORKDIR /build

# Download cJSON at build time — no vendoring needed
RUN mkdir -p /cjson && \
    wget -q "https://raw.githubusercontent.com/DaveGamble/cJSON/${CJSON_VERSION}/cJSON.h" -O /cjson/cJSON.h && \
    wget -q "https://raw.githubusercontent.com/DaveGamble/cJSON/${CJSON_VERSION}/cJSON.c" -O /cjson/cJSON.c && \
    echo "cJSON ${CJSON_VERSION} downloaded OK"

# Copy source
COPY CMakeLists.txt .
COPY src/            src/

# Configure & build — point CMake at the downloaded cJSON
RUN cmake -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCJSON_DIR=/cjson \
    && cmake --build build --parallel $(nproc)

# ─────────────────────────────────────────────────────────────────────────────
# Stage 2: Runtime — minimal Alpine image
# ─────────────────────────────────────────────────────────────────────────────
FROM alpine:3.20

RUN apk add --no-cache \
    libcurl \
    sqlite-libs \
    ca-certificates

COPY --from=builder /build/build/tgbot /usr/local/bin/tgbot

VOLUME ["/data"]

ENTRYPOINT ["tgbot"]
