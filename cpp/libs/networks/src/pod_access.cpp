#include "firewall_internal.hpp"

namespace skeleton {
namespace {

std::string HostCidr(const std::string& address) {
    return address.find('/') == std::string::npos ? address + "/32" : address;
}

void AddOutgoingPodAccess(const Topology& topology, const KubernetesPlan& cluster,
                           const ConnectionPlan& flow, std::vector<PodAccessRule>& rules) {
    PodAccessRule rule{"out", {}, {}, flow.protocol, flow.frontend_ports};
    if (flow.frontend_address.empty()) {
        rule.peers = {"0.0.0.0/0"};
        rule.exclusions = InternetExclusions(topology.definitions.site);
    } else {
        rule.peers.push_back(HostCidr(flow.frontend_address));
        if (cluster.nodes.contains(flow.to)) rule.peers.push_back(cluster.nodes.at(flow.to).pods.ToString());
    }
    rules.push_back(std::move(rule));
}

std::vector<std::string> IncomingSources(const KubernetesPlan& cluster, const ConnectionPlan& flow) {
    std::vector<std::string> sources;
    if (!flow.nat_network.empty()) sources.push_back(HostCidr(flow.nat_address));
    else {
        sources.push_back(HostCidr(flow.source_address));
        if (cluster.nodes.contains(flow.from)) sources.push_back(cluster.nodes.at(flow.from).pods.ToString());
    }
    return sources;
}

void AddPublishedPodAccess(const Topology& topology, const KubernetesPlan& cluster,
                            std::map<std::string, std::vector<PodAccessRule>>& rules) {
    for (const auto& [id, network] : topology.definitions.networks)
        for (const auto& service : network.exports) {
            if (service.scope != "uplink" || !cluster.nodes.contains(service.vm)) continue;
            rules[service.vm].push_back({"in", service.sources, {}, service.protocol, {service.target_port}});
        }
}

}  // namespace

void BuildPodAccess(const Topology& topology, const AddressPlan&,
                    const KubernetesPlan& cluster, NetworkPolicyPlan& policy) {
    for (const auto& [id, node] : cluster.nodes) {
        const std::vector<std::string> local{node.pods.ToString(), HostCidr(node.address)};
        policy.pod_access[id].push_back({"in", local, {}, "any", {}});
        policy.pod_access[id].push_back({"out", local, {}, "any", {}});
    }
    for (const auto& flow : policy.connections) {
        if (cluster.nodes.contains(flow.from))
            AddOutgoingPodAccess(topology, cluster, flow, policy.pod_access[flow.from]);
        if (!cluster.nodes.contains(flow.to)) continue;
        const auto sources = IncomingSources(cluster, flow);
        policy.pod_access[flow.to].push_back({"in", sources, {}, flow.protocol, flow.target_ports});
        if (!flow.service_name.empty()) {
            auto& clients = policy.service_clients[flow.to][flow.service_name];
            clients.insert(sources.begin(), sources.end());
        }
    }
    AddPublishedPodAccess(topology, cluster, policy.pod_access);
}

}  // namespace skeleton
