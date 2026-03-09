cmake_minimum_required(VERSION 3.28)

project(
    scribe
    DESCRIPTION "A C++ Logging Library"
    LANGUAGES CXX
    VERSION 0.1.0
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(
    SCRIBE_BUILD_TESTS
    "Enable building tests. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

add_library(scribe INTERFACE)

target_sources(
    scribe
    PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

set_target_properties(scribe PROPERTIES VERIFY_INTERFACE_HEADER_SETS ON)

add_subdirectory(include/scribe)

if (SCRIBE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
