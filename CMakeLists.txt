cmake_minimum_required(VERSION 3.20)
project(Stainless)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for better IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler flags (using -D_GLIBCXX_DEBUG with matching vcpkg antlr4 triplet)
set(CMAKE_CXX_FLAGS_DEBUG "-D_GLIBCXX_DEBUG -g3 -O0 -fsanitize=undefined,address -fno-omit-frame-pointer")
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
include_directories(${CMAKE_SOURCE_DIR})
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
        ${CMAKE_SOURCE_DIR}/Stainless.g4
    DEPENDS ${CMAKE_SOURCE_DIR}/Stainless.g4
    COMMENT "Generating ANTLR4 parser and lexer from Stainless.g4"
    VERBATIM
)

# Precompiled header setup
set(PCH_HEADER ${CMAKE_SOURCE_DIR}/pch.hpp)

# Source files
set(SOURCES
    main.cpp
    object.cpp
    exception.cpp
    ast.cpp
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