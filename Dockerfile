# ==========================================
# 1. Builder Stage (크로스 컴파일 환경)
# ==========================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# 필수 빌드 도구 및 ARM64 크로스 컴파일러 설치
RUN apt-get update && apt-get install -y \
    build-essential \
    crossbuild-essential-arm64 \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    cmake \
    ninja-build \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    nasm \
    perl \
    make \
    autoconf \
    automake \
    libtool \
    && rm -rf /var/lib/apt/lists/*

# vcpkg 설치
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg && \
    /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

# ARM64 크로스 컴파일용 커스텀 Triplet 작성
RUN echo 'set(VCPKG_TARGET_ARCHITECTURE arm64)' > /opt/vcpkg/triplets/arm64-linux-cross.cmake && \
    echo 'set(VCPKG_CRT_LINKAGE dynamic)' >> /opt/vcpkg/triplets/arm64-linux-cross.cmake && \
    echo 'set(VCPKG_LIBRARY_LINKAGE dynamic)' >> /opt/vcpkg/triplets/arm64-linux-cross.cmake && \
    echo 'set(VCPKG_CMAKE_SYSTEM_NAME Linux)' >> /opt/vcpkg/triplets/arm64-linux-cross.cmake

WORKDIR /app

# vcpkg 패키지 미리 설치 (Docker 캐시 활용)
COPY vcpkg.json ./
RUN vcpkg install --triplet=arm64-linux-cross

# 프로젝트 전체 소스코드 복사
COPY . .

# CMake 설정 및 빌드 (핵심 탐색 옵션 통합)
RUN cmake -B build -S . \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=arm64-linux-cross \
    -DVCPKG_HOST_TRIPLET=x64-linux \
    -DVCPKG_MANIFEST_MODE=OFF \
    -DCMAKE_FIND_ROOT_PATH="/app/vcpkg_installed/arm64-linux-cross;/usr/aarch64-linux-gnu" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH

RUN cmake --build build

# ==========================================
# 2. Runtime Stage (최종 실행 이미지)
# ==========================================
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# SSL 통신을 위한 인증서 및 기본 런타임 의존성 설치
RUN apt-get update && apt-get install -y \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 빌드 결과물 복사
COPY --from=builder /app/build/RailVMSApp /app/RailVMSApp
RUN chmod +x /app/RailVMSApp

CMD ["./RailVMSApp"]