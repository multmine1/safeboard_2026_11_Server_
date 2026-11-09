#include <gtest/gtest.h>

#include "fixture.hpp"
#include "hypervisor_fixture.hpp"
#include "skeleton/pipeline/generator.hpp"

namespace skeleton::test {
namespace {

GeneratedFiles Compile(const std::filesystem::path& source) {
    const auto topology = BuildTopology(ReadSourceDefinitions(source));
    return GenerateConfiguration(topology, AllocateAddressPlan(topology));
}

}  // namespace

TEST(Export, ReusesThePreparedHypervisorImageAndStableGuestDiskNames) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto site = SiteJson();
    site["image"]["path"] = "/srv/example-vms/nvme/images/debian-13.qcow2";
    site["image"]["prepare_ram_mib"] = 3072;
    site["image"]["prepare_retries"] = 5;
    site["image"]["prepare_retry_delay_seconds"] = 7;
    site["image"]["apt_config"] = {{"path", "/etc/apt/apt.conf.d/80-acquire"},
                                   {"content", "Acquire::Retries \"0\";\n"}};
    site["services"]["storage"] = {{"mount_root", "/srv"}, {"fstype", "ext4"}, {"opts", "defaults"}};
    site["services"]["ssh"]["admin_settings"] = {{"groups", {"sudo"}}, {"shell", "/bin/bash"}};
    auto db = VmJson(kDb, "db", "postgres");
    db["disks"].push_back({{"name", "data"}, {"storage", "ssd"}, {"size_gib", 64}});
    WriteJson(directory.path / "machines/db.vm.json", db);
    WriteJson(directory.path / "site.json", site);

    const auto files = Compile(directory.path);

    const auto module = Json::parse(files.at("tofu/modules/vm/main.tf.json"));
    EXPECT_FALSE(module.at("resource").at("libvirt_volume").contains("image"));
    const auto data = Json::parse(files.at("tofu/modules/vm/data.tf.json")).at("locals");
    EXPECT_EQ(data.at("volumes").at(kDb + "/os").at("backing_store").at("path"), site.at("image").at("path"));
    EXPECT_EQ(data.at("domains").at(kDb).at("devices").at("disks").at(0).at("serial"), "os");
    const auto host = Json::parse(files.at("ansible/host_vars/" + kDb + ".json"));
    EXPECT_EQ(host.at("mounts").at(0).at("src"), "/dev/disk/by-id/virtio-data");
    EXPECT_EQ(host.at("mounts").at(0).at("path"), "/srv/data");
    EXPECT_EQ(host.at("access").at("admin").at("groups"), Json::array({"sudo"}));
    EXPECT_EQ(host.at("package_files").at(site.at("image").at("apt_config").at("path").get<std::string>()),
              site.at("image").at("apt_config").at("content"));
    EXPECT_EQ(Json::parse(files.at("ansible/image.json")).at("image"), site.at("image"));
}

TEST(Export, PublishesBackupCommandsAndCredentialPolicyWithoutChangingInfrastructureResources) {
    auto definitions = Definitions();
    auto& host = definitions.site.hypervisor_settings;
    host = HypervisorJson();
    host["controller_home"] = "/home/operator";
    host["controller_project_directory"] = "/home/operator/project";
    definitions.site.hypervisor_alias = host.at("hypervisor_alias");
    const auto before = BuildTopology(definitions);
    const auto unchanged = GenerateConfiguration(before, AllocateAddressPlan(before));
    definitions.site.backup = BackupDefinition{"hd", "backups/project",
        "environment/.tools/anaconda/envs/project/bin", "environment/.tools/backup/cache",
        "environment/.tools/backup/restore", 64, {{"daily", 7}},
        {{"project", BackupSetKind::Files, {"."}, {"environment/.tools"}}}};
    definitions.site.backup->capture = BackupCaptureDefinition{"/var/tmp", "environment/.tools/backup-capture", 32};
    definitions.site.services["controller"] = {{"project_directory", "/Users/operator/project"}};
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    ASSERT_TRUE(files.contains("ansible/backup.json"));
    const auto backup = Json::parse(files.at("ansible/backup.json")).at("backup");
    EXPECT_EQ(backup.at("repository"), "/srv/example-vms/hd/backups/project");
    EXPECT_EQ(backup.at("sets").at("project").at("argv").back(), "/Users/operator/project");
    EXPECT_EQ(backup.at("repository_url"), "sftp:" + definitions.site.hypervisor_alias + ":/srv/example-vms/hd/backups/project");
    EXPECT_FALSE(backup.contains("schedule"));
    EXPECT_EQ(backup.at("hypervisor"), Json::parse(files.at("ansible/hypervisor.json")).at("controller"));
    EXPECT_EQ(Json::parse(files.at("ansible/deployment.json")).at("backup_password_length"), 64);
    for (const auto& [path, content] : unchanged)
        if (path.string().starts_with("tofu/")) EXPECT_EQ(files.at(path), content) << path;
}

