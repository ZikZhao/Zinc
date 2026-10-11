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
set(COMPILE_COMMANDS_COPY ${CMAKE_SOURCE_DIR}/build/compile_commands.json)

find_package(antlr4-runtime CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)

if(TARGET mimalloc-static)
    set(MIMALLOC_TARGET mimalloc-static)
else()
    set(MIMALLOC_TARGET mimalloc)
endif()

add_compile_options(-stdlib=libstdc++)
add_link_options(-stdlib=libstdc++)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wno-unused-parameter -Wno-missing-field-initializers)


# ASan 配置开关。默认跟随 Debug，可用 -DZINC_ASAN=ON/OFF 显式覆盖。
option(ZINC_ASAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" AUTO)
if(ZINC_ASAN STREQUAL "AUTO")
    if(CMAKE_BUILD_TYPE MATCHES "Debug")
        set(ZINC_ASAN ON)
    else()
        set(ZINC_ASAN OFF)
    endif()
endif()

if(ZINC_ASAN)
    message(STATUS "Configuring build with Sanitizers (ASan + UBSan)...")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
else()
    message(STATUS "Configuring Release build...")
    add_compile_options(-DNDEBUG)
endif()

# ── mimalloc 的 ASan 支持 ────────────────────────────────────────────────
# mimalloc 的 MI_TRACK_ASAN 是「构建 mimalloc 自身」时的 CMake 选项：它让
# mimalloc 在分配/释放时调用 ASAN_POISON/UNPOISON_MEMORY_REGION。
# 在项目里 -DMI_TRACK_ASAN=1 是无效的——公开头 mimalloc.h 并不包含定义这些
# 宏的 mimalloc/track.h，消费方的 TU 里定义它不会改变已编译好的库。
#
# 因此 ASan 配置必须使用启用了该选项的 vcpkg triplet：x64-linux-asan
# （见 $VCPKG_ROOT/triplets/x64-linux-asan.cmake，它通过
#  VCPKG_CMAKE_CONFIGURE_OPTIONS 把 -DMI_TRACK_ASAN=ON 透传给 port）。
# 该 triplet 同时给所有 CMake 依赖加上 -fsanitize=address,undefined。
if(ZINC_ASAN)
    if(NOT DEFINED VCPKG_TARGET_TRIPLET)
        message(WARNING
            "ZINC_ASAN=ON 但 VCPKG_TARGET_TRIPLET 未设置："
            "链接到的 mimalloc 很可能没有 MI_TRACK_ASAN 支持，"
            "ASan 会因 mimalloc 的 fork/大块分配回收而漏报。"
            "请使用 debug-asan 预设（triplet = x64-linux-asan）。")
    elseif(NOT VCPKG_TARGET_TRIPLET MATCHES "asan")
        message(WARNING
            "ZINC_ASAN=ON 但 vcpkg triplet 是 '${VCPKG_TARGET_TRIPLET}'（不含 asan）。"
            "该 triplet 下的 mimalloc 不带 MI_TRACK_ASAN，"
            "请改用 debug-asan 预设或 -DVCPKG_TARGET_TRIPLET=x64-linux-asan。")
    else()
        message(STATUS "mimalloc ASan 追踪: 由 triplet '${VCPKG_TARGET_TRIPLET}' 提供 "
                       "(MI_TRACK_ASAN=ON)")
    endif()
endif()

add_custom_target(sync_compile_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/build
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${COMPILE_COMMANDS_COPY}
    COMMENT "Updating compile_commands.json for clangd"
    VERBATIM
)

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
set(STD_D_ZN_SRC "${SOURCE_DIR}/std.d.zn")
set(CONVERTER_PY "${CMAKE_SOURCE_DIR}/tools/runtime-converter.py")
set(EMBED_HPP "${CMAKE_BINARY_DIR}/embed.hpp")

add_custom_command(
    OUTPUT ${EMBED_HPP}
    COMMAND ${Python3_EXECUTABLE} ${CONVERTER_PY} ${EMBED_HPP} ${STD_D_ZN_SRC}
    DEPENDS ${STD_D_ZN_SRC}
    COMMENT "Embedding std.d.zn into binary..."
    VERBATIM
)

# Precompiled header
set(PCH_HEADER ${SOURCE_DIR}/pch.hpp)

# Main executable
set(SRC_MAIN ${SOURCE_DIR}/main.cpp)
file(GLOB SRC_HEADERS ${SOURCE_DIR}/*.hpp)

add_executable(zinc ${SRC_MAIN} ${EMBED_HPP})
add_dependencies(zinc sync_compile_commands)
target_link_libraries(zinc PRIVATE antlr4_generated_lib ${MIMALLOC_TARGET})
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
        ${EMBED_HPP}
    )
    
    add_dependencies(unit_tests sync_compile_commands)
    target_link_libraries(unit_tests PRIVATE GTest::gtest_main antlr4_generated_lib ${MIMALLOC_TARGET})
    target_include_directories(unit_tests PRIVATE ${SOURCE_DIR} ${CMAKE_BINARY_DIR} ${ANTLR4_OUTPUT_DIR})
    target_compile_options(unit_tests PRIVATE -include ${MIMALLOC_INCLUDE_DIR}/mimalloc-new-delete.h)
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
        ${EMBED_HPP}
    )
    
    add_dependencies(benchmarks sync_compile_commands)
    target_link_libraries(benchmarks PRIVATE benchmark::benchmark antlr4_generated_lib ${MIMALLOC_TARGET})
    target_include_directories(benchmarks PRIVATE ${SOURCE_DIR} ${CMAKE_BINARY_DIR} ${ANTLR4_OUTPUT_DIR})
    target_compile_options(benchmarks PRIVATE -include ${MIMALLOC_INCLUDE_DIR}/mimalloc-new-delete.h)
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