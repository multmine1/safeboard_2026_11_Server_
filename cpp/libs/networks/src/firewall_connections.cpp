#include "firewall_internal.hpp"

namespace skeleton {

void AddGuestAccess(const Topology& topology, const AddressPlan& plan, const std::string& vm_id,
                     const GuestAccessRule& access, FirewallRules& rules) {
    const auto& site = topology.definitions.site;
    const auto lan = Json(InterfaceName(site, "lan")).dump();
    std::string match;
    const bool incoming = access.direction == "in" || access.direction == "forward-in";
    const bool forwarded = access.direction.starts_with("forward-");
    const auto pods = forwarded ? Json(site.services.at("kubernetes").at("pod_interface_glob")).dump() : "";
    if (incoming) {
        const auto& router = topology.definitions.networks.at(topology.vm_networks.at(vm_id)).router;
        match = "iifname " + lan + " ";
        if (forwarded) match += "oifname " + pods + " ";
        match += "ether saddr " + plan.vms.at(router).at("lan").mac + " ";
    } else match = (forwarded ? "iifname " + pods + " " : "") + "oifname " + lan + " ";
    match += "ip saddr " + access.source + " ";
    match += access.destination == "any" ? FormatInternetDestination(site) : "ip daddr " + access.destination;
    match += " ct state new " + FormatPortMatch(access.protocol, access.ports) + " accept";
    (forwarded ? rules.forward : incoming ? rules.input : rules.output).insert(match);
    rules.guest_access.push_back(access);
}

namespace {

std::string RouterFlowInterfaces(const Topology& topology, const AddressPlan& plan,
                                 const ConnectionPlan& flow, const std::string& network) {
    const auto& router = topology.definitions.networks.at(network).router;
    auto interfaces = FormatFlowInterfaces(topology, plan, network, flow.source_network, flow.target_network,
                                           router == flow.from, router == flow.to);
    if ((router == flow.from && flow.source_pod) || (router == flow.to && flow.target_pod)) {
        const auto pods = Json(topology.definitions.site.services.at("kubernetes").at("pod_interface_glob")).dump();
        if (router == flow.from && flow.source_pod) interfaces = "iifname " + pods + " " + interfaces;
        if (router == flow.to && flow.target_pod) interfaces += "oifname " + pods + " ";
    }
    return interfaces;
}

void CompileRouterPermissions(const Topology& topology, const AddressPlan& plan, const ConnectionPlan& flow,
    std::map<std::string, FirewallRules>& compiled) {
    bool after_nat = false;
    for (const auto& network : flow.routers) {
        const auto& router = topology.definitions.networks.at(network).router;
        const bool at_nat = network == flow.nat_network;
        const auto source = after_nat ? flow.nat_address : flow.source_address;
        const auto destination = flow.dnat && !after_nat && !at_nat ? flow.frontend_address : flow.target_address;
        const auto& ports = flow.dnat && !after_nat && !at_nat ? flow.frontend_ports : flow.target_ports;
        const bool local_source = router == flow.from && !flow.source_pod;
        const bool local_target = router == flow.to && !flow.target_pod;
        const auto interfaces = RouterFlowInterfaces(topology, plan, flow, network);
        SelectFirewallChain(compiled[router], local_source, local_target).insert(
            interfaces + "ip saddr " + source + " " + (destination.empty()
                ? FormatInternetDestination(topology.definitions.site) : "ip daddr " + destination) +
            " ct state new " + FormatPortMatch(flow.protocol, ports) + " accept");
        compiled[router].guest_access.push_back({local_source ? "out" : local_target ? "in" : "forward",
            source, destination.empty() ? "any" : destination, flow.protocol, ports});
        if (at_nat) after_nat = true;
    }
}

void CompileGuestEndpoints(const Topology& topology, const AddressPlan& plan, const ConnectionPlan& flow,
    std::map<std::string, FirewallRules>& compiled) {
    if (!topology.IsRouter(flow.from))
        AddGuestAccess(topology, plan, flow.from, {flow.source_pod ? "forward-out" : "out", flow.source_address,
            flow.frontend_address.empty() ? "any" : flow.frontend_address, flow.protocol, flow.frontend_ports}, compiled[flow.from]);
    if (!flow.target_network.empty() && !topology.IsRouter(flow.to))
        AddGuestAccess(topology, plan, flow.to, {flow.target_pod ? "forward-in" : "in", flow.nat_network.empty() ? flow.source_address : flow.nat_address,
            flow.target_address, flow.protocol, flow.target_ports}, compiled[flow.to]);
}

void CompileNat(const Topology& topology, const ConnectionPlan& flow,
                std::map<std::string, FirewallRules>& compiled) {
    if (!flow.nat_network.empty()) {
        auto& gateway = compiled[topology.definitions.networks.at(flow.nat_network).router];
        if (flow.dnat)
            gateway.prerouting.insert("ip saddr " + flow.source_address + " ip daddr " + flow.frontend_address + " " +
                FormatPortMatch(flow.protocol, flow.frontend_ports) + " dnat to " + flow.target_address + ":" + std::to_string(flow.target_ports.front()));
        gateway.postrouting.insert("ip saddr " + flow.source_address + " ip daddr " + flow.target_address + " " +
            FormatPortMatch(flow.protocol, flow.target_ports) + (flow.dnat ? " ct status dnat" : "") + " snat to " + flow.nat_address);
    }
}

}  // namespace

void CompileResolvedConnection(const Topology& topology, const AddressPlan& plan, const ConnectionPlan& flow,
    std::map<std::string, FirewallRules>& compiled) {
    CompileRouterPermissions(topology, plan, flow, compiled);
    CompileGuestEndpoints(topology, plan, flow, compiled);
    CompileNat(topology, flow, compiled);
    if (flow.target_network.empty()) compiled.at(topology.definitions.networks.at(topology.root).router).masquerade = true;
}

}  // namespace skeleton
