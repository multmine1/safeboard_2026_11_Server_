#include "firewall_internal.hpp"

namespace skeleton {
namespace {

std::vector<std::string> MemberSources(const Topology& topology, const AddressPlan& addresses,
                                      const KubernetesPlan& cluster, const std::string& member) {
    if (!topology.IsRouter(member)) {
        std::vector<std::string> sources{FormatIpv4(addresses.vms.at(member).at("lan").address.address)};
        if (cluster.nodes.contains(member)) sources.push_back(cluster.nodes.at(member).pods.ToString());
        return sources;
    }
    std::set<std::string> sources;
    const auto& child = topology.router_networks.at(member);
    for (const auto& [id, network] : topology.vm_networks) {
        if (!topology.IsDescendant(network, child)) continue;
        for (const auto& [role, nic] : addresses.vms.at(id)) sources.insert(FormatIpv4(nic.address.address));
        if (cluster.nodes.contains(id)) sources.insert(cluster.nodes.at(id).pods.ToString());
    }
    return {sources.begin(), sources.end()};
}

void GuardRouterLan(const Topology& topology, const AddressPlan& addresses, const KubernetesPlan& cluster,
                     const std::string& network_id, FirewallRules& rules) {
    const auto lan = "iifname " + Json(InterfaceName(topology.definitions.site, "lan")).dump() + " ";
    std::vector<std::string> macs;
    for (const auto& member : topology.definitions.networks.at(network_id).members) {
        const auto role = topology.IsRouter(member) ? "wan" : "lan";
        const auto mac = addresses.vms.at(member).at(role).mac;
        macs.push_back(mac);
        rules.ingress_guards.insert(lan + "ether saddr " + mac + " ip saddr != " +
            FormatNftSet(MemberSources(topology, addresses, cluster, member)) + " drop");
    }
    rules.ingress_guards.insert(macs.empty() ? lan + "drop" : lan + "ether saddr != " + FormatNftSet(macs) + " drop");
}

}  // namespace

void CompileIngressGuards(const Topology& topology, const AddressPlan& addresses,
                          const KubernetesPlan& cluster, std::map<std::string, FirewallRules>& compiled) {
    const auto& site = topology.definitions.site;
    for (const auto& [id, vm] : topology.definitions.vms) {
        auto& guards = compiled[id].ingress_guards;
        const auto& network = topology.vm_networks.at(id);
        if (!topology.IsRouter(id) || network != topology.root) {
            const auto role = topology.IsRouter(id) ? "wan" : "lan";
            const auto parent_network = topology.IsRouter(id) ? topology.parents.at(network) : network;
            const auto& parent = topology.definitions.networks.at(parent_network).router;
            guards.insert("iifname " + Json(InterfaceName(site, role)).dump() + " ether saddr != " +
                addresses.vms.at(parent).at("lan").mac + " drop");
        }
        if (topology.IsRouter(id)) GuardRouterLan(topology, addresses, cluster, network, compiled[id]);
        if (cluster.nodes.contains(id))
            guards.insert("iifname " + Json(cluster.settings.at("pod_interface_glob")).dump() +
                " ip saddr != " + cluster.nodes.at(id).pods.ToString() + " drop");
    }
    const auto& root = topology.definitions.networks.at(topology.root).router;
    auto& guards = compiled.at(root).ingress_guards;
    const auto wan = "iifname " + Json(InterfaceName(site, "wan")).dump() + " ip saddr ";
    guards.insert(wan + site.ipv4_pool + " drop");
    if (!cluster.nodes.empty()) guards.insert(wan + cluster.configuration.at("spec").at("network").at("podCIDR").get<std::string>() + " drop");
}

}  // namespace skeleton
