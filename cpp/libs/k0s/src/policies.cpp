#include "native.hpp"

#include <cctype>

namespace skeleton::k0s {
namespace {

Json SerializePodRule(const PodAccessRule& access) {
    const auto peer_side = access.direction == "in" ? "source" : "destination";
    Json rule{{"action", "Allow"}, {peer_side, {{"nets", access.peers}}}};
    if (!access.exclusions.empty()) rule[peer_side]["notNets"] = access.exclusions;
    if (access.protocol == "any") return rule;
    auto protocol = access.protocol;
    std::transform(protocol.begin(), protocol.end(), protocol.begin(), [](unsigned char c) { return std::toupper(c); });
    rule["protocol"] = protocol;
    if (!access.ports.empty()) rule["destination"]["ports"] = access.ports;
    return rule;
}

Json SerializeVmPolicy(const KubernetesNodePlan& node, const std::string& network_namespace,
                       const std::vector<PodAccessRule>& access) {
    Json spec{{"selector", "skeleton.vm == '" + node.id + "'"},
        {"types", {"Ingress", "Egress"}}, {"ingress", Json::array()}, {"egress", Json::array()}};
    for (const auto& rule : access)
        spec[rule.direction == "in" ? "ingress" : "egress"].push_back(SerializePodRule(rule));
    return {{"apiVersion", "crd.projectcalico.org/v1"}, {"kind", "NetworkPolicy"},
        {"metadata", {{"name", "vm-" + node.id}, {"namespace", network_namespace}}}, {"spec", spec}};
}

}  // namespace

Json SerializeNetworkPolicies(const Topology& topology, const KubernetesPlan& cluster,
                              const NetworkPolicyPlan& policy) {
    Json items = Json::array();
    for (const auto& [id, network] : topology.definitions.networks) {
        const auto& name = cluster.namespaces.at(id);
        items.push_back({{"apiVersion", "v1"}, {"kind", "Namespace"},
            {"metadata", {{"name", name}, {"labels", {{"skeleton.network", id}}}}}});
        items.push_back({{"apiVersion", "crd.projectcalico.org/v1"}, {"kind", "NetworkPolicy"},
            {"metadata", {{"name", "default-deny"}, {"namespace", name}}},
            {"spec", {{"selector", "all()"}, {"types", {"Ingress", "Egress"}}}}});
    }
    for (const auto& [id, node] : cluster.nodes)
        items.push_back(SerializeVmPolicy(node, cluster.namespaces.at(node.network), policy.pod_access.at(id)));
    return {{"apiVersion", "v1"}, {"kind", "List"}, {"items", items}};
}

}  // namespace skeleton::k0s
