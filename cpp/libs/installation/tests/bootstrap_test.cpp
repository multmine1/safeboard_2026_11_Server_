#include <gtest/gtest.h>

#include "skeleton/installation/bootstrap.hpp"

namespace skeleton::test {
namespace {

HostDiskPlan Disks() {
    return {"gpt", {{"fast", "/dev/disk/by-id/virtio-system", "system", 16ULL << 30, {
        {1, 1, 513, "vfat", "/boot/efi", std::nullopt, true},
        {2, 513, 16383, "ext4", "/", std::nullopt, false}}}}};
}

Json BootstrapSettings() {
    return {{"payload_directory", "/skeleton"}, {"ram_directory", "/run/installer"}, {"previous_root_filesystem", "ext4"},
        {"authorized_keys_mode", "0600"}, {"root_home", "/root"},
        {"sshd", {{"path", "/etc/ssh/sshd_config.d/00-installer.conf"}, {"mode", "0644"},
            {"options", {{"PermitRootLogin", "prohibit-password"}, {"PasswordAuthentication", "no"}, {"KbdInteractiveAuthentication", "no"}}}}},
        {"ifupdown", {{"path", "/etc/network/interfaces"}, {"mode", "0644"}, {"content", "auto lo\niface lo inet loopback\n"}}},
        {"services", {{"ssh", "enable"}, {"NetworkManager", "enable"}, {"networking", "disable"}}}};
}

InstallerBootstrapContext Context() {
    return {"operator", "/home/operator", {"ssh-ed25519 AAAAHUMAN"},
        {{"/etc/ssh/ssh_host_ed25519_key", "0600"}, {"/etc/ssh/ssh_host_ed25519_key.pub", "0644"}},
        {{"/etc/netplan/stand.yaml", "0600", "{\"network\":{\"version\":2}}\n"}},
        "#!/bin/sh\nexit 0\n", "#!/bin/sh\nexit 0\n"};
}

}  // namespace

TEST(InstallerBootstrap, PreparesPublicFilesAndOneLineCallbacksWithRamOnlyHostKeyRetention) {
    const auto plan = BuildInstallerBootstrapPlan(Disks(), BootstrapSettings(),
        "/dev/disk/by-uuid/10000000-0000-4000-8000-000000000001", Context());
    EXPECT_EQ(plan.payload_directory, "/skeleton");
    EXPECT_EQ(plan.overlay.at("authorized_keys"), "ssh-ed25519 AAAAHUMAN\n");
    EXPECT_EQ(plan.overlay.at("previous_root_filesystem"), "ext4\n");
    EXPECT_EQ(plan.overlay.at("hostkeys"), "0600 /etc/ssh/ssh_host_ed25519_key\n0644 /etc/ssh/ssh_host_ed25519_key.pub\n");
    EXPECT_NE(plan.early_command.find("/run/installer"), std::string::npos);
    EXPECT_NE(plan.early_command.find("/dev/disk/by-id/virtio-system"), std::string::npos);
    EXPECT_NE(plan.late_command.find("/home/operator"), std::string::npos);
    EXPECT_EQ(plan.early_command.find_first_of("\r\n\\"), std::string::npos);
    EXPECT_EQ(plan.late_command.find_first_of("\r\n\\"), std::string::npos);
    EXPECT_NE(plan.overlay.at("files").find("0600 /home/operator/.ssh/authorized_keys authorized_keys"), std::string::npos);
    EXPECT_NE(plan.overlay.at("files").find("0600 /root/.ssh/authorized_keys authorized_keys"), std::string::npos);
    EXPECT_NE(plan.overlay.at("services").find("disable networking"), std::string::npos);
    EXPECT_EQ(plan.overlay.at("file-0"), Context().files.front().content);
    EXPECT_EQ(plan.overlay.at("retain.sh"), Context().retain_script);
}

TEST(InstallerBootstrap, RejectsUnsafePathsPrivatePayloadAndConflictingTargetFiles) {
    for (const auto fault : {"ram", "old_root", "user", "key", "host_key", "mode", "duplicate", "multiline_option", "missing_keys", "root_auth", "private_file", "overlap", "scripts", "filesystem"}) {
        SCOPED_TRACE(fault);
        auto settings = BootstrapSettings();
        auto context = Context();
        std::string previous = "/dev/disk/by-uuid/10000000-0000-4000-8000-000000000001";
        if (std::string(fault) == "ram") settings["ram_directory"] = "/srv/retained";
        if (std::string(fault) == "old_root") previous = "/dev/sda2";
        if (std::string(fault) == "user") context.user = "operator;reboot";
        if (std::string(fault) == "key") context.authorized_keys = {"-----BEGIN OPENSSH PRIVATE KEY-----"};
        if (std::string(fault) == "host_key") context.host_keys.front().path = "/etc/ssh/../../shadow";
        if (std::string(fault) == "mode") context.files.front().mode = "0666";
        if (std::string(fault) == "duplicate") context.files.push_back(context.files.front());
        if (std::string(fault) == "multiline_option") settings["sshd"]["options"]["PermitRootLogin"] = "no\nPasswordAuthentication yes";
        if (std::string(fault) == "missing_keys") context.host_keys.clear();
        if (std::string(fault) == "root_auth") settings["sshd"]["options"]["PasswordAuthentication"] = "yes";
        if (std::string(fault) == "private_file") context.files.front().content = "-----BEGIN OPENSSH PRIVATE KEY-----";
        if (std::string(fault) == "overlap") settings["payload_directory"] = settings.at("ram_directory");
        if (std::string(fault) == "scripts") context.finish_script.clear();
        if (std::string(fault) == "filesystem") settings["previous_root_filesystem"] = "xfs";
        EXPECT_THROW(BuildInstallerBootstrapPlan(Disks(), settings, previous, context), ConfigurationError);
    }
}

}  // namespace skeleton::test
