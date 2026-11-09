add_library(skeleton_planning STATIC
    src/guest.cpp
    src/guest_access.cpp
    src/guest_network.cpp
    src/router.cpp
    src/infrastructure.cpp
    src/resources.cpp
    src/system_readiness.cpp
    src/reset.cpp
    src/backup.cpp
    src/backup_producers.cpp
    src/backup_nfs.cpp
    src/backup_quiesce.cpp
    src/backup_filesystem.cpp
    src/hypervisor.cpp
    src/host_requirements.cpp
    src/installation.cpp
    src/pci.cpp
    src/hypervisor_proxy.cpp
    src/hypervisor_nfs.cpp
    src/monitoring.cpp
    src/sshd.cpp
    src/local_access.cpp
    src/browser_proxy.cpp
    src/registry.cpp
    src/publications.cpp
    src/workloads.cpp
    src/workload_context.cpp
    src/workload_navigation.cpp
    src/workload_compute.cpp
    src/workload_order.cpp
    src/workload_resources.cpp
    src/workload_storage.cpp
    src/workload_secrets.cpp
)
target_include_directories(skeleton_planning PUBLIC include)
target_link_libraries(skeleton_planning PUBLIC skeleton_networks skeleton_vm skeleton_placement skeleton_cleanup skeleton_installation)

if(BUILD_TESTING)
    add_executable(skeleton_planning_tests tests/access_test.cpp tests/browser_proxy_test.cpp tests/router_test.cpp tests/hypervisor_test.cpp tests/pci_test.cpp tests/monitoring_test.cpp tests/workloads_test.cpp tests/reset_test.cpp tests/workload_compute_test.cpp tests/backup_test.cpp)
    target_link_libraries(skeleton_planning_tests PRIVATE skeleton_planning skeleton_test_support)
    gtest_discover_tests(skeleton_planning_tests DISCOVERY_TIMEOUT 30)
endif()
