# ==============================================================================
# Multi-Stage Build for Mermai Blockchain Node & CLI
# ==============================================================================

# Build stage
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libssl-dev \
    libsqlite3-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source tree
COPY . .

# Configure and compile in Release mode
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release -j$(nproc)

# Final minimal runtime stage
FROM ubuntu:22.04 AS runtime

RUN apt-get update && apt-get install -y \
    libssl3 \
    libsqlite3-0 \
    ca-certificates \
    curl \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /mermai

# Copy compiled binaries
COPY --from=builder /app/build/mermai-node /usr/local/bin/mermai-node
COPY --from=builder /app/build/mermai-cli /usr/local/bin/mermai-cli

# Copy genesis config and scripts
COPY scripts /mermai/scripts
COPY mermai_explorer /mermai/mermai_explorer

EXPOSE 6400 7400 8080

ENTRYPOINT ["mermai-node"]
CMD ["--port", "6400", "--rpc-port", "7400", "--db", "/data/mermai.db"]
