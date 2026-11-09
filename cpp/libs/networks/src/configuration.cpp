#include "skeleton/networks/configuration.hpp"

namespace skeleton {
namespace {

Json GenerateNicNetplan(const Topology& topology, const AddressPlan& plan,
                        const std::string& vm_id, const NicPlan& nic) {
    const auto& site = topology.definitions.site;
    Json configuration = {{"match", {{"macaddress", nic.mac}}},
                          {"set-name", InterfaceName(site, nic.role)},
                          {"addresses", {nic.address.ToString()}}};
    if (nic.gateway) {
        Json routes = Json::array();
        const auto gateway = FormatIpv4(*nic.gateway);
        if (!nic.address.Contains(*nic.gateway))
            routes.push_back({{"to", gateway + "/32"}, {"scope", "link"}});
        routes.push_back({{"to", "default"}, {"via", gateway}});
        configuration["routes"] = routes;
    }
    if (nic.role == "lan") {
        const auto& router = topology.definitions.networks.at(topology.vm_networks.at(vm_id)).router;
        const auto server = topology.IsRouter(vm_id)
            ? site.services.at("dns_resolver").at("local_address").get<std::string>()
            : FormatIpv4(plan.vms.at(router).at("lan").address.address);
        configuration["nameservers"]["addresses"] = Json::array({server});
    }
    return configuration;
}

Json GenerateDescendantRoutes(const Topology& topology, const AddressPlan& plan,
                              const std::string& network_id) {
    Json routes = Json::array();
    for (const auto& [id, subnet] : plan.subnets) {
        if (id == network_id || !topology.IsDescendant(id, network_id)) continue;
        auto child = id;
        while (topology.parents.at(child) != network_id) child = topology.parents.at(child);
        const auto& router = topology.definitions.networks.at(child).router;
        routes.push_back({{"to", subnet.ToString()},
                          {"via", FormatIpv4(plan.vms.at(router).at("wan").address.address)}});
    }
    return routes;
}

}  // namespace

std::string InterfaceName(const SiteDefinition& site, const std::string& role) {
    return site.services.at("network").at("interfaces").at(role).get<std::string>();
}

Json GenerateNetplan(const Topology& topology, const AddressPlan& plan,
                     const std::string& vm_id) {
    const auto& site = topology.definitions.site;
    Json interfaces = Json::object();
    for (const auto& [role, nic] : plan.vms.at(vm_id))
        interfaces[InterfaceName(site, role)] = GenerateNicNetplan(topology, plan, vm_id, nic);
    if (topology.IsRouter(vm_id)) {
        const auto routes = GenerateDescendantRoutes(topology, plan, topology.router_networks.at(vm_id));
        if (!routes.empty()) interfaces[InterfaceName(site, "lan")]["routes"] = routes;
    }
    return {{"network", {{"version", 2},
                          {"renderer", site.services.at("network").at("renderer")},
                          {"ethernets", interfaces}}}};
}

Json GenerateHypervisorConfiguration(const Topology& topology, const AddressPlan& plan) {
    const auto& site = topology.definitions.site;
    const auto& root = topology.definitions.networks.at(topology.root).router;
    const auto gateway = FormatIpv4(plan.vms.at(root).at("wan").address.address);
    Json result{{"ssh_alias", site.hypervisor_alias},
            {"resolver", {{"servers", {gateway}}}},
            {"routes", {{{"to", site.ipv4_pool}, {"via", gateway}}}},
            {"activate_after", root}};
    if (site.services.contains("hypervisor")) {
        result["controller"] = site.services.at("hypervisor");
        result["controller"]["libvirt_uri"] = site.provider.at("uri");
        if (result["controller"].contains("nwfilter")) {
            result["controller"]["nwfilter"]["name"] = site.defaults.at("nic").at("filter_ref").at("filter");
            if (result["controller"].contains("local_libvirt_uri"))
                result["controller"]["nwfilter"]["uri"] = result["controller"].at("local_libvirt_uri");
        }
    }
    return result;
}

}  // namespace skeleton