TEST(Export, RecordsResetIdentitiesFromTheSameVmStoragePlanAsNativeTofu) {
    auto definitions = Definitions();
    definitions.site.storage_root = "/srv/${literal}/volumes";
    definitions.site.image_path = "/srv/cache/debian.qcow2";
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    const auto scope = Json::parse(files.at("tofu/reset-scope.json"));
    const auto& resources = scope.at("resources");
    EXPECT_EQ(resources.size(), 22);
    const auto vm = "module.vm.libvirt_domain.domains[" + Json(kDb).dump() + "]";
    EXPECT_EQ(resources.at(vm).at("/name"), "vm-" + kDb);
    const auto pool = "module.vm.libvirt_pool.storage[" + Json(kDb + "/nvme").dump() + "]";
    EXPECT_EQ(resources.at(pool).at("/target/path"), "/srv/${literal}/volumes/nvme/volumes/" + kDb);
    const auto volume = "module.vm.libvirt_volume.volumes[" + Json(kDb + "/os").dump() + "]";
    EXPECT_EQ(resources.at(volume).at("/path"), "/srv/${literal}/volumes/nvme/volumes/" + kDb + "/os.qcow2");
}

TEST(Export, ReportsConfiguredResourcesAndTheInitialPowerBudgetWithoutChangingVmOutputs) {
    auto definitions = Definitions();
    definitions.site.services["capacity"] = {
        {"hypervisor_memory_kib", 12 * 1024 * 1024}, {"active_vm_limit_mib", 10240}};
    definitions.vms.at(kApi).ram_mib = 8192;
    definitions.vms.at(kApi).domain["running"] = false;
    definitions.vms.at(kDb).disks.push_back({"data", "ssd", 64});
    const auto topology = BuildTopology(definitions);
    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    const auto report = Json::parse(files.at("reports/resources.json"));
    const auto software = Json::parse(files.at("reports/software.json"));
    EXPECT_EQ(software.at("guests").size(), 4);
    EXPECT_TRUE(software.at("hypervisor").is_null());
    EXPECT_TRUE(software.at("application_report").is_null());

    EXPECT_EQ(report.at("power_basis"), "source_initial_running");
    const auto& configured = report.at("totals").at("configured");
    EXPECT_EQ(configured.at("vm_count"), 4);
    EXPECT_EQ(configured.at("vcpu"), 8);
    EXPECT_EQ(configured.at("ram_mib"), 14336);
    EXPECT_EQ(configured.at("disk_gib_by_storage"), Json({{"nvme", 128}, {"ssd", 64}}));
    EXPECT_EQ(report.at("totals").at("initially_running").at("ram_mib"), 6144);
    EXPECT_EQ(report.at("memory").at("hypervisor_reserve_kib"), 2 * 1024 * 1024);
    EXPECT_EQ(report.at("memory").at("vm_headroom_mib"), 4096);
    const auto& stopped = report.at("vms").at(kApi);
    EXPECT_FALSE(stopped.at("initially_running").get<bool>());
    EXPECT_FALSE(stopped.at("startup_memory").at("within_budget").get<bool>());
    EXPECT_EQ(stopped.at("startup_memory").at("shortfall_mib"), 4096);
    const auto domains = Json::parse(files.at("tofu/modules/vm/data.tf.json")).at("locals").at("domains");
    EXPECT_FALSE(domains.at(kApi).at("running").get<bool>());
    EXPECT_EQ(domains.at(kApi).at("memory"), 8192);
}

