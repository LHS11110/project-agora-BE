cmake_minimum_required(VERSION 3.16)
project(agora_cpp_server VERSION 1.0.0 LANGUAGES CXX)

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
    ${HTTPLIB_INCLUDE_DIRS}
    /usr/include
)

# Custom target to ensure uSockets.a is built
add_custom_target(usockets_build
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets default
    BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets/uSockets.a
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets
)

# Source files
file(GLOB_RECURSE CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Executable
add_executable(agora_cpp_server ${CPP_SOURCES})
add_dependencies(agora_cpp_server usockets_build)

# Target compile definitions
target_compile_definitions(agora_cpp_server PRIVATE LIBUS_NO_SSL)

# Target link libraries
target_link_libraries(agora_cpp_server
    PRIVATE
    Threads::Threads
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets/uSockets.a
    z
    ${HTTPLIB_LIBRARIES}
    crypto
    ssl
    sybdb
)

target_compile_options(agora_cpp_server PRIVATE ${HTTPLIB_CFLAGS_OTHER} -Wall -Wextra -O2)
