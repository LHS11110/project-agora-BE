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
# Keep the epoll pointer fix in this repository rather than changing a nested
# Git submodule, whose dirty files are ignored by the parent repository.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchUSocketsEpoll.cmake")
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

target_compile_options(agora_cpp_server PRIVATE
    ${HTTPLIB_CFLAGS_OTHER}
    -Wall -Wextra -O2 -g -fno-omit-frame-pointer
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Keep application symbols available to the fatal-signal stack trace.
    target_link_options(agora_cpp_server PRIVATE -rdynamic)
endif()

# Keep attribution/license texts with an installed C++ binary. System shared
# libraries are not copied by this install target.
install(TARGETS agora_cpp_server RUNTIME DESTINATION bin)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE
    ${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_LICENSES.md
    DESTINATION share/agora/licenses
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/LICENSE
    DESTINATION share/agora/licenses RENAME uWebSockets-Apache-2.0.txt)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets/LICENSE
    DESTINATION share/agora/licenses RENAME uSockets-Apache-2.0.txt)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jwt-cpp/LICENSE
    DESTINATION share/agora/licenses RENAME jwt-cpp-MIT.txt)
