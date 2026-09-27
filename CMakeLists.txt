cmake_minimum_required(VERSION 3.20)
project(RDR2_Checkerboard_Rendering VERSION 1.0.0 LANGUAGES C CXX)

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
set(CBR_HEADERS
    ${CBR_INCLUDE_DIR}/cbr/cbr_engine.h
    ${CBR_INCLUDE_DIR}/cbr/config.h
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

# Define shared library (ASI plugin is a renamed DLL)
add_library(rdr2-cbr SHARED ${CBR_HEADERS} ${CBR_SOURCES} ${CBR_SHADERS})

# Configure output extension as .asi for game loaders
set_target_properties(rdr2-cbr PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "rdr2-cbr"
)

# Windows specific definitions
target_compile_definitions(rdr2-cbr PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    CBR_EXPORTS
)

# MSVC optimization flags
if(MSVC)
    target_compile_options(rdr2-cbr PRIVATE
        /W4
        /MP
        /Oi
        /Ot
        $<$<CONFIG:Release>:/O2 /GL /GS->
    )
    target_link_options(rdr2-cbr PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
endif()

# Find Vulkan if available
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
    target_include_directories(rdr2-cbr PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(rdr2-cbr PRIVATE ${Vulkan_LIBRARIES})
    target_compile_definitions(rdr2-cbr PRIVATE CBR_VULKAN_SUPPORT=1)
else()
    message(STATUS "Vulkan SDK not found, using dynamic runtime linking.")
    target_compile_definitions(rdr2-cbr PRIVATE CBR_VULKAN_SUPPORT=1)
endif()

# DirectX 12 linking on Windows
if(WIN32)
    target_link_libraries(rdr2-cbr PRIVATE
        d3d12.lib
        dxgi.lib
    )
    target_compile_definitions(rdr2-cbr PRIVATE CBR_DX12_SUPPORT=1)
endif()

# Copy sample configuration to output directory post-build
add_custom_command(TARGET rdr2-cbr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/cbr.ini"
    "$<TARGET_FILE_DIR:rdr2-cbr>/cbr.ini"
    COMMENT "Copying cbr.ini to target build directory"
)
