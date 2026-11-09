#include <gtest/gtest.h>

#include "fixture.hpp"

namespace skeleton::test {
namespace {

Json BackupJson() {
    return {{"storage", "hd"}, {"repository_directory", "backups/project"},
        {"bin_directory", "environment/.tools/anaconda/envs/project/bin"},
        {"cache_directory", "environment/.tools/backup/cache"},
        {"restore_directory", "environment/.tools/backup/restore"},
        {"password_length", 64}, {"retention", {{"daily", 7}, {"monthly", 6}}},
        {"sets", {{{"name", "project"}, {"kind", "files"}, {"paths", {"."}},
                   {"excludes", {"environment/.tools"}}}, {{"name", "state"}, {"kind", "tofu-state"}}}}};
}

SourceDefinitions ReadBackup(const std::filesystem::path& root, const Json& value) {
    auto site = SiteJson();
    site["services"]["backup"] = value;
    WriteJson(root / "site.json", site);
    return ReadSourceDefinitions(root);
}

}  // namespace

TEST(BackupInput, ReadsTypedPolicyWithoutLeavingASecondRawCopy) {
    TemporaryDirectory directory;
    WriteSources(directory.path);

    const auto definitions = ReadBackup(directory.path, BackupJson());

    ASSERT_TRUE(definitions.site.backup);
    const auto& backup = *definitions.site.backup;
    EXPECT_EQ(backup.storage, "hd");
    EXPECT_EQ(backup.password_length, 64);
    EXPECT_EQ(backup.retention.at("monthly"), 6);
    ASSERT_EQ(backup.sets.size(), 2);
    EXPECT_EQ(backup.sets.front().kind, BackupSetKind::Files);
    EXPECT_EQ(backup.sets.back().kind, BackupSetKind::TofuState);
    EXPECT_FALSE(definitions.site.services.contains("backup"));
}

TEST(BackupInput, RejectsUnknownFieldsKindsAndMalformedRequiredValues) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    for (const auto fault : {"unknown", "kind", "missing", "fractional", "set_fields", "retention"}) {
        SCOPED_TRACE(fault);
        auto backup = BackupJson();
        const auto name = std::string(fault);
        if (name == "unknown") backup["password"] = "forbidden";
        if (name == "kind") backup["sets"][0]["kind"] = "shell";
        if (name == "missing") backup.erase("cache_directory");
        if (name == "fractional") backup["password_length"] = 64.5;
        if (name == "set_fields") backup["sets"][0]["command"] = "arbitrary";
        if (name == "retention") backup["retention"]["hourly"] = 1;
        EXPECT_THROW(ReadBackup(directory.path, backup), ConfigurationError);
    }
}

TEST(BackupInput, ReadsNativeCaptureReferencesAndRejectsMissingOrUnboundedFields) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto backup = BackupJson();
    backup["capture"] = {{"guest_directory", "/var/tmp"},
        {"local_directory", "environment/.tools/backup/capture"}, {"reserve_mib", 32}};
    backup["sets"].push_back({{"name", "cluster"}, {"kind", "k0s"}, {"max_size_mib", 64}});
    backup["sets"].push_back({{"name", "database"}, {"kind", "postgresql"},
        {"workload", "postgres"}, {"max_size_mib", 128}});
    const auto definitions = ReadBackup(directory.path, backup);
    ASSERT_TRUE(definitions.site.backup->capture);
    EXPECT_EQ(definitions.site.backup->sets.back().workload, "postgres");
    EXPECT_EQ(definitions.site.backup->sets.back().kind, BackupSetKind::Postgresql);
    backup["sets"].back().erase("workload");
    EXPECT_THROW(ReadBackup(directory.path, backup), ConfigurationError);
    backup["sets"].back()["workload"] = "postgres";
    backup["sets"].back()["max_size_mib"] = "unlimited";
    EXPECT_THROW(ReadBackup(directory.path, backup), ConfigurationError);
}

