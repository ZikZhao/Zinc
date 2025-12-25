cmake_minimum_required(VERSION 3.20)
project(Stainless)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for better IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(TEST_DIR ${CMAKE_SOURCE_DIR}/test)

# Set build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -fsanitize=undefined,address -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wno-unused-parameter -Wno-missing-field-initializers")

# Linker flags for debug build
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=undefined,address")

# Find required packages
find_package(antlr4-runtime CONFIG REQUIRED)

# Set ANTLR4 JAR location
set(ANTLR4_JAR_LOCATION ${CMAKE_SOURCE_DIR}/tools/antlr-tool.jar)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

# Include directories
include_directories(${SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

# Set output directory for ANTLR4 generated files
set(ANTLR4_OUTPUT_DIR ${CMAKE_BINARY_DIR}/antlr4_generated)

# ANTLR4 generated file names
set(ANTLR4_GENERATED_SRC
    ${ANTLR4_OUTPUT_DIR}/StainlessLexer.cpp
    ${ANTLR4_OUTPUT_DIR}/StainlessParser.cpp
    ${ANTLR4_OUTPUT_DIR}/StainlessBaseListener.cpp
    ${ANTLR4_OUTPUT_DIR}/StainlessListener.cpp
    ${ANTLR4_OUTPUT_DIR}/StainlessBaseVisitor.cpp
    ${ANTLR4_OUTPUT_DIR}/StainlessVisitor.cpp
)

# Generate ANTLR4 parser and lexer using custom command
add_custom_command(
    OUTPUT ${ANTLR4_GENERATED_SRC}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ANTLR4_OUTPUT_DIR}
    COMMAND java -jar ${ANTLR4_JAR_LOCATION}
        -Dlanguage=Cpp
        -o ${ANTLR4_OUTPUT_DIR}
        -visitor
        -listener
        ${SOURCE_DIR}/Stainless.g4
    DEPENDS ${SOURCE_DIR}/Stainless.g4
    COMMENT "Generating ANTLR4 parser and lexer from Stainless.g4"
    VERBATIM
)

# Precompiled header setup
set(PCH_HEADER ${SOURCE_DIR}/pch.hpp)

# Source files
set(SOURCES
    ${SOURCE_DIR}/main.cpp
    ${SOURCE_DIR}/object.cpp
    ${SOURCE_DIR}/exception.cpp
    ${SOURCE_DIR}/ast.cpp
)

# Create a library for ANTLR generated code to suppress warnings
add_library(antlr4_generated_lib STATIC ${ANTLR4_GENERATED_SRC})
target_link_libraries(antlr4_generated_lib PUBLIC antlr4_static)
# Mark ANTLR generated code includes as SYSTEM to suppress warnings
target_include_directories(antlr4_generated_lib SYSTEM PUBLIC ${ANTLR4_OUTPUT_DIR})
# Disable warnings for ANTLR generated code
target_compile_options(antlr4_generated_lib PRIVATE -w)

# Create executable
add_executable(interpreter ${SOURCES})

# Link ANTLR4 generated library and runtime
target_link_libraries(interpreter PRIVATE antlr4_generated_lib)

# Set precompiled header
target_precompile_headers(interpreter PRIVATE ${PCH_HEADER})

# Set target properties
set_target_properties(interpreter PROPERTIES
    OUTPUT_NAME interpreter
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out
)

# Custom target for clean (equivalent to make clean)
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/out
    COMMENT "Cleaning all build artifacts"
)

# Symlink compile_commands.json to a fixed location
if (CMAKE_EXPORT_COMPILE_COMMANDS)
    set(BUILD_ROOT "${CMAKE_SOURCE_DIR}/build")
    file(MAKE_DIRECTORY "${BUILD_ROOT}")
    get_filename_component(REAL_BINARY_DIR "${CMAKE_BINARY_DIR}" ABSOLUTE)
    get_filename_component(REAL_BUILD_ROOT "${BUILD_ROOT}" ABSOLUTE)
    
    if (NOT "${REAL_BINARY_DIR}" STREQUAL "${REAL_BUILD_ROOT}")
        file(CREATE_LINK 
            "${CMAKE_BINARY_DIR}/compile_commands.json" 
            "${BUILD_ROOT}/compile_commands.json" 
            SYMBOLIC
        )
        message(STATUS "Symlinked compile_commands.json to ${BUILD_ROOT}")
    endif()
endif()

# Unit tests
enable_testing()

find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)

add_executable(unit_tests
    ${TEST_DIR}/test_main.cpp
    ${SOURCE_DIR}/object.cpp 
    ${SOURCE_DIR}/exception.cpp
    ${SOURCE_DIR}/ast.cpp
)
add_executable(benchmarks
    ${TEST_DIR}/bench_main.cpp
    ${SOURCE_DIR}/object.cpp 
    ${SOURCE_DIR}/exception.cpp
    ${SOURCE_DIR}/ast.cpp
)

target_link_libraries(unit_tests PRIVATE GTest::gtest_main antlr4_generated_lib antlr4_static)
target_link_libraries(benchmarks PRIVATE benchmark::benchmark antlr4_generated_lib antlr4_static)
target_include_directories(unit_tests PRIVATE ${ANTLR4_OUTPUT_DIR})
target_include_directories(benchmarks PRIVATE ${ANTLR4_OUTPUT_DIR})
target_precompile_headers(unit_tests PRIVATE ${PCH_HEADER})
target_precompile_headers(benchmarks PRIVATE ${PCH_HEADER})

include(GoogleTest)
gtest_discover_tests(unit_tests)