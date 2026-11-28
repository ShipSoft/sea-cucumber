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

add_executable(test_unknown_fields test_unknown_fields.cpp)
target_link_libraries(test_unknown_fields PRIVATE sea_cucumber_core)
add_test(NAME unknown_fields COMMAND test_unknown_fields)

add_executable(test_name_pattern test_name_pattern.cpp)
target_link_libraries(test_name_pattern PRIVATE sea_cucumber_core)
add_test(NAME name_pattern COMMAND test_name_pattern)

add_executable(test_view_config test_view_config.cpp)
target_link_libraries(test_view_config PRIVATE sea_cucumber_core)
add_test(NAME view_config COMMAND test_view_config)

# Numeric flags that must be finite and positive are rejected at parse time,
# before any ROOT or geometry work, so these need no input files.
foreach(val nan inf 0 -1)
    add_test(NAME args_scale_rejects_${val} COMMAND sea_cucumber --scale ${val})
    add_test(
        NAME args_down_min_rejects_${val}
        COMMAND make_demo_event --down-min ${val}
    )
    set_tests_properties(
        args_scale_rejects_${val}
        args_down_min_rejects_${val}
        PROPERTIES PASS_REGULAR_EXPRESSION "needs a finite, positive value"
    )
endforeach()
