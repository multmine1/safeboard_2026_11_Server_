#pragma once

#include "skeleton/placement/workloads.hpp"

namespace skeleton {

struct WorkloadDefinition {
    std::string name;
    std::string vm;
    bool enabled;
    Json specification;
    Json patches;
    std::map<std::string, PodDefinition> compute;
};

}  // namespace skeleton
