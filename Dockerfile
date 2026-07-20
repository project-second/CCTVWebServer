# ==========================================
# 1. Builder Stage (모든 호스트 개발 도구 통합)
# ==========================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# vcpkg 및 오픈소스 포트(FFmpeg, OpenSSL, Boost 등)가 요구하는 모든 빌드 도구 일괄 설치
RUN apt-get update && apt-get install -y \
    # [컴파일러 & 기본 도구]
    build-essential \
    crossbuild-essential-arm64 \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    gfortran-aarch64-linux-gnu \
    # [빌드 시스템]
    cmake \
    ninja-build \
    meson \
    make \
    autoconf \
    autoconf-archive \
    automake \
    libtool \
    pkg-config \
    # [스크립트 언어 & 파이썬 생태계 (Meson, OpenSSL 등 필수)]
    python3 \
    python3-pip \
    python3-setuptools \
    python3-wheel \
    python3-jinja2 \
    perl \
    ruby \
    # [어셈블러 & 파서/파서 제너레이터 (FFmpeg, GLib, Vulkan 등 필수)]
    nasm \
    yasm \
    flex \
    bison \
    gperf \
    gettext \
    autopoint \
    texinfo \
    # [유틸리티 & 네트워크]
    git \
    curl \
    wget \
    zip \
    unzip \
    tar \
    ca-certificates \
    # [타겟 커널 헤더]
    linux-libc-dev \
    linux-libc-dev-arm64-cross \
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

# 1. vcpkg.json 복사 후 패키지 미리 빌드 (arm64-linux-cross 전용)
COPY vcpkg.json ./
RUN vcpkg install --triplet=arm64-linux-cross

# 2. 소스코드 전체 복사
COPY . .

# 3. vcpkg pkg-config 및 CMake 모듈 탐색 경로 환경변수 설정
ENV PKG_CONFIG_PATH="/app/vcpkg_installed/arm64-linux-cross/lib/pkgconfig"
ENV CMAKE_PREFIX_PATH="/app/vcpkg_installed/arm64-linux-cross"

# CMake 설정 및 빌드
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
    -DCMAKE_MODULE_PATH="/app/cmake" \
    -DCMAKE_PREFIX_PATH="/app/vcpkg_installed/arm64-linux-cross" \
    -DCMAKE_FIND_ROOT_PATH="/app/vcpkg_installed/arm64-linux-cross;/usr/aarch64-linux-gnu" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH

RUN cmake --build build

# ==========================================
# 2. Runtime Stage (라즈베리 파이용 실행 이미지)
# ==========================================
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# SSL 통신 및 기본 런타임 호환성 패키지
RUN apt-get update && apt-get install -y \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 빌드 결과물 복사
COPY --from=builder /app/build/RailVMSApp /app/RailVMSApp
RUN chmod +x /app/RailVMSApp

CMD ["./RailVMSApp"]