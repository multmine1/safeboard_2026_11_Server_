add_library(skeleton_ansible STATIC
    src/export.cpp
    src/backup.cpp
    src/installation.cpp
)
target_include_directories(skeleton_ansible PUBLIC include)
target_link_libraries(skeleton_ansible PUBLIC skeleton_planning)

if(BUILD_TESTING)
    add_executable(skeleton_ansible_tests tests/backup_test.cpp)
    target_link_libraries(skeleton_ansible_tests PRIVATE skeleton_ansible skeleton_test_support)
    gtest_discover_tests(skeleton_ansible_tests DISCOVERY_TIMEOUT 30)
endif()
