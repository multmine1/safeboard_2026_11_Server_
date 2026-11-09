#include <gtest/gtest.h>

#include "../src/backup.hpp"

namespace skeleton::test {
namespace {

InfrastructurePlan QuiescedPlan() {
    InfrastructurePlan plan;
    plan.topology.definitions.site.backup.emplace();
    auto& backup = *plan.topology.definitions.site.backup;
    backup.quiesce = {"cloud"};
    backup.sets = {{"sql", BackupSetKind::Postgresql, {}, {}, "postgres", 64},
        {"files", BackupSetKind::Nfs, {}, {}, "files", {}, "cloud"}};
    plan.kubernetes.controller = "controller";
    plan.kubernetes.settings["paths"]["binary"] = "/usr/local/bin/k0s";
    plan.topology.host_access_paths = {{"controller", {"root", "management-router", "controller"}},
        {"app", {"root", "public-router", "app"}}, {"db", {"root", "public-router", "db"}}};
    auto& cloud = plan.applications["cloud"];
    cloud.component = "nextcloud";
    cloud.enabled = true;
    cloud.vm = "app";
    cloud.network_namespace = "net-public";
    cloud.required_vms = {"app", "db"};
    cloud.deployment_order = {"postgres", "files", "cloud"};
    cloud.readiness = {{{"resource", "deployment/cloud-app"}, {"rollout", true}}};
    cloud.resources = {{{"kind", "PersistentVolume"}, {"spec", {{"nfs", {{"path", "/srv/cloud"}, {"server", "router.example.invalid"}}}}}}};
    plan.applications["postgres"].component = "cloudnative-pg";
    plan.applications["postgres"].resources = {{{"apiVersion", "postgresql.cnpg.io/v1"}, {"kind", "Cluster"}}};
    plan.applications["files"].resources = Json::array();
    plan.nfs_exports[{"files", "cloud"}] = {"files", "db", "nfs", "cloud", "/srv/cloud", {"app"}, {}, "backup-files"};
    plan.vms["app"].variables["connections"]["nfs"]["fqdn"] = "router.example.invalid";
    return plan;
}

InfrastructurePlan MailBackupPlan() {
    auto plan = QuiescedPlan();
    plan.topology.definitions.site.backup->quiesce = {"mail"};
    plan.topology.definitions.site.backup->sets.back() = {"blobs", BackupSetKind::Nfs, {}, {}, "mail", {}, "messages"};
    plan.kubernetes.settings["paths"]["unit_directory"] = "/etc/systemd/system";
    auto& mail = plan.applications["mail"];
    mail.component = "stalwart";
    mail.enabled = true;
    mail.vm = "app";
    mail.required_vms = {"app", "db"};
    mail.deployment_order = {"postgres", "mail"};
    mail.resources = {{{"kind", "PersistentVolume"}, {"spec", {{"local", {{"path", "/srv/messages/stalwart"}}}}}}};
    plan.vms["app"].variables["mounts"] = {{{"path", "/srv/messages"}, {"fstype", "ext4"},
        {"src", "/dev/disk/by-id/virtio-messages"}}};
    plan.nfs_exports.clear();
    plan.nfs_exports[{"backup-mail", "messages"}] = {"mail", "app", "backup-mail", "messages",
        "/srv/messages/stalwart", {}, {}, "host-mail"};
    return plan;
}

}  // namespace

TEST(BackupPlanning, ResolvesMaintenanceFromThePreparedDeploymentAndAllRoutedDependencies) {
    const auto sources = BuildBackupQuiesceSources(QuiescedPlan());
    ASSERT_TRUE(sources.contains("cloud"));
    const auto& source = sources.at("cloud");
    EXPECT_EQ(source.vm, "controller");
    EXPECT_EQ(source.required_vms, (std::set<std::string>{"root", "management-router", "controller", "public-router", "app", "db"}));
    const std::vector<std::string> prefix{"/usr/local/bin/k0s", "kubectl", "exec", "--namespace", "net-public",
        "--container", "nextcloud", "deployment/cloud-app", "--", "php", "occ"};
    auto expected = prefix;
    expected.insert(expected.end(), {"status", "--output=json"});
    EXPECT_EQ(source.status, expected);
    expected = prefix;
    expected.insert(expected.end(), {"maintenance:mode", "--on"});
    EXPECT_EQ(source.enter, expected);
    expected.back() = "--off";
    EXPECT_EQ(source.leave, expected);
}

TEST(BackupPlanning, RejectsUnresolvedMaintenanceTargetsAndUnmatchedDatabaseOrFileCopies) {
    for (const auto fault : {"unknown", "disabled", "component", "deployment", "ambiguous", "sql", "files", "export", "endpoint"}) {
        SCOPED_TRACE(fault);
        auto plan = QuiescedPlan();
        auto& cloud = plan.applications.at("cloud");
        const std::string name = fault;
        if (name == "unknown") plan.topology.definitions.site.backup->quiesce = {"unknown"};
        if (name == "disabled") cloud.enabled = false;
        if (name == "component") cloud.component = "other";
        if (name == "deployment") cloud.readiness = Json::array();
        if (name == "ambiguous") cloud.readiness.push_back(cloud.readiness.front());
        if (name == "sql") plan.topology.definitions.site.backup->sets.erase(plan.topology.definitions.site.backup->sets.begin());
        if (name == "files") plan.topology.definitions.site.backup->sets.pop_back();
        if (name == "export") plan.nfs_exports.begin()->second.path = "/unrelated";
        if (name == "endpoint") cloud.resources[0]["spec"]["nfs"]["server"] = "another.invalid";
        EXPECT_THROW(BuildBackupQuiesceSources(plan), ConfigurationError);
    }
}

TEST(BackupPlanning, ResolvesMailFreezeOnlyForItsDedicatedMountAndMatchedSqlAndBlobCopies) {
    const auto sources = BuildBackupQuiesceSources(MailBackupPlan());
    const auto& mail = sources.at("mail");
    ASSERT_TRUE(mail.filesystem);
    EXPECT_EQ(mail.vm, "app");
    EXPECT_EQ(mail.filesystem->mount, "/srv/messages");
    EXPECT_EQ(mail.filesystem->unit_directory, "/etc/systemd/system");
    EXPECT_EQ(mail.required_vms, (std::set<std::string>{"root", "public-router", "app", "db"}));
    EXPECT_TRUE(mail.enter.empty());

    for (const auto fault : {"sql", "blobs", "export", "host", "vm", "mount", "fstype", "root"}) {
        SCOPED_TRACE(fault);
        auto invalid = MailBackupPlan();
        const std::string name = fault;
        if (name == "sql") invalid.topology.definitions.site.backup->sets.erase(invalid.topology.definitions.site.backup->sets.begin());
        if (name == "blobs") invalid.topology.definitions.site.backup->sets.pop_back();
        if (name == "export") invalid.nfs_exports.begin()->second.path = "/srv/messages/unrelated";
        if (name == "host") invalid.nfs_exports.begin()->second.hypervisor_service.reset();
        if (name == "vm") invalid.nfs_exports.begin()->second.server = "db";
        if (name == "mount") invalid.vms.at("app").variables["mounts"] = Json::array();
        if (name == "fstype") invalid.vms.at("app").variables["mounts"][0]["fstype"] = "nfs";
        if (name == "root") invalid.vms.at("app").variables["mounts"][0]["path"] = "/";
        EXPECT_THROW(BuildBackupQuiesceSources(invalid), ConfigurationError);
    }
}

}  // namespace skeleton::test
