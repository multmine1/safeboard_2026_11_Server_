#include <gtest/gtest.h>

#include "skeleton/vm/storage.hpp"

namespace skeleton::test {

TEST(VmStorage, SharesStableFilesDirectoriesAndLinksAcrossStorageClasses) {
    const VmDefinition vm{"id", "db", "database", 2, 4096,
        {{"os", "nvme", 16}, {"fast", "nvme", 32}, {"data", "ssd", 64}}};

    const auto plan = BuildVmStoragePlan(vm, {"/srv/pools", "/home/operator", "qcow2"});

    EXPECT_EQ(plan.directories.size(), 2);
    EXPECT_EQ(plan.files.at("os"), "/srv/pools/nvme/volumes/id/os.qcow2");
    EXPECT_EQ(plan.files.at("fast").parent_path(), plan.directories.at("nvme"));
    EXPECT_EQ(plan.files.at("data"), "/srv/pools/ssd/volumes/id/data.qcow2");
    EXPECT_EQ(plan.seed, "/srv/pools/nvme/volumes/id/seed.iso");
    EXPECT_EQ(plan.links.at("ssd"), "/home/operator/VMs/ssd/id");
}

TEST(VmStorage, RejectsEscapingComponentsAndMissingOsStorage) {
    for (const auto invalid : {"id", "storage", "format", "missing_os"}) {
        auto vm = VmDefinition{"id", "db", "database", 2, 4096, {{"os", "nvme", 16}}};
        VmStorageSettings settings{"/srv/pools", "/home/operator", "qcow2"};
        if (std::string(invalid) == "id") vm.id = "../escape";
        if (std::string(invalid) == "storage") vm.disks.front().storage = "../escape";
        if (std::string(invalid) == "format") settings.volume_format = "../escape";
        if (std::string(invalid) == "missing_os") vm.disks.clear();

        EXPECT_THROW(BuildVmStoragePlan(vm, settings), ConfigurationError);
    }
}

}  // namespace skeleton::test
