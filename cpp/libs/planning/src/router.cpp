#include "guest.hpp"

namespace skeleton {
namespace {

Json BuildRuntimePayload(const SiteDefinition& site, const RouterRuntimePlan& router) {
    const auto filename = std::filesystem::path(router.settings.at("apply_playbook").get<std::string>()).filename().string();
    return {{filename, site.asset_data.at("router_apply")},
        {"network.yml", site.asset_data.at("network_tasks")},
        {"firewall.yml", site.asset_data.at("firewall_tasks")}, {"ufw.yml", site.asset_data.at("ufw_tasks")},
        {"configuration.json", Json{{"network", router.host_configuration}}.dump(2) + "\n"}};
}

RouterRuntimePlan BuildRouterRuntime(InfrastructurePlan& plan, const std::string& id, const std::string& network) {
    const auto& site = plan.topology.definitions.site;
    auto& vm = plan.vms.at(id);
    RouterRuntimePlan router;
    router.id = id; router.network = network;
    router.settings = plan.kubernetes.settings.at("router_runtime");
    const auto state = std::filesystem::path(router.settings.at("state_directory").get<std::string>());
    if (std::filesystem::path(router.settings.at("apply_playbook").get<std::string>()).parent_path() != state)
        throw ConfigurationError("router apply_playbook must be inside state_directory");
    const auto marker = (state / "owner").string();
    vm.variables["network_owner"] = {{"owner", "k0s"}, {"marker", marker}};
    router.host_configuration = vm.variables.at("network");
    router.host_configuration["owner_marker"] = marker;
    router.corefile = BuildClusterCorefile(plan.topology, plan.kubernetes, vm.dns, network);
    router.dns_data["Corefile"] = router.corefile;
    for (const auto& [path, data] : vm.dns.zones) router.dns_data[std::filesystem::path(path).filename().string()] = data;
    router.environment = plan.kubernetes.api_environment;
    router.payload = BuildRuntimePayload(site, router);
    router.fingerprint = std::to_string(StableHash(router.payload.dump() + router.dns_data.dump() +
        router.settings.dump() + router.environment.dump() + site.asset_data.at("dns_rbac").dump()));
    return router;
}

}  // namespace

std::map<std::string, RouterRuntimePlan> BuildRouterRuntimePlans(InfrastructurePlan& plan) {
    std::map<std::string, RouterRuntimePlan> routers;
    if (plan.kubernetes.nodes.empty()) return routers;
    for (const auto& [id, network] : plan.topology.definitions.networks)
        routers.emplace(network.router, BuildRouterRuntime(plan, network.router, id));
    return routers;
}

}  // namespace skeleton
