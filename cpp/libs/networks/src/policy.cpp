#include "firewall_internal.hpp"

namespace skeleton {
namespace {

void AddPodDnsConnections(const Topology& topology, const AddressPlan& addresses,
                          const KubernetesPlan& cluster, std::vector<ConnectionPlan>& connections) {
    const auto port = topology.definitions.site.services.at("dns").at("port").get<std::uint16_t>();
    for (const auto& [id, node] : cluster.nodes) {
        if (node.role == "router") continue;
        const auto& router = topology.definitions.networks.at(node.network).router;
        for (const auto& protocol : {"tcp", "udp"})
            connections.push_back(ResolveConnection(topology, addresses, {id, router, protocol, {port}}));
    }
}

}  // namespace

NetworkPolicyPlan BuildNetworkPolicyPlan(const Topology& topology, const AddressPlan& addresses,
                                       const KubernetesPlan& cluster) {
    NetworkPolicyPlan policy;
    policy.connections = ResolveConnections(topology, addresses);
    AddPodDnsConnections(topology, addresses, cluster, policy.connections);
    policy.firewalls = CompileFirewallRules(topology, addresses, policy.connections);
    CompileIngressGuards(topology, addresses, cluster, policy.firewalls);
    CompileClusterFirewall(topology, addresses, cluster, policy);
    BuildPodAccess(topology, addresses, cluster, policy);
    return policy;
}

}  // namespace skeleton
