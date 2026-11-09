#include "backup.hpp"
#include "skeleton/cleanup/boundary.hpp"

namespace skeleton {
namespace {

BackupHostContext BuildHostBackupContext(const InfrastructurePlan& plan, const BackupContext& context) {
    if (!plan.hypervisor.contains("reset")) throw ConfigurationError("native host backup requires the shared maintenance lock");
    const auto& bootstrap = plan.hypervisor.at("bootstrap");
    BackupHostContext host{bootstrap.at("hypervisor_user"), bootstrap.at("controller_user"),
        plan.hypervisor.at("reset").at("operation_lock_file").get<std::string>(), {}};
    const auto& assets = plan.topology.definitions.site.asset_data;
    if (!assets.contains("host_backup")) throw ConfigurationError("host backup requires its pinned installation asset");
    host.installation = assets.at("host_backup").at("restic");
    std::set<std::string> connections;
    for (const auto& [name, source] : context.remote_sources) connections.insert(source.vm);
    for (const auto& [name, source] : context.quiesce_sources) connections.insert(source.vm);
    for (const auto& id : connections) {
        const auto& vm = plan.vms.at(id);
        host.connections[id] = {vm.connection.at("host"),
            vm.variables.at("access").at("admin_user"), vm.connection.at("port").get<std::uint16_t>()};
    }
    return host;
}

BackupContext BuildBackupContext(const InfrastructurePlan& plan) {
    if (!plan.hypervisor.contains("bootstrap"))
        throw ConfigurationError("backup requires the complete physical hypervisor configuration");
    const auto& bootstrap = plan.hypervisor.at("bootstrap");
    BackupContext context;
    context.project_directory = plan.controller_project_directory.value();
    context.repository_ssh_alias = plan.topology.definitions.site.hypervisor_alias;
    context.repository_excluded_paths.emplace_back(bootstrap.at("controller_project_directory").get<std::string>());
    for (const auto& [id, vm] : plan.vms)
        context.domain_names.emplace(id, vm.variables.at("libvirt_name").get<std::string>());
    context.repository_excluded_paths.emplace_back(plan.topology.definitions.site.vmctl_home);
    for (const auto& pool : bootstrap.at("storage_pools")) {
        const auto volumes = std::filesystem::path(pool.at("path").get<std::string>());
        context.repository_excluded_paths.push_back(volumes);
        for (const auto& mount : bootstrap.at("storage_mounts"))
            if (mount.at("path").get<std::string>() == volumes.parent_path().string())
                context.mounts.emplace(pool.at("name").get<std::string>(),
                    BackupMount{volumes.parent_path(), mount.at("src").get<std::string>()});
    }
    if (plan.hypervisor.contains("reset")) {
        const auto& reset = plan.hypervisor.at("reset");
        context.tofu_directory = reset.at("paths").at("tofu").get<std::string>();
        context.tofu_state_file = reset.at("paths").at("state").get<std::string>();
        for (const auto key : {"remove_files", "remove_links", "volume_directories"})
            for (const auto& path : reset.at(key)) context.repository_excluded_paths.emplace_back(path.get<std::string>());
    }
    context.remote_sources = BuildBackupProducers(plan);
    context.nfs_sources = BuildBackupNfsSources(plan);
    context.quiesce_sources = BuildBackupQuiesceSources(plan);
    if (plan.topology.definitions.site.backup->host) context.host = BuildHostBackupContext(plan, context);
    return context;
}

void PrepareHostBackupReset(Json& reset, const BackupHostPlan& host, const Json& bootstrap) {
    const CleanupBoundary boundary(reset.at("boundary").at("paths").get<std::vector<std::filesystem::path>>(),
                                   reset.at("boundary").at("trees").get<std::vector<std::filesystem::path>>());
    std::set<std::string> retained;
    for (const auto& [name, companions] : bootstrap.at("hypervisor_units").items()) {
        retained.insert(name);
        for (const auto& companion : companions) retained.insert(companion.get<std::string>());
    }
    for (const auto& [name, unit] : host.units.items()) {
        if (retained.contains(name)) throw ConfigurationError("host backup cannot replace a retained foundation unit");
    }
    std::vector<std::string> units{host.timer_unit, host.gate_unit};
    for (const auto& [name, unit] : host.units.items())
        if (name != host.timer_unit && name != host.gate_unit) units.push_back(name);
    for (const auto& name : units) {
        if (std::find(reset["stop_services"].begin(), reset["stop_services"].end(), Json(name)) == reset["stop_services"].end())
            reset["stop_services"].push_back(name);
    }
    for (const auto& file : host.files) {
        boundary.ValidateTarget({file, RemovalKind::File});
        if (std::find(reset["remove_files"].begin(), reset["remove_files"].end(), Json(file.string())) == reset["remove_files"].end())
            reset["remove_files"].push_back(file.string());
    }
    for (const auto& directory : host.directories) {
        boundary.ValidateTarget({directory, RemovalKind::Directory});
        reset["remove_directories"].push_back(directory.string());
    }
    const auto& account = host.repository_access.at("account");
    if (!reset.contains("service_accounts")) reset["service_accounts"] = Json::array();
    reset["service_accounts"].push_back({{"name", account.at("name")}, {"home", account.at("home")},
        {"shell", account.at("shell")}, {"group", host.repository_access.at("group")}});
    reset["backup_repository_access"]["service_user"] = account.at("name");
}

void PrepareBackupRepositoryReset(Json& reset, const Json& policy, const std::filesystem::path& repository) {
    if (!policy.contains("backup_repository") || !policy.at("backup_repository").is_string())
        throw ConfigurationError("reset requires an explicit backup_repository policy: preserve or remove");
    const auto action = policy.at("backup_repository").get<std::string>();
    if (action == "preserve") {
        reset["boundary"]["trees"].push_back(repository.string());
    } else if (action == "remove") {
        const CleanupBoundary boundary(reset.at("boundary").at("paths").get<std::vector<std::filesystem::path>>(),
                                       reset.at("boundary").at("trees").get<std::vector<std::filesystem::path>>());
        boundary.ValidateTarget({repository, RemovalKind::Directory});
        reset["remove_directories"].push_back(repository.string());
    } else {
        throw ConfigurationError("reset backup_repository policy must be preserve or remove");
    }
}

}  // namespace

void PrepareBackupPlan(InfrastructurePlan& plan) {
    const auto& definition = plan.topology.definitions.site.backup;
    if (!definition) return;
    plan.backup = BuildBackupPlan(*definition, BuildBackupContext(plan));
    if (plan.hypervisor.contains("reset")) {
        auto& reset = plan.hypervisor["reset"];
        PrepareBackupRepositoryReset(reset, plan.topology.definitions.site.services.at("reset"), plan.backup->repository);
        reset["backup_repository_access"] = {{"parents", plan.backup->repository_parents},
            {"user", reset.at("account").at("name")}, {"repository", plan.backup->repository.string()}};
        if (plan.backup->host) PrepareHostBackupReset(reset, *plan.backup->host, plan.hypervisor.at("bootstrap"));
    }
}

}  // namespace skeleton
