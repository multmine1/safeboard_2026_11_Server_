#pragma once

#include "skeleton/planning/infrastructure.hpp"
#include "skeleton/cleanup/boundary.hpp"

namespace skeleton {

void AddResourceReport(GeneratedFiles& files, const InfrastructurePlan& plan);
void AddWorkloadResourceReport(GeneratedFiles& files, const InfrastructurePlan& plan);
void AddSoftwareReport(GeneratedFiles& files, const InfrastructurePlan& plan);
Json AddCleanupPreview(Json native_preview, const std::vector<RemovalTarget>& targets);

}  // namespace skeleton
