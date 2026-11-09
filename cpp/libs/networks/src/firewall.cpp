#include "firewall_internal.hpp"

namespace skeleton {
namespace {

void CompileUplinkExports(const Topology& topology, const AddressPlan& plan,
                          std::map<std::string, FirewallRules>& compiled) {
    const auto& root = topology.definitions.networks.at(topology.root).router;
    const auto& site = topology.definitions.site;
    const auto wan = Json(InterfaceName(site, "wan")).dump();
    for (const auto& [id, network] : topology.definitions.networks) {
        for (const auto& service : network.exports) {
            if (service.scope != "uplink") continue;
            const auto destination = FormatIpv4(plan.vms.at(service.vm).at("lan").address.address);
            for (const auto& source : service.sources) {
                ParseCidr(source);
                compiled[root].prerouting.insert("iifname " + wan + " ip saddr " + source + " ip daddr " +
                    FormatIpv4(plan.vms.at(root).at("wan").address.address) + " " + FormatPortMatch(service.protocol, {service.port}) +
                    " dnat to " + destination + ":" + std::to_string(service.target_port));
                for (const auto& hop : topology.RouterPath(topology.root, id)) {
                    const auto& router = topology.definitions.networks.at(hop).router;
                    compiled[router].forward.insert(FormatFlowInterfaces(topology, plan, hop, {}, id, false, false) +
                        "ip saddr " + source + " ip daddr " + destination + " ct state new " +
                        FormatPortMatch(service.protocol, {service.target_port}) + " accept");
                    compiled[router].guest_access.push_back({"forward", source, destination,
                        service.protocol, {service.target_port}});
                }
                AddGuestAccess(topology, plan, service.vm,
                    {"in", source, destination, service.protocol, {service.target_port}}, compiled[service.vm]);
            }
        }
    }
}

void AddDnsPair(FirewallRules& rules, const std::string& direction, const std::string& source,
                const std::string& destination, const std::string& match, std::uint16_t port) {
    for (const auto& protocol : {"tcp", "udp"}) {
        (direction == "in" ? rules.input : rules.output).insert(match + protocol + " dport " + std::to_string(port) + " accept");
        rules.guest_access.push_back({direction, source, destination, protocol, {port}});
    }
}

void CompileRouterDns(const Topology& topology, const AddressPlan& plan,
                       const std::string& network_id, FirewallRules& rules) {
    const auto& site = topology.definitions.site;
    const auto port = site.services.at("dns").at("port").get<std::uint16_t>();
    const auto lan = Json(InterfaceName(site, "lan")).dump();
    const auto wan = Json(InterfaceName(site, "wan")).dump();
    const auto subnet = plan.subnets.at(network_id).ToString();
    AddDnsPair(rules, "in", subnet, "any", "iifname " + lan + " ip saddr " + subnet + " ", port);
    auto upstreams = site.dns_upstreams;
    if (network_id == topology.root) {
        AddDnsPair(rules, "in", site.hypervisor_address, "any", "iifname " + wan + " ip saddr " + site.hypervisor_address + " ", port);
    } else {
        const auto& parent = topology.definitions.networks.at(topology.parents.at(network_id)).router;
        const auto parent_ip = FormatIpv4(plan.vms.at(parent).at("lan").address.address);
        AddDnsPair(rules, "in", parent_ip, "any", "iifname " + wan + " " + ParentMacGuard(topology, plan, network_id, "wan") +
                                "ip saddr " + parent_ip + " ", port);
        upstreams = {parent_ip};
    }
    for (const auto& [child, parent] : topology.parents)
        if (parent == network_id)
            upstreams.push_back(FormatIpv4(plan.vms.at(topology.definitions.networks.at(child).router).at("wan").address.address));
    const auto& configured = topology.definitions.networks.at(network_id).dns_upstreams;
    if (!configured.empty()) upstreams.insert(upstreams.end(), configured.begin(), configured.end());
    for (const auto& address : upstreams) AddDnsPair(rules, "out", "any", address, "ip daddr " + address + " ", port);
}

void CompileExternalRouterDns(const Topology& topology, const AddressPlan& plan,
                              std::map<std::string, FirewallRules>& compiled) {
    const auto port = topology.definitions.site.services.at("dns").at("port").get<std::uint16_t>();
    for (const auto& [id, network] : topology.definitions.networks) {
        if (id == topology.root) continue;
        for (const auto& upstream : network.dns_upstreams) {
            ParseIpv4(upstream);
            for (const auto& protocol : {"tcp", "udp"}) {
                auto flow = ResolveConnection(topology, plan, {network.router, "internet", protocol, {port}});
                flow.target_address = flow.frontend_address = upstream;
                CompileResolvedConnection(topology, plan, flow, compiled);
            }
        }
    }
}

void CompileManagement(const Topology& topology, const AddressPlan& plan,
                        std::map<std::string, FirewallRules>& compiled) {
    const auto& site = topology.definitions.site;
    const auto port = site.services.at("ssh").at("port").get<unsigned>();
    for (const auto& [vm_id, vm] : topology.definitions.vms) {
        const auto& target = topology.vm_networks.at(vm_id);
        const auto path = topology.RouterPath(topology.root, target);
        const auto destination = FormatIpv4(plan.vms.at(vm_id).at("lan").address.address);
        for (const auto& network : path) {
            const auto& router = topology.definitions.networks.at(network).router;
            const auto interfaces = FormatFlowInterfaces(topology, plan, network, {}, target, false, router == vm_id);
            for (const auto& source : site.management_sources) {
                SelectFirewallChain(compiled[router], false, router == vm_id).insert(
                    interfaces + "ip saddr " + source + " ip daddr " + destination +
                    " ct state new tcp dport " + std::to_string(port) + " accept");
                compiled[router].guest_access.push_back({router == vm_id ? "in" : "forward", source,
                    destination, "tcp", {static_cast<std::uint16_t>(port)}});
            }
        }
        if (!topology.IsRouter(vm_id))
            for (const auto& source : site.management_sources)
                AddGuestAccess(topology, plan, vm_id,
                    {"in", source, destination, "tcp", {static_cast<std::uint16_t>(port)}}, compiled[vm_id]);
    }
    const auto& root = topology.definitions.networks.at(topology.root).router;
    for (const auto& source : site.management_sources) {
        compiled[root].input.insert("iifname " + Json(InterfaceName(site, "wan")).dump() + " ip saddr " + source +
                                     " tcp dport " + std::to_string(port) + " accept");
        compiled[root].guest_access.push_back({"in", source, "any", "tcp", {static_cast<std::uint16_t>(port)}});
    }
}

void CompileGuestDns(const Topology& topology, const AddressPlan& plan,
                      std::map<std::string, FirewallRules>& compiled) {
    const auto port = topology.definitions.site.services.at("dns").at("port").get<std::uint16_t>();
    for (const auto& [id, vm] : topology.definitions.vms) {
        if (topology.IsRouter(id)) continue;
        const auto& router = topology.definitions.networks.at(topology.vm_networks.at(id)).router;
        const auto source = FormatIpv4(plan.vms.at(id).at("lan").address.address);
        const auto destination = FormatIpv4(plan.vms.at(router).at("lan").address.address);
        for (const auto& protocol : {"tcp", "udp"})
            AddGuestAccess(topology, plan, id, {"out", source, destination, protocol, {port}}, compiled[id]);
    }
}

}  // namespace

std::map<std::string, FirewallRules> CompileFirewallRules(const Topology& topology, const AddressPlan& plan) {
    return CompileFirewallRules(topology, plan, ResolveConnections(topology, plan));
}

std::map<std::string, FirewallRules> CompileFirewallRules(const Topology& topology, const AddressPlan& plan,
                                                       const std::vector<ConnectionPlan>& connections) {
    std::map<std::string, FirewallRules> compiled;
    for (const auto& [id, network] : topology.definitions.networks)
        CompileRouterDns(topology, plan, id, compiled[network.router]);
    CompileManagement(topology, plan, compiled);
    CompileGuestDns(topology, plan, compiled);
    CompileExternalRouterDns(topology, plan, compiled);
    for (const auto& flow : connections) CompileResolvedConnection(topology, plan, flow, compiled);
    CompileUplinkExports(topology, plan, compiled);
    return compiled;
}

}  // namespace skeleton