TEST(Export, PreparesEveryRequiredRouterForRuntimeAvailabilityWithoutDependingOnInitialPower) {
    auto definitions = Definitions();
    definitions.vms.at(kGate).domain["running"] = false;
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    const auto dependencies = Json::parse(files.at("ansible/deployment.json")).at("vm_dependencies");
    EXPECT_EQ(dependencies.at(kEdge), Json::array({kEdge}));
    EXPECT_EQ(dependencies.at(kGate), Json::array({kEdge, kGate}));
    EXPECT_EQ(dependencies.at(kDb), Json::array({kEdge, kDb}));
    EXPECT_EQ(dependencies.at(kApi), Json::array({kEdge, kGate, kApi}));
}

TEST(Export, SerializesThePreparedHostSshPolicyWithoutPrivateKeyContents) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings = HypervisorJson();
    definitions.site.hypervisor_settings["hypervisor_sshd"] = SshPolicyJson();
    definitions.site.hypervisor_settings["hypervisor_sshd"]["private_key"] = "must-not-be-exported";
    definitions.site.hypervisor_settings["hypervisor_sshd"]["backup"]["content"] = "must-not-be-exported";
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto files = GenerateConfiguration(topology, plan.addresses);

    const auto exported = Json::parse(files.at("ansible/sshd.json")).at("sshd");
    EXPECT_EQ(exported, plan.hypervisor.at("sshd"));
    EXPECT_FALSE(exported.contains("private_key"));
    EXPECT_FALSE(exported.at("backup").contains("content"));
    EXPECT_FALSE(exported.at("backup").at("files").at(0).contains("content"));
}

TEST(Export, DerivesBothControllerInventoryGroupsFromTheLocalHostSettings) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings = HypervisorJson();
    definitions.site.hypervisor_settings["hypervisor_bootstrap_alias"] = "bootstrap-test";
    definitions.site.hypervisor_settings["controller_user"] = "operator";
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    const auto groups = Json::parse(files.at("ansible/inventory.json")).at("all").at("children");
    const auto host = groups.at("hypervisors").at("hosts").at("bootstrap-test");
    EXPECT_EQ(host.at("ansible_connection"), "ssh");
    EXPECT_EQ(host.at("ansible_user"), "operator");
    EXPECT_EQ(host.at("ansible_python_interpreter"), definitions.site.hypervisor_settings.at("ansible_python_interpreter"));
    EXPECT_TRUE(groups.at("libvirt").at("hosts").contains(definitions.site.hypervisor_alias));
    EXPECT_EQ(groups.at("libvirt").at("hosts").at(definitions.site.hypervisor_alias).at("ansible_connection"), "ssh");
}

