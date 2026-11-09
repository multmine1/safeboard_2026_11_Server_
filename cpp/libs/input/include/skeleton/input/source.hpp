#pragma once

#include "skeleton/topology/model.hpp"

namespace skeleton {

SourceDefinitions ReadSourceDefinitions(const std::filesystem::path& directory);
std::vector<WorkloadDefinition> ReadWorkloadDefinitions(const Json& definitions, const Json& components);

}  // namespace skeleton
