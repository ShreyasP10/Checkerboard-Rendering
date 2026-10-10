cmake_minimum_required(VERSION 3.20)
project(RDR2_Checkerboard_Rendering VERSION 1.0.0 LANGUAGES CXX)

# Required for MSVC_RUNTIME_LIBRARY target property (static CRT below)
cmake_policy(SET CMP0091 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure 64-bit build
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RDR2 Checkerboard Rendering Mod requires a 64-bit target.")
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Source and Include directories
set(CBR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(CBR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(CBR_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

include_directories(
    ${CBR_INCLUDE_DIR}
)

# Header files
# NOTE: include/cbr/limits.h intentionally NOT listed here: it is an optional
# compatibility shim (see below). config.h is the single source of truth for
# limits so a clean checkout builds even if limits.h is absent.
set(CBR_HEADERS
    ${CBR_INCLUDE_DIR}/cbr/cbr_engine.h
    ${CBR_INCLUDE_DIR}/cbr/checkerboard_mapping.h
    ${CBR_INCLUDE_DIR}/cbr/config.h
    ${CBR_INCLUDE_DIR}/cbr/depth_convention.h
    ${CBR_INCLUDE_DIR}/cbr/hooks.h
    ${CBR_INCLUDE_DIR}/cbr/jitter_manager.h
    ${CBR_INCLUDE_DIR}/cbr/logger.h
    ${CBR_INCLUDE_DIR}/cbr/reconstruction_pass.h
    ${CBR_INCLUDE_DIR}/cbr/render_target_manager.h
    ${CBR_INCLUDE_DIR}/cbr/ui_overlay.h
)

# Source files
set(CBR_SOURCES
    ${CBR_SOURCE_DIR}/main.cpp
    ${CBR_SOURCE_DIR}/cbr_engine.cpp
    ${CBR_SOURCE_DIR}/config.cpp
    ${CBR_SOURCE_DIR}/hooks.cpp
    ${CBR_SOURCE_DIR}/hooks_vulkan.cpp
    ${CBR_SOURCE_DIR}/hooks_dx12.cpp
    ${CBR_SOURCE_DIR}/jitter_manager.cpp
    ${CBR_SOURCE_DIR}/logger.cpp
    ${CBR_SOURCE_DIR}/reconstruction_pass.cpp
    ${CBR_SOURCE_DIR}/render_target_manager.cpp
    ${CBR_SOURCE_DIR}/ui_overlay.cpp
)

# Shaders
set(CBR_SHADERS
    ${CBR_SHADER_DIR}/cbr_reconstruct.comp
    ${CBR_SHADER_DIR}/cbr_reconstruct.hlsl
    ${CBR_SHADER_DIR}/cbr_resolve_simple.comp
)

# Prevent MSVC from attempting default FXC compilation on shaders in IDE
set_source_files_properties(${CBR_SHADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

# Define shared library (ASI plugin is a renamed DLL)
add_library(rdr2-cbr SHARED ${CBR_HEADERS} ${CBR_SOURCES} ${CBR_SHADERS})

# Configure output extension as .asi for game loaders
set_target_properties(rdr2-cbr PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "rdr2-cbr"
)

# Windows specific definitions
# (WIN32_LEAN_AND_MEAN / NOMINMAX are defined, guarded, in the sources that include windows.h)
target_compile_definitions(rdr2-cbr PRIVATE
    CBR_EXPORTS
)

# Static CRT: the plugin must not depend on a redistributable that the game may not ship
set_property(TARGET rdr2-cbr PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MSVC optimization and hardening flags
if(MSVC)
    target_compile_options(rdr2-cbr PRIVATE
        /W4
        /MP
        /Oi
        /Ot
        /guard:cf
        /sdl
        $<$<CONFIG:Release>:/O2 /GL /GS>
    )
    target_link_options(rdr2-cbr PRIVATE
        /guard:cf
        /DYNAMICBASE
        /NXCOMPAT
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /CETCOMPAT>
    )
endif()

# Find Vulkan headers if available (dynamic runtime resolution is used for function pointers)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan SDK headers found.")
    # Prefer the imported target over the deprecated Vulkan_INCLUDE_DIRS variable.
    if(TARGET Vulkan::Headers)
        target_link_libraries(rdr2-cbr PRIVATE Vulkan::Headers)
    elseif(DEFINED Vulkan_INCLUDE_DIRS)
        target_include_directories(rdr2-cbr PRIVATE ${Vulkan_INCLUDE_DIRS})
    endif()
else()
    message(STATUS "Vulkan SDK not found, using dynamic runtime function pointers only.")
endif()
# Vulkan support does not need the SDK at build time (functions are resolved at runtime)
target_compile_definitions(rdr2-cbr PRIVATE CBR_VULKAN_SUPPORT=1)

# DirectX 12 linking on Windows
if(WIN32)
    target_link_libraries(rdr2-cbr PRIVATE
        d3d12.lib
        dxgi.lib
    )
    target_compile_definitions(rdr2-cbr PRIVATE CBR_DX12_SUPPORT=1)
endif()

# Optional: compile shaders when the toolchain is available (UNTESTED on Windows; verify locally)
find_program(CBR_GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/Bin)
find_program(CBR_DXC dxc HINTS $ENV{VULKAN_SDK}/Bin)
set(CBR_SHADER_OUT_DIR "$<TARGET_FILE_DIR:rdr2-cbr>/shaders")
set(CBR_COMPILED_SHADERS "")

if(CBR_GLSLANG)
    foreach(shader cbr_reconstruct cbr_resolve_simple)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>/${shader}.spv
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>
            COMMAND ${CBR_GLSLANG} -V ${CBR_SHADER_DIR}/${shader}.comp -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>/${shader}.spv
            DEPENDS ${CBR_SHADER_DIR}/${shader}.comp
            COMMENT "Compiling ${shader}.comp to SPIR-V"
            VERBATIM)
        list(APPEND CBR_COMPILED_SHADERS ${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>/${shader}.spv)
    endforeach()
else()
    message(STATUS "glslangValidator not found: SPIR-V shaders will not be built.")
endif()

if(CBR_DXC)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>/cbr_reconstruct.dxil
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>
        COMMAND ${CBR_DXC} -T cs_6_0 -E CSMain ${CBR_SHADER_DIR}/cbr_reconstruct.hlsl -Fo ${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>/cbr_reconstruct.dxil
        DEPENDS ${CBR_SHADER_DIR}/cbr_reconstruct.hlsl
        COMMENT "Compiling cbr_reconstruct.hlsl to DXIL"
        VERBATIM)
    list(APPEND CBR_COMPILED_SHADERS ${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>/cbr_reconstruct.dxil)
    list(APPEND CBR_COMPILED_SHADERS ${CMAKE_BINARY_DIR}/shaders/cbr_reconstruct.dxil)
else()
    message(STATUS "dxc not found: DXIL shaders will not be built.")
endif()

if(CBR_COMPILED_SHADERS)
    add_custom_target(cbr_shaders ALL DEPENDS ${CBR_COMPILED_SHADERS})
    add_dependencies(rdr2-cbr cbr_shaders)
endif()

# Host-side unit tests (portable code only; no Windows APIs or GPU required)
#   cmake -S . -B build-tests -DCBR_BUILD_TESTS=ON
#   cmake --build build-tests --target cbr_tests && ctest --test-dir build-tests --output-on-failure
option(CBR_BUILD_TESTS "Build host-side unit tests" OFF)
if(CBR_BUILD_TESTS)
    enable_testing()
    set(CBR_TEST_CORE_SOURCES
        ${CBR_SOURCE_DIR}/config.cpp
        ${CBR_SOURCE_DIR}/logger.cpp
        ${CBR_SOURCE_DIR}/jitter_manager.cpp
        ${CBR_SOURCE_DIR}/render_target_manager.cpp
        ${CBR_SOURCE_DIR}/reconstruction_pass.cpp
    )
    add_executable(cbr_tests tests/test_core.cpp ${CBR_TEST_CORE_SOURCES})
    target_include_directories(cbr_tests PRIVATE ${CBR_INCLUDE_DIR})
    add_test(NAME cbr_core COMMAND cbr_tests)

    # Engine tests use fake hook installers (the real ones are Windows-only), one mode per process
    add_executable(cbr_engine_tests
        tests/test_engine.cpp
        ${CBR_TEST_CORE_SOURCES}
        ${CBR_SOURCE_DIR}/cbr_engine.cpp
        ${CBR_SOURCE_DIR}/hooks.cpp
        ${CBR_SOURCE_DIR}/ui_overlay.cpp
    )
    target_include_directories(cbr_engine_tests PRIVATE ${CBR_INCLUDE_DIR})
    add_test(NAME cbr_engine      COMMAND cbr_engine_tests)
    add_test(NAME cbr_engine_auto COMMAND cbr_engine_tests auto)

    # Equivalence verification between C++ checkerboard mapping and compute shaders
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME cbr_shader_mapping COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_shader_mapping.py)
    endif()
endif()

# Copy sample configuration to output directory post-build
add_custom_command(TARGET rdr2-cbr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/cbr.ini"
    "$<TARGET_FILE_DIR:rdr2-cbr>/cbr.ini"
    COMMENT "Copying cbr.ini to target build directory"
)

if(CBR_COMPILED_SHADERS)
    add_custom_command(TARGET rdr2-cbr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:rdr2-cbr>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CBR_COMPILED_SHADERS} "$<TARGET_FILE_DIR:rdr2-cbr>/shaders/"
        COMMENT "Copying compiled shaders to target build directory"
    )
endif()
