#include "paths.hpp"

#include <limits>

namespace skeleton::backup {
namespace {

std::uint64_t BytesFromMib(std::uint64_t mib) {
    constexpr std::uint64_t bytes_per_mib = 1024 * 1024;
    if (mib == 0 || mib > std::numeric_limits<std::uint64_t>::max() / bytes_per_mib)
        throw ConfigurationError("backup capture size must be positive and fit the supported byte range");
    return mib * bytes_per_mib;
}

std::uint64_t RequiredFreeBytes(std::uint64_t size, std::uint64_t copies, std::uint64_t reserve) {
    if (size > (std::numeric_limits<std::uint64_t>::max() - reserve) / copies)
        throw ConfigurationError("backup staging space budget overflows the supported byte range");
    return size * copies + reserve;
}

const BackupRemoteSource& ResolveRemoteSource(const BackupSetDefinition& definition, const BackupContext& context) {
    const auto source = context.remote_sources.find(definition.name);
    if (source == context.remote_sources.end() || source->second.kind != definition.kind ||
        source->second.vm.empty() || source->second.argv.empty() ||
        !source->second.required_vms.contains(source->second.vm))
        throw ConfigurationError("backup capture requires a resolved native producer and its controller path");
    return source->second;
}

}  // namespace

void PrepareCapture(BackupSetPlan& plan, const BackupSetDefinition& definition,
                    const BackupDefinition& policy, const BackupContext& context) {
    if (!policy.capture || !definition.max_size_mib || !definition.paths.empty() || !definition.excludes.empty())
        throw ConfigurationError("remote backup requires capture paths and a size limit without file paths or excludes");
    if ((definition.kind == BackupSetKind::Postgresql && (!definition.workload || definition.workload->empty())) ||
        (definition.kind == BackupSetKind::K0s && definition.workload))
        throw ConfigurationError("only PostgreSQL backup sets require a workload reference");
    const auto& source = ResolveRemoteSource(definition, context);
    const auto guest_directory = NormalizePath(policy.capture->guest_directory);
    for (const auto& path : source.protected_directories)
        if (PathsOverlap(guest_directory, NormalizePath(path)))
            throw ConfigurationError("backup staging must remain outside the controller data and configuration directories");
    const auto size = BytesFromMib(*definition.max_size_mib);
    const auto reserve = BytesFromMib(policy.capture->reserve_mib);
    const auto filename = definition.name + (definition.kind == BackupSetKind::K0s ? ".tar.gz" : ".sql");
    BackupCapturePlan capture{source.vm, {"prlimit", "--fsize=" + std::to_string(size), "--"},
        source.required_vms, filename, size,
        RequiredFreeBytes(size, definition.kind == BackupSetKind::K0s ? 2 : 1, reserve), RequiredFreeBytes(size, 1, reserve)};
    capture.argv.insert(capture.argv.end(), source.argv.begin(), source.argv.end());
    plan.capture = std::move(capture);
    plan.argv.insert(plan.argv.end(), {"--stdin-from-command", "--stdin-filename", filename, "--", "cat"});
}

}  // namespace skeleton::backup
