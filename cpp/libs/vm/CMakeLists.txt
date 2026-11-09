add_library(skeleton_vm STATIC
    src/resources.cpp
    src/initialization.cpp
    src/storage.cpp
    src/pci.cpp
)
target_include_directories(skeleton_vm PUBLIC include)
target_link_libraries(skeleton_vm PUBLIC skeleton_contracts)

if(BUILD_TESTING)
    add_executable(skeleton_vm_tests tests/initialization_test.cpp tests/storage_test.cpp)
    target_link_libraries(skeleton_vm_tests PRIVATE skeleton_vm GTest::gtest_main)
    gtest_discover_tests(skeleton_vm_tests)
endif()
