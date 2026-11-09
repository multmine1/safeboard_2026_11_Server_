#include "backup.hpp"

namespace skeleton {

void ValidateBackupDatabaseCopies(const InfrastructurePlan& plan, const ApplicationPlan& application) {
    bool found = false;
    for (const auto& name : application.deployment_order) {
        const auto& dependency = plan.applications.at(name);
        for (const auto& resource : dependency.resources) {
            if (resource.value("apiVersion", "") != "postgresql.cnpg.io/v1" || resource.value("kind", "") != "Cluster") continue;
            const auto& sets = plan.topology.definitions.site.backup->sets;
            if (std::none_of(sets.begin(), sets.end(), [&](const auto& set) {
                return set.kind == BackupSetKind::Postgresql && set.workload == name;
            })) throw ConfigurationError("quiesced workload requires its PostgreSQL dependency in the same backup job");
            found = true;
        }
    }
    if (!found) throw ConfigurationError("quiesced workload requires a resolved PostgreSQL dependency");
}

namespace {

bool CoversNfsVolume(const InfrastructurePlan& plan, const ApplicationPlan& application, const Json& volume) {
    const auto& sets = plan.topology.definitions.site.backup->sets;
    for (const auto& [key, exported] : plan.nfs_exports) {
        if (exported.path != volume.at("path").get<std::string>() || !exported.clients.contains(application.vm)) continue;
        const auto& endpoint = plan.vms.at(application.vm).variables.at("connections").at(exported.service);
        if (endpoint.at("fqdn") != volume.at("server")) continue;
        if (std::any_of(sets.begin(), sets.end(), [&](const auto& set) {
            return set.kind == BackupSetKind::Nfs && set.workload == exported.application && set.export_name == exported.name;
        })) return true;
    }
    return false;
}

void ValidateFileCopies(const InfrastructurePlan& plan, const ApplicationPlan& application) {
    bool found = false;
    for (const auto& resource : application.resources) {
        if (resource.value("kind", "") != "PersistentVolume") continue;
        if (!resource.contains(Json::json_pointer("/spec/nfs")) ||
            !CoversNfsVolume(plan, application, resource.at("spec").at("nfs")))
            throw ConfigurationError("Nextcloud maintenance requires each file volume in the same NFS backup job");
        found = true;
    }
    if (!found) throw ConfigurationError("Nextcloud maintenance requires a resolved file volume");
}

std::string ResolveMaintenanceDeployment(const ApplicationPlan& application) {
    std::string deployment;
    for (const auto& readiness : application.readiness) {
        if (!readiness.value("rollout", false) || !readiness.value("resource", "").starts_with("deployment/")) continue;
        if (!deployment.empty()) throw ConfigurationError("Nextcloud maintenance deployment is ambiguous");
        deployment = readiness.at("resource").get<std::string>();
    }
    if (deployment.empty()) throw ConfigurationError("Nextcloud maintenance requires its prepared Deployment readiness target");
    return deployment;
}

BackupQuiesceSource BuildQuiesceSource(const InfrastructurePlan& plan, const ApplicationPlan& application) {
    ValidateBackupDatabaseCopies(plan, application);
    ValidateFileCopies(plan, application);
    const auto& controller = plan.kubernetes.controller;
    const auto& control_path = plan.topology.host_access_paths.at(controller);
    BackupQuiesceSource source{controller, {}, {}, {}, {control_path.begin(), control_path.end()}};
    for (const auto& vm : application.required_vms) {
        const auto& path = plan.topology.host_access_paths.at(vm);
        source.required_vms.insert(path.begin(), path.end());
    }
    const std::vector<std::string> command{plan.kubernetes.settings.at("paths").at("binary"), "kubectl", "exec",
        "--namespace", application.network_namespace, "--container", "nextcloud", ResolveMaintenanceDeployment(application), "--", "php", "occ"};
    source.status = source.enter = source.leave = command;
    source.status.insert(source.status.end(), {"status", "--output=json"});
    source.enter.insert(source.enter.end(), {"maintenance:mode", "--on"});
    source.leave.insert(source.leave.end(), {"maintenance:mode", "--off"});
    return source;
}

}  // namespace

std::map<std::string, BackupQuiesceSource> BuildBackupQuiesceSources(const InfrastructurePlan& plan) {
    std::map<std::string, BackupQuiesceSource> sources;
    for (const auto& name : plan.topology.definitions.site.backup->quiesce) {
        if (!plan.applications.contains(name) || !plan.applications.at(name).enabled)
            throw ConfigurationError("backup quiesce requires an enabled workload reference");
        const auto& application = plan.applications.at(name);
        if (application.component == "nextcloud") sources.emplace(name, BuildQuiesceSource(plan, application));
        else if (application.component == "stalwart") sources.emplace(name, BuildFilesystemBackupSource(plan, application));
        else throw ConfigurationError("backup quiesce requires a supported workload component");
    }
    return sources;
}

}  // namespace skeleton
