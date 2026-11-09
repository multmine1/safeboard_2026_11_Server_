#pragma once

#include "skeleton/topology/settings.hpp"
#include "skeleton/vm/types.hpp"
#include "skeleton/networks/types.hpp"

namespace skeleton {

struct SourceDefinitions {
    SiteDefinition site;
    std::map<std::string, VmDefinition> vms;
    std::map<std::string, NetworkDefinition> networks;
};

struct HostServiceEndpoint {
    std::string address;
    std::string fqdn;
    std::string socket;
    std::string url;
    std::uint16_t port;
};

struct Topology {
    SourceDefinitions definitions;
    std::string root;
    std::map<std::string, std::string> parents;
    std::map<std::string, std::string> vm_networks;
    std::map<std::string, std::string> router_networks;
    std::map<std::string, std::string> network_domains;
    std::map<std::string, std::string> vm_domains;
    std::vector<std::string> configuration_order;
    std::map<std::string, std::string> service_networks;
    std::map<std::string, std::string> service_domains;
    std::map<std::string, std::string> service_ids;
    std::map<std::string, HostServiceEndpoint> host_services;
    std::map<std::string, std::vector<std::string>> host_access_paths;

    bool IsRouter(const std::string& vm_id) const;
    bool IsDescendant(const std::string& network_id,
                      const std::string& ancestor_id) const;
    std::vector<std::string> RouterPath(const std::string& from_network,
                                        const std::string& to_network) const;
    std::string CommonAncestor(const std::string& from_network, const std::string& to_network) const;
    const GatewayService& Service(const std::string& id) const;
    const GatewayService& ServiceNamed(const std::string& name) const;
};

Topology BuildTopology(SourceDefinitions definitions);

}  // namespace skeleton
