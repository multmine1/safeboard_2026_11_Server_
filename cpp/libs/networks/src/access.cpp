#include "skeleton/networks/access.hpp"

namespace skeleton {
namespace {

void AddControlPermission(Topology& topology, const std::string& from, const std::string& to,
                          std::vector<std::uint16_t> ports) {
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    topology.definitions.networks.at(topology.vm_networks.at(from)).allow.push_back({from, to, "tcp", std::move(ports)});
}

void ResolveClusterPermissions(Topology& topology, const KubernetesPlan& cluster) {
    if (cluster.nodes.empty()) return;
    const auto api = cluster.configuration.at("spec").at("api").at("port").get<std::uint16_t>();
    const auto& ports = cluster.settings.at("ports");
    for (const auto& [id, node] : cluster.nodes) {
        if (id == cluster.controller) continue;
        AddControlPermission(topology, id, cluster.controller, {api});
        AddControlPermission(topology, cluster.controller, id,
            {ports.at("kubelet").get<std::uint16_t>()});
    }
}

}  // namespace

void ResolveCommonPermissions(Topology& topology, const KubernetesPlan& cluster) {
    for (const auto& [id, vm] : topology.definitions.vms) {
        auto& allow = topology.definitions.networks.at(topology.vm_networks.at(id)).allow;
        for (auto rule : topology.definitions.site.common_allow) {
            rule.from = id;
            allow.push_back(std::move(rule));
        }
    }
    ResolveClusterPermissions(topology, cluster);
}

}  // namespace skeleton
