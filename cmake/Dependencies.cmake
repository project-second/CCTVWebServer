# C++ 표준 설정
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS system json)
find_package(OpenSSL REQUIRED)

