cmake_minimum_required(VERSION 3.16...3.30)

# Eigenständig bauen (cmake -S tests): nur der DSP-Kern, ohne OBS und Qt
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(automixer-dsp-tests LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(MSVC)
    add_compile_options(/W3 /WX /utf-8)
  else()
    add_compile_options(-Wall -Wextra -Werror)
  endif()
  enable_testing()
  add_subdirectory(../src/dsp dsp)
endif()

add_executable(
  automixer-dsp-tests
  test-main.cpp
  test-framework.hpp
  signals.hpp
  test-loudness.cpp
  test-true-peak.cpp
  test-vad.cpp
  test-output-stage.cpp
  test-regulator.cpp
  test-status.cpp
  test-auto-preset.cpp
  test-calibration.cpp
)
target_link_libraries(automixer-dsp-tests PRIVATE automixer-dsp)

foreach(
  _group
  IN
  ITEMS loudness true-peak vad output-stage regulator status auto-preset calibration
)
  add_test(NAME ${_group} COMMAND automixer-dsp-tests ${_group})
endforeach()