TEST(BackupInput, RejectsTheRetiredServerControllerSchedule) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto backup = BackupJson();
    backup["schedule"] = {{"name", "project-backup"}, {"unit_directory", "/etc/systemd/system"},
        {"credential_file", "/etc/project/vault.cred"}, {"calendar", "daily"},
        {"randomized_delay_seconds", 300}, {"timeout_seconds", 3600}, {"persistent", true}, {"password_client", "scripts/vault_password.py"}};
    EXPECT_THROW(ReadBackup(directory.path, backup), ConfigurationError);
}

TEST(BackupInput, ReadsNativeHostJobsWithoutASecondAnsibleController) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto backup = BackupJson();
    backup["host"] = {{"name", "project-backup"}, {"binary", "/usr/local/bin/restic"},
        {"account", {{"name", "svc-backup"}, {"shell", "/usr/sbin/nologin"},
            {"file_mode", "0440"}, {"directory_mode", "2770"}}},
        {"unit_directory", "/etc/systemd/system"}, {"capture_directory", "/var/cache/project/capture"},
        {"cache_directory", "/var/cache/project/restic"}, {"credential_directory", "/etc/project/credentials"},
        {"known_hosts", "/etc/project/known_hosts"}, {"host_key_file", "/etc/ssh/ssh_host_ed25519_key.pub"},
        {"tmpfiles_path", "/etc/tmpfiles.d/project-backup.conf"}, {"credential_key", "host"},
        {"installation_directory", "/var/cache/project/installation"},
        {"service_options", {{"TimeoutStartSec", 3600}, {"UMask", "0077"}}},
        {"ssh_options", {{"BatchMode", "yes"}, {"StrictHostKeyChecking", "yes"},
            {"IdentitiesOnly", "yes"}, {"IdentityAgent", "none"}, {"ConnectTimeout", 15}}},
        {"timer", {{"Timer", {{"OnCalendar", "daily"}, {"Persistent", true}}},
                   {"Install", {{"WantedBy", "timers.target"}}}}}};
    const auto definitions = ReadBackup(directory.path, backup);
    ASSERT_TRUE(definitions.site.backup->host->account);
    EXPECT_EQ(definitions.site.backup->host->account->name, "svc-backup");
    backup["host"]["account"]["groups"] = {"sudo"};
    EXPECT_THROW(ReadBackup(directory.path, backup), ConfigurationError);
}

TEST(BackupInput, ReadsAnNfsExportReferenceWithoutDuplicatingItsEndpointOrStoragePath) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto backup = BackupJson();
    backup["sets"].push_back({{"name", "cloud-files"}, {"kind", "nfs"},
        {"workload", "nfs-public"}, {"export", "cloud"}});
    EXPECT_NO_THROW(ReadBackup(directory.path, backup));
    backup["sets"].back()["path"] = "/manual/copy";
    EXPECT_THROW(ReadBackup(directory.path, backup), ConfigurationError);
}

TEST(BackupInput, ReadsQuiescedWorkloadReferencesWithoutUserShellCommands) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto backup = BackupJson();
    backup["quiesce"] = {"cloud"};
    EXPECT_NO_THROW(ReadBackup(directory.path, backup));
    backup["quiesce"] = {{{"workload", "cloud"}, {"command", "arbitrary"}}};
    EXPECT_THROW(ReadBackup(directory.path, backup), ConfigurationError);
}

TEST(BackupInput, ReadsAnExplicitFilesystemWatchdogWithoutUnrelatedTimerControls) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto backup = BackupJson();
    backup["filesystem_watchdog"] = {{"OnActiveSec", 120}, {"AccuracySec", 1}};
    const auto definitions = ReadBackup(directory.path, backup);
    ASSERT_TRUE(definitions.site.backup->filesystem_watchdog);
    EXPECT_EQ(definitions.site.backup->filesystem_watchdog->at("OnActiveSec"), 120);
    backup["filesystem_watchdog"]["OnBootSec"] = 10;
    EXPECT_THROW(ReadBackup(directory.path, backup), ConfigurationError);
}

}  // namespace skeleton::test
