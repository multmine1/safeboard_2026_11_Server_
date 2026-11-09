add_library(skeleton_pipeline STATIC
    src/generator.cpp
    src/reset.cpp
)
target_include_directories(skeleton_pipeline PUBLIC include)
target_link_libraries(skeleton_pipeline PUBLIC skeleton_input skeleton_planning skeleton_tofu skeleton_ansible skeleton_k0s skeleton_graphviz skeleton_report skeleton_output)
