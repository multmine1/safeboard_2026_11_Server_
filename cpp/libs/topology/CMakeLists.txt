add_library(skeleton_topology STATIC
    src/topology.cpp
    src/host_services.cpp
    src/pci.cpp
)
target_include_directories(skeleton_topology PUBLIC include)
target_link_libraries(skeleton_topology PUBLIC skeleton_vm skeleton_network_types skeleton_placement skeleton_backup)

if(BUILD_TESTING)
    add_executable(skeleton_topology_tests
        tests/topology_test.cpp
    )
    target_link_libraries(skeleton_topology_tests PRIVATE skeleton_topology skeleton_test_support)
    gtest_discover_tests(skeleton_topology_tests)
endif()
