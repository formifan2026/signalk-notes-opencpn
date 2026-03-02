#!/usr/bin/env bash
set -e

echo "=== Building OpenCPN Plugin for Ubuntu 24.04 (Noble) x86_64 ==="

docker run --rm \
  -v $(pwd):/workspace \
  -w /workspace \
  ubuntu:24.04 \
  bash -c "
    apt-get update &&
    apt-get install -y \
      cmake g++ make git \
      libwxgtk3.2-dev wx-common \
      libglu1-mesa-dev freeglut3-dev mesa-common-dev \
      libcurl4-openssl-dev \
      libtinyxml-dev \
      libarchive-dev \
      zlib1g-dev \
      opencpn-plugin-dev &&

    mkdir -p build &&
    cd build &&
    cmake -DCMAKE_BUILD_TYPE=Release .. &&
    make -j\$(nproc)
  "

echo "=== Build finished ==="
