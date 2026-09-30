cmake_minimum_required(VERSION 3.26)

include(cmake/versions.cmake)

# Static MSVC runtime (/MT) for this project and every fetched dependency,
# so the exe needs no VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(fractals LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(FRACTALS_CUDA "Build the CUDA kernels (falls back to CPU-only if nvcc is missing)" ON)

if(FRACTALS_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    set(CMAKE_CUDA_ARCHITECTURES ${FRACTALS_CUDA_ARCHITECTURES})
    set(CMAKE_CUDA_RUNTIME_LIBRARY Static)
    enable_language(CUDA)
    find_package(CUDAToolkit 12.0 REQUIRED)
    message(STATUS "fractals: CUDA ${CMAKE_CUDA_COMPILER_VERSION}, architectures ${CMAKE_CUDA_ARCHITECTURES}")
  else()
    message(WARNING "fractals: nvcc not found, building CPU-only")
    set(FRACTALS_CUDA OFF)
  endif()
else()
  message(STATUS "fractals: CUDA disabled, building CPU-only")
endif()

# ---------------------------------------------------------------------------
# Dependencies: built from source, statically, inside the build folder
# ---------------------------------------------------------------------------
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
# Build FreeType etc. from source and link them in (SFML defaults to the
# system copies on Linux, which would add shared-library dependencies)
set(SFML_USE_SYSTEM_DEPS OFF CACHE BOOL "" FORCE)
if(WIN32)
  set(SFML_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
  set(TGUI_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(SFML
  GIT_REPOSITORY https://github.com/SFML/SFML.git
  GIT_TAG ${FRACTALS_SFML_GIT_TAG}
  GIT_SHALLOW ON
  SYSTEM)
FetchContent_MakeAvailable(SFML)
# SFML's bundled FreeType and HarfBuzz need each other, but SFML leaves out the
# FreeType -> HarfBuzz link. MSVC doesn't care; GNU ld needs HarfBuzz repeated
# after FreeType, which CMake does once the cycle is declared.
if(TARGET freetype AND TARGET harfbuzz AND NOT MSVC)
  target_link_libraries(freetype INTERFACE harfbuzz)
endif()

set(TGUI_BACKEND SFML_GRAPHICS CACHE STRING "" FORCE)
set(TGUI_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(TGUI_BUILD_GUI_BUILDER OFF CACHE BOOL "" FORCE)
set(TGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TGUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TGUI_BUILD_DOC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(TGUI
  GIT_REPOSITORY https://github.com/texus/TGUI.git
  GIT_TAG ${FRACTALS_TGUI_GIT_TAG}
  GIT_SHALLOW ON
  SYSTEM)
FetchContent_MakeAvailable(TGUI)

# nlohmann/json for the fractal keys (the release tarball is much smaller than the repo)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v${FRACTALS_JSON_VERSION}/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SYSTEM)
FetchContent_MakeAvailable(json)

# Boost.Multiprecision in standalone mode (header-only, no other Boost) for the
# deep-zoom reference orbit; only its headers are used
FetchContent_Declare(boost_mp
  GIT_REPOSITORY https://github.com/boostorg/multiprecision.git
  GIT_TAG ${FRACTALS_BOOST_MP_GIT_TAG}
  GIT_SHALLOW ON
  SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(boost_mp)
# Standalone Boost.Multiprecision still needs Boost.Config (also header-only)
FetchContent_Declare(boost_config
  GIT_REPOSITORY https://github.com/boostorg/config.git
  GIT_TAG ${FRACTALS_BOOST_MP_GIT_TAG}
  GIT_SHALLOW ON
  SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(boost_config)

# CMakeRC is a single CMake script; SOURCE_SUBDIR points at a folder without a
# CMakeLists.txt so only the sources are downloaded (no tests get added).
FetchContent_Declare(cmrc
  GIT_REPOSITORY https://github.com/vector-of-bool/cmrc.git
  GIT_TAG ${FRACTALS_CMRC_GIT_TAG}
  GIT_SHALLOW ON
  SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(cmrc)
# CMakeRC 2.0.1 asks for CMake 3.3: CMake 4 refuses that and 3.x warns about it
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
include(${cmrc_SOURCE_DIR}/CMakeRC.cmake)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# Default files built into the exe; extracted into FractalsData/ on first run
file(GLOB FRACTALS_ASSETS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/themes/*
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/escape_images/*)
cmrc_add_resource_library(fractals_assets
  NAMESPACE fractals_assets
  WHENCE assets
  ${FRACTALS_ASSETS})

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
add_executable(fractals src/fractals_with_gui_cuda.cpp src/runtime.cpp src/deepzoom.cpp)
target_include_directories(fractals SYSTEM PRIVATE ${boost_mp_SOURCE_DIR}/include ${boost_config_SOURCE_DIR}/include)
target_compile_definitions(fractals PRIVATE BOOST_MP_STANDALONE)
target_link_libraries(fractals PRIVATE fractals_assets nlohmann_json::nlohmann_json)

if(FRACTALS_CUDA)
  target_sources(fractals PRIVATE src/buddha_cuda_kernel.cu src/deepzoom_cuda.cu)
  # cudart comes from CMAKE_CUDA_RUNTIME_LIBRARY=Static; never link the driver
  # library (-lcuda), so the exe still starts on machines without an NVIDIA driver.
  # --fmad=false: no fused multiply-add, like the CPU build, so GPU and CPU images
  # match. Per-thread default streams let the render threads' tiles run side by side.
  target_compile_options(fractals PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-Wno-deprecated-gpu-targets --fmad=false --default-stream per-thread>)
  target_compile_definitions(fractals PRIVATE FRACTALS_CUDA=1)
else()
  target_sources(fractals PRIVATE src/cuda_stub.cpp)
  target_compile_definitions(fractals PRIVATE FRACTALS_CUDA=0)
endif()

target_link_libraries(fractals PRIVATE TGUI::TGUI SFML::Graphics SFML::Window SFML::System)

if(MSVC)
  target_compile_options(fractals PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3 /utf-8>)
  target_compile_definitions(fractals PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  # GUI app: no console window (--console opens one). Keep main() as the entry point.
  set_target_properties(fractals PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(fractals PRIVATE /ENTRY:mainCRTStartup)
else()
  target_compile_options(fractals PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -fwrapv>)
  find_package(Threads REQUIRED)
  target_link_libraries(fractals PRIVATE Threads::Threads)
  # Only glibc, X11, GL and udev stay dynamic (checked by scripts/check-linux-deps.sh)
  target_link_options(fractals PRIVATE -static-libstdc++ -static-libgcc)
endif()
