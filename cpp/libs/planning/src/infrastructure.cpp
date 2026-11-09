#include "guest.hpp"
#include "workloads.hpp"
#include "resources.hpp"
#include "system_readiness.hpp"
#include "reset.hpp"
#include "backup.hpp"
#include "monitoring.hpp"
#include "hypervisor_proxy.hpp"
#include "pci.hpp"
#include "host_requirements.hpp"
#include "installation.hpp"
#include "skeleton/networks/access.hpp"

namespace skeleton {
namespace {

std::optional<HostDiskPlan> PrepareHostDiskPlan(const SiteDefinition& site) {
    const bool has_layout = site.asset_data.contains("host_installation");
    const bool has_disks = site.hypervisor_settings.contains("hypervisor_installation");
    if (!has_layout && !has_disks) return std::nullopt;
    if (!has_layout || !has_disks)
        throw ConfigurationError("host installation requires both a layout asset and local physical disk identities");
    return BuildHostDiskPlan(site.asset_data.at("host_installation"),
        site.hypervisor_settings.at("hypervisor_installation").at("disks"),
        site.hypervisor_settings.at("generator").at("storage_classes"), site.storage_root);
}

std::optional<std::filesystem::path> ResolveControllerDirectory(const SiteDefinition& site) {
    if (!site.services.contains("controller")) {
        if (site.backup || site.services.contains("reset"))
            throw ConfigurationError("maintenance requires an explicit controller.project_directory");
        return std::nullopt;
    }
    const auto path = std::filesystem::path(site.services.at("controller").at("project_directory").get<std::string>());
    if (!path.is_absolute() || path == path.root_path() || std::find(path.begin(), path.end(), "..") != path.end())
        throw ConfigurationError("controller.project_directory must be an absolute workspace path without parent traversal");
    return path.lexically_normal();
}

std::map<std::pair<std::string, std::string>, AllowedVmPath> BuildAllowedPaths(
    const Topology& topology, const AddressPlan& addresses, const std::vector<ConnectionPlan>& connections) {
    std::map<std::pair<std::string, std::string>, AllowedVmPath> paths;
    for (const auto& flow : connections) {
        if (flow.target_network.empty()) continue;
        if (topology.IsRouter(flow.from) || topology.IsRouter(flow.to)) continue;
        auto& path = paths[{flow.from, flow.to}];
        if (path.nodes.empty()) {
            path.nodes.push_back(flow.from);
            for (const auto& hop : flow.routers) path.nodes.push_back(topology.definitions.networks.at(hop).router);
            path.nodes.push_back(flow.to);
            for (std::size_t i = 1; i < path.nodes.size(); ++i)
                path.links.push_back(ResolvePhysicalLink(addresses, path.nodes[i - 1], path.nodes[i]));
        }
        path.flows.push_back(flow);
    }
    return paths;
}

void PrepareHypervisorPlan(InfrastructurePlan& plan) {
    plan.hypervisor = GenerateHypervisorConfiguration(plan.topology, plan.addresses);
    PrepareHypervisorNfsSources(plan);
    if (plan.topology.definitions.site.hypervisor_settings.empty()) return;
    plan.hypervisor["bootstrap"] = BuildHypervisorBootstrap(plan.topology, plan.hypervisor);
    const auto& bootstrap = plan.hypervisor.at("bootstrap");
    if (!bootstrap.contains("hypervisor_cockpit") || !bootstrap.at("hypervisor_cockpit").contains("certificate")) return;
    if (plan.secret_configuration.empty())
        plan.secret_configuration = {{"settings", plan.kubernetes.settings.at("pki")}, {"certificates", Json::array()}};
    plan.secret_configuration["certificates"].push_back(bootstrap.at("hypervisor_cockpit").at("certificate"));
}

}  // namespace

InfrastructurePlan BuildInfrastructurePlan(Topology topology, AddressPlan addresses) {
    InfrastructurePlan plan{std::move(topology), std::move(addresses)};
    plan.host_requirements = BuildHostRequirements(plan.topology.definitions.site);
    plan.image = BuildHostImagePlan(plan.topology.definitions.site, plan.host_requirements);
    plan.host_installation = PrepareHostDiskPlan(plan.topology.definitions.site);
    plan.controller_project_directory = ResolveControllerDirectory(plan.topology.definitions.site);
    plan.kubernetes = BuildKubernetesPlan(plan.topology, plan.addresses);
    plan.resources = BuildInfrastructureResources(plan.topology, plan.kubernetes);
    plan.system_readiness = BuildSystemReadiness(plan.kubernetes);
    ResolveCommonPermissions(plan.topology, plan.kubernetes);
    plan.policy = BuildNetworkPolicyPlan(plan.topology, plan.addresses, plan.kubernetes);
    plan.vms = BuildVmPlans(plan.topology, plan.addresses, plan.policy);
    plan.local_access = BuildLocalAccessPlan(plan);
    PrepareRegistryClients(plan);
    plan.nfs_exports = BuildNfsExportPlans(plan);
    BuildApplicationPlans(plan);
    PrepareWorkloadResources(plan);
    plan.routers = BuildRouterRuntimePlans(plan);
    PrepareHypervisorPlan(plan);
    PrepareHypervisorPci(plan);
    PrepareHypervisorProxies(plan);
    PrepareHypervisorMonitoring(plan);
    PrepareResetPlan(plan);
    PrepareBackupPlan(plan);
    plan.paths = BuildAllowedPaths(plan.topology, plan.addresses, plan.policy.connections);
    if (plan.topology.definitions.site.hypervisor_settings.contains("hypervisor_sshd"))
        plan.hypervisor["sshd"] = BuildHypervisorSshPlan(plan);
    PrepareInstallerBootstrap(plan);
    return plan;
}

}  // namespace skeleton
