#pragma once
#include "skeleton/graphviz/export.hpp"
namespace skeleton {
void AddGraphvizImage(GeneratedFiles& files, const std::filesystem::path& stem, const std::string& graph);
void AddAllowedPathGraphs(GeneratedFiles& files, const InfrastructurePlan& plan);
}  // namespace skeleton
