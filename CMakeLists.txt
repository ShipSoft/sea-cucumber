# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) CERN for the benefit of the SHiP Collaboration
#
# sea_cucumber -- standalone REve event display for the SHiP experiment.
# Reads the official RNTuple event data model and GeoModel .db geometry,
# mirroring aegir's dependency set (ROOT 6.40+, SHiPDataModel, GeoModel).
#
# Recommended build is via pixi (see pixi.toml):
#     pixi run build && pixi run test && pixi run lint

cmake_minimum_required(VERSION 3.16)
project(sea_cucumber VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)          # matches the SHiP EDM (root_cxx_standard==23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(SHIP_USE_GEOMETRY_SERVICE "Load .db via SHiPGeometryService (aegir way)" OFF)
option(SEA_CUCUMBER_BUILD_TESTS "Build the CTest suite" ON)

# --- ROOT: RNTuple needs 6.40+ and the ROOTNTuple component (per aegir) ------
find_package(ROOT 6.40 CONFIG REQUIRED
  COMPONENTS Core RIO Tree Hist MathCore Geom Gdml ROOTEve ROOTWebDisplay ROOTNTuple)

# --- Official SHiP event data model (dictionary classes) ---------------------
find_package(SHiPDataModel CONFIG REQUIRED)

# --- GeoModel: read the .db (GMDBManager + ReadGeoModel) ---------------------
find_package(GeoModelCore CONFIG REQUIRED)   # GeoModelKernel
find_package(GeoModelIO   CONFIG REQUIRED)   # GeoModelDBManager, GeoModelRead

# --- View-config parser ------------------------------------------------------
find_package(tomlplusplus CONFIG REQUIRED)

# --- library with the reusable core (so tests can link it too) ---------------
add_library(sea_cucumber_core STATIC
  src/RNTupleEventSource.cxx
  src/GeoModelLoader.cxx
  src/GeoModelGeometrySource.cxx
  src/ViewConfig.cxx)

target_include_directories(sea_cucumber_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(sea_cucumber_core PUBLIC
  ROOT::Core ROOT::RIO ROOT::Tree ROOT::Hist ROOT::MathCore
  ROOT::Geom ROOT::Gdml ROOT::ROOTEve ROOT::ROOTWebDisplay ROOT::ROOTNTuple
  SHiP::SHiPDataModel
  GeoModelCore::GeoModelKernel
  GeoModelIO::GeoModelDBManager
  GeoModelIO::GeoModelRead
  tomlplusplus::tomlplusplus)

if(SHIP_USE_GEOMETRY_SERVICE)
  target_compile_definitions(sea_cucumber_core PUBLIC SHIP_USE_GEOMETRY_SERVICE)
  # find_package(SHiPGeometryService CONFIG REQUIRED)               # <-- confirm name
  # target_link_libraries(sea_cucumber_core PUBLIC SHiPGeometry::SHiPGeometryService)
endif()

target_compile_options(sea_cucumber_core PRIVATE -Wall -Wextra -Wpedantic)

# --- the executable ----------------------------------------------------------
add_executable(sea_cucumber src/sea_cucumber.cxx)
target_link_libraries(sea_cucumber PRIVATE sea_cucumber_core)

# --- demo-event generator (writes a synthetic events RNTuple) ----------------
add_executable(make_demo_event tools/make_demo_event.cxx)
target_link_libraries(make_demo_event PRIVATE sea_cucumber_core)

# --- install -----------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS sea_cucumber make_demo_event RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY views/ DESTINATION ${CMAKE_INSTALL_DATADIR}/sea_cucumber/views)

# --- tests -------------------------------------------------------------------
if(SEA_CUCUMBER_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
  enable_testing()
  add_subdirectory(tests)
elseif(SEA_CUCUMBER_BUILD_TESTS)
  message(WARNING "tests/ not found -- skipping the test suite "
                  "(re-extract the full archive to enable `pixi run test`)")
endif()
