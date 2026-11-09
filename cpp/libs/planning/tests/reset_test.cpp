#include <gtest/gtest.h>

#include "hypervisor_fixture.hpp"
#include "monitoring_fixture.hpp"
#include "kubernetes_fixture.hpp"
#include "proxy_fixture.hpp"
#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::test {
namespace {

SourceDefinitions ResetDefinitions() {
    auto definitions = Definitions();
    auto host = HypervisorJson();
    host["controller_home"] = "/home/operator";
    host["controller_project_directory"] = "/home/operator/project";
    host["controller_access"] = {{"ssh_config", ".ssh/config"}, {"known_hosts", ".ssh/known_hosts"}, {"marker", "# {mark} hypervisor"}};
    host["hypervisor_sshd"] = SshPolicyJson();
    definitions.site.hypervisor_settings = host;
    definitions.site.services["controller"] = {{"project_directory", "/home/operator/project"},
        {"known_hosts", "/Users/controller/.ssh/known_hosts"}};
    definitions.site.hypervisor_alias = host.at("hypervisor_alias");
    definitions.site.services["hypervisor"]["local_libvirt_uri"] = host.at("generator").at("controller").at("local_libvirt_uri");
    definitions.site.services["reset"] = {{"directory", "environment/.tools/reset"},
        {"tofu_directory", "configs/generated/tofu"}, {"validator", "environment/.tools/build/cpp/config-generator"},
        {"directory_mode", "0700"}, {"file_mode", "0600"}, {"state_file", "terraform.tfstate"},
        {"backup_repository", "preserve"}, {"operation_lock_file", "/run/lock/project-maintenance.lock"},
        {"workspace", "default"}, {"session_retries", 10}, {"session_delay_seconds", 1},
        {"files", {{"plan", "destroy.tfplan"}, {"snapshot", "tofu.json"}, {"filesystem", "filesystem.json"}, {"preview", "preview.json"}}}};
    return definitions;
}

}  // namespace

TEST(ResetHost, SeparatesControllerStateAndBackupInputsFromThePreservedHypervisorWorkspace) {
    auto definitions = ResetDefinitions();
    definitions.site.services["controller"]["project_directory"] = "/Users/operator/stand";
    definitions.site.backup = BackupDefinition{"hd", "backups/project",
        "environment/.tools/env/bin", "environment/.tools/backup/cache",
        "environment/.tools/backup/restore", 64, {{"daily", 7}},
        {{"project", BackupSetKind::Files, {"."}, {}}, {"state", BackupSetKind::TofuState, {}, {}}}};
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& reset = plan.hypervisor.at("reset");
    EXPECT_EQ(reset.at("paths").at("state"), "/Users/operator/stand/configs/generated/tofu/terraform.tfstate");
    EXPECT_EQ(reset.at("controller_known_hosts"), "/Users/controller/.ssh/known_hosts");
    EXPECT_EQ(reset.at("operation_lock_file"), "/run/lock/project-maintenance.lock");
    EXPECT_EQ(reset.at("operation_lock_alias"), plan.hypervisor.at("controller").at("admin_alias"));
    EXPECT_EQ(plan.backup->project_directory, "/Users/operator/stand");
    EXPECT_EQ(plan.backup->sets.at("state").working_directory, "/Users/operator/stand/configs/generated/tofu");
    EXPECT_TRUE(reset.at("boundary").at("trees").get<std::set<std::string>>().contains("/home/operator/project"));
}

