# eta_hsm v2 toolchain: GCC 16 (P2996 reflection) + CMake + Bazel.
#
# Ubuntu 26.04 ships g++-16, the first GCC line with experimental C++26
# reflection (`-std=c++26 -freflection`). This image is the single supported
# toolchain for both the CMake and Bazel builds; see docs/adr/0001.
FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        g++-16 \
        gcc-16 \
        binutils \
        cmake \
        make \
        ninja-build \
        git \
        ca-certificates \
        curl \
        python3 \
        gcovr \
 && rm -rf /var/lib/apt/lists/*

# Make g++-16/gcc-16 the default cc/c++ so plain `cmake`/`bazel` pick them up.
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-16 100 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-16 \
        --slave /usr/bin/cc cc /usr/bin/gcc-16 \
        --slave /usr/bin/c++ c++ /usr/bin/g++-16

# Bazelisk (resolves the version pinned by .bazelversion at run time).
RUN curl -fsSL -o /usr/local/bin/bazel \
        https://github.com/bazelbuild/bazelisk/releases/download/v1.29.0/bazelisk-linux-arm64 \
 && chmod +x /usr/local/bin/bazel

ENV CC=/usr/bin/gcc-16 \
    CXX=/usr/bin/g++-16

WORKDIR /src/workspace
