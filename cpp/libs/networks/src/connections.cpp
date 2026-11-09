#include "skeleton/networks/connections.hpp"

namespace skeleton {
namespace {

std::string LanAddress(const AddressPlan& addresses, const std::string& vm) {
    return FormatIpv4(addresses.vms.at(vm).at("lan").address.address);
}

void ResolveGateway(const Topology& topology, const AddressPlan& addresses, ConnectionPlan& flow) {
    if (flow.to == "hypervisor") {
        flow.nat_network = topology.root;
        const auto& root = topology.definitions.networks.at(topology.root).router;
        flow.nat_address = FormatIpv4(addresses.vms.at(root).at("wan").address.address);
        return;
    }
    if (flow.target_network.empty() || flow.source_network == flow.target_network) return;
    const auto& firewall = topology.definitions.site.services.at("firewall");
    if (!flow.dnat && !firewall.at("inter_network_snat").get<bool>()) return;
    flow.nat_network = topology.CommonAncestor(flow.source_network, flow.target_network);
    flow.nat_address = LanAddress(addresses, topology.definitions.networks.at(flow.nat_network).router);
    if (flow.dnat) flow.frontend_address = flow.nat_address;
}

void MergeEndpointPorts(Json& endpoint, const std::vector<std::uint16_t>& incoming) {
    auto ports = incoming;
    if (endpoint.contains("ports")) {
        const auto previous = endpoint.at("ports").get<std::vector<std::uint16_t>>();
        if (previous.empty() || incoming.empty()) ports.clear();
        else ports.insert(ports.end(), previous.begin(), previous.end());
    }
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    endpoint["ports"] = ports;
}

void FinalizeClientConnections(Json& connections) {
    for (auto& [name, connection] : connections.items()) {
        for (auto& [protocol, endpoint] : connection.at("endpoints").items()) {
            const auto& ports = endpoint.at("ports");
            if (ports.size() != 1) continue;
            endpoint["port"] = ports.front();
            endpoint["socket"] = connection.at("address").get<std::string>() + ":" +
                std::to_string(ports.front().get<unsigned>());
            endpoint["dns_socket"] = connection.at("fqdn").get<std::string>() + ":" +
                std::to_string(ports.front().get<unsigned>());
        }
        if (connection.at("endpoints").size() != 1) continue;
        const auto endpoint = connection.at("endpoints").begin();
        connection["protocol"] = endpoint.key();
        connection.merge_patch(endpoint.value());
    }
}

std::string ConnectionDomain(const Topology& topology, const ConnectionPlan& flow) {
    if (flow.to == "hypervisor") {
        if (flow.service_name.empty()) return topology.definitions.site.hypervisor_address;
        return topology.host_services.at(flow.service_name).fqdn;
    }
    if (flow.service_name.empty()) return topology.vm_domains.at(flow.to);
    return topology.service_domains.at(flow.service_name);
}

}  // namespace

ConnectionPlan ResolveConnection(const Topology& topology, const AddressPlan& addresses,
                                 const AccessRule& rule) {
    ConnectionPlan flow;
    flow.from = rule.from;
    flow.to = topology.service_networks.contains(rule.to) ? topology.Service(rule.to).vm : rule.to;
    if (rule.to.starts_with("hypervisor:")) {
        flow.to = "hypervisor";
        flow.service_name = rule.to.substr(std::string("hypervisor:").size());
    }
    flow.source_network = topology.vm_networks.at(flow.from);
    if (topology.vm_networks.contains(flow.to)) flow.target_network = topology.vm_networks.at(flow.to);
    auto source_role = "lan";
    if (topology.IsRouter(flow.from) && !topology.IsDescendant(flow.target_network, flow.source_network)) source_role = "wan";
    flow.source_address = FormatIpv4(addresses.vms.at(flow.from).at(source_role).address.address);
    if (!flow.target_network.empty()) flow.target_address = flow.frontend_address = LanAddress(addresses, flow.to);
    else if (flow.to == "hypervisor")
        flow.target_address = flow.frontend_address = topology.definitions.site.hypervisor_address;
    flow.protocol = rule.protocol;
    flow.frontend_ports = flow.target_ports = rule.ports;
    if (topology.service_networks.contains(rule.to)) {
        const auto& service = topology.Service(rule.to);
        flow.service_name = service.name;
        flow.target_ports = {service.target_port};
        flow.dnat = flow.source_network != flow.target_network;
        if (!flow.dnat) flow.frontend_ports = flow.target_ports;
    }
    flow.routers = topology.RouterPath(flow.source_network, flow.target_network.empty() ? topology.root : flow.target_network);
    ResolveGateway(topology, addresses, flow);
    return flow;
}

std::vector<ConnectionPlan> ResolveConnections(const Topology& topology, const AddressPlan& addresses) {
    std::vector<ConnectionPlan> connections;
    for (const auto& [id, network] : topology.definitions.networks)
        for (const auto& rule : network.allow) connections.push_back(ResolveConnection(topology, addresses, rule));
    return connections;
}

NetworkLinkPlan ResolvePhysicalLink(const AddressPlan& addresses, const std::string& from, const std::string& to) {
    for (const auto& [source_role, source] : addresses.vms.at(from))
        for (const auto& [target_role, target] : addresses.vms.at(to))
            if (!source.network.empty() && source.network == target.network)
                return {from, to, source_role, target_role, source.network};
    throw ConfigurationError("no shared network for path hop " + from + " -> " + to);
}

Json GenerateClientConnections(const Topology& topology, const std::vector<ConnectionPlan>& flows,
                                const std::string& vm_id) {
    Json connections = Json::object();
    for (const auto& flow : flows) {
        if (flow.from != vm_id || flow.to == "internet") continue;
        const bool host = flow.to == "hypervisor";
        const auto name = !flow.service_name.empty() ? flow.service_name : host ? flow.to : topology.vm_domains.at(flow.to);
        const auto names = !host && !flow.service_name.empty() ? topology.ServiceNamed(name).Names()
                                                              : std::vector<std::string>{name};
        for (const auto& alias : names) {
            auto& endpoint = connections[alias];
            endpoint["address"] = flow.frontend_address;
            endpoint["fqdn"] = alias == name ? ConnectionDomain(topology, flow) : topology.service_domains.at(alias);
            MergeEndpointPorts(endpoint["endpoints"][flow.protocol], flow.frontend_ports);
        }
    }
    FinalizeClientConnections(connections);
    return connections;
}

}  // namespace skeleton
