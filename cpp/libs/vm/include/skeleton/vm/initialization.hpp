#pragma once

#include "skeleton/vm/types.hpp"

namespace skeleton {

struct VmInitializationPlan {
    Json metadata;
    Json user_data;
    Json network;
};

struct VmInitializationSettings {
    std::string fqdn;
    std::string bootstrap_user;
    std::string bootstrap_public_key;
    Json user_defaults;
    Json cloud_init_defaults;
};

VmInitializationPlan BuildVmInitialization(const VmDefinition& vm,
    const VmInitializationSettings& settings, const Json& network, const Json& files);

}  // namespace skeleton
