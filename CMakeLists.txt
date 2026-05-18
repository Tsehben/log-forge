cmake_minimum_required(VERSION 3.10)
project(LogForge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(LogForge main.cpp LogStore.cpp)
