cmake_minimum_required(VERSION 3.16)
project(ps5decrypt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Headers ──
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── Sources ──
set(DECRYPT_SOURCES
    ps5decrypt.cpp
    KeyDatabase.cpp
)

# ── Main executable ──
add_executable(ps5decrypt ${DECRYPT_SOURCES})

target_compile_options(ps5decrypt PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -O2>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /O2>
)

# ── Install ──
install(TARGETS ps5decrypt RUNTIME DESTINATION bin)
