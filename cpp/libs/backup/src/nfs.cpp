#include "paths.hpp"

#include <sstream>

namespace skeleton::backup {

BackupNfsPlan BuildNfsSet(const BackupSetDefinition& definition, const BackupDefinition& policy,
                         const BackupContext& context) {
    if (!definition.workload || !definition.export_name || !policy.host || !policy.host->nfs ||
        !context.host || !context.nfs_sources.contains(definition.name) ||
        !definition.paths.empty() || !definition.excludes.empty() || definition.max_size_mib)
        throw ConfigurationError("NFS backup requires a resolved export and native host mount policy without copied paths");
    const auto& source = context.nfs_sources.at(definition.name);
    if (source.source.empty() || !source.port || source.vm.empty() || !source.required_vms.contains(source.vm))
        throw ConfigurationError("NFS backup requires its published router endpoint and complete dependency path");
    std::set<std::string> options;
    std::istringstream input(policy.host->nfs->options);
    for (std::string option; std::getline(input, option, ',');) {
        if (option.empty() || option.starts_with("port=") || option.starts_with("addr="))
            throw ConfigurationError("NFS backup cannot duplicate the prepared endpoint or port");
        options.insert(option);
    }
    if (!options.contains("ro") || options.contains("rw") || !options.contains("nosuid") ||
        !options.contains("nodev") || !options.contains("noexec") || !options.contains("soft"))
        throw ConfigurationError("NFS backup requires a read-only restricted mount with bounded failed reads");
    return {source.vm, source.source, policy.host->nfs->options + ",port=" + std::to_string(source.port),
        policy.host->nfs->directory / definition.name, source.required_vms};
}

}  // namespace skeleton::backup
