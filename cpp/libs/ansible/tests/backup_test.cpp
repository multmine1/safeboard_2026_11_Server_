#include <gtest/gtest.h>

#include "../src/backup.hpp"

namespace skeleton::test {

TEST(BackupExport, PublishesPreparedGuestUnitsAndFilesystemStatusWithoutRecalculatingThem) {
    InfrastructurePlan plan;
    plan.hypervisor["controller"] = {{"admin_alias", "host-bootstrap"}};
    plan.backup.emplace();
    auto& backup = *plan.backup;
    backup.guest_quiesce_units["mail-vm"] = {{"unit_directory", "/etc/systemd/system"},
        {"units", {{"prepared.service", {{"Service", {{"ExecStart", "prepared-command"}}}}}}}};
    BackupQuiesceSource source{"mail-vm", {"prepared-status"}, {"prepared-enter"}, {"prepared-leave"}, {"mail-vm"},
        BackupFilesystemSource{"/srv/messages", "/etc/systemd/system"}};
    backup.quiesce.push_back({"mail", "/var/cache/backup/mail.maintenance", source});
    backup.host.emplace();
    backup.host->repository_access = {{"owner", "svc-backup"}, {"transport_user", "vmctl"},
        {"permission_argv", {{"prepared", "permissions"}}}};
    GeneratedFiles files;
    AddBackupConfiguration(files, plan);
    const auto exported = Json::parse(files.at("ansible/backup.json")).at("backup");
    EXPECT_EQ(exported.at("guest_quiesce_units"), backup.guest_quiesce_units);
    EXPECT_EQ(exported.at("quiesce").at(0).at("filesystem_mount"), "/srv/messages");
    EXPECT_EQ(exported.at("quiesce").at(0).at("status_argv"), source.status);
    EXPECT_EQ(exported.at("repository_access"), backup.host->repository_access);
}

}  // namespace skeleton::test
