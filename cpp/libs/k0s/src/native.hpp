#pragma once
#include "skeleton/networks/kubernetes.hpp"
#include "skeleton/networks/policy.hpp"
#include "skeleton/planning/infrastructure.hpp"
namespace skeleton::k0s {
std::string FormatNodeLabels(const Json& labels);
std::string FormatNodeTaints(const Json& taints);
Json SerializeCalico(const KubernetesPlan& plan);
Json SerializeNodeInstallation(const KubernetesPlan& plan, const KubernetesNodePlan& node);
Json SerializeNetworkPolicies(const Topology& topology, const KubernetesPlan& cluster,
                              const NetworkPolicyPlan& policy);
void AddRouterConfigurations(GeneratedFiles& files, const InfrastructurePlan& plan);
Json SerializeManifestStacks(const InfrastructurePlan& plan, const Json& router_order);
void AddApplicationConfigurations(GeneratedFiles& files, const InfrastructurePlan& plan);
}  // namespace skeleton::k0s
