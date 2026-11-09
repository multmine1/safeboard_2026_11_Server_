#include "backup.hpp"

namespace skeleton {

std::map<std::string, BackupNfsSource> BuildBackupNfsSources(const InfrastructurePlan& plan) {
    std::map<std::string, BackupNfsSource> result;
    for (const auto& definition : plan.topology.definitions.site.backup->sets) {
        if (definition.kind != BackupSetKind::Nfs) continue;
        const NfsExportPlan* found = nullptr;
        for (const auto& [key, exported] : plan.nfs_exports)
            if (definition.workload == exported.application && definition.export_name == exported.name) {
                if (found) throw ConfigurationError("NFS backup export reference is ambiguous");
                found = &exported;
            }
        if (!found || !found->hypervisor_service)
            throw ConfigurationError("NFS backup requires its declared workload export and a read-only hypervisor grant");
        const auto& source = plan.hypervisor.at("nfs_sources").at(*found->hypervisor_service).at(found->name);
        result[definition.name] = {found->server, source.at("src"), source.at("port").get<std::uint16_t>(),
            source.at("required_vms").get<std::set<std::string>>()};
    }
    return result;
}

}  // namespace skeleton
