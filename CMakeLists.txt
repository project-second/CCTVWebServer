cmake_minimum_required(VERSION 3.25)
project(RailVMSApp LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# 외부 설정 및 의존성 불러오기
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

# 실행 파일 생성
add_executable(RailVMSApp
    src/main.cpp
    src/core/Config.cpp
    src/core/CryptoHelper.cpp
    src/core/Database.cpp
    src/core/HttpServer.cpp
    src/core/ReplyHandlers.cpp
    src/core/StreamRelayer.cpp
    src/core/WebSocketSession.cpp
)

# 타겟 설정 (타겟 생성 후에는 안전하게 링크 가능)
target_link_libraries(RailVMSApp PRIVATE
    Boost::headers
    Boost::system
    Boost::json
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    unofficial::sqlite3::sqlite3
    OpenSSL::SSL
    OpenSSL::Crypto
)
# FFmpeg 헤더 및 라이브러리 경로/파일 연결
if(FFMPEG_FOUND)
    target_include_directories(RailVMSApp PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(RailVMSApp PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(RailVMSApp PRIVATE ${FFMPEG_LIBRARIES})
endif()

