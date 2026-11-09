#include "workloads.hpp"

namespace skeleton {

void PrepareHypervisorNfsSources(InfrastructurePlan& plan) {
    Json sources = Json::object();
    const auto& root = plan.topology.definitions.networks.at(plan.topology.root).router;
    for (const auto& [key, exported] : plan.nfs_exports) {
        if (!exported.hypervisor_service) continue;
        const auto& service = plan.topology.ServiceNamed(*exported.hypervisor_service);
        sources[*exported.hypervisor_service][exported.name] = {
            {"server_vm", exported.server},
            {"src", FormatIpv4(plan.addresses.vms.at(root).at("wan").address.address) + ":" + exported.path},
            {"port", service.port}, {"fqdn", plan.topology.service_domains.at(*exported.hypervisor_service)},
            {"required_vms", plan.topology.host_access_paths.at(exported.server)}};
    }
    if (!sources.empty()) plan.hypervisor["nfs_sources"] = std::move(sources);
}

}  // namespace skeleton
