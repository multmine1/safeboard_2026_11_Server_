#include <gtest/gtest.h>

#include "hypervisor_fixture.hpp"
#include "skeleton/pipeline/generator.hpp"

namespace skeleton::test {
namespace {

void EnableHostInstallation(SourceDefinitions& definitions) {
    auto& site = definitions.site;
    site.asset_data["host_installation"] = {{"partition_table", "gpt"}, {"alignment_mib", 1}, {"storage", {
        {"nvme", {{{"filesystem", "vfat"}, {"mountpoint", "/boot/efi"}, {"size_mib", 512}},
                  {{"filesystem", "ext4"}, {"mountpoint", "/"}, {"size_mib", "remaining"}, {"minimum_mib", 4096}}}},
        {"ssd", {{{"filesystem", "ext4"}, {"size_mib", "remaining"}, {"minimum_mib", 1}}}},
        {"hd", {{{"filesystem", "ext4"}, {"size_mib", "remaining"}, {"minimum_mib", 1}}}}
    }}};
    auto& identities = site.hypervisor_settings["hypervisor_installation"]["disks"];
    for (const auto& name : {"nvme", "ssd", "hd"}) {
        identities[name] = {{"device", "/dev/disk/by-id/virtio-" + std::string(name)}, {"serial", name}, {"size_bytes", 16ULL << 30}};
        if (std::string(name) == "nvme") continue;
        site.hypervisor_settings["generator"]["storage_classes"][name] = {
            {"fstype", "ext4"}, {"uuid", std::string(name) == "ssd" ?
                "10000000-0000-4000-8000-000000000001" : "10000000-0000-4000-8000-000000000002"}};
    }
}

}  // namespace

TEST(HostInstallation, ExportsASeparatePreparedDiskPlanWithoutChangingInfrastructure) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings = HypervisorJson();
    const auto topology = BuildTopology(definitions);
    const auto before = GenerateConfiguration(topology, AllocateAddressPlan(topology));
    EXPECT_FALSE(before.contains("ansible/hypervisor-installation.json"));
    EnableHostInstallation(definitions);
    auto& site = definitions.site;
    const auto& identities = site.hypervisor_settings.at("hypervisor_installation").at("disks");
    const auto with_installation = BuildTopology(definitions);

    const auto files = GenerateConfiguration(with_installation, AllocateAddressPlan(with_installation));

    const auto disks = Json::parse(files.at("ansible/hypervisor-installation.json")).at("installation").at("disks");
    ASSERT_EQ(disks.size(), 3);
    EXPECT_EQ(disks.at(1).at("partitions").at(0).at("part_end"), "513MiB");
    EXPECT_EQ(disks.at(1).at("device"), identities.at("nvme").at("device"));
    EXPECT_FALSE(Json::parse(files.at("ansible/hypervisor-bootstrap.json")).contains("hypervisor_installation"));
    for (const auto& [name, contents] : before)
        if (name.string().starts_with("tofu/")) EXPECT_EQ(files.at(name), contents) << name;
    site.asset_data.erase("host_installation");
    const auto incomplete = BuildTopology(definitions);
    EXPECT_THROW(GenerateConfiguration(incomplete, AllocateAddressPlan(incomplete)), ConfigurationError);
}


