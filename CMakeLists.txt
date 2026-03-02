cmake_minimum_required(VERSION 3.24)
project(Zinc)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

set(SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(TEST_DIR ${CMAKE_SOURCE_DIR}/tests)
set(ANTLR4_JAR_LOCATION ${CMAKE_SOURCE_DIR}/tools/antlr-tool.jar)
set(ANTLR4_OUTPUT_DIR ${CMAKE_BINARY_DIR}/antlr4_generated)

find_package(antlr4-runtime CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wno-unused-parameter -Wno-missing-field-initializers)


if(CMAKE_BUILD_TYPE MATCHES "Debug")
    message(STATUS "Configuring Debug build with Sanitizers...")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
else()
    message(STATUS "Configuring Release build...")
    add_compile_options(-DNDEBUG)
endif()

# ANTLR4 source generation
set(ANTLR4_GENERATED_SRC
    ${ANTLR4_OUTPUT_DIR}/ZincLexer.cpp
    ${ANTLR4_OUTPUT_DIR}/ZincParser.cpp
    ${ANTLR4_OUTPUT_DIR}/ZincBaseListener.cpp
    ${ANTLR4_OUTPUT_DIR}/ZincListener.cpp
    ${ANTLR4_OUTPUT_DIR}/ZincBaseVisitor.cpp
    ${ANTLR4_OUTPUT_DIR}/ZincVisitor.cpp
)

add_custom_command(
    OUTPUT ${ANTLR4_GENERATED_SRC}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ANTLR4_OUTPUT_DIR}
    COMMAND java -jar ${ANTLR4_JAR_LOCATION}
        -Dlanguage=Cpp
        -o ${ANTLR4_OUTPUT_DIR}
        -visitor
        -listener
        ${SOURCE_DIR}/Zinc.g4
    DEPENDS ${SOURCE_DIR}/Zinc.g4
    COMMENT "Generating ANTLR4 parser and lexer from Zinc.g4"
    VERBATIM
)


add_library(antlr4_generated_lib STATIC ${ANTLR4_GENERATED_SRC})
target_link_libraries(antlr4_generated_lib PUBLIC antlr4_static)
target_include_directories(antlr4_generated_lib SYSTEM PUBLIC ${ANTLR4_OUTPUT_DIR})
target_compile_options(antlr4_generated_lib PRIVATE -w)

# Runtime embedding generation
set(RUNTIME_HPP_SRC "${SOURCE_DIR}/runtime.hpp")
set(STD_D_ZN_SRC "${SOURCE_DIR}/std.d.zn")
set(CONVERTER_PY "${CMAKE_SOURCE_DIR}/tools/runtime-converter.py")
set(RUNTIME_HPP_STR "${CMAKE_BINARY_DIR}/runtime-str.hpp")

add_custom_command(
    OUTPUT ${RUNTIME_HPP_STR}
    COMMAND ${Python3_EXECUTABLE} ${CONVERTER_PY} ${RUNTIME_HPP_STR} ${RUNTIME_HPP_SRC} ${STD_D_ZN_SRC}
    DEPENDS ${RUNTIME_HPP_SRC} ${STD_D_ZN_SRC}
    COMMENT "Embedding runtime.hpp and std.d.zn into binary..."
    VERBATIM
)

# Precompiled header
set(PCH_HEADER ${SOURCE_DIR}/pch.hpp)

# Main executable
set(SRC_MAIN ${SOURCE_DIR}/main.cpp)
file(GLOB SRC_HEADERS ${SOURCE_DIR}/*.hpp)

add_executable(zinc ${SRC_MAIN} ${RUNTIME_HPP_STR})
target_link_libraries(zinc PRIVATE antlr4_generated_lib)
target_include_directories(zinc PRIVATE ${SOURCE_DIR} ${CMAKE_BINARY_DIR})
target_precompile_headers(zinc PRIVATE ${PCH_HEADER})

set_target_properties(zinc PROPERTIES
    OUTPUT_NAME zinc
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Google Test
enable_testing()
file(GLOB TEST_SOURCES ${TEST_DIR}/*_test.cpp)

if(TEST_SOURCES)
    add_executable(unit_tests 
        ${TEST_SOURCES}
        ${SRC_HEADERS}
        ${RUNTIME_HPP_STR}
    )
    
    target_link_libraries(unit_tests PRIVATE GTest::gtest_main antlr4_generated_lib)
    target_include_directories(unit_tests PRIVATE ${SOURCE_DIR} ${CMAKE_BINARY_DIR} ${ANTLR4_OUTPUT_DIR})
    target_precompile_headers(unit_tests PRIVATE ${PCH_HEADER})
    target_compile_options(unit_tests PRIVATE -fno-access-control)

    set_target_properties(unit_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    include(GoogleTest)
    gtest_discover_tests(unit_tests)
    message(STATUS "Unit tests configured: ${TEST_SOURCES}")
endif()

# Benchmarks
file(GLOB BENCH_SOURCES ${TEST_DIR}/*_bench.cpp)

if(BENCH_SOURCES)
    add_executable(benchmarks
        ${BENCH_SOURCES}
        ${SRC_HEADERS}
        ${RUNTIME_HPP_STR}
    )
    
    target_link_libraries(benchmarks PRIVATE benchmark::benchmark antlr4_generated_lib)
    target_include_directories(benchmarks PRIVATE ${SOURCE_DIR} ${CMAKE_BINARY_DIR} ${ANTLR4_OUTPUT_DIR})
    target_precompile_headers(benchmarks PRIVATE ${PCH_HEADER})
    
    set_target_properties(benchmarks PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    
    message(STATUS "Benchmarks configured: ${BENCH_SOURCES}")
endif()



# Clean target to remove all build artifacts
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/bin
    COMMENT "Cleaning all build artifacts"
)