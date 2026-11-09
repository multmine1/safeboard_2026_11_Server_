add_library(skeleton_installation STATIC src/disks.cpp src/bootstrap.cpp src/preseed.cpp)
target_include_directories(skeleton_installation PUBLIC include)
target_link_libraries(skeleton_installation PUBLIC skeleton_contracts)

if(BUILD_TESTING)
    add_executable(skeleton_installation_tests tests/disks_test.cpp tests/bootstrap_test.cpp tests/preseed_test.cpp)
    target_link_libraries(skeleton_installation_tests PRIVATE skeleton_installation GTest::gtest_main)
    gtest_discover_tests(skeleton_installation_tests)
endif()
