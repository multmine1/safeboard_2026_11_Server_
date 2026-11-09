#include "native.hpp"

#include <cctype>

namespace skeleton::k0s {
namespace {

Json ManifestStack(const Json& settings, const std::string& name, Json files) {
    const auto& root = settings.at("paths").at("manifest_directory").get<std::string>();
    return {{"name", name}, {"directory", (std::filesystem::path(root) / name).string()}, {"files", files}};
}

}  // namespace

Json SerializeManifestStacks(const InfrastructurePlan& plan, const Json& router_order) {
    const auto& settings = plan.kubernetes.settings;
    const auto prefix = settings.at("deployment").at("stack_prefix").get<std::string>();
    if (prefix.empty() || !std::all_of(prefix.begin(), prefix.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-';
        })) throw ConfigurationError("k0s stack_prefix must contain only letters, digits and hyphens");
    Json base = Json::array();
    base.push_back(ManifestStack(settings, prefix + "-network", {
        {"calico.yaml", "k0s/calico.json"}, {"policies.yaml", "k0s/policies.json"}}));
    base.push_back(ManifestStack(settings, prefix + "-router-rbac", {{"rbac.yaml", "k0s/router-rbac.json"}}));
    Json routers = Json::array();
    for (const auto& value : router_order) {
        const auto id = value.get<std::string>();
        auto stack = ManifestStack(settings, prefix + "-router-" + id,
            {{"router.yaml", "k0s/routers/" + id + "/manifests.json"}});
        stack["id"] = id;
        stack["fingerprint"] = plan.routers.at(id).fingerprint;
        routers.push_back(std::move(stack));
    }
    return {{"base", base}, {"routers", routers}};
}

}  // namespace skeleton::k0s
