# DSP-Kern des Auto Mixers: Lautheit, True Peak, Spracherkennung, Limiter und Regelung.
# Bewusst ohne OBS und Qt, damit er sich eigenständig testen lässt (siehe tests/).
add_library(automixer-dsp STATIC)

target_sources(
  automixer-dsp
  PRIVATE
    analyzer.cpp
    analyzer.hpp
    auto-preset.cpp
    auto-preset.hpp
    biquad.hpp
    calibration.cpp
    calibration.hpp
    loudness-meter.cpp
    loudness-meter.hpp
    output-stage.cpp
    output-stage.hpp
    preset.cpp
    preset.hpp
    regulator.cpp
    regulator.hpp
    status.cpp
    status.hpp
    triple-buffer.hpp
    true-peak.cpp
    true-peak.hpp
    types.cpp
    types.hpp
    vad.cpp
    vad.hpp
)

target_include_directories(automixer-dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(automixer-dsp PUBLIC cxx_std_17)
set_target_properties(automixer-dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
