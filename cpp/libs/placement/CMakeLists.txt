add_library(skeleton_placement STATIC src/resources.cpp src/workloads.cpp)
target_include_directories(skeleton_placement PUBLIC include)
target_link_libraries(skeleton_placement PUBLIC skeleton_contracts)

if(BUILD_TESTING)
    add_executable(skeleton_placement_tests tests/resources_test.cpp tests/workloads_test.cpp)
    target_link_libraries(skeleton_placement_tests PRIVATE skeleton_placement GTest::gtest_main)
    gtest_discover_tests(skeleton_placement_tests)
endif()
