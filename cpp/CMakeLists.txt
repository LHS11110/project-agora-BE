cmake_minimum_required(VERSION 3.16)
project(agora_cpp_server VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find threads and pkg-config
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HTTPLIB REQUIRED cpp-httplib)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jwt-cpp/include
    ${HTTPLIB_INCLUDE_DIRS}
    /usr/include
)

# Build vendored uSockets with CMake. Its upstream Makefile always tries the
# Windows-only lib.exe before falling back to ar, which produces a misleading
# warning on every Linux build.
file(GLOB_RECURSE USOCKETS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets/src/*.c"
)
add_library(usockets STATIC ${USOCKETS_SOURCES})
target_include_directories(usockets PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets/src
)
target_compile_definitions(usockets PRIVATE LIBUS_NO_SSL)
target_compile_options(usockets PRIVATE -O3)

# Source files
file(GLOB_RECURSE CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Executable
add_executable(agora_cpp_server ${CPP_SOURCES})

# Target compile definitions
target_compile_definitions(agora_cpp_server PRIVATE LIBUS_NO_SSL)

# Target link libraries
target_link_libraries(agora_cpp_server
    PRIVATE
    Threads::Threads
    usockets
    z
    ${HTTPLIB_LIBRARIES}
    crypto
    ssl
    sybdb
)

target_compile_options(agora_cpp_server PRIVATE ${HTTPLIB_CFLAGS_OTHER} -Wall -Wextra -O2)
