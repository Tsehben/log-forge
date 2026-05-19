cmake_minimum_required(VERSION 3.15)
project(LogForge VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Protobuf and gRPC installed via Homebrew
# Using CONFIG for Protobuf prevents Homebrew conflicts with gRPC
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

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

# The Server Executable
add_executable(logforge_server server.cpp)
target_link_libraries(logforge_server logstore logforge_proto)

# The Client Executable
add_executable(logforge_client client.cpp)
target_link_libraries(logforge_client logforge_proto)

# Keeping the local interactive tool from Part 3
add_executable(logforge_local main.cpp)
target_link_libraries(logforge_local logstore)
