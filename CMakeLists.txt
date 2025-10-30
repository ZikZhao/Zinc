cmake_minimum_required(VERSION 3.20)
project(DAG)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-D_GLIBCXX_DEBUG -g3 -O0 -fsanitize=undefined,address -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wno-unused-parameter -Wno-missing-field-initializers")

# Linker flags for debug build
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=undefined,address")

# Find required packages
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

# Include directories
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

# Generate parser files with Bison (must be done first)
BISON_TARGET(Parser
    ${CMAKE_SOURCE_DIR}/parser.y
    ${CMAKE_BINARY_DIR}/out/parser.tab.cpp
    DEFINES_FILE ${CMAKE_BINARY_DIR}/out/parser.tab.hpp
    COMPILE_FLAGS "--verbose"
)

# Generate lexer files with Flex (depends on parser header)
FLEX_TARGET(Lexer
    ${CMAKE_SOURCE_DIR}/lexer.l
    ${CMAKE_BINARY_DIR}/out/lex.yy.cc
    COMPILE_FLAGS "--c++"
)

# Ensure Bison runs before Flex (Flex needs parser.tab.hpp)
ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

# Explicitly add dependencies to ensure proper build order
set_source_files_properties(
    ${FLEX_Lexer_OUTPUTS}
    PROPERTIES
    OBJECT_DEPENDS ${BISON_Parser_OUTPUT_HEADER}
)

# Precompiled header setup
set(PCH_HEADER ${CMAKE_SOURCE_DIR}/pch.hpp)

# Source files
set(SOURCES
    main.cpp
    object.cpp
    exception.cpp
    ast.cpp
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Lexer_OUTPUTS}
)

# Create executable
add_executable(interpreter ${SOURCES})

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