TEST(Export, ProducesTwoNativeTofuModulesAndConsistentGuestFiles) {
    auto definitions = Definitions();
    definitions.vms.at(kEdge).type = "postgres";
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);

    const auto files = GenerateConfiguration(topology, addresses);

    const auto root = Json::parse(files.at("tofu/main.tf.json"));
    EXPECT_EQ(root.at("module").size(), 2);
    EXPECT_FALSE(root.contains("resource"));
    const auto inventory = Json::parse(files.at("ansible/inventory.json")).at("all").at("hosts");
    EXPECT_EQ(inventory.at(kDb).at("ansible_connection"), "ssh");
    EXPECT_EQ(inventory.at(kDb).at("ansible_python_interpreter"), "/usr/bin/python3");
    const auto vm = Json::parse(files.at("tofu/modules/vm/data.tf.json")).at("locals");
    EXPECT_EQ(vm.at("domains").at(kDb).at("memory"), 2048);
    EXPECT_EQ(vm.at("volumes").at(kDb + "/os").at("capacity"), 34359738368ULL);
    EXPECT_FALSE(vm.at("volumes").at(kDb + "/seed.iso").contains("target"));
    const auto& filter = vm.at("domains").at(kDb).at("devices").at("interfaces").at(0).at("filter_ref");
    EXPECT_EQ(filter.at("filter"), topology.definitions.site.defaults.at("nic").at("filter_ref").at("filter"));
    EXPECT_EQ(filter.at("parameters"), Json::array({{{"name", "IP"},
        {"value", FormatIpv4(addresses.vms.at(kDb).at("lan").address.address)}}}));
    const auto host = Json::parse(files.at("ansible/host_vars/" + kApi + ".json"));
    EXPECT_EQ(host.at("fqdn"), topology.vm_domains.at(kApi));
    const auto netplan = Json::parse(host.at("network").at("files").at("/etc/netplan/60-infrastructure.yaml").get<std::string>());
    EXPECT_EQ(netplan.at("network").at("ethernets").at("lan0").at("match").at("macaddress"),
              addresses.vms.at(kApi).at("lan").mac);
    EXPECT_NE(files.at("graphviz/topology.dot").find(kChild), std::string::npos);
    EXPECT_NE(files.at("graphviz/topology.svg").find("<svg"), std::string::npos);
    const auto router = Json::parse(files.at("ansible/host_vars/" + kEdge + ".json"));
    EXPECT_EQ(router.at("packages"), Json::array({"postgresql", "nftables", "coredns"}));
}

TEST(Export, HandsRuntimePowerToTheOperatorWhileKeepingInitialPowerAndVmConfiguration) {
    auto definitions = Definitions();
    definitions.site.defaults["domain_lifecycle"] = {{"ignore_changes", {"running"}}};
    definitions.vms.at(kApi).domain["running"] = false;
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    const auto module = Json::parse(files.at("tofu/modules/vm/main.tf.json"));
    const auto& resource = module.at("resource").at("libvirt_domain").at("domains");
    EXPECT_EQ(resource.at("lifecycle"), definitions.site.defaults.at("domain_lifecycle"));
    EXPECT_TRUE(resource.contains("memory"));
    EXPECT_TRUE(resource.contains("autostart"));
    const auto data = Json::parse(files.at("tofu/modules/vm/data.tf.json")).at("locals");
    EXPECT_EQ(data.at("domains").at(kApi).at("running"), false);
    EXPECT_EQ(data.at("domains").at(kDb).at("running"), true);
    EXPECT_NE(module.at("output").at("vms").at("value").get<std::string>().find("running = vm.running"),
              std::string::npos);
}

TEST(Export, DoesNotIgnorePowerWithoutAnExplicitLifecyclePolicy) {
    const auto definitions = Definitions();
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    const auto module = Json::parse(files.at("tofu/modules/vm/main.tf.json"));
    EXPECT_FALSE(module.at("resource").at("libvirt_domain").at("domains").contains("lifecycle"));
    EXPECT_FALSE(module.at("resource").at("libvirt_volume").at("volumes").contains("lifecycle"));
}

TEST(Export, KeepsExistingBootstrapNetworksWhileRetainingCurrentGuestNetworkPlans) {
    auto definitions = Definitions();
    definitions.site.defaults["cloud_init_lifecycle"] = {{"ignore_changes", {"network_config"}}};
    definitions.site.defaults["volume_lifecycle"] = {{"ignore_changes", {"create"}}};
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);

    const auto files = GenerateConfiguration(topology, addresses);

    const auto module = Json::parse(files.at("tofu/modules/vm/main.tf.json"));
    const auto& seed = module.at("resource").at("libvirt_cloudinit_disk").at("seeds");
    EXPECT_EQ(seed.at("lifecycle"), definitions.site.defaults.at("cloud_init_lifecycle"));
    EXPECT_TRUE(seed.contains("user_data"));
    const auto& volumes = module.at("resource").at("libvirt_volume");
    EXPECT_EQ(volumes.at("volumes").at("lifecycle"), definitions.site.defaults.at("volume_lifecycle"));
    EXPECT_FALSE(volumes.at("image").contains("lifecycle"));
    const auto host = Json::parse(files.at("ansible/host_vars/" + kEdge + ".json"));
    EXPECT_EQ(host.at("address"), FormatIpv4(addresses.vms.at(kEdge).at("lan").address.address));
    EXPECT_TRUE(host.at("network").at("files").contains("/etc/netplan/60-infrastructure.yaml"));
}

