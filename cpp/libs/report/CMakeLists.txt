add_library(skeleton_report STATIC src/resources.cpp src/workloads.cpp src/cleanup.cpp src/software.cpp)
target_include_directories(skeleton_report PUBLIC include)
target_link_libraries(skeleton_report PUBLIC skeleton_planning)

if(BUILD_TESTING)
    add_executable(skeleton_report_tests tests/export_test.cpp)
    target_link_libraries(skeleton_report_tests PRIVATE skeleton_report skeleton_test_support)
    gtest_discover_tests(skeleton_report_tests DISCOVERY_TIMEOUT 30)
endif()
