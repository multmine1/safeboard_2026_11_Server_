#pragma once

#include "skeleton/networks/kubernetes.hpp"

namespace skeleton {

void ResolveCommonPermissions(Topology& topology, const KubernetesPlan& cluster);

}  // namespace skeleton