TEST(Export, IsIndependentOfFileLayoutAndUnorderedInputOrder) {
    TemporaryDirectory first;
    TemporaryDirectory second;
    WriteSources(first.path);
    WriteSources(second.path);
    auto disk_order = VmJson(kDb, "db", "postgres");
    disk_order["disks"].push_back({{"name", "data"}, {"storage", "ssd"}, {"size_gib", 17}});
    WriteJson(first.path / "machines/db.vm.json", disk_order);
    std::reverse(disk_order["disks"].begin(), disk_order["disks"].end());
    WriteJson(second.path / "machines/db.vm.json", disk_order);
    std::filesystem::rename(second.path / "machines", second.path / "renamed");
    auto root = Json::parse(std::ifstream(second.path / "root.network.json"));
    std::reverse(root["members"].begin(), root["members"].end());
    WriteJson(second.path / "root.network.json", root);

    EXPECT_EQ(Compile(first.path), Compile(second.path));
}

TEST(Export, KeepsSourceStringsLiteralInTerraformJson) {
    auto definitions = Definitions();
    definitions.vms.at(kDb).domain["description"] = "${literal} %{literal}";
    definitions.site.defaults["nic"]["filter_ref"]["parameters"] = {{{"name", "CUSTOM"}, {"value", "${literal}"}}};
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));
    const auto data = Json::parse(files.at("tofu/modules/vm/data.tf.json"));

    EXPECT_EQ(data.at("locals").at("domains").at(kDb).at("description"), "$${literal} %%{literal}");
    const auto& parameters = data.at("locals").at("domains").at(kDb).at("devices").at("interfaces").at(0).at("filter_ref").at("parameters");
    EXPECT_EQ(parameters.at(0).at("value"), "$${literal}");
    EXPECT_EQ(parameters.at(1).at("name"), "IP");
}

TEST(Export, IncludesConfiguredHumanUsersOnEveryGuestWithoutEmbeddingPasswords) {
    const auto definitions = Definitions();
    const auto expected = definitions.site.services.at("ssh").at("users");
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    for (const auto& [id, vm] : definitions.vms) {
        const auto host = Json::parse(files.at("ansible/host_vars/" + id + ".json"));
        EXPECT_EQ(host.at("users"), expected);
        EXPECT_NE(host.at("users").at(0).at("password").get<std::string>().find("{{"), std::string::npos);
        EXPECT_NE(host.at("access").at("admin_user"), expected.at(0).at("name"));
    }
}

TEST(Export, AddsNftablesToEveryGuestAndMatchingUfwRulesToDemoGuests) {
    auto definitions = Definitions();
    definitions.vms.at(kApi).type = "demo";
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);

    const auto files = GenerateConfiguration(topology, addresses);

    for (const auto& [id, vm] : definitions.vms) {
        const auto host = Json::parse(files.at("ansible/host_vars/" + id + ".json"));
        EXPECT_TRUE(host.at("network").at("files").contains("/etc/nftables.conf"));
        if (id == kApi) {
            EXPECT_EQ(host.at("network").at("ufw").at("state"), "enabled");
            const auto& rules = host.at("network").at("ufw").at("rules");
            EXPECT_TRUE(std::any_of(rules.begin(), rules.end(), [&](const auto& rule) {
                return rule.at("direction") == "out" && rule.value("port", "") == "5432" &&
                    rule.at("to_ip") == FormatIpv4(addresses.vms.at(kDb).at("lan").address.address);
            }));
        } else EXPECT_FALSE(host.contains("ufw"));
    }
}

