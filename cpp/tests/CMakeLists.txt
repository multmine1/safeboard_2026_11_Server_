add_executable(skeleton_integration_tests
    integration/export_test.cpp
    integration/services_test.cpp
    integration/reset_test.cpp
    integration/pci_test.cpp
    integration/host_requirements_test.cpp
    integration/installation_test.cpp
)
target_link_libraries(skeleton_integration_tests PRIVATE skeleton_pipeline skeleton_test_support)
gtest_discover_tests(skeleton_integration_tests DISCOVERY_TIMEOUT 30)
