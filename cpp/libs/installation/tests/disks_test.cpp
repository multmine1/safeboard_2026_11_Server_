#include <gtest/gtest.h>

#include "skeleton/installation/disks.hpp"

namespace skeleton::test {
namespace {

Json Layout() {
    return {{"partition_table", "gpt"}, {"alignment_mib", 1}, {"storage", {
        {"fast", {{{"filesystem", "vfat"}, {"mountpoint", "/boot/efi"}, {"size_mib", 512}},
                  {{"filesystem", "ext4"}, {"mountpoint", "/"}, {"size_mib", "remaining"}, {"minimum_mib", 4096}}}},
        {"bulk", {{{"filesystem", "ext4"}, {"size_mib", "remaining"}, {"minimum_mib", 1}}}}
    }}};
}

Json Identities() {
    return {{"fast", {{"device", "/dev/disk/by-id/nvme-test"}, {"serial", "NVME1"}, {"size_bytes", 16ULL << 30}}},
        {"bulk", {{"device", "/dev/disk/by-id/ata-test"}, {"serial", "SSD1"}, {"size_bytes", 8ULL << 30}}}};
}

Json Storage() {
    return {{"fast", Json::object()}, {"bulk", {{"uuid", "10000000-0000-4000-8000-000000000001"}, {"fstype", "ext4"}}}};
}

}  // namespace

TEST(InstallationDisks, BuildsOnlyEfiRootAndOnePartitionPerDataDisk) {
    const auto plan = BuildHostDiskPlan(Layout(), Identities(), Storage(), "/srv/storage");
    ASSERT_EQ(plan.disks.size(), 2);
    EXPECT_EQ(plan.partition_table, "gpt");
    const auto& bulk = plan.disks.at(0);
    EXPECT_EQ(bulk.device, "/dev/disk/by-id/ata-test");
    EXPECT_EQ(bulk.serial, "SSD1");
    ASSERT_EQ(bulk.partitions.size(), 1);
    EXPECT_EQ(bulk.partitions.front().number, 1);
    EXPECT_EQ(bulk.partitions.front().start_mib, 1);
    EXPECT_EQ(bulk.partitions.front().end_mib, 8191);
    EXPECT_EQ(bulk.partitions.front().mountpoint, "/srv/storage/bulk");
    EXPECT_EQ(bulk.partitions.front().filesystem_uuid, Storage().at("bulk").at("uuid"));
    const auto& system = plan.disks.at(1);
    ASSERT_EQ(system.partitions.size(), 2);
    EXPECT_TRUE(system.partitions.at(0).esp);
    EXPECT_EQ(system.partitions.at(0).start_mib, 1);
    EXPECT_EQ(system.partitions.at(0).end_mib, 513);
    EXPECT_EQ(system.partitions.at(1).start_mib, 513);
    EXPECT_EQ(system.partitions.at(1).end_mib, 16383);
    EXPECT_EQ(system.partitions.at(1).mountpoint, "/");
    EXPECT_FALSE(system.partitions.at(1).filesystem_uuid);
}

TEST(InstallationDisks, RejectsAmbiguousOrUnstablePhysicalDiskIdentities) {
    for (const auto fault : {"missing", "extra", "alias", "serial", "partition", "traversal", "directory", "size"}) {
        SCOPED_TRACE(fault);
        auto ids = Identities();
        if (std::string(fault) == "missing") ids.erase("bulk");
        if (std::string(fault) == "extra") ids["other"] = ids.at("bulk");
        if (std::string(fault) == "alias") ids["bulk"]["device"] = ids.at("fast").at("device");
        if (std::string(fault) == "serial") ids["bulk"]["serial"] = ids.at("fast").at("serial");
        if (std::string(fault) == "partition") ids["bulk"]["device"] = "/dev/disk/by-id/ata-test-part1";
        if (std::string(fault) == "traversal") ids["bulk"]["device"] = "/dev/disk/by-id/../sda";
        if (std::string(fault) == "directory") ids["bulk"]["device"] = "/dev/disk/by-id/..";
        if (std::string(fault) == "size") ids["bulk"]["size_bytes"] = -1;
        EXPECT_THROW(BuildHostDiskPlan(Layout(), ids, Storage(), "/srv/storage"), ConfigurationError);
    }
}

TEST(InstallationDisks, RejectsInsufficientCapacityAndInvalidRemainingPartitions) {
    for (const auto fault : {"full", "last", "two_remaining", "minimum", "overflow"}) {
        SCOPED_TRACE(fault);
        auto layout = Layout();
        auto ids = Identities();
        auto& partitions = layout["storage"]["fast"];
        if (std::string(fault) == "full") ids["fast"]["size_bytes"] = 4609ULL << 20;
        if (std::string(fault) == "last") partitions[1]["size_mib"] = 4096;
        if (std::string(fault) == "two_remaining") partitions[0]["size_mib"] = "remaining";
        if (std::string(fault) == "minimum") partitions[1].erase("minimum_mib");
        if (std::string(fault) == "overflow") partitions[0]["size_mib"] = UINT64_MAX;
        EXPECT_THROW(BuildHostDiskPlan(layout, ids, Storage(), "/srv/storage"), ConfigurationError);
    }
}

TEST(InstallationDisks, RejectsMissingBootPartitionsAndMismatchedDataMounts) {
    for (const auto fault : {"table", "root", "efi", "split_boot", "mount", "uuid", "filesystem"}) {
        SCOPED_TRACE(fault);
        auto layout = Layout();
        auto storage = Storage();
        if (std::string(fault) == "table") layout["partition_table"] = "msdos";
        if (std::string(fault) == "root") layout["storage"]["fast"][1]["mountpoint"] = "/home";
        if (std::string(fault) == "efi") layout["storage"]["fast"][0]["filesystem"] = "ext4";
        if (std::string(fault) == "split_boot") layout["storage"]["bulk"][0]["mountpoint"] = "/boot/efi";
        if (std::string(fault) == "mount") storage["bulk"]["fstype"] = "xfs";
        if (std::string(fault) == "uuid") storage["bulk"].erase("uuid");
        if (std::string(fault) == "filesystem") layout["storage"]["bulk"][0]["filesystem"] = "xfs";
        EXPECT_THROW(BuildHostDiskPlan(layout, Identities(), storage, "/srv/storage"), ConfigurationError);
    }
}

}  // namespace skeleton::test
