#include "skeleton/installation/disks.hpp"

#include <regex>

namespace skeleton {
namespace {

std::uint64_t ReadPositive(const Json& value) {
    if (!value.is_number_unsigned() && (!value.is_number_integer() || value.get<std::int64_t>() <= 0))
        throw ConfigurationError("installation sizes must be positive integers");
    const auto size = value.get<std::uint64_t>();
    if (size == 0) throw ConfigurationError("installation sizes must be positive integers");
    return size;
}

std::string ReadLine(const Json& value) {
    if (!value.is_string() || value.get_ref<const std::string&>().empty() ||
        value.get_ref<const std::string&>().find_first_of("\r\n") != std::string::npos)
        throw ConfigurationError("installation identity requires a nonempty single-line string");
    return value.get<std::string>();
}

InstallationDisk ReadDiskIdentity(const std::string& name, const Json& identity) {
    const auto device = ReadLine(identity.at("device"));
    const auto path = std::filesystem::path(device);
    if (path.parent_path() != "/dev/disk/by-id" || path.filename().empty() || path != path.lexically_normal() ||
        !std::regex_match(path.filename().string(), std::regex("[a-zA-Z0-9_.:+-]+")) ||
        std::regex_search(device, std::regex("-part[0-9]+$")))
        throw ConfigurationError("installation requires a stable whole-disk /dev/disk/by-id path");
    return {name, device, ReadLine(identity.at("serial")), ReadPositive(identity.at("size_bytes")), {}};
}

std::uint64_t ResolvePartitionEnd(const Json& definition, std::uint64_t start,
    std::uint64_t limit, std::uint64_t alignment, bool last) {
    const auto& size = definition.at("size_mib");
    if (size.is_string() && size == "remaining") {
        if (!last || start >= limit || ReadPositive(definition.at("minimum_mib")) > limit - start)
            throw ConfigurationError("remaining partition must be last and fit its declared minimum");
        return limit;
    }
    if (last) throw ConfigurationError("the final partition must use remaining space");
    const auto fixed = ReadPositive(size);
    if (fixed % alignment != 0 || start >= limit || fixed >= limit - start)
        throw ConfigurationError("fixed partition exceeds disk capacity or alignment");
    return start + fixed;
}

InstallationPartition ReadPartition(const Json& definition, unsigned number,
    std::uint64_t start, std::uint64_t end, const Json& storage,
    const std::filesystem::path& data_mountpoint) {
    const auto filesystem = ReadLine(definition.at("filesystem"));
    if (filesystem != "ext4" && filesystem != "vfat")
        throw ConfigurationError("minimal host layout supports ext4 and EFI vfat");
    const auto mount = definition.contains("mountpoint") ? ReadLine(definition.at("mountpoint")) : data_mountpoint.string();
    const bool esp = mount == "/boot/efi";
    const bool root = mount == "/";
    InstallationPartition result{number, start, end, filesystem, mount, std::nullopt, esp};
    result.minimum_mib = definition.contains("minimum_mib") ? ReadPositive(definition.at("minimum_mib")) : end - start;
    if (esp || root) {
        if (!storage.empty() || filesystem != (esp ? "vfat" : "ext4"))
            throw ConfigurationError("EFI/root filesystems conflict with the storage mount configuration");
    } else {
        if (mount != data_mountpoint || storage.at("fstype") != filesystem || filesystem != "ext4")
            throw ConfigurationError("data partition must match its existing storage class mount");
        const auto uuid = ReadLine(storage.at("uuid"));
        if (!std::regex_match(uuid, std::regex("[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}")))
            throw ConfigurationError("data filesystem UUID must be explicit and valid");
        result.filesystem_uuid = uuid;
    }
    return result;
}

void BuildDiskPartitions(InstallationDisk& disk, const Json& definitions,
    const Json& storage, const std::filesystem::path& storage_root, std::uint64_t alignment) {
    const auto size_mib = disk.size_bytes / (1024 * 1024);
    if (size_mib / 2 <= alignment || !definitions.is_array() || definitions.empty())
        throw ConfigurationError("installation disk has no usable aligned partition capacity");
    const auto limit = (size_mib / alignment - 1) * alignment;
    auto start = alignment;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto end = ResolvePartitionEnd(definitions.at(index), start, limit, alignment, index + 1 == definitions.size());
        disk.partitions.push_back(ReadPartition(definitions.at(index), index + 1, start, end,
            storage, storage_root / disk.storage_class));
        start = end;
    }
}

void ValidateBootLayout(const HostDiskPlan& plan) {
    const InstallationDisk* root_disk = nullptr;
    const InstallationDisk* efi_disk = nullptr;
    std::set<std::string> uuids;
    for (const auto& disk : plan.disks) {
        for (const auto& partition : disk.partitions) {
            if (partition.mountpoint == "/") {
                if (root_disk) throw ConfigurationError("installation requires exactly one root partition");
                root_disk = &disk;
            }
            if (partition.esp) {
                if (efi_disk) throw ConfigurationError("installation requires exactly one EFI partition");
                efi_disk = &disk;
            }
            if (partition.filesystem_uuid && !uuids.insert(*partition.filesystem_uuid).second)
                throw ConfigurationError("installation filesystem UUIDs must be unique");
        }
    }
    if (!root_disk || root_disk != efi_disk || root_disk->partitions.size() != 2 || !root_disk->partitions.front().esp)
        throw ConfigurationError("minimal UEFI installation requires EFI then root on the same disk");
    for (const auto& disk : plan.disks)
        if (&disk != root_disk && disk.partitions.size() != 1)
            throw ConfigurationError("minimal data disks require one partition");
}

std::uint64_t DecimalMegabytes(std::uint64_t mib) {
    const auto bytes = mib * (1024 * 1024);
    return bytes / 1000000 + (bytes % 1000000 != 0);
}

DebianPartitionRecipe BuildDebianRecipe(const HostDiskPlan& plan) {
    DebianPartitionRecipe recipe;
    for (const auto& disk : plan.disks)
        for (const auto& partition : disk.partitions) {
            // Partman's first allocation includes the leading alignment area.
            if (partition.esp) recipe.efi_megabytes = DecimalMegabytes(partition.end_mib);
            if (partition.mountpoint == "/") recipe.root_minimum_megabytes = DecimalMegabytes(partition.minimum_mib);
        }
    return recipe;
}

HostDiskPlan BuildDiskPlan(const Json& layout, const Json& identities,
    const Json& storage_classes, const std::filesystem::path& storage_root) {
    if (layout.at("partition_table") != "gpt") throw ConfigurationError("host installation requires GPT");
    if (!storage_root.is_absolute() || storage_root == storage_root.root_path() || storage_root != storage_root.lexically_normal())
        throw ConfigurationError("installation storage_root must be a normalized absolute directory");
    const auto& definitions = layout.at("storage");
    if (!definitions.is_object() || !identities.is_object() || !storage_classes.is_object() ||
        definitions.size() != identities.size() || definitions.size() != storage_classes.size())
        throw ConfigurationError("installation layout, identities and storage classes must name the same disks");
    HostDiskPlan plan{layout.at("partition_table"), {}};
    std::set<std::string> devices, serials;
    const auto alignment = ReadPositive(layout.at("alignment_mib"));
    for (const auto& [name, partitions] : definitions.items()) {
        if (!std::regex_match(name, std::regex("[a-zA-Z0-9_-]+"))) throw ConfigurationError("invalid installation storage class");
        auto disk = ReadDiskIdentity(name, identities.at(name));
        if (!devices.insert(disk.device).second || !serials.insert(disk.serial).second)
            throw ConfigurationError("installation disks must have distinct devices and serials");
        BuildDiskPartitions(disk, partitions, storage_classes.at(name), storage_root, alignment);
        plan.disks.push_back(std::move(disk));
    }
    ValidateBootLayout(plan);
    plan.debian_recipe = BuildDebianRecipe(plan);
    return plan;
}

}  // namespace

HostDiskPlan BuildHostDiskPlan(const Json& layout, const Json& identities,
    const Json& storage_classes, const std::filesystem::path& storage_root) {
    try {
        return BuildDiskPlan(layout, identities, storage_classes, storage_root);
    } catch (const Json::exception& error) {
        throw ConfigurationError(std::string("invalid host installation layout: ") + error.what());
    }
}

}  // namespace skeleton
