#pragma once

#include "skeleton/networks/address.hpp"
#include "skeleton/networks/connections.hpp"
#include "skeleton/networks/kubernetes.hpp"

namespace skeleton {

struct GuestAccessRule {
    std::string direction;
    std::string source;
    std::string destination;
    std::string protocol;
    std::vector<std::uint16_t> ports;
};

struct FirewallRules {
    std::set<std::string> ingress_guards;
    std::set<std::string> input;
    std::set<std::string> forward;
    std::set<std::string> output;
    std::set<std::string> prerouting;
    std::set<std::string> postrouting;
    bool masquerade = false;
    std::vector<GuestAccessRule> guest_access;
};

struct DnsConfiguration {
    std::string corefile;
    std::map<std::string, std::string> zones;
    std::vector<std::string> bindings;
};

std::string InterfaceName(const SiteDefinition& site, const std::string& role);
Json GenerateNetplan(const Topology& topology, const AddressPlan& plan,
                     const std::string& vm_id);
std::map<std::string, FirewallRules> CompileFirewallRules(const Topology& topology,
                                                       const AddressPlan& plan);
std::map<std::string, FirewallRules> CompileFirewallRules(const Topology& topology,
    const AddressPlan& plan, const std::vector<ConnectionPlan>& connections);
std::string GenerateNftablesConfiguration(const Topology& topology, const AddressPlan& plan,
                                         const std::string& vm_id, const FirewallRules& rules);
DnsConfiguration GenerateCoreDnsConfiguration(const Topology& topology, const AddressPlan& plan,
                                              const std::string& router_id);
DnsConfiguration GenerateCoreDnsConfiguration(const Topology& topology, const AddressPlan& plan,
    const std::vector<ConnectionPlan>& connections, const std::string& router_id);
std::string BuildClusterCorefile(const Topology& topology, const KubernetesPlan& cluster,
    const DnsConfiguration& dns, const std::string& network);
Json GenerateHypervisorConfiguration(const Topology& topology, const AddressPlan& plan);
Json GenerateHypervisorHostNetwork(const SiteDefinition& site);

}  // namespace skeleton
