add_library(skeleton_input STATIC
    src/source.cpp
    src/json_reader.cpp
    src/vm_reader.cpp
    src/pci_reader.cpp
    src/network_reader.cpp
    src/site_reader.cpp
    src/instance_reader.cpp
    src/hypervisor_reader.cpp
    src/workload_reader.cpp
    src/workload_compute_reader.cpp
    src/cleanup_reader.cpp
    src/backup_reader.cpp
    src/backup_host_reader.cpp
)
target_include_directories(skeleton_input PUBLIC include)
target_link_libraries(skeleton_input PUBLIC skeleton_topology skeleton_cleanup)

if(BUILD_TESTING)
    add_executable(skeleton_input_tests
        tests/input_test.cpp
        tests/cleanup_test.cpp
        tests/workload_compute_test.cpp
        tests/backup_test.cpp
    )
    target_link_libraries(skeleton_input_tests PRIVATE skeleton_input skeleton_test_support)
    gtest_discover_tests(skeleton_input_tests DISCOVERY_TIMEOUT 30)
endif()
