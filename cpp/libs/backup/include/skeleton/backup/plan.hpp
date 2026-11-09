#pragma once

#include "skeleton/contracts/types.hpp"
#include "skeleton/backup/host.hpp"

namespace skeleton {

enum class BackupSetKind { Files, TofuState, K0s, Postgresql, Nfs };

struct BackupSetDefinition {
    std::string name;
    BackupSetKind kind;
    std::vector<std::filesystem::path> paths;
    std::vector<std::string> excludes;
    std::optional<std::string> workload;
    std::optional<std::uint64_t> max_size_mib;
    std::optional<std::string> export_name;
};

struct BackupCaptureDefinition {
    std::filesystem::path guest_directory;
    std::filesystem::path local_directory;
    std::uint64_t reserve_mib;
};

struct BackupDefinition {
    std::string storage;
    std::filesystem::path repository_directory;
    std::filesystem::path bin_directory;
    std::filesystem::path cache_directory;
    std::filesystem::path restore_directory;
    std::uint64_t password_length;
    std::map<std::string, std::uint64_t> retention;
    std::vector<BackupSetDefinition> sets;
    std::optional<BackupCaptureDefinition> capture;
    std::optional<BackupHostDefinition> host;
    std::vector<std::string> quiesce;
    std::optional<Json> filesystem_watchdog;
};

struct BackupMount {
    std::filesystem::path path;
    std::string source;
};

struct BackupRemoteSource {
    BackupSetKind kind;
    std::string vm;
    std::vector<std::string> argv;
    std::set<std::string> required_vms;
    std::vector<std::filesystem::path> protected_directories;
};

struct BackupNfsSource {
    std::string vm, source;
    std::uint16_t port;
    std::set<std::string> required_vms;
};

struct BackupFilesystemSource {
    std::filesystem::path mount, unit_directory;
};

struct BackupQuiesceSource {
    std::string vm;
    std::vector<std::string> status, enter, leave;
    std::set<std::string> required_vms;
    std::optional<BackupFilesystemSource> filesystem;
};

struct BackupContext {
    std::filesystem::path project_directory;
    std::optional<std::filesystem::path> tofu_directory;
    std::map<std::string, BackupMount> mounts;
    std::vector<std::filesystem::path> repository_excluded_paths;
    std::optional<std::filesystem::path> tofu_state_file;
    std::map<std::string, BackupRemoteSource> remote_sources;
    std::map<std::string, std::string> domain_names;
    std::string repository_ssh_alias;
    std::optional<BackupHostContext> host;
    std::map<std::string, BackupNfsSource> nfs_sources;
    std::map<std::string, BackupQuiesceSource> quiesce_sources;
};

struct BackupCapturePlan {
    std::string vm;
    std::vector<std::string> argv;
    std::set<std::string> required_vms;
    std::string filename;
    std::uint64_t max_size_bytes;
    std::uint64_t guest_required_free_bytes;
    std::uint64_t local_required_free_bytes;
};

struct BackupCapturePaths {
    std::filesystem::path guest_directory;
    std::filesystem::path local_directory;
};

struct BackupNfsPlan {
    std::string vm, source, options;
    std::filesystem::path directory;
    std::set<std::string> required_vms;
};

struct BackupQuiescePlan {
    std::string name;
    std::filesystem::path marker;
    BackupQuiesceSource commands;
};

struct BackupSetPlan {
    std::filesystem::path working_directory;
    std::vector<std::string> argv;
    std::vector<std::filesystem::path> required_files;
    std::optional<BackupCapturePlan> capture;
    std::optional<BackupNfsPlan> nfs;
};

struct BackupPlan {
    std::filesystem::path project_directory;
    std::filesystem::path repository;
    std::string repository_url;
    std::vector<std::filesystem::path> repository_parents;
    std::filesystem::path repository_config;
    std::filesystem::path mount;
    std::filesystem::path cache;
    std::filesystem::path restore_directory;
    std::uint64_t password_length;
    std::vector<std::string> mount_check_argv;
    std::vector<std::string> repository_realpath_argv;
    std::vector<std::string> init_argv;
    std::vector<std::string> snapshots_argv;
    std::vector<std::string> check_argv;
    std::vector<std::string> retention_plan_argv;
    std::vector<std::string> retention_argv;
    std::vector<std::string> restore_argv;
    std::map<std::string, BackupSetPlan> sets;
    std::optional<BackupCapturePaths> capture;
    std::set<std::string> required_domains;
    std::optional<BackupHostPlan> host;
    std::vector<BackupQuiescePlan> quiesce;
    std::map<std::string, Json> guest_quiesce_units;
};

BackupPlan BuildBackupPlan(const BackupDefinition& definition, const BackupContext& context);

}  // namespace skeleton
