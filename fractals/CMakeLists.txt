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
set(SFML_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(SFML
  GIT_REPOSITORY https://github.com/SFML/SFML.git
  GIT_TAG ${FRACTALS_SFML_GIT_TAG}
  GIT_SHALLOW ON
  SYSTEM)
FetchContent_MakeAvailable(SFML)

set(TGUI_BACKEND SFML_GRAPHICS CACHE STRING "" FORCE)
set(TGUI_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(TGUI_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
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

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
add_executable(fractals src/fractals_with_gui_cuda.cpp)

if(FRACTALS_CUDA)
  target_sources(fractals PRIVATE src/buddha_cuda_kernel.cu)
  # cudart comes from CMAKE_CUDA_RUNTIME_LIBRARY=Static; never link the driver
  # library (-lcuda), so the exe still starts on machines without an NVIDIA driver.
  target_compile_options(fractals PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Wno-deprecated-gpu-targets>)
  target_compile_definitions(fractals PRIVATE FRACTALS_CUDA=1)
else()
  target_sources(fractals PRIVATE src/cuda_stub.cpp)
  target_compile_definitions(fractals PRIVATE FRACTALS_CUDA=0)
endif()

target_link_libraries(fractals PRIVATE TGUI::TGUI SFML::Graphics SFML::Window SFML::System)

if(MSVC)
  target_compile_options(fractals PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3 /utf-8>)
  target_compile_definitions(fractals PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(fractals PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -fwrapv>)
  find_package(Threads REQUIRED)
  target_link_libraries(fractals PRIVATE Threads::Threads)
endif()

# Until M3 embeds these, the app loads assets relative to its working
# directory: stage them next to the exe.
add_custom_command(TARGET fractals POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/assets/themes $<TARGET_FILE_DIR:fractals>/themes
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/assets/escape_images $<TARGET_FILE_DIR:fractals>/escape_image
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:fractals>/screenshots
  VERBATIM)
