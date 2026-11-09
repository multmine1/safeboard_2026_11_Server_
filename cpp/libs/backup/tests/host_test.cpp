#include <gtest/gtest.h>

#include "host_fixture.hpp"

namespace skeleton::test {

TEST(BackupHost, SeparatesTheRepositoryWriterFromTransportAndCompilesOneSharedAccessPolicy) {
    auto definition = HostDefinition();
    auto context = HostContext();
    const auto plan = BuildBackupPlan(definition, context);
    const auto& host = *plan.host;
    EXPECT_EQ(host.repository_user, "svc-backup");
    EXPECT_NE(host.repository_user, context.host->transport_user);
    EXPECT_EQ(host.units.at("project-backup-work.service").at("Service").at("User"), "svc-backup");
    EXPECT_EQ(host.units.at("project-backup-verify.service").at("Service").at("User"), "svc-backup");
    EXPECT_EQ(host.repository_access.at("transport_user"), "vmctl");
    EXPECT_EQ(host.repository_access.at("owner"), "svc-backup");
    EXPECT_EQ(host.repository_access.at("group"), "svc-backup");
    EXPECT_EQ(host.repository_access.at("users"), Json::array({"svc-backup", "vmctl"}));
    EXPECT_EQ(host.repository_access.at("account").at("home"), definition.host->cache_directory.string());
    EXPECT_EQ(host.repository_access.at("file_mode"), "0440");
    const std::vector<std::string> files{"find", "/data/hd/backups/project", "-type", "f", "-exec", "chmod", "--changes", "0440", "{}", "+"};
    EXPECT_EQ(host.repository_access.at("permission_argv").at(0), files);
    for (const auto fault : {"missing", "transport", "controller", "root", "shell", "files", "writable", "directories"}) {
        SCOPED_TRACE(fault);
        auto invalid = definition;
        const std::string name = fault;
        if (name == "missing") invalid.host->account.reset();
        else {
            auto& account = *invalid.host->account;
            if (name == "transport") account.name = context.host->transport_user;
            if (name == "controller") account.name = context.host->lock_user;
            if (name == "root") account.name = "root";
            if (name == "shell") account.shell = "/bin/bash";
            if (name == "files") account.file_mode = "0644";
            if (name == "writable") account.file_mode = "0640";
            if (name == "directories") account.directory_mode = "0770";
        }
        EXPECT_THROW(BuildBackupPlan(invalid, context), ConfigurationError);
    }
}

TEST(BackupHost, CompilesLockedNativeJobsAndKeepsMacFilesOffTheHypervisor) {
    const auto plan = BuildBackupPlan(HostDefinition(), HostContext());
    ASSERT_TRUE(plan.host);
    const auto& host = *plan.host;
    const auto& gate = host.units.at("project-backup.service").at("Service");
    EXPECT_EQ(gate.at("Type"), "exec");
    EXPECT_EQ(gate.at("StandardInput"), "file:/run/lock/project-maintenance.lock");
    EXPECT_NE(gate.at("ExecStart").get<std::string>().find("--wait"), std::string::npos);
    const auto& work = host.units.at("project-backup-work.service");
    EXPECT_EQ(work.at("Service").at("User"), "svc-backup");
    EXPECT_EQ(work.at("Unit").at("After"), "project-backup.service project-backup-database.service");
    EXPECT_EQ(work.dump().find("/work/project"), std::string::npos);
    EXPECT_EQ(work.at("Service").at("ExecStart").dump().find("forget"), std::string::npos);
    EXPECT_NE(host.units.at("project-backup-verify.service").at("Service").at("ExecStart").back().get<std::string>().find("forget"), std::string::npos);
    EXPECT_EQ(host.units.size(), 5);
    const auto& producer = host.units.at("project-backup-database.service").at("Service");
    EXPECT_FALSE(producer.contains("StandardInput"));
    EXPECT_EQ(producer.at("StandardError"), "journal");
    EXPECT_EQ(producer.at("LoadCredentialEncrypted").at(0), "guest-controller:/etc/project/credentials/guest-controller.cred");
    EXPECT_EQ(producer.at("LimitFSIZE"), 16 * 1024 * 1024);
    EXPECT_NE(producer.at("ExecStart").at(0).get<std::string>().find("$$x%%y"), std::string::npos);
    EXPECT_EQ(host.connections.at("controller").at("known_host"), "[10.1.2.3]:2222");
    EXPECT_NE(host.tmpfiles_content.find("operator"), std::string::npos);
    EXPECT_EQ(host.units.at(host.timer_unit).at("Timer").at("Unit"), host.gate_unit);
}

TEST(BackupHost, RejectsUnsafeHostPathsUnresolvedAccessAndDuplicatedRuntimePolicy) {
    for (const auto fault : {"context", "connection", "overlap", "repository", "relative", "command", "ssh", "timer", "credential", "empty"}) {
        SCOPED_TRACE(fault);
        auto definition = HostDefinition();
        auto context = HostContext();
        const auto name = std::string(fault);
        if (name == "context") context.host.reset();
        if (name == "connection") context.host->connections.clear();
        if (name == "overlap") definition.host->cache_directory = definition.host->capture_directory;
        if (name == "repository") definition.host->capture_directory = "/data/hd/backups/project/staging";
        if (name == "relative") definition.host->credential_directory = "relative";
        if (name == "command") definition.host->service_options["ExecStart"] = "other";
        if (name == "ssh") definition.host->ssh_options["StrictHostKeyChecking"] = "no";
        if (name == "timer") definition.host->timer["Timer"]["Unit"] = "other.service";
        if (name == "credential") definition.host->credential_key = "null";
        if (name == "empty") definition.sets.erase(definition.sets.begin());
        EXPECT_THROW(BuildBackupPlan(definition, context), ConfigurationError);
    }
}

TEST(BackupHost, ReadsNativeNfsFilesInAPrivateMountNamespaceWithoutStagingAnotherArchive) {
    auto definition = HostDefinition();
    definition.host->nfs = BackupNfsSettings{"/var/cache/project/mounts",
        "ro,nosuid,nodev,noexec,vers=4.2,soft,timeo=50,retrans=2"};
    BackupSetDefinition files{"cloud-files", BackupSetKind::Nfs};
    files.workload = "nfs-public";
    files.export_name = "cloud";
    definition.sets.push_back(files);
    auto context = HostContext();
    context.nfs_sources[files.name] = {"controller", "192.0.2.10:/srv/cloud", 12049, {"controller"}};
    const auto plan = BuildBackupPlan(definition, context);
    ASSERT_TRUE(plan.sets.at("cloud-files").nfs);
    EXPECT_FALSE(plan.sets.at("cloud-files").capture);
    const auto& nfs = *plan.sets.at("cloud-files").nfs;
    EXPECT_EQ(nfs.source, "192.0.2.10:/srv/cloud");
    EXPECT_EQ(nfs.directory, "/var/cache/project/mounts/cloud-files");
    EXPECT_NE(nfs.options.find("port=12049"), std::string::npos);
    const auto& work = plan.host->units.at("project-backup-work.service").at("Service");
    EXPECT_EQ(work.at("PrivateMounts"), true);
    EXPECT_NE(work.at("ExecStart").at(0).get<std::string>().find("runuser"), std::string::npos);
    EXPECT_EQ(work.at("ExecStart").at(0).get<std::string>().front(), '!');
    EXPECT_NE(work.at("ExecStart").at(0).get<std::string>().find("batch-$${INVOCATION_ID}"), std::string::npos);
    EXPECT_EQ(work.at("ExecStart").at(0).get<std::string>().find("umount"), std::string::npos);
    definition.host->nfs->options = "rw,nosuid,nodev,noexec,soft";
    EXPECT_THROW(BuildBackupPlan(definition, context), ConfigurationError);
    definition.host->nfs->options = "ro,nosuid,nodev,noexec,soft,port=2049";
    EXPECT_THROW(BuildBackupPlan(definition, context), ConfigurationError);
}

TEST(BackupHost, FreezesBeforeCapturesAndReleasesBeforeVerificationWithOneSnapshotBatch) {
    auto definition = HostDefinition();
    definition.quiesce = {"cloud"};
    auto context = HostContext();
    context.quiesce_sources["cloud"] = {"controller", {"k0s", "status"}, {"k0s", "enter"},
        {"k0s", "leave"}, {"controller"}};
    const auto plan = BuildBackupPlan(definition, context);
    const auto& units = plan.host->units;
    ASSERT_TRUE(units.contains("project-backup-quiesce.service"));
    EXPECT_EQ(units.at("project-backup-quiesce.service").at("Service").at("RemainAfterExit"), true);
    EXPECT_EQ(units.at("project-backup-database.service").at("Unit").at("Requires"), "project-backup-quiesce.service");
    const auto script = units.at("project-backup.service").at("Service").at("ExecStart").get<std::string>();
    EXPECT_LT(script.find("start' 'project-backup-quiesce.service"), script.find("start' '--wait' 'project-backup-work.service"));
    EXPECT_TRUE(units.contains("project-backup-verify.service"));
    EXPECT_FALSE(units.at("project-backup-verify.service").at("Unit").contains("Requires"));
    EXPECT_LT(script.find("stop' 'project-backup-quiesce.service"), script.find("start' '--wait' 'project-backup-verify.service"));
    const auto& work = units.at("project-backup-work.service").at("Service").at("ExecStart");
    EXPECT_EQ(work.dump().find("forget"), std::string::npos);
    EXPECT_NE(work.dump().find("batch-${INVOCATION_ID}"), std::string::npos);
    const auto& completed = units.at("project-backup-work.service").at("Service").at("ExecStartPost");
    EXPECT_NE(completed.get<std::string>().find("captured"), std::string::npos);
    EXPECT_NE(completed.get<std::string>().find("batch-${INVOCATION_ID}"), std::string::npos);
    EXPECT_NE(units.at("project-backup-quiesce.service").dump().find("cloud.maintenance"), std::string::npos);
    definition.quiesce.push_back("cloud");
    EXPECT_THROW(BuildBackupPlan(definition, context), ConfigurationError);
}

TEST(BackupHost, CompilesGuestFilesystemOwnershipAndDeadlineBeforeClaimingTheBatch) {
    auto definition = HostDefinition();
    definition.quiesce = {"mail"};
    definition.filesystem_watchdog = {{"OnActiveSec", 120}, {"AccuracySec", 1}};
    auto context = HostContext();
    context.quiesce_sources["mail"] = {"controller", {}, {}, {}, {"controller"},
        BackupFilesystemSource{"/srv/mail", "/etc/systemd/system"}};
    const auto plan = BuildBackupPlan(definition, context);
    const auto& guest = plan.guest_quiesce_units.at("controller");
    const auto& units = guest.at("units");
    const auto& freeze = units.at("project-backup-mail-freeze.service").at("Service");
    EXPECT_EQ(freeze.at("Type"), "oneshot");
    EXPECT_EQ(freeze.at("RemainAfterExit"), true);
    EXPECT_EQ(freeze.at("ExecStopPost").get<std::string>().find("--unfreeze"), std::string::npos);
    EXPECT_NE(freeze.at("ExecStart").get<std::string>().find("/srv/mail"), std::string::npos);
    EXPECT_NE(freeze.at("ExecStop").get<std::string>().find("--unfreeze"), std::string::npos);
    EXPECT_EQ(units.at("project-backup-mail-watchdog.timer").at("Timer").at("OnActiveSec"), 120);
    EXPECT_EQ(units.at("project-backup-mail-watchdog.timer").at("Timer").at("AccuracySec"), 1);
    const auto& after = plan.host->units.at("project-backup-work.service").at("Service").at("ExecStartPost");
    ASSERT_TRUE(after.is_array());
    EXPECT_NE(after.front().get<std::string>().find("project-backup-freeze-check.service"), std::string::npos);
    EXPECT_NE(after.back().get<std::string>().find("captured"), std::string::npos);
    const auto& check = plan.host->units.at("project-backup-freeze-check.service").at("Service");
    EXPECT_FALSE(check.contains("User"));
    EXPECT_NE(check.at("ExecStart").dump().find("ActiveState"), std::string::npos);
    EXPECT_EQ(check.at("LoadCredentialEncrypted").size(), 2);
    EXPECT_EQ(plan.host->units.at("project-backup-work.service").at("Service").at("LoadCredentialEncrypted").size(), 1);

    for (const auto fault : {"policy", "root", "duration", "unit-directory"}) {
        auto invalid = definition;
        auto bad_context = context;
        if (std::string(fault) == "policy") invalid.filesystem_watchdog.reset();
        if (std::string(fault) == "root") bad_context.quiesce_sources["mail"].filesystem->mount = "/";
        if (std::string(fault) == "duration") invalid.filesystem_watchdog->at("OnActiveSec") = 0;
        if (std::string(fault) == "unit-directory") bad_context.quiesce_sources["mail"].filesystem->unit_directory = "/";
        EXPECT_THROW(BuildBackupPlan(invalid, bad_context), ConfigurationError);
    }
    definition.quiesce.clear();
    EXPECT_THROW(BuildBackupPlan(definition, context), ConfigurationError);
}

}  // namespace skeleton::test
