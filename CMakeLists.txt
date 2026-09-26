# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) CERN for the benefit of the SHiP Collaboration
#
# sea_cucumber -- standalone REve event display for the SHiP experiment.
# Reads the official RNTuple event data model and GeoModel .db geometry,
# mirroring aegir's dependency set (ROOT 6.40+, SHiPDataModel, GeoModel).
#
# Recommended build is via pixi (see pixi.toml):
#     pixi run build && pixi run test && pixi run lint

cmake_minimum_required(VERSION 3.21) # matches the pixi.toml pin
project(sea_cucumber VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23) # matches the SHiP EDM (root_cxx_standard==23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(
    SHIP_USE_GEOMETRY_SERVICE
    "Load .db via SHiPGeometryService (aegir way)"
    OFF
)
option(SEA_CUCUMBER_BUILD_TESTS "Build the CTest suite" ON)

# Warnings for every target in this project (executables and tools included,
# not just the core library). Directory-scoped, so the tests/ subdirectory
# inherits it too; the generated dictionary TU comes along, which is fine.
add_compile_options(-Wall -Wextra -Wpedantic)

# --- ROOT: RNTuple needs 6.40+ and the ROOTNTuple component (per aegir) ------
find_package(
    ROOT
    6.40
    CONFIG
    REQUIRED
    COMPONENTS
        Core
        RIO
        Tree
        Hist
        MathCore
        Geom
        Gdml
        ROOTEve
        ROOTWebDisplay
        ROOTNTuple
)

# --- Official SHiP event data model (dictionary classes) ---------------------
find_package(SHiPDataModel CONFIG REQUIRED)

# --- GeoModel: read the .db (GMDBManager + ReadGeoModel) ---------------------
find_package(GeoModelCore CONFIG REQUIRED) # GeoModelKernel
find_package(GeoModelIO CONFIG REQUIRED) # GeoModelDBManager, GeoModelRead

# --- View-config parser ------------------------------------------------------
find_package(tomlplusplus CONFIG REQUIRED)

# --- ROOT dictionary for the in-GUI event navigator --------------------------
# The "// *MENU*" tags on EventNavigator only take effect through dictionary
# info, which is what the Eve7 client reads to build an element's context menu.
root_generate_dictionary(
    G__SeaCucumberNav
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EventNavigator.h
    LINKDEF
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EventNavigatorLinkDef.h
    OPTIONS
    -I${CMAKE_CURRENT_SOURCE_DIR}/src
)

# --- library with the reusable core (so tests can link it too) ---------------
# The dictionary TU (G__SeaCucumberNav.cxx) is deliberately NOT part of this
# static library: its self-registration lives in a translation unit nothing
# references, so a static-library link would drop it and the REve context menu
# would come up empty. It is compiled into the display executable instead.
add_library(
    sea_cucumber_core
    STATIC
    src/RNTupleEventSource.cxx
    src/GeoModelLoader.cxx
    src/GeoModelGeometrySource.cxx
    src/ViewConfig.cxx
    src/GeometryCache.cxx
    src/EventNavigator.cxx
)

target_include_directories(
    sea_cucumber_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    sea_cucumber_core
    PUBLIC
        ROOT::Core
        ROOT::RIO
        ROOT::Tree
        ROOT::Hist
        ROOT::MathCore
        ROOT::Geom
        ROOT::Gdml
        ROOT::ROOTEve
        ROOT::ROOTWebDisplay
        ROOT::ROOTNTuple
        SHiP::SHiPDataModel
        GeoModelCore::GeoModelKernel
        GeoModelIO::GeoModelDBManager
        GeoModelIO::GeoModelRead
        tomlplusplus::tomlplusplus
)

if(SHIP_USE_GEOMETRY_SERVICE)
    target_compile_definitions(
        sea_cucumber_core
        PUBLIC SHIP_USE_GEOMETRY_SERVICE
    )
    # find_package(SHiPGeometryService CONFIG REQUIRED)               # <-- confirm name
    # target_link_libraries(sea_cucumber_core PUBLIC SHiPGeometry::SHiPGeometryService)
endif()

# --- the executable ----------------------------------------------------------
add_executable(sea_cucumber src/sea_cucumber.cxx G__SeaCucumberNav.cxx)
target_link_libraries(sea_cucumber PRIVATE sea_cucumber_core)

# --- demo-event generator (writes a synthetic events RNTuple) ----------------
add_executable(make_demo_event tools/make_demo_event.cxx)
target_link_libraries(make_demo_event PRIVATE sea_cucumber_core)

add_executable(make_geometry_cache tools/make_geometry_cache.cxx)
target_link_libraries(make_geometry_cache PRIVATE sea_cucumber_core)

add_executable(make_web_data tools/make_web_data.cxx)
target_link_libraries(make_web_data PRIVATE sea_cucumber_core)
# Single source for the version shown by the web page: the project version.
target_compile_definitions(
    make_web_data
    PRIVATE SEA_CUCUMBER_VERSION="${PROJECT_VERSION}"
)

# --- install -----------------------------------------------------------------
include(GNUInstallDirs)
install(
    TARGETS sea_cucumber make_demo_event make_geometry_cache make_web_data
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# The navigator dictionary's .pcm and .rootmap are looked up next to the
# executable at runtime; without them the installed binary loses the
# right-click menu. rootcling names both after the library the dictionary
# belongs to, not after the G__ target, hence the lib prefix.
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/libSeaCucumberNav_rdict.pcm
        ${CMAKE_CURRENT_BINARY_DIR}/libSeaCucumberNav.rootmap
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
    DIRECTORY views/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sea_cucumber/views
)
install(
    DIRECTORY logo/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sea_cucumber/logo
    OPTIONAL
)
# The static web frontend (three.js vendored), so an installed package can
# serve the page directly. Generated display files (web/data, web/VERSION)
# stay out; make_web_data produces them next to the served copy.
install(
    DIRECTORY web/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sea_cucumber/web
    PATTERN "data" EXCLUDE
    PATTERN "VERSION" EXCLUDE
)

# --- tests -------------------------------------------------------------------
if(
    SEA_CUCUMBER_BUILD_TESTS
    AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt
)
    enable_testing()
    add_subdirectory(tests)
elseif(SEA_CUCUMBER_BUILD_TESTS)
    message(
        WARNING
        "tests/ not found -- skipping the test suite "
        "(re-extract the full archive to enable `pixi run test`)"
    )
endif()