TEST(HostInstallation, ExportsThePreparedFirstBootNetworkAndOnlyPublicInstallerAssets) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings = HypervisorJson();
    EnableHostInstallation(definitions);
    auto& host = definitions.site.hypervisor_settings;
    host["hypervisor_sshd"] = SshPolicyJson();
    host["controller_home"] = "/home/operator";
    host["hypervisor_installation"]["previous_root"] = "/dev/disk/by-uuid/10000000-0000-4000-8000-000000000099";
    definitions.site.asset_data["host_installation"]["bootstrap"] = {
        {"payload_directory", "/skeleton"}, {"ram_directory", "/run/installer"}, {"previous_root_filesystem", "ext4"}, {"authorized_keys_mode", "0600"}, {"root_home", "/root"},
        {"sshd", {{"path", "/etc/ssh/sshd_config.d/00-installer.conf"}, {"mode", "0644"},
            {"options", {{"PermitRootLogin", "prohibit-password"}, {"PasswordAuthentication", "no"}, {"KbdInteractiveAuthentication", "no"}}}}},
        {"ifupdown", {{"path", "/etc/network/interfaces"}, {"mode", "0644"}, {"content", "auto lo\niface lo inet loopback\n"}}},
        {"services", {{"ssh", "enable"}, {"NetworkManager", "enable"}, {"networking", "disable"}}}};
    definitions.site.asset_data["installer_retain"] = "#!/bin/sh\nexit 0\n";
    definitions.site.asset_data["installer_finish"] = "#!/bin/sh\nexit 0\n";
    definitions.site.asset_data["host_installer"] = {{"preseed", {{"d-i", {
        {"passwd/user-password-crypted", {{"type", "password"}, {"value", "!"}}},
        {"pkgsel/include", {{"type", "string"}, {"value", "python3"}}}
    }}}}};
    host["hypervisor_installation"]["firmware"] = {"stand-nic"};
    definitions.site.asset_data["host_installer"]["firmware_catalog"]["stand-nic"] = {
        {"package", "firmware-stand"}, {"version", "1"}, {"filename", "firmware-stand_1_all.deb"},
        {"url", "https://example.invalid/firmware-stand_1_all.deb"}, {"checksum", "sha256:" + std::string(64, 'a')},
        {"source_directory", "usr/lib/firmware"}, {"destination_directory", "usr/lib/firmware"},
        {"private_metadata", "must not be exported"}};
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    const auto bootstrap = Json::parse(files.at("ansible/installer/bootstrap.json")).at("installer_bootstrap");
    EXPECT_NE(bootstrap.at("early_command").get<std::string>().find("by-uuid"), std::string::npos);
    const auto host_plan = Json::parse(files.at("ansible/hypervisor-bootstrap.json"));
    EXPECT_EQ(Json::parse(files.at("ansible/installer/overlay/skeleton/file-0")), host_plan.at("hypervisor_netplan"));
    EXPECT_EQ(files.at("ansible/installer/overlay/skeleton/retain.sh"), definitions.site.asset_data.at("installer_retain").get<std::string>());
    const auto keys = files.at("ansible/installer/overlay/skeleton/authorized_keys");
    EXPECT_NE(keys.find("fixture-human-key"), std::string::npos);
    EXPECT_EQ(keys.find("PRIVATE KEY"), std::string::npos);
    const auto& manifest = files.at("ansible/installer/overlay/skeleton/files");
    EXPECT_NE(manifest.find(host_plan.at("hypervisor_network").at("path").get<std::string>()), std::string::npos);
    EXPECT_EQ(manifest.find("ssh_host_ed25519_key"), std::string::npos);
    EXPECT_NE(files.at("ansible/installer/overlay/skeleton/hostkeys").find("ssh_host_ed25519_key"), std::string::npos);
    const auto& preseed = files.at("ansible/installer/overlay/preseed.cfg");
    EXPECT_NE(preseed.find("netcfg/get_ipaddress string 192.0.2.2"), std::string::npos);
    EXPECT_NE(preseed.find("netcfg/get_netmask string 255.255.255.0"), std::string::npos);
    EXPECT_NE(preseed.find("passwd/username string operator"), std::string::npos);
    EXPECT_NE(preseed.find("partman/early_command string '/bin/sh'"), std::string::npos);
    EXPECT_EQ(preseed.find("\\n"), std::string::npos);
    EXPECT_NE(preseed.find("pkgsel/include string python3 firmware-stand=1"), std::string::npos);
    EXPECT_EQ(bootstrap.at("firmware_artifacts").at(0).at("name"), "stand-nic");
    EXPECT_FALSE(bootstrap.at("firmware_artifacts").at(0).contains("private_metadata"));
    for (const auto firmware : {Json::array({"missing"}), Json::array({"stand-nic", "stand-nic"})}) {
        host["hypervisor_installation"]["firmware"] = firmware;
        const auto invalid = BuildTopology(definitions);
        EXPECT_THROW(GenerateConfiguration(invalid, AllocateAddressPlan(invalid)), ConfigurationError);
    }
}

TEST(HostInstallation, ConvertsTheAlignedEfiEndToDecimalPartmanUnits) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings = HypervisorJson();
    EnableHostInstallation(definitions);
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    const auto& recipe = files.at("ansible/installer/partitioning.preseed");
    EXPECT_NE(recipe.find("538 538 538 free"), std::string::npos);
    EXPECT_NE(recipe.find("4295 4295 -1 ext4"), std::string::npos);
    EXPECT_EQ(recipe.find("/dev/sda"), std::string::npos);
}

}  // namespace skeleton::test
