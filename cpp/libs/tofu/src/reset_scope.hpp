#pragma once

#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::tofu {

std::string ResourceAddress(const std::string& module, const std::string& type,
                            const std::string& name, const std::string& key);
Json BuildResetScope(const Json& networks, const Json& vm_data,
                     const std::map<std::string, VmPlan>& guests, const SiteDefinition& site);

}  // namespace skeleton::tofu
