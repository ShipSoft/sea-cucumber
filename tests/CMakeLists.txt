# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) CERN for the benefit of the SHiP Collaboration

add_executable(test_rntuple_roundtrip test_rntuple_roundtrip.cpp)
target_link_libraries(test_rntuple_roundtrip PRIVATE sea_cucumber_core)
add_test(NAME rntuple_roundtrip COMMAND test_rntuple_roundtrip)

add_executable(test_db_resolution test_db_resolution.cpp)
target_link_libraries(test_db_resolution PRIVATE sea_cucumber_core)
add_test(NAME db_resolution COMMAND test_db_resolution)

add_executable(test_geometry_cache test_geometry_cache.cpp)
target_link_libraries(test_geometry_cache PRIVATE sea_cucumber_core)
add_test(NAME geometry_cache COMMAND test_geometry_cache)

add_executable(test_user_config test_user_config.cpp)
target_link_libraries(test_user_config PRIVATE sea_cucumber_core)
add_test(NAME user_config COMMAND test_user_config)
