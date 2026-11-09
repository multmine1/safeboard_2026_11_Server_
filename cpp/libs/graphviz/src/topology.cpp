#include "graph.hpp"

#include <sstream>


namespace skeleton {
namespace {

void WriteVmNode(std::ostream& graph, const InfrastructurePlan& infrastructure, const std::string& vm_id) {
    const auto& topology = infrastructure.topology;
    const auto& vm = topology.definitions.vms.at(vm_id);
    auto label = topology.vm_domains.at(vm_id);
    if (topology.IsRouter(vm_id)) label += "\nDNS: " + topology.network_domains.at(topology.vm_networks.at(vm_id));
    for (const auto& [role, nic] : infrastructure.addresses.vms.at(vm_id))
        label += "\n" + InterfaceName(topology.definitions.site, role) + ": " + nic.address.ToString();
    label += "\n" + std::to_string(vm.vcpu) + " vCPU / " + std::to_string(vm.ram_mib) + " MiB";
    if (infrastructure.kubernetes.nodes.contains(vm_id)) {
        const auto& node = infrastructure.kubernetes.nodes.at(vm_id);
        label += "\n" + node.role + "\nPods: " + node.pods.ToString();
    }
    graph << "        " << Json(vm_id).dump() << " [label=" << Json(label).dump() << "];\n";
}

void WritePhysicalUplink(std::ostream& graph, const InfrastructurePlan& infrastructure) {
    const auto& topology = infrastructure.topology;
    const auto& site = topology.definitions.site;
    const auto& root = topology.definitions.networks.at(topology.root).router;
    graph << "    physical_lan [label=" << Json(site.uplink_bridge + "\nGateway: " + site.uplink_gateway).dump() << "];\n"
          << "    hypervisor [label=" << Json(site.hypervisor_alias + "\n" + site.hypervisor_address).dump() << "];\n"
          << "    hypervisor -> physical_lan;\n"
          << "    physical_lan -> " << Json(root).dump() << ";\n";
}

std::string GenerateGraph(const InfrastructurePlan& infrastructure) {
    const auto& topology = infrastructure.topology;
    const auto& plan = infrastructure.addresses;
    std::ostringstream graph;
    graph << "digraph infrastructure {\n    rankdir=LR;\n    node [shape=box];\n";
    WritePhysicalUplink(graph, infrastructure);
    for (const auto& [id, network] : topology.definitions.networks) {
        graph << "    subgraph " << Json("cluster_" + id).dump() << " {\n        label="
              << Json(topology.network_domains.at(id) + "\n" + plan.subnets.at(id).ToString()).dump() << ";\n";
        for (const auto& [vm_id, owner] : topology.vm_networks) {
            if (owner != id) continue;
            WriteVmNode(graph, infrastructure, vm_id);
        }
        graph << "    }\n";
    }
    for (const auto& [id, network] : topology.definitions.networks)
        for (const auto& member : network.members)
            graph << "    " << Json(network.router).dump() << " -> " << Json(member).dump() << ";\n";
    graph << "}\n";
    return graph.str();
}

 }  // namespace

void AddGraphvizConfiguration(GeneratedFiles& files, const InfrastructurePlan& plan) {
    AddGraphvizImage(files, "graphviz/topology", GenerateGraph(plan));
    AddAllowedPathGraphs(files, plan);
}

}  // namespace skeleton
