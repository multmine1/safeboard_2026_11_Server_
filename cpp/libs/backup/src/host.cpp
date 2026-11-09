#include "paths.hpp"
#include "host_units.hpp"

#include <regex>

namespace skeleton::backup {
namespace {

void ValidateHostPaths(const BackupHostDefinition& host, const BackupPlan& plan, const BackupContext& context) {
    std::vector<std::filesystem::path> owned{host.binary, host.capture_directory, host.cache_directory,
        host.credential_directory, host.known_hosts, host.tmpfiles_path, host.installation_directory};
    if (host.nfs) owned.push_back(host.nfs->directory);
    for (std::size_t i = 0; i < owned.size(); ++i) {
        const auto& path = owned.at(i);
        if (!path.is_absolute() || path == path.root_path() || path.filename().empty() ||
            path != NormalizePath(path) || path.string().find_first_of("\r\n%") != std::string::npos)
            throw ConfigurationError("host backup paths require normalized absolute locations without unit specifiers");
        for (const auto& other : context.repository_excluded_paths)
            if (PathsOverlap(path, other)) throw ConfigurationError("host backup paths overlap protected hypervisor data");
        if (PathsOverlap(path, plan.repository)) throw ConfigurationError("host backup cannot stage within its repository");
        for (std::size_t j = 0; j < i; ++j)
            if (PathsOverlap(path, owned.at(j))) throw ConfigurationError("host backup files and directories overlap");
    }
    for (const auto& path : {host.unit_directory, host.host_key_file})
        if (!path.is_absolute() || path.filename().empty() || path != NormalizePath(path))
            throw ConfigurationError("host backup unit and SSH host key locations must be absolute");
}

void ValidateHostOptions(const BackupHostDefinition& host) {
    const std::set<std::string> service_keys{"TimeoutStartSec", "TimeoutStopSec", "UMask", "MemoryMax", "CPUQuota", "IOWeight", "Nice"};
    for (const auto& [key, value] : host.service_options.items())
        if (!service_keys.contains(key) || (!value.is_string() && !value.is_number_integer()))
            throw ConfigurationError("host backup service policy cannot override generated runtime commands or identity");
    if (host.service_options.value("UMask", "") != "0077" || !host.service_options.contains("TimeoutStartSec"))
        throw ConfigurationError("host backup requires private output and an explicit timeout");
    const std::set<std::string> ssh_keys{"BatchMode", "StrictHostKeyChecking", "IdentitiesOnly", "IdentityAgent",
        "ConnectTimeout", "ServerAliveInterval", "ServerAliveCountMax"};
    for (const auto& [key, value] : host.ssh_options.items())
        if (!ssh_keys.contains(key) || (!value.is_string() && !value.is_number_integer()))
            throw ConfigurationError("host backup SSH options must preserve the prepared endpoint and credentials");
    for (const auto key : {"BatchMode", "StrictHostKeyChecking", "IdentitiesOnly"})
        if (host.ssh_options.value(key, "") != "yes") throw ConfigurationError("host backup requires pinned key-only SSH");
    if (host.ssh_options.value("IdentityAgent", "") != "none" || host.credential_key != "host")
        throw ConfigurationError("host backup uses host-encrypted systemd credentials without an interactive agent");
    if (!host.timer.contains("Timer") || !host.timer.at("Timer").is_object() ||
        !host.timer.at("Timer").contains("OnCalendar") || host.timer.at("Timer").contains("Unit") ||
        host.timer.contains("Service") || host.timer.contains("Unit"))
        throw ConfigurationError("host backup timer requires a calendar and cannot override the generated unit");
}

}  // namespace

void PrepareHostBackup(BackupPlan& plan, const BackupDefinition& definition, const BackupContext& context) {
    if (!definition.host) return;
    if (!context.host || context.host->transport_user.empty() || context.host->lock_user.empty() ||
        !context.host->lock_file.is_absolute()) throw ConfigurationError("host backup requires resolved host access and maintenance lock");
    const auto& settings = *definition.host;
    if (!std::regex_match(settings.name, std::regex("[a-z0-9][a-z0-9-]{0,62}")))
        throw ConfigurationError("host backup name must be a unit-safe label");
    ValidateHostPaths(settings, plan, context);
    ValidateHostOptions(settings);
    BackupHostPlan host;
    host.settings = settings;
    host.installation = context.host->installation;
    const auto checksum = host.installation.at("checksum").get<std::string>();
    const auto archive = std::filesystem::path(host.installation.at("archive_name").get<std::string>());
    if (!std::regex_match(checksum, std::regex("sha256:[0-9a-f]{64}")) || archive.has_parent_path() ||
        archive.extension() != ".bz2" || !host.installation.at("url").get<std::string>().starts_with("https://"))
        throw ConfigurationError("host Restic requires a pinned HTTPS bzip2 installation artifact");
    host.installation["archive"] = (settings.installation_directory / archive).string();
    host.installation["extracted"] = (settings.installation_directory / archive.stem()).string();
    PrepareRepositoryAccess(host, plan, *context.host);
    host.gate_unit = settings.name + ".service";
    host.timer_unit = settings.name + ".timer";
    BuildHostUnits(host, plan, *context.host);
    if (host.connections.empty() && std::none_of(plan.sets.begin(), plan.sets.end(), [](const auto& set) { return set.second.nfs.has_value(); }))
        throw ConfigurationError("host backup requires at least one prepared remote data source");
    host.tmpfiles_content = "f " + Json(context.host->lock_file.string()).dump() + " 0600 " + context.host->lock_user + " - -\n";
    host.files = {settings.binary, settings.known_hosts, settings.tmpfiles_path};
    for (const auto& [name, unit] : host.units.items()) host.files.push_back(settings.unit_directory / name);
    host.directories = {settings.capture_directory, settings.cache_directory, settings.credential_directory, settings.installation_directory};
    if (settings.nfs) host.directories.push_back(settings.nfs->directory);
    plan.host = std::move(host);
}

}  // namespace skeleton::backup
