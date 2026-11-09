add_library(skeleton_backup STATIC src/plan.cpp src/paths.cpp src/sets.cpp src/capture.cpp src/account.cpp
    src/host.cpp src/host_units.cpp src/nfs.cpp src/host_commands.cpp src/host_quiesce.cpp src/guest_freeze.cpp)
target_include_directories(skeleton_backup PUBLIC include)
target_link_libraries(skeleton_backup PUBLIC skeleton_contracts)

if(BUILD_TESTING)
    add_executable(skeleton_backup_tests tests/plan_test.cpp tests/capture_test.cpp tests/host_test.cpp tests/quiesce_test.cpp)
    target_include_directories(skeleton_backup_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
    target_link_libraries(skeleton_backup_tests PRIVATE skeleton_backup GTest::gtest_main)
    gtest_discover_tests(skeleton_backup_tests DISCOVERY_TIMEOUT 30)
endif()
