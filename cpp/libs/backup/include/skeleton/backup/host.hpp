#pragma once

#include "skeleton/contracts/types.hpp"

namespace skeleton {

struct BackupNfsSettings {
    std::filesystem::path directory;
    std::string options;
};

struct BackupAccountDefinition {
    std::string name;
    std::filesystem::path shell;
    std::string file_mode, directory_mode;
};

struct BackupHostDefinition {
    std::string name;
    std::filesystem::path binary, unit_directory, capture_directory, cache_directory;
    std::filesystem::path credential_directory, known_hosts, host_key_file, tmpfiles_path, installation_directory;
    std::string credential_key;
    Json service_options, ssh_options, timer;
    std::optional<BackupNfsSettings> nfs;
    std::optional<BackupAccountDefinition> account;
};

struct BackupHostConnection {
    std::string address, user;
    std::uint16_t port;
};

struct BackupHostContext {
    std::string transport_user, lock_user;
    std::filesystem::path lock_file;
    std::map<std::string, BackupHostConnection> connections;
    Json installation = Json::object();
};

struct BackupHostPlan {
    BackupHostDefinition settings;
    std::string repository_user, gate_unit, timer_unit, tmpfiles_content;
    Json connections = Json::object();
    Json units = Json::object();
    Json installation = Json::object();
    Json repository_access = Json::object();
    std::vector<std::filesystem::path> files, directories;
};

}  // namespace skeleton
