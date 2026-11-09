#include "backup.hpp"

namespace skeleton {
namespace {

bool IsBelow(const std::filesystem::path& path, const std::filesystem::path& root) {
    return root != root.root_path() && root != path &&
        std::mismatch(root.begin(), root.end(), path.begin(), path.end()).first == root.end();
}

std::filesystem::path ResolveDedicatedMount(const InfrastructurePlan& plan, const ApplicationPlan& application,
                                          const std::filesystem::path& path) {
    std::filesystem::path result;
    for (const auto& mount : plan.vms.at(application.vm).variables.at("mounts")) {
        const std::filesystem::path root = mount.at("path").get<std::string>();
        if (!IsBelow(path, root)) continue;
        if (!result.empty()) throw ConfigurationError("mail filesystem backup mount is ambiguous");
        if (mount.at("fstype") != "ext4" || !std::filesystem::path(mount.at("src").get<std::string>()).is_absolute())
            throw ConfigurationError("mail filesystem backup requires its prepared ext4 data disk");
        result = root;
    }
    if (result.empty()) throw ConfigurationError("mail filesystem backup requires a dedicated data mount");
    return result;
}

void ValidateBlobCopy(const InfrastructurePlan& plan, const ApplicationPlan& application, const std::string& path) {
    const auto& sets = plan.topology.definitions.site.backup->sets;
    for (const auto& [key, exported] : plan.nfs_exports) {
        if (exported.server != application.vm || exported.path != path || !exported.hypervisor_service) continue;
        if (std::any_of(sets.begin(), sets.end(), [&](const auto& set) {
            return set.kind == BackupSetKind::Nfs && set.workload == exported.application && set.export_name == exported.name;
        })) return;
    }
    throw ConfigurationError("mail filesystem backup requires every local volume in the same host NFS backup job");
}

std::filesystem::path ResolveMailMount(const InfrastructurePlan& plan, const ApplicationPlan& application) {
    std::filesystem::path mount;
    for (const auto& resource : application.resources) {
        if (resource.value("kind", "") != "PersistentVolume") continue;
        if (!resource.contains(Json::json_pointer("/spec/local/path")))
            throw ConfigurationError("mail filesystem backup requires prepared local file volumes");
        const auto path = resource.at("spec").at("local").at("path").get<std::string>();
        ValidateBlobCopy(plan, application, path);
        const auto current = ResolveDedicatedMount(plan, application, path);
        if (!mount.empty() && mount != current)
            throw ConfigurationError("mail file volumes must share one dedicated backup mount");
        mount = current;
    }
    if (mount.empty()) throw ConfigurationError("mail filesystem backup requires a resolved local file volume");
    return mount;
}

}  // namespace

BackupQuiesceSource BuildFilesystemBackupSource(const InfrastructurePlan& plan, const ApplicationPlan& application) {
    ValidateBackupDatabaseCopies(plan, application);
    const auto& path = plan.topology.host_access_paths.at(application.vm);
    BackupQuiesceSource source{application.vm, {}, {}, {}, {path.begin(), path.end()},
        BackupFilesystemSource{ResolveMailMount(plan, application),
            plan.kubernetes.settings.at("paths").at("unit_directory").get<std::string>()}};
    for (const auto& vm : application.required_vms) {
        const auto& dependency = plan.topology.host_access_paths.at(vm);
        source.required_vms.insert(dependency.begin(), dependency.end());
    }
    return source;
}

}  // namespace skeleton
