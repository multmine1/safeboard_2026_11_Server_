#include "readers.hpp"

namespace skeleton::input {

BackupHostDefinition ReadBackupHost(const Json& value, const std::string& context) {
    CheckFields(value, {"name", "binary", "unit_directory", "capture_directory", "cache_directory",
        "credential_directory", "known_hosts", "host_key_file", "tmpfiles_path", "credential_key",
        "installation_directory", "service_options", "ssh_options", "timer", "nfs", "account"}, context);
    BackupHostDefinition result;
    result.name = ReadName(value, "name", context);
    result.binary = ReadString(value, "binary", context);
    result.unit_directory = ReadString(value, "unit_directory", context);
    result.capture_directory = ReadString(value, "capture_directory", context);
    result.cache_directory = ReadString(value, "cache_directory", context);
    result.credential_directory = ReadString(value, "credential_directory", context);
    result.known_hosts = ReadString(value, "known_hosts", context);
    result.host_key_file = ReadString(value, "host_key_file", context);
    result.tmpfiles_path = ReadString(value, "tmpfiles_path", context);
    result.installation_directory = ReadString(value, "installation_directory", context);
    result.credential_key = ReadString(value, "credential_key", context);
    for (const auto key : {"service_options", "ssh_options", "timer"})
        if (!Require(value, key, context).is_object()) Fail(context + "/" + key, "expected an object");
    result.service_options = value.at("service_options");
    result.ssh_options = value.at("ssh_options");
    result.timer = value.at("timer");
    const auto& account = Require(value, "account", context);
    const auto location = context + "/account";
    CheckFields(account, {"name", "shell", "file_mode", "directory_mode"}, location);
    result.account = BackupAccountDefinition{ReadString(account, "name", location),
        ReadString(account, "shell", location),
        ReadString(account, "file_mode", location), ReadString(account, "directory_mode", location)};
    if (value.contains("nfs")) {
        const auto& nfs = value.at("nfs");
        const auto location = context + "/nfs";
        CheckFields(nfs, {"directory", "options"}, location);
        result.nfs = BackupNfsSettings{ReadString(nfs, "directory", location), ReadString(nfs, "options", location)};
    }
    return result;
}

}  // namespace skeleton::input
