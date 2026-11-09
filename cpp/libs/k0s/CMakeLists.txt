add_library(skeleton_k0s STATIC
    src/native.cpp
    src/calico.cpp
    src/nodes.cpp
    src/export.cpp
    src/policies.cpp
    src/router.cpp
    src/stacks.cpp
    src/applications.cpp
)
target_include_directories(skeleton_k0s PUBLIC include)
target_link_libraries(skeleton_k0s PUBLIC skeleton_planning)

if(BUILD_TESTING)
    add_executable(skeleton_k0s_tests tests/export_test.cpp)
    target_link_libraries(skeleton_k0s_tests PRIVATE skeleton_k0s skeleton_test_support)
    gtest_discover_tests(skeleton_k0s_tests DISCOVERY_TIMEOUT 30)
endif()
