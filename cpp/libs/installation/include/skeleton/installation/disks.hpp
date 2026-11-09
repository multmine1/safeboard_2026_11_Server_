#pragma once

#include "skeleton/contracts/types.hpp"

namespace skeleton {

struct InstallationPartition {
    unsigned number;
    std::uint64_t start_mib;
    std::uint64_t end_mib;
    std::string filesystem;
    std::string mountpoint;
    std::optional<std::string> filesystem_uuid;
    bool esp;
    std::uint64_t minimum_mib = 0;
};

struct InstallationDisk {
    std::string storage_class;
    std::string device;
    std::string serial;
    std::uint64_t size_bytes;
    std::vector<InstallationPartition> partitions;
};

struct DebianPartitionRecipe {
    std::uint64_t efi_megabytes = 0;
    std::uint64_t root_minimum_megabytes = 0;
};

struct HostDiskPlan {
    std::string partition_table;
    std::vector<InstallationDisk> disks;
    DebianPartitionRecipe debian_recipe;
};

HostDiskPlan BuildHostDiskPlan(const Json& layout, const Json& identities,
    const Json& storage_classes, const std::filesystem::path& storage_root);

}  // namespace skeleton
