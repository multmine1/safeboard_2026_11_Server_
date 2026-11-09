add_library(skeleton_tofu STATIC
    src/export.cpp
    src/reset_scope.cpp
    src/reset_validation.cpp
)
target_include_directories(skeleton_tofu PUBLIC include)
target_link_libraries(skeleton_tofu PUBLIC skeleton_planning)

if(BUILD_TESTING)
    add_executable(skeleton_tofu_tests tests/reset_test.cpp)
    target_link_libraries(skeleton_tofu_tests PRIVATE skeleton_tofu GTest::gtest_main)
    gtest_discover_tests(skeleton_tofu_tests)
endif()
