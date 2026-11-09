find_path(Graphviz_INCLUDE_DIR graphviz/gvc.h HINTS "$ENV{CONDA_PREFIX}/include" REQUIRED)
find_library(Graphviz_GVC gvc HINTS "$ENV{CONDA_PREFIX}/lib" REQUIRED)
find_library(Graphviz_CGRAPH cgraph HINTS "$ENV{CONDA_PREFIX}/lib" REQUIRED)

add_library(skeleton_graphviz STATIC
    src/topology.cpp
    src/paths.cpp
    src/svg.cpp
)
target_include_directories(skeleton_graphviz PUBLIC include)
target_link_libraries(skeleton_graphviz PUBLIC skeleton_planning)
target_link_libraries(skeleton_graphviz PRIVATE "${Graphviz_GVC}" "${Graphviz_CGRAPH}")
target_include_directories(skeleton_graphviz PRIVATE "${Graphviz_INCLUDE_DIR}")

if(BUILD_TESTING)
    add_executable(skeleton_graphviz_tests tests/paths_test.cpp)
    target_link_libraries(skeleton_graphviz_tests PRIVATE skeleton_graphviz skeleton_test_support)
    gtest_discover_tests(skeleton_graphviz_tests DISCOVERY_TIMEOUT 30)
endif()
