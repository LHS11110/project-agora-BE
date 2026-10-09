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
    ${HTTPLIB_INCLUDE_DIRS}
    /usr/include
)

file(GLOB_RECURSE USOCKETS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets/src/*.c")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchUSocketsEpoll.cmake")
add_library(usockets STATIC ${USOCKETS_SOURCES})
target_include_directories(usockets PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets/src)
target_sources(usockets PRIVATE third_party/uWebSockets/uSockets/src/crypto/sni_tree.cpp)
target_compile_definitions(usockets PUBLIC LIBUS_USE_OPENSSL)
target_link_libraries(usockets PUBLIC ssl crypto)

# Layers compile independently. Application services never compile storage
# driver implementation or protocol adapters into their own source units.
add_library(agora_observability STATIC src/ElasticsearchBulkLogBuffer.cpp)
target_link_libraries(agora_observability PUBLIC Threads::Threads ${HTTPLIB_LIBRARIES} crypto ssl)

file(GLOB MEMORY_SOURCES CONFIGURE_DEPENDS src/memory/*.cpp)
add_library(agora_memory STATIC ${MEMORY_SOURCES})
target_link_libraries(agora_memory PUBLIC agora_observability Threads::Threads ${HTTPLIB_LIBRARIES} crypto ssl sybdb)

file(GLOB SERVICE_MEMORY_SOURCES CONFIGURE_DEPENDS src/service_memory/*.cpp)
add_library(agora_service_memory STATIC ${SERVICE_MEMORY_SOURCES} src/Canvas.cpp src/CanvasPassword.cpp)
target_link_libraries(agora_service_memory PUBLIC agora_memory)

file(GLOB API_SOURCES CONFIGURE_DEPENDS src/api/*.cpp)
add_library(agora_api STATIC ${API_SOURCES})
target_link_libraries(agora_api PUBLIC usockets agora_observability ${HTTPLIB_LIBRARIES} z crypto ssl)

file(GLOB SERVICE_SOURCES CONFIGURE_DEPENDS src/service/*.cpp)
add_library(agora_service STATIC ${SERVICE_SOURCES})
target_link_libraries(agora_service PUBLIC agora_api agora_service_memory)

add_executable(agora_cpp_server src/main.cpp)
target_link_libraries(agora_cpp_server PRIVATE agora_service)
target_compile_definitions(agora_observability PRIVATE AGORA_BUILD_VERSION=\"${PROJECT_VERSION}\")
foreach(target agora_observability agora_memory agora_service_memory agora_api agora_service agora_cpp_server)
    target_compile_options(${target} PRIVATE ${HTTPLIB_CFLAGS_OTHER} -Wall -Wextra -O2 -g -fno-omit-frame-pointer)
endforeach()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
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

# First-party unit and local integration tests. The integration test runs a
# fake Redis Sentinel/primary pair on loopback and needs no external services.
include(CTest)
if(BUILD_TESTING)
    add_executable(agora_cpp_storage_broker_fixture tests/storage_broker_fixture.cpp)
    target_link_libraries(agora_cpp_storage_broker_fixture PRIVATE agora_memory)
    target_compile_options(agora_cpp_storage_broker_fixture PRIVATE ${HTTPLIB_CFLAGS_OTHER})

    add_executable(agora_cpp_broker_socket_fixture tests/broker_socket_fixture.cpp)
    target_link_libraries(agora_cpp_broker_socket_fixture PRIVATE agora_service)
    add_executable(agora_cpp_query_tests tests/query_tests.cpp)
    add_test(NAME agora_cpp_query_tests COMMAND agora_cpp_query_tests)
    add_executable(agora_cpp_host_tests tests/canvas_host_test.cpp)
    target_compile_options(agora_cpp_host_tests PRIVATE -Wall -Wextra -O2)
    add_test(NAME agora_cpp_host_tests COMMAND agora_cpp_host_tests)

    add_executable(agora_cpp_unit_tests tests/unit_tests.cpp)
    target_link_libraries(agora_cpp_unit_tests PRIVATE agora_service_memory)
    add_test(NAME agora_cpp_unit_tests COMMAND agora_cpp_unit_tests)

    add_executable(agora_cpp_generic_memory_tests tests/generic_memory_tests.cpp)
    target_link_libraries(agora_cpp_generic_memory_tests PRIVATE agora_memory)
    add_test(NAME agora_cpp_generic_memory_tests COMMAND agora_cpp_generic_memory_tests)

    add_executable(agora_cpp_memory_tests tests/canvas_memory_tests.cpp)
    target_link_libraries(agora_cpp_memory_tests PRIVATE agora_service_memory)
    add_test(NAME agora_cpp_memory_tests COMMAND agora_cpp_memory_tests)

    add_executable(agora_cpp_redis_failover_integration tests/redis_failover_integration.cpp)
    target_link_libraries(agora_cpp_redis_failover_integration PRIVATE agora_service_memory)
    add_test(NAME agora_cpp_redis_failover_integration COMMAND agora_cpp_redis_failover_integration)
    set_tests_properties(agora_cpp_redis_failover_integration PROPERTIES TIMEOUT 20)

    add_executable(agora_cpp_layer_tests tests/layer_tests.cpp)
    target_link_libraries(agora_cpp_layer_tests PRIVATE agora_service_memory)
    add_test(NAME agora_cpp_layer_tests COMMAND agora_cpp_layer_tests)
    add_test(NAME agora_cpp_layer_dependencies
        COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_layers.cmake)
endif()
