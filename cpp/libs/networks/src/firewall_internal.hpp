#pragma once

#include "skeleton/networks/policy.hpp"

namespace skeleton {

std::string FormatNftSet(const std::vector<std::string>& values);
std::string FormatPortMatch(const std::string& protocol, const std::vector<std::uint16_t>& values);
std::string ParentMacGuard(const Topology& topology, const AddressPlan& addresses,
                           const std::string& network, const std::string& ingress_role);
std::set<std::string>& SelectFirewallChain(FirewallRules& rules, bool local_source, bool local_target);
std::string FormatFlowInterfaces(const Topology& topology, const AddressPlan& addresses,
    const std::string& router_network, const std::string& source_network,
    const std::string& target_network, bool local_source, bool local_target);
std::vector<std::string> InternetExclusions(const SiteDefinition& site);
std::string FormatInternetDestination(const SiteDefinition& site);

void AddGuestAccess(const Topology& topology, const AddressPlan& addresses, const std::string& vm,
                    const GuestAccessRule& access, FirewallRules& rules);
void CompileResolvedConnection(const Topology& topology, const AddressPlan& addresses,
                               const ConnectionPlan& flow, std::map<std::string, FirewallRules>& rules);
void CompileIngressGuards(const Topology& topology, const AddressPlan& addresses,
                          const KubernetesPlan& cluster, std::map<std::string, FirewallRules>& rules);
void CompileClusterFirewall(const Topology& topology, const AddressPlan& addresses,
                            const KubernetesPlan& cluster, NetworkPolicyPlan& policy);
void BuildPodAccess(const Topology& topology, const AddressPlan& addresses,
                    const KubernetesPlan& cluster, NetworkPolicyPlan& policy);

}  // namespace skeleton
