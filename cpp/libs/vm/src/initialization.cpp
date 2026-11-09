#include "skeleton/vm/initialization.hpp"

namespace skeleton {

VmInitializationPlan BuildVmInitialization(const VmDefinition& vm,
    const VmInitializationSettings& settings, const Json& network, const Json& files) {
    auto user = settings.user_defaults;
    user["name"] = settings.bootstrap_user;
    user["ssh_authorized_keys"] = Json::array({settings.bootstrap_public_key});
    auto data = settings.cloud_init_defaults;
    data["hostname"] = vm.name;
    data["fqdn"] = settings.fqdn;
    data["users"] = Json::array({user});
    if (!data.contains("write_files")) data["write_files"] = Json::array();
    for (const auto& [path, content] : files.items())
        data["write_files"].push_back({{"path", path}, {"content", content}});
    return {{{"instance-id", vm.id}, {"local-hostname", vm.name}}, std::move(data), network};
}

}  // namespace skeleton
