# syntax=docker/dockerfile:1
ARG TARGET_PLATFORM=linux/arm64

# ========================================================
# 1단계: 빌드 스테이지 (x86_64 호스트 환경에서 실행)
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# FFmpeg/OpenSSL 등의 Autotools 빌드를 위해 autoconf, automake, libtool 추가
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    zip \
    unzip \
    nasm \
    tar \
    pkg-config \
    bison \
    python3 \
    autoconf \
    automake \
    libtool \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

# vcpkg 설치 및 환경 설정
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg && \
    /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${PATH}:${VCPKG_ROOT}"

# vcpkg 전용 aarch64(ARM64) 크로스 컴파일 triplet 생성
# 💡 [핵심 해결책] VCPKG_CHOST를 지정해야 FFmpeg/OpenSSL 빌드가 실패하지 않습니다.
RUN echo 'set(VCPKG_TARGET_ARCHITECTURE arm64)' > /opt/vcpkg/triplets/arm64-linux-cross.cmake && \
    echo 'set(VCPKG_CRT_LINKAGE dynamic)' >> /opt/vcpkg/triplets/arm64-linux-cross.cmake && \
    echo 'set(VCPKG_LIBRARY_LINKAGE static)' >> /opt/vcpkg/triplets/arm64-linux-cross.cmake && \
    echo 'set(VCPKG_CMAKE_SYSTEM_NAME Linux)' >> /opt/vcpkg/triplets/arm64-linux-cross.cmake && \
    echo 'set(VCPKG_CHOST aarch64-linux-gnu)' >> /opt/vcpkg/triplets/arm64-linux-cross.cmake && \
    echo 'set(VCPKG_C_COMPILER aarch64-linux-gnu-gcc)' >> /opt/vcpkg/triplets/arm64-linux-cross.cmake && \
    echo 'set(VCPKG_CXX_COMPILER aarch64-linux-gnu-g++)' >> /opt/vcpkg/triplets/arm64-linux-cross.cmake

ENV VCPKG_DEFAULT_TARGET_TRIPLET=arm64-linux-cross
ENV VCPKG_FORCE_SYSTEM_BINARIES=1

# 의존성 패키지 선행 빌드 (vcpkg.json 캐싱 레이어)
WORKDIR /app
COPY vcpkg.json ./
RUN vcpkg install

# 소스 코드 복사 및 CMake 빌드
COPY . .

RUN cmake -B build -S . \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_FIND_ROOT_PATH=/usr/aarch64-linux-gnu \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=arm64-linux-cross \
    -DVCPKG_HOST_TRIPLET=x64-linux \
    -DVCPKG_MANIFEST_MODE=OFF \
    -DVCPKG_INSTALLED_DIR=/app/vcpkg_installed

RUN cmake --build build

# ========================================================
# 2단계: 런타임 스테이지 (최종 라즈베리 파이 ARM64 타겟 환경)
FROM --platform=$TARGET_PLATFORM ubuntu:24.04 AS runtime

WORKDIR /app

# 빌드된 단일 실행 파일만 복사
COPY --from=builder /app/build/RailVMSApp .
RUN chmod +x ./RailVMSApp

ENTRYPOINT ["./RailVMSApp"]