FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY CMakeLists.txt .
COPY proto/ proto/
COPY *.h *.cpp ./

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target logforge_server --target logforge_client -j$(nproc)