TEST(ResetHost, RequiresAnExplicitAbsoluteControllerWorkspaceWithoutAHypervisorFallback) {
    for (const auto path : {"", "/", "relative/project", "/Users/operator/../project"}) {
        SCOPED_TRACE(path);
        auto definitions = ResetDefinitions();
        if (std::string(path).empty()) definitions.site.services.erase("controller");
        else definitions.site.services["controller"]["project_directory"] = path;
        const auto topology = BuildTopology(definitions);

        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(ResetHost, RequiresAnExplicitAbsoluteControllerTrustFile) {
    for (const auto& path : {Json(), Json(""), Json("/"), Json("relative/known_hosts"),
                            Json("/Users/controller/../known_hosts"), Json("/Users/controller/."), Json(12)}) {
        SCOPED_TRACE(path.dump());
        auto definitions = ResetDefinitions();
        if (path.is_null()) definitions.site.services["controller"].erase("known_hosts");
        else definitions.site.services["controller"]["known_hosts"] = path;
        const auto topology = BuildTopology(definitions);

        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(ResetHost, RetiresDeclaredUnitFilesAndCredentialsWithoutKeepingTheirOldScheduler) {
    auto definitions = ResetDefinitions();
    definitions.site.services["reset"]["retired"] = {
        {"units", {"/etc/systemd/system/project-backup.service", "/etc/systemd/system/project-backup.timer"}},
        {"files", {"/etc/project/backup/vault.cred"}}};
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    const auto& reset = plan.hypervisor.at("reset");
    const auto files = reset.at("remove_files").get<std::set<std::string>>();
    const auto services = reset.at("stop_services").get<std::set<std::string>>();

    EXPECT_TRUE(files.contains("/etc/systemd/system/project-backup.service"));
    EXPECT_TRUE(files.contains("/etc/systemd/system/project-backup.timer"));
    EXPECT_TRUE(files.contains("/etc/project/backup/vault.cred"));
    EXPECT_TRUE(services.contains("project-backup.service"));
    EXPECT_TRUE(services.contains("project-backup.timer"));
}

TEST(ResetHost, RefusesStoppingFoundationUnitsThroughAnUnrelatedRetiredFile) {
    for (const auto unit : {"ssh", "ssh.service", "ssh.socket", "sshd.service", "libvirtd-ro.socket", "NetworkManager.service"}) {
        SCOPED_TRACE(unit);
        auto definitions = ResetDefinitions();
        definitions.site.services["reset"]["retired"] = {
            {"units", {"/var/lib/project/" + std::string(unit)}}, {"files", Json::array()}};
        const auto topology = BuildTopology(definitions);

        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(ResetHost, KeepsDeclaredSocketProxyAndCockpitUnitsInTheSameCleanupScope) {
    auto definitions = ResetDefinitions();
    EnableHostProxy(definitions);
    definitions.site.hypervisor_settings["hypervisor_cockpit"] = {
        {"socket", "cockpit.socket"}, {"unit", "cockpit.service"},
        {"dropin", "/etc/systemd/system/cockpit.socket.d/listen.conf"},
        {"tls", {{"certificate_file", "/etc/cockpit/ws-certs.d/stand.cert"},
                 {"private_key_file", "/etc/cockpit/ws-certs.d/stand.key"}}}};
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    const auto& reset = plan.hypervisor.at("reset");
    const auto files = reset.at("remove_files").get<std::set<std::string>>();
    const auto services = reset.at("stop_services").get<std::set<std::string>>();
    EXPECT_TRUE(files.contains("/etc/systemd/system/stand-proxy-admin.socket"));
    EXPECT_TRUE(files.contains("/etc/systemd/system/stand-proxy-admin.service"));
    EXPECT_TRUE(services.contains("stand-proxy-admin.socket"));
    EXPECT_TRUE(services.contains("stand-proxy-admin.service"));
    EXPECT_TRUE(services.contains("cockpit.socket"));
    EXPECT_TRUE(services.contains("cockpit.service"));
}

TEST(ResetHost, PreparesPhysicalDnsAndExactVmPathsWhilePreservingThePlatformAndAccess) {
    const auto topology = BuildTopology(ResetDefinitions());
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& reset = plan.hypervisor.at("reset");
    EXPECT_EQ(reset.at("project_directory"), "/home/operator/project");
    EXPECT_EQ(reset.at("paths").at("plan"), "/home/operator/project/environment/.tools/reset/destroy.tfplan");
    EXPECT_EQ(reset.at("controller_access").at("known_hosts"), "/home/operator/.ssh/known_hosts");
    EXPECT_EQ(reset.at("bootstrap_netplan"), plan.hypervisor.at("bootstrap").at("hypervisor_netplan"));
    EXPECT_EQ(reset.at("volume_directories").size(), 4);
    EXPECT_EQ(reset.at("account").at("home"), "/home/vmctl");
    EXPECT_EQ(reset.at("platform_pools"), plan.hypervisor.at("bootstrap").at("storage_pools"));
    EXPECT_EQ(reset.at("known_host_aliases"), Json::array({kEdge, kGate, kDb, kApi}));
    EXPECT_EQ(reset.at("bootstrap_netplan").at("network").at("bridges").at("br-wan").at("nameservers").at("addresses"),
              Json::array({"192.0.2.1"}));
    EXPECT_EQ(reset.at("transition_netplan").at("network").at("bridges").at("br-wan").at("nameservers"),
              reset.at("bootstrap_netplan").at("network").at("bridges").at("br-wan").at("nameservers"));
    EXPECT_EQ(reset.at("transition_netplan").at("network").at("bridges").at("br-wan").at("routes"),
              plan.hypervisor.at("bootstrap").at("hypervisor_runtime_netplan").at("network").at("bridges").at("br-wan").at("routes"));
}

TEST(ResetHost, RejectsRemovingTheControllerOrItsWorkingTree) {
    for (const bool account : {false, true}) {
        auto definitions = ResetDefinitions();
        if (account) definitions.site.hypervisor_settings["hypervisor_user"] = "operator";
        else definitions.site.hypervisor_settings["controller_project_directory"] = "/home/vmctl/project";
        const auto topology = BuildTopology(definitions);

        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(ResetHost, RejectsAPlanWorkspaceThatCouldOverwriteNativeStateOrItsValidator) {
    for (const auto workspace : {"configs/generated/tofu", "configs/generated", "configs/generated/tofu/reset", "environment/.tools/build/cpp"}) {
        SCOPED_TRACE(workspace);
        auto definitions = ResetDefinitions();
        definitions.site.services["reset"]["directory"] = workspace;
        if (std::string(workspace) == "environment/.tools/build/cpp")
            definitions.site.services["reset"]["files"]["plan"] = "config-generator";
        const auto topology = BuildTopology(definitions);

        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(ResetHost, IncludesTheHostCollectorFilesAndServiceInTheReviewedCleanupScope) {
    auto definitions = ResetDefinitions();
    EnableHostMonitoring(definitions);
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    const auto& reset = plan.hypervisor.at("reset");
    const auto files = reset.at("remove_files").get<std::set<std::string>>();
    const auto services = reset.at("stop_services").get<std::set<std::string>>();
    EXPECT_TRUE(files.contains("/usr/local/bin/telegraf"));
    EXPECT_TRUE(files.contains("/etc/skeleton/host-metrics.toml"));
    EXPECT_TRUE(files.contains("/etc/systemd/system/skeleton-host-metrics.service"));
    EXPECT_TRUE(services.contains("skeleton-host-metrics.service"));
}

TEST(BackupHost, UsesTheDeclaredPhysicalMountAndPreparedStatePathAndPreservesItsRepository) {
    auto definitions = ResetDefinitions();
    definitions.site.backup = BackupDefinition{"hd", "backups/project",
        "environment/.tools/anaconda/envs/project/bin", "environment/.tools/backup/cache",
        "environment/.tools/backup/restore", 64, {{"daily", 7}},
        {{"project", BackupSetKind::Files, {"."}, {}}, {"state", BackupSetKind::TofuState, {}, {}}}};
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    ASSERT_TRUE(plan.backup);
    EXPECT_EQ(plan.backup->repository, "/srv/example-vms/hd/backups/project");
    const auto& reset = plan.hypervisor.at("reset");
    EXPECT_EQ(plan.backup->sets.at("state").working_directory.string(), reset.at("paths").at("tofu").get<std::string>());
    EXPECT_EQ(reset.at("backup_repository_access").at("parents"), Json::array({"/srv/example-vms/hd", "/srv/example-vms/hd/backups"}));
    EXPECT_EQ(reset.at("backup_repository_access").at("user"), reset.at("account").at("name"));
    EXPECT_EQ(reset.at("backup_repository_access").at("repository"), plan.backup->repository.string());
    const auto preserved = reset.at("boundary").at("trees").get<std::set<std::string>>();
    EXPECT_TRUE(preserved.contains(plan.backup->repository.string()));

    definitions.site.backup->repository_directory = "volumes/backup";
    const auto invalid = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(invalid, AllocateAddressPlan(invalid)), ConfigurationError);
}

TEST(BackupHost, IncludesOnlyItsDeclaredRepositoryWhenTheResetPolicyRemovesBackups) {
    auto definitions = ResetDefinitions();
    definitions.site.services["reset"]["backup_repository"] = "remove";
    definitions.site.backup = BackupDefinition{"hd", "backups/project",
        "environment/.tools/env/bin", "environment/.tools/backup/cache",
        "environment/.tools/backup/restore", 64, {{"daily", 7}},
        {{"project", BackupSetKind::Files, {"."}, {}}}};
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    const auto& reset = plan.hypervisor.at("reset");

    EXPECT_EQ(reset.at("remove_directories"), Json::array({plan.backup->repository.string()}));
    const auto preserved = reset.at("boundary").at("trees").get<std::set<std::string>>();
    EXPECT_FALSE(preserved.contains(plan.backup->repository.string()));
    EXPECT_TRUE(preserved.contains("/home/operator/project"));
    EXPECT_TRUE(preserved.contains("/home/operator/.ssh"));
}

TEST(BackupHost, RequiresAnExplicitSupportedRepositoryResetPolicy) {
    for (const auto policy : {Json(), Json("unknown"), Json(true)}) {
        auto definitions = ResetDefinitions();
        definitions.site.backup = BackupDefinition{"hd", "backups/project",
            "environment/.tools/env/bin", "environment/.tools/backup/cache",
            "environment/.tools/backup/restore", 64, {{"daily", 7}},
            {{"project", BackupSetKind::Files, {"."}, {}}}};
        if (policy.is_null()) definitions.site.services["reset"].erase("backup_repository");
        else definitions.site.services["reset"]["backup_repository"] = policy;
        const auto topology = BuildTopology(definitions);

        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(BackupHost, ResolvesNativeDatabaseAndK0sProducersThroughThePreparedControllerAndRouterPaths) {
    auto definitions = ResetDefinitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["paths"]["data_directory"] = "/var/lib/example-cluster";
    definitions.site.backup = BackupDefinition{"hd", "backups/project", "environment/.tools/env/bin",
        "environment/.tools/backup/cache", "environment/.tools/backup/restore", 64, {{"daily", 7}},
        {{"cluster", BackupSetKind::K0s, {}, {}, {}, 64},
         {"database", BackupSetKind::Postgresql, {}, {}, "postgres", 128}},
        BackupCaptureDefinition{"/var/tmp", "environment/.tools/backup/capture", 32}};
    const Json service{{"selectorType", "rw"}, {"serviceTemplate", {{"metadata", {{"name", "database-primary"}}}}}};
    const Json cluster{{"apiVersion", "postgresql.cnpg.io/v1"}, {"kind", "Cluster"},
        {"metadata", {{"name", "postgres"}}},
        {"spec", {{"managed", {{"services", {{"additional", Json::array({service})}}}}}}}};
    SetWorkloads(definitions, Json::array({{{"name", "postgres"}, {"vm", kDb}, {"enabled", true},
        {"resources", Json::array({cluster})}}}));
    definitions.vms.at(kApi).domain["running"] = false;
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    const auto& capture = plan.backup->sets.at("database").capture;
    ASSERT_TRUE(capture);
    EXPECT_EQ(capture->vm, kApi);
    EXPECT_EQ(capture->required_vms, (std::set<std::string>{kEdge, kGate, kApi, kDb}));
    EXPECT_NE(std::find(capture->argv.begin(), capture->argv.end(), "service/database-primary"), capture->argv.end());
    const auto& native = plan.backup->sets.at("cluster").capture;
    ASSERT_TRUE(native);
    EXPECT_NE(std::find(native->argv.begin(), native->argv.end(), "/var/lib/example-cluster"), native->argv.end());
    definitions.site.backup->sets.back().workload = "missing";
    const auto invalid = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(invalid, AllocateAddressPlan(invalid)), ConfigurationError);
}

TEST(BackupHost, IncludesNativeCredentialsUnitsAndCachesInResetAndProtectsFoundationUnits) {
    auto definitions = ResetDefinitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["paths"]["data_directory"] = "/var/lib/example-cluster";
    definitions.site.backup = BackupDefinition{"hd", "backups/project", "environment/.tools/env/bin",
        "environment/.tools/backup/cache", "environment/.tools/backup/restore", 64, {{"daily", 7}},
        {{"cluster", BackupSetKind::K0s, {}, {}, {}, 64}},
        BackupCaptureDefinition{"/var/tmp", "environment/.tools/backup/capture", 32}};
    definitions.site.backup->host = BackupHostDefinition{"project-backup", "/usr/local/bin/restic", "/etc/systemd/system",
        "/var/cache/project/capture", "/var/cache/project/restic", "/etc/project/credentials", "/etc/project/known_hosts",
        "/etc/ssh/ssh_host_ed25519_key.pub", "/etc/tmpfiles.d/project-backup.conf", "/var/cache/project/installation",
        "host", {{"TimeoutStartSec", 3600}, {"UMask", "0077"}},
        {{"BatchMode", "yes"}, {"StrictHostKeyChecking", "yes"}, {"IdentitiesOnly", "yes"}, {"IdentityAgent", "none"}},
        {{"Timer", {{"OnCalendar", "daily"}}}}};
    definitions.site.backup->host->account = BackupAccountDefinition{"svc-backup", "/usr/sbin/nologin", "0440", "2770"};
    definitions.site.asset_data["host_backup"]["restic"] = {{"url", "https://example.org/restic.bz2"},
        {"checksum", "sha256:" + std::string(64, 'a')}, {"archive_name", "restic.bz2"}};
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    const auto& reset = plan.hypervisor.at("reset");
    const auto units = reset.at("stop_services").get<std::vector<std::string>>();
    EXPECT_LT(std::find(units.begin(), units.end(), "project-backup.timer"),
              std::find(units.begin(), units.end(), "project-backup.service"));
    EXPECT_LT(std::find(units.begin(), units.end(), "project-backup.service"),
              std::find(units.begin(), units.end(), "project-backup-cluster.service"));
    EXPECT_TRUE(reset.at("remove_directories").get<std::set<std::string>>().contains("/etc/project/credentials"));
    EXPECT_TRUE(reset.at("remove_files").get<std::set<std::string>>().contains("/etc/tmpfiles.d/project-backup.conf"));
    EXPECT_EQ(plan.backup->host->connections.at(kApi).at("address"), plan.vms.at(kApi).connection.at("host"));
    EXPECT_EQ(reset.at("service_accounts").at(0).at("name"), "svc-backup");
    EXPECT_EQ(reset.at("backup_repository_access").at("service_user"), "svc-backup");
    definitions.site.backup->host->name = "ssh";
    const auto invalid = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(invalid, AllocateAddressPlan(invalid)), ConfigurationError);
}

}  // namespace skeleton::test
