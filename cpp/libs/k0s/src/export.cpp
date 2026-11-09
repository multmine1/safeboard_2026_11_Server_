#include "skeleton/k0s/export.hpp"
#include "native.hpp"

namespace skeleton {
namespace {

Json SerializeSystemReadiness(const std::vector<SystemWorkloadReadiness>& workloads) {
    Json readiness = Json::array();
    for (const auto& workload : workloads)
        readiness.push_back({{"namespace", workload.network_namespace}, {"resource", workload.resource},
            {"pod_selector", workload.pod_selector ? Json(*workload.pod_selector) : Json(nullptr)},
            {"nodes", workload.nodes}});
    return readiness;
}

}  // namespace

void AddK0sConfigurations(GeneratedFiles& files, const InfrastructurePlan& plan) {
    const auto& cluster = plan.kubernetes;
    if (cluster.nodes.empty()) return;
    files["k0s/configuration.json"] = cluster.configuration.dump(2) + "\n";
    files["k0s/calico.json"] = k0s::SerializeCalico(cluster).dump(2) + "\n";
    files["k0s/policies.json"] = k0s::SerializeNetworkPolicies(plan.topology, cluster, plan.policy).dump(2) + "\n";
    k0s::AddRouterConfigurations(files, plan);
    k0s::AddApplicationConfigurations(files, plan);
    Json order = Json::array({cluster.controller}), routers = Json::array();
    for (const auto& id : plan.topology.configuration_order)
        if (cluster.nodes.contains(id)) {
            if (id != cluster.controller) order.push_back(id);
            if (plan.routers.contains(id)) routers.push_back(id);
        }
    files["k0s/cluster.json"] = Json{{"controller", cluster.controller},
        {"order", order}, {"routers", routers}, {"stacks", k0s::SerializeManifestStacks(plan, routers)},
        {"system_readiness", SerializeSystemReadiness(plan.system_readiness)},
        {"settings", cluster.settings}}.dump(2) + "\n";
    for (const auto& [id, node] : cluster.nodes)
        files["k0s/nodes/" + id + ".json"] = k0s::SerializeNodeInstallation(cluster, node).dump(2) + "\n";
}

}  // namespace skeleton
