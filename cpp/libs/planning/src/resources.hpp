#pragma once

#include "skeleton/placement/resources.hpp"
#include "skeleton/networks/kubernetes.hpp"

namespace skeleton {

ResourcePlan BuildInfrastructureResources(const Topology& topology, const KubernetesPlan& kubernetes);

}  // namespace skeleton