TEST(Export, BootstrapsRouterDnsBeforeSwitchingToItsOwnCoreDns) {
    const auto definitions = Definitions();
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));
    const auto data = Json::parse(files.at("tofu/modules/vm/data.tf.json"));
    const auto initial = Json::parse(data.at("locals").at("seeds").at(kEdge).at("network_config").get<std::string>());
    const auto host = Json::parse(files.at("ansible/host_vars/" + kEdge + ".json"));
    const auto final = Json::parse(host.at("network").at("files").at("/etc/netplan/60-infrastructure.yaml").get<std::string>());

    EXPECT_EQ(initial.at("ethernets").at("wan0").at("nameservers").at("addresses"), definitions.site.dns_upstreams);
    EXPECT_FALSE(initial.at("ethernets").at("lan0").contains("nameservers"));
    EXPECT_EQ(Json::parse(host.at("bootstrap_netplan").get<std::string>()).at("network"), initial);

    const auto child = Json::parse(data.at("locals").at("seeds").at(kGate).at("network_config").get<std::string>());
    const auto parent_address = FormatIpv4(AllocateAddressPlan(topology).vms.at(kEdge).at("lan").address.address);
    EXPECT_EQ(child.at("ethernets").at("wan0").at("nameservers").at("addresses"), Json::array({parent_address}));
    EXPECT_FALSE(child.at("ethernets").at("lan0").contains("nameservers"));
    EXPECT_EQ(final.at("network").at("ethernets").at("lan0").at("nameservers").at("addresses"),
              Json::array({definitions.site.services.at("dns_resolver").at("local_address")}));
}

TEST(Publication, PreservesTofuStateAndRemovesObsoleteGeneratedFiles) {
    TemporaryDirectory directory;
    const auto output = directory.path / "generated";
    WriteGeneratedFiles({{"old.json", "old"}, {"tofu/main.tf.json", "{}"}}, output);
    WriteJson(output / "tofu/terraform.tfstate", {{"serial", 7}});

    WriteGeneratedFiles({{"new.json", "new"}, {"tofu/main.tf.json", "{}"}}, output);

    EXPECT_FALSE(std::filesystem::exists(output / "old.json"));
    EXPECT_TRUE(std::filesystem::exists(output / "new.json"));
    EXPECT_EQ(Json::parse(std::ifstream(output / "tofu/terraform.tfstate")).at("serial"), 7);
}

TEST(Publication, KeepsThePreviousResultWhenTheNewModelIsInvalid) {
    TemporaryDirectory directory;
    const auto source = directory.path / "source";
    const auto output = directory.path / "generated";
    WriteSources(source);
    RunGenerationPipeline(source, output);
    const auto before = Json::parse(std::ifstream(output / "manifest.json"));
    WriteJson(source / "site.json", Json::object());

    EXPECT_THROW(RunGenerationPipeline(source, output), ConfigurationError);

    EXPECT_EQ(Json::parse(std::ifstream(output / "manifest.json")), before);
    EXPECT_TRUE(std::filesystem::exists(output / "graphviz/topology.svg"));
}

TEST(Publication, DoesNotFollowAnOutputDirectorySymlinkOutsideTheResult) {
    TemporaryDirectory directory;
    const auto output = directory.path / "generated";
    const auto outside = directory.path / "outside";
    WriteGeneratedFiles({{"assets/data", "original"}}, output);
    std::filesystem::rename(output / "assets", outside);
    std::filesystem::create_directory_symlink(outside, output / "assets");

    EXPECT_THROW(WriteGeneratedFiles({{"assets/data", "replacement"}}, output), ConfigurationError);

    std::ifstream stream(outside / "data");
    std::string content;
    stream >> content;
    EXPECT_EQ(content, "original");
}

}  // namespace skeleton::test
