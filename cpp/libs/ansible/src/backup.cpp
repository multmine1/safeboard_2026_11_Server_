#include "backup.hpp"

namespace skeleton {
namespace {

Json SerializeCapture(const BackupCapturePlan& capture) {
    return {{"vm", capture.vm}, {"argv", capture.argv}, {"required_vms", capture.required_vms},
        {"filename", capture.filename}, {"max_size_bytes", capture.max_size_bytes},
        {"guest_required_free_bytes", capture.guest_required_free_bytes},
        {"local_required_free_bytes", capture.local_required_free_bytes}};
}

Json SerializeHost(const BackupHostPlan& host) {
    const auto& settings = host.settings;
    Json result{{"name", settings.name}, {"binary", settings.binary.string()}, {"installation", host.installation},
        {"installation_directory", settings.installation_directory.string()},
        {"unit_directory", settings.unit_directory.string()}, {"capture_directory", settings.capture_directory.string()},
        {"cache_directory", settings.cache_directory.string()}, {"credential_directory", settings.credential_directory.string()},
        {"credential_key", settings.credential_key}, {"known_hosts", settings.known_hosts.string()},
        {"host_key_file", settings.host_key_file.string()}, {"tmpfiles_path", settings.tmpfiles_path.string()},
        {"tmpfiles_content", host.tmpfiles_content}, {"repository_user", host.repository_user},
        {"gate_unit", host.gate_unit}, {"timer_unit", host.timer_unit}, {"connections", host.connections}, {"units", host.units}};
    if (settings.nfs) result["nfs"] = {{"directory", settings.nfs->directory.string()}};
    return result;
}

}  // namespace

void AddBackupConfiguration(GeneratedFiles& files, const InfrastructurePlan& plan) {
    if (!plan.backup) return;
    const auto& backup = *plan.backup;
    Json sets = Json::object();
    for (const auto& [name, set] : backup.sets) {
        if (set.nfs) continue;
        sets[name] = {{"working_directory", set.working_directory.string()}, {"argv", set.argv},
                      {"required_files", set.required_files}};
        if (set.capture) {
            sets[name]["capture"] = SerializeCapture(*set.capture);
        }
    }
    Json configuration{{"project_directory", backup.project_directory.string()},
        {"hypervisor", plan.hypervisor.at("controller")},
        {"repository", backup.repository.string()}, {"repository_config", backup.repository_config.string()},
        {"repository_url", backup.repository_url},
        {"repository_parents", backup.repository_parents},
        {"mount", backup.mount.string()}, {"cache", backup.cache.string()},
        {"restore_directory", backup.restore_directory.string()},
        {"mount_check_argv", backup.mount_check_argv}, {"repository_realpath_argv", backup.repository_realpath_argv},
        {"init_argv", backup.init_argv}, {"snapshots_argv", backup.snapshots_argv},
        {"check_argv", backup.check_argv}, {"retention_plan_argv", backup.retention_plan_argv},
        {"retention_argv", backup.retention_argv}, {"restore_argv", backup.restore_argv}, {"sets", sets},
        {"required_domains", backup.required_domains}};
    if (backup.capture) configuration["capture"] = {{"guest_directory", backup.capture->guest_directory.string()},
        {"local_directory", backup.capture->local_directory.string()}};
    if (!backup.quiesce.empty()) {
        configuration["quiesce"] = Json::array();
        for (const auto& application : backup.quiesce) {
            Json entry{{"workload", application.name}, {"marker", application.marker.string()},
                {"vm", application.commands.vm}, {"status_argv", application.commands.status},
                {"enter_argv", application.commands.enter}, {"leave_argv", application.commands.leave}};
            if (application.commands.filesystem)
                entry["filesystem_mount"] = application.commands.filesystem->mount.string();
            configuration["quiesce"].push_back(std::move(entry));
        }
    }
    if (!backup.guest_quiesce_units.empty()) configuration["guest_quiesce_units"] = backup.guest_quiesce_units;
    if (backup.host) {
        configuration["repository_access"] = backup.host->repository_access;
        configuration["host"] = SerializeHost(*backup.host);
        Json directories = Json::array();
        for (const auto& [name, set] : backup.sets)
            if (set.nfs) directories.push_back(set.nfs->directory.string());
        if (backup.host->settings.nfs) configuration["host"]["nfs"]["mount_directories"] = directories;
    }
    if (plan.hypervisor.contains("reset")) {
        configuration["operation_lock_file"] = plan.hypervisor.at("reset").at("operation_lock_file");
        configuration["operation_lock_alias"] = plan.hypervisor.at("reset").at("operation_lock_alias");
    }
    if (!backup.required_domains.empty()) configuration["libvirt_uri"] = plan.hypervisor.at("controller").at("local_libvirt_uri");
    files["ansible/backup.json"] = Json{{"backup", configuration}}.dump(2) + "\n";
}

}  // namespace skeleton
