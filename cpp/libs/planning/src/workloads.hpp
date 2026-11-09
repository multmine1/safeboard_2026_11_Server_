#pragma once

#include "skeleton/planning/infrastructure.hpp"

namespace skeleton {
Json BuildApplicationNavigation(const InfrastructurePlan& plan, const Json& definition);
Json BuildApplicationContext(const InfrastructurePlan& plan, const Json& definition,
                             const std::map<std::string, PodResourcePlan>& compute, const Json& patches);
std::map<std::string, PodResourcePlan> BuildApplicationCompute(const WorkloadDefinition& definition);
Json BuildComputeContext(const std::map<std::string, PodResourcePlan>& compute);
void ValidateComputeBindings(const std::map<std::string, PodResourcePlan>& compute, const Json& patches);
void PrepareWorkloadResources(InfrastructurePlan& plan);
unsigned ResolveFrontendPort(const InfrastructurePlan& plan, const std::string& service, const std::string& via);
Json BuildApplicationStorage(const InfrastructurePlan& plan, const Json& definition);
void PrepareHypervisorNfsSources(InfrastructurePlan& plan);
std::map<std::pair<std::string, std::string>, NfsExportPlan> BuildNfsExportPlans(const InfrastructurePlan& plan);
void BuildApplicationPlans(InfrastructurePlan& plan);
void OrderApplications(InfrastructurePlan& plan);
void PrepareApplicationSecrets(InfrastructurePlan& plan);
Json ResolveApplicationResources(const Json& payload, const std::string& network_namespace,
                                 const std::map<std::string, std::string>& namespaces);
std::map<std::string, std::string> ResolveApplicationFiles(const Json& payload);
}  // namespace skeleton
