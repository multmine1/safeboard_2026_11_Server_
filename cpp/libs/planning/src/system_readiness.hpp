#pragma once

#include "skeleton/planning/infrastructure.hpp"

namespace skeleton {

std::vector<SystemWorkloadReadiness> BuildSystemReadiness(const KubernetesPlan& kubernetes);

}  // namespace skeleton
