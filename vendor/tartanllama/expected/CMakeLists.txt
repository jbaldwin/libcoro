cmake_minimum_required(VERSION 3.0)
project(expected CXX)

# set(EXPECTED_SOURCE_FILES
#     inc/expected/expected.hpp
# )

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE inc)
