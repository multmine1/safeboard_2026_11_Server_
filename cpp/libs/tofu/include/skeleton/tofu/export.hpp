#pragma once

#include "skeleton/planning/infrastructure.hpp"

namespace skeleton {

void AddTofuConfigurations(GeneratedFiles& files, const InfrastructurePlan& plan);
Json ValidateDestroyPlan(const Json& scope, const Json& native_plan);
Json ValidateResetSnapshot(const Json& scope, const Json& snapshot);

}  // namespace skeleton
