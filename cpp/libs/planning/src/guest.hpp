#pragma once
#include "skeleton/planning/infrastructure.hpp"
namespace skeleton {
std::optional<std::string> ResolveGuestSshGateway(const SiteDefinition& site);
Json BuildGuestSshConnection(const SiteDefinition& site, const std::string& id,
                             const std::string& address, const std::string& python,
                             const std::optional<std::string>& gateway);
void SplitGuestNetwork(VmPlan& guest, const Topology& topology, const std::string& vm_id);
std::map<std::string, RouterRuntimePlan> BuildRouterRuntimePlans(InfrastructurePlan& plan);
std::map<std::string, VmPlan> BuildVmPlans(const Topology& topology, const AddressPlan& addresses,
                                        const NetworkPolicyPlan& policy);
}  // namespace skeleton
