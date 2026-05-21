cmake_minimum_required(VERSION 3.15)
project(LogForge VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Protobuf: prefer CONFIG mode so gRPC's find_dependency(Protobuf CONFIG) does not
# re-define already-existing targets (Homebrew/macOS). Fall back to module mode when
# no config file is present (apt on Debian/Ubuntu).
find_package(Protobuf QUIET CONFIG)
if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()
# gRPC: CONFIG mode required — both apt and Homebrew ship gRPCConfig.cmake
find_package(gRPC CONFIG REQUIRED)
# ZLIB: built-in CMake module — available on macOS (system/Homebrew) and Debian (zlib1g-dev)
find_package(ZLIB REQUIRED)

# Setup proto file
set(PROTO_FILE "proto/logforge.proto")
get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)

# The generated files will be placed in the build directory
set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/logforge.pb.cc")
set(PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/logforge.pb.h")
set(GRPC_SRC "${CMAKE_CURRENT_BINARY_DIR}/logforge.grpc.pb.cc")
set(GRPC_HDR "${CMAKE_CURRENT_BINARY_DIR}/logforge.grpc.pb.h")

# Find the gRPC C++ plugin
find_program(gRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

# Custom command to invoke protoc and generate the C++ code
add_custom_command(
      OUTPUT "${PROTO_SRC}" "${PROTO_HDR}" "${GRPC_SRC}" "${GRPC_HDR}"
      COMMAND protobuf::protoc
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_DIR}"
        --plugin=protoc-gen-grpc="${gRPC_CPP_PLUGIN_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_FILE}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_FILE}")

# Make sure we can include the generated headers
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# Create a library from the generated protobuf and gRPC code
add_library(logforge_proto
    ${PROTO_SRC}
    ${PROTO_HDR}
    ${GRPC_SRC}
    ${GRPC_HDR}
)
target_link_libraries(logforge_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)

# The core LogStore library
add_library(logstore LogStore.cpp)
target_link_libraries(logstore PUBLIC ZLIB::ZLIB)

# The Server Executable
add_executable(logforge_server server.cpp)
target_link_libraries(logforge_server logstore logforge_proto)

# The Client Executable
add_executable(logforge_client client.cpp)
target_link_libraries(logforge_client logforge_proto)

# Keeping the local interactive tool from Part 3
add_executable(logforge_local main.cpp)
target_link_libraries(logforge_local logstore)

# The Benchmark Executable
add_executable(logforge_benchmark benchmarks/logforge_benchmark.cpp)
target_link_libraries(logforge_benchmark PRIVATE logstore)
target_include_directories(logforge_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ---- Unit Tests (GoogleTest) ------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK   OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_executable(logstore_tests tests/logstore_test.cpp)
target_link_libraries(logstore_tests PRIVATE logstore GTest::gtest_main)
target_include_directories(logstore_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(logstore_tests)
