cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# Das Plugin braucht beides; die Schalter bleiben für die CMake-Hilfsskripte der Vorlage erhalten
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# DSP-Kern ohne OBS- und Qt-Abhängigkeit (eigenständig testbar, siehe tests/)
add_subdirectory(src/dsp)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE automixer-dsp)

option(ENABLE_TESTS "Tests des DSP-Kerns mitbauen (CTest)" OFF)
if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets
)

target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
