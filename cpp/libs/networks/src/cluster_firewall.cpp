#include "firewall_internal.hpp"

namespace skeleton {
namespace {

bool HasPods(const KubernetesPlan& cluster, const std::string& id) {
    return cluster.nodes.contains(id);
}

void CompileBgp(const Topology& topology, const AddressPlan& addresses,
                 const KubernetesPlan& cluster, std::map<std::string, FirewallRules>& rules) {
    const auto port = cluster.settings.at("bgp").at("port").get<std::uint16_t>();
    const auto& site = topology.definitions.site;
    for (const auto& peer : cluster.peers) {
        const auto match = "ip saddr " + peer.source_address + " ip daddr " + peer.target_address +
            " ct state new tcp dport " + std::to_string(port) + " accept";
        rules[peer.from].output.insert("oifname " + Json(InterfaceName(site, peer.source_role)).dump() + " " + match);
        rules[peer.to].input.insert("iifname " + Json(InterfaceName(site, peer.target_role)).dump() + " ether saddr " +
            addresses.vms.at(peer.from).at(peer.source_role).mac + " " + match);
        rules[peer.from].guest_access.push_back({"out", peer.source_address, peer.target_address, "tcp", {port}});
        rules[peer.to].guest_access.push_back({"in", peer.source_address, peer.target_address, "tcp", {port}});
    }
}

void CompilePodVariants(const Topology& topology, const AddressPlan& addresses,
                         const KubernetesPlan& cluster, const ConnectionPlan& flow,
                         std::map<std::string, FirewallRules>& rules) {
    const bool source_pods = HasPods(cluster, flow.from);
    const bool target_pods = HasPods(cluster, flow.to);
    auto source_flow = flow;
    if (source_pods) {
        source_flow.source_address = cluster.nodes.at(flow.from).pods.ToString();
        source_flow.source_pod = true;
        CompileResolvedConnection(topology, addresses, source_flow, rules);
        if (flow.target_network.empty() && flow.nat_network.empty())
            rules.at(topology.definitions.networks.at(topology.root).router).postrouting.insert(
                "oifname " + Json(InterfaceName(topology.definitions.site, "wan")).dump() +
                " ip saddr " + source_flow.source_address + " masquerade");
    }
    if (!target_pods) return;
    const auto pods = cluster.nodes.at(flow.to).pods.ToString();
    if (!topology.IsRouter(flow.to))
        AddGuestAccess(topology, addresses, flow.to, {"forward-in",
            flow.nat_network.empty() ? flow.source_address : flow.nat_address, pods,
            flow.protocol, flow.target_ports}, rules[flow.to]);
    if (flow.dnat) return;
    auto target_flow = flow;
    target_flow.target_address = target_flow.frontend_address = pods;
    target_flow.target_pod = true;
    CompileResolvedConnection(topology, addresses, target_flow, rules);
    if (!source_pods) return;
    target_flow.source_address = source_flow.source_address;
    target_flow.source_pod = true;
    CompileResolvedConnection(topology, addresses, target_flow, rules);
}

void CompileLocalPods(const KubernetesPlan& cluster, const KubernetesNodePlan& node, FirewallRules& rules) {
    const auto pods = node.pods.ToString();
    const auto interface = Json(cluster.settings.at("pod_interface_glob")).dump();
    rules.input.insert("iifname " + interface + " ip saddr " + pods + " ip daddr " + node.address + " accept");
    rules.output.insert("oifname " + interface + " ip saddr " + node.address + " ip daddr " + pods + " accept");
    rules.output.insert("ct status dnat ip saddr " + node.address + " ip daddr " + node.address + " accept");
    rules.output.insert("ct status dnat ip saddr " + node.address + " ip daddr " + pods + " accept");
    rules.forward.insert("iifname " + interface + " oifname " + interface + " ip saddr " + pods + " ip daddr " + pods + " accept");
    rules.guest_access.push_back({"in", pods, node.address, "any", {}});
    rules.guest_access.push_back({"out", node.address, pods, "any", {}});
    rules.guest_access.push_back({"forward-in", pods, pods, "any", {}});
}

void CompilePublishedPodIngress(const Topology& topology, const AddressPlan& addresses,
                                 const KubernetesPlan& cluster, std::map<std::string, FirewallRules>& rules) {
    for (const auto& [id, network] : topology.definitions.networks)
        for (const auto& service : network.exports) {
            if (service.scope != "uplink" || !HasPods(cluster, service.vm)) continue;
            for (const auto& source : service.sources)
                AddGuestAccess(topology, addresses, service.vm,
                    {"forward-in", source, cluster.nodes.at(service.vm).pods.ToString(), service.protocol,
                        {service.target_port}}, rules[service.vm]);
        }
}

}  // namespace

void CompileClusterFirewall(const Topology& topology, const AddressPlan& addresses,
                            const KubernetesPlan& cluster, NetworkPolicyPlan& policy) {
    if (cluster.nodes.empty()) return;
    CompileBgp(topology, addresses, cluster, policy.firewalls);
    for (const auto& flow : policy.connections) CompilePodVariants(topology, addresses, cluster, flow, policy.firewalls);
    for (const auto& [id, node] : cluster.nodes) CompileLocalPods(cluster, node, policy.firewalls[id]);
    CompilePublishedPodIngress(topology, addresses, cluster, policy.firewalls);
}

}  // namespace skeleton
