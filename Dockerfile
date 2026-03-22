# ─────────────────────────────────────────────────────────────────────────────
# Stage 1: Build
# ─────────────────────────────────────────────────────────────────────────────
FROM --platform=$BUILDPLATFORM alpine:3.20 AS builder

ARG CJSON_VERSION=v1.7.18

# Build tools
RUN apk add --no-cache \
    cmake \
    ninja \
    gcc \
    g++ \
    musl-dev \
    curl-dev \
    sqlite-dev \
    curl-static \
    sqlite-static \
    openssl-dev \
    openssl-libs-static \
    nghttp2-static \
    zlib-static \
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
      -DCMAKE_EXE_LINKER_FLAGS="-static" \
    && cmake --build build --parallel $(nproc)

# Confirm binary is static
RUN file build/tgbot && (ldd build/tgbot 2>&1 || true)

# ─────────────────────────────────────────────────────────────────────────────
# Stage 2: Runtime — scratch image, absolute minimum footprint
# ─────────────────────────────────────────────────────────────────────────────
FROM scratch

COPY --from=builder /build/build/tgbot /tgbot

VOLUME ["/data"]

ENTRYPOINT ["/tgbot"]
