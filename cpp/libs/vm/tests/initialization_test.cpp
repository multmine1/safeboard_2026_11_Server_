#include <gtest/gtest.h>
#include "skeleton/vm/initialization.hpp"

namespace skeleton::test {

TEST(Vm, PreparesCloudInitWithoutAnExporterDependency) {
    const VmDefinition vm{"vm-id", "db", "database", 2, 2048, {}, Json::object(), {}};
    const VmInitializationSettings settings{"db.example.test", "root", "ssh-ed25519 public",
        {{"lock_passwd", true}}, {{"disable_root", false}, {"ssh_pwauth", false}}};
    const Json network{{"version", 2}, {"ethernets", Json::object()}};
    const Json files{{"/etc/ssh/access.conf", "PermitRootLogin prohibit-password\n"}};

    const auto plan = BuildVmInitialization(vm, settings, network, files);

    EXPECT_EQ(plan.metadata.at("instance-id"), vm.id);
    EXPECT_EQ(plan.user_data.at("fqdn"), settings.fqdn);
    EXPECT_EQ(plan.user_data.at("users").at(0).at("name"), settings.bootstrap_user);
    EXPECT_EQ(plan.user_data.at("users").at(0).at("ssh_authorized_keys").at(0), settings.bootstrap_public_key);
    EXPECT_EQ(plan.user_data.at("write_files").at(0).at("path"), "/etc/ssh/access.conf");
    EXPECT_EQ(plan.network, network);
}

}  // namespace skeleton::test
