#include "graph.hpp"
#include "skeleton/networks/connections.hpp"

#include <sstream>

namespace skeleton {
namespace {

using VmPair = std::pair<std::string, std::string>;

std::string DescribeProtocol(const AccessRule& rule) {
    std::string result = rule.protocol;
    if (rule.ports.empty()) return result;
    result += '/';
    for (const auto port : rule.ports) {
        if (result.back() != '/') result += ',';
        result += std::to_string(port);
    }
    return result;
}

std::set<std::string> DescribeFlows(const AllowedVmPath& path) {
    std::set<std::string> descriptions;
    for (const auto& flow : path.flows) {
        auto description = DescribeProtocol({flow.from, flow.to, flow.protocol, flow.frontend_ports});
        if (flow.dnat) description += " [DNAT " + flow.frontend_address + ":" +
            std::to_string(flow.frontend_ports.front()) + " -> " + flow.target_address + ":" +
            std::to_string(flow.target_ports.front()) + "]";
        if (!flow.nat_network.empty()) description += " [SNAT " + flow.source_address + " -> " + flow.nat_address + "]";
        descriptions.insert(std::move(description));
    }
    return descriptions;
}

std::string DescribeInterface(const SiteDefinition& site, const NicPlan& nic) {
    return InterfaceName(site, nic.role) + " (" + nic.role + "): " + nic.address.ToString();
}

void WritePathNodes(std::ostream& graph, const Topology& topology, const AddressPlan& plan,
                     const KubernetesPlan& cluster, const AllowedVmPath& path) {
    const auto& nodes = path.nodes;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        std::set<std::string> interfaces;
        const auto& site = topology.definitions.site;
        if (i > 0) interfaces.insert(DescribeInterface(site, plan.vms.at(nodes[i]).at(path.links[i - 1].target_role)));
        if (i + 1 < nodes.size()) interfaces.insert(DescribeInterface(site, plan.vms.at(nodes[i]).at(path.links[i].source_role)));
        auto label = topology.vm_domains.at(nodes[i]) + (topology.IsRouter(nodes[i]) ? "\nrouter" : "\nVM");
        if (cluster.nodes.contains(nodes[i]))
            label += "\nPods: " + cluster.nodes.at(nodes[i]).pods.ToString();
        for (const auto& interface : interfaces) label += "\n" + interface;
        graph << "    " << Json(nodes[i]).dump() << " [label=" << Json(label).dump() << "];\n";
    }
}

void WritePathLinks(std::ostream& graph, const Topology& topology, const AddressPlan& plan,
                     const std::vector<NetworkLinkPlan>& links) {
    const auto& site = topology.definitions.site;
    for (const auto& link : links) {
        const auto label = InterfaceName(site, link.source_role) + " -> " + InterfaceName(site, link.target_role) +
            "\n" + topology.network_domains.at(link.network) + "\n" + plan.bridges.at(link.network);
        graph << "    " << Json(link.from).dump() << " -> " << Json(link.to).dump()
              << " [label=" << Json(label).dump() << "];\n";
    }
}

std::string GenerateAllowedPath(const Topology& topology, const AddressPlan& plan,
                                const KubernetesPlan& cluster, const VmPair& pair, const AllowedVmPath& path) {
    auto label = topology.vm_domains.at(pair.first) + " -> " + topology.vm_domains.at(pair.second);
    label += "\nAllowed NEW:";
    for (const auto& protocol : DescribeFlows(path)) label += " " + protocol;
    label += "\nReplies: established,related along the same path";
    std::ostringstream graph;
    graph << "digraph allowed_path {\n    rankdir=LR;\n    labelloc=t;\n    label="
          << Json(label).dump() << ";\n    node [shape=box];\n";
    WritePathNodes(graph, topology, plan, cluster, path);
    WritePathLinks(graph, topology, plan, path.links);
    graph << "}\n";
    return graph.str();
}

}  // namespace

void AddAllowedPathGraphs(GeneratedFiles& files, const InfrastructurePlan& plan) {
    for (const auto& [pair, path] : plan.paths)
        AddGraphvizImage(files, std::filesystem::path("graphviz/paths") / pair.first / pair.second,
                         GenerateAllowedPath(plan.topology, plan.addresses, plan.kubernetes, pair, path));
}

}  // namespace skeleton
