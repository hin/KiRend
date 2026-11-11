cmake_minimum_required(VERSION 3.21)

project(kirend
    VERSION 0.1.0
    DESCRIPTION "Render KiCad PCB layouts as line art"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KIREND_BUILD_TESTS "Build unit tests" ON)

find_package(OpenCASCADE CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)
find_package(lunasvg CONFIG REQUIRED)

if(TARGET zstd::libzstd)
    set(KIREND_ZSTD_TARGET zstd::libzstd)
elseif(TARGET zstd::libzstd_static)
    set(KIREND_ZSTD_TARGET zstd::libzstd_static)
else()
    set(KIREND_ZSTD_TARGET zstd::libzstd_shared)
endif()

# Library with everything except main(), so tests can link against it.
add_library(kirend_core STATIC
    src/sexpr/sexpr.cpp
    src/pcb/board_reader.cpp
    src/paths/base64.cpp
    src/paths/embedded_files.cpp
    src/paths/path_resolver.cpp
    src/geometry/arc.cpp
    src/geometry/outline.cpp
    src/geometry/board_geometry.cpp
    src/geometry/layer_lines.cpp
    src/models/model_loader.cpp
    src/models/mesh_builder.cpp
    src/render/camera.cpp
    src/render/scene.cpp
    src/render/line_renderer.cpp
    src/output/svg_writer.cpp
    src/output/png_writer.cpp
    src/cli/options.cpp
    src/kirend.cpp
)

target_include_directories(kirend_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(kirend_core PUBLIC KIREND_VERSION="${PROJECT_VERSION}")

target_link_libraries(kirend_core PUBLIC
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim
    TKShHealing TKMesh TKXSBase TKDE TKDESTEP TKDEVRML TKLCAF TKXCAF TKCAF
    ${KIREND_ZSTD_TARGET}
    nlohmann_json::nlohmann_json
    argparse::argparse
    lunasvg::lunasvg
)

if(MSVC)
    target_compile_options(kirend_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(kirend_core PRIVATE -Wall -Wextra)
endif()

# MinGW (MSYS2): link libstdc++, libgcc and winpthread statically, so kirend.exe runs without
# the MinGW runtime DLLs. All other dependencies are static through the vcpkg triplet.
if(MINGW)
    target_link_options(kirend_core PUBLIC -static)
endif()

# OpenCASCADE headers use M_PI, which MinGW and MSVC only define with _USE_MATH_DEFINES in strict
# C++20 mode.
if(WIN32)
    target_compile_definitions(kirend_core PUBLIC _USE_MATH_DEFINES)
endif()

add_executable(kirend src/main.cpp)
target_link_libraries(kirend PRIVATE kirend_core)

install(TARGETS kirend RUNTIME DESTINATION bin)

if(KIREND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
