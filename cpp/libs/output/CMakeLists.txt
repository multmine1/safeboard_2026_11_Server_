add_library(skeleton_output STATIC
    src/publication.cpp
)
target_include_directories(skeleton_output PUBLIC include)
target_link_libraries(skeleton_output PUBLIC skeleton_contracts)

if(BUILD_TESTING)
    add_executable(skeleton_output_tests tests/publication_test.cpp)
    target_include_directories(skeleton_output_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
    target_link_libraries(skeleton_output_tests PRIVATE skeleton_output GTest::gtest_main)
    gtest_discover_tests(skeleton_output_tests)
endif()
