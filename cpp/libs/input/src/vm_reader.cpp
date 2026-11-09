#include "readers.hpp"

#include <fstream>
#include <limits>
#include <regex>
#include <sstream>

namespace skeleton::input {

DiskDefinition ReadDisk(const Json& value, const std::string& context) {
    CheckFields(value, {"name", "storage", "size_gib"}, context);
    DiskDefinition disk{ReadName(value, "name", context),
                        ReadName(value, "storage", context),
                        ReadPositive(value, "size_gib", context,
                                     std::numeric_limits<std::uint64_t>::max() >> 30)};
    if (disk.name.size() > 20) Fail(context + "/name", "virtio disk serial is limited to 20 characters");
    return disk;
}

std::vector<DiskDefinition> ReadDisks(const Json& value, const std::string& context) {
    const auto& array = Require(value, "disks", context);
    if (!array.is_array()) Fail(context + "/disks", "expected an array");
    std::map<std::string, DiskDefinition> disks;
    for (std::size_t index = 0; index < array.size(); ++index) {
        auto disk = ReadDisk(array[index], context + "/disks/" + std::to_string(index));
        if (!disks.emplace(disk.name, disk).second)
            Fail(context + "/disks", "duplicate disk name " + disk.name);
    }
    if (!disks.contains("os")) Fail(context + "/disks", "an os disk is required");
    std::vector<DiskDefinition> result{disks.at("os")};
    for (const auto& [name, disk] : disks)
        if (name != "os") result.push_back(disk);
    return result;
}

Json ReadDomainOverrides(const Json& value, const std::string& context) {
    const auto domain = value.value("domain", Json::object());
    if (!domain.is_object()) Fail(context + "/domain", "expected an object");
    for (const auto& key : {"name", "type", "id", "uuid", "vcpu", "memory", "memory_unit"})
        if (domain.contains(key)) Fail(context + "/domain/" + key, "field is managed by the generator");
    if (domain.contains("devices")) {
        const auto& devices = domain.at("devices");
        if (!devices.is_object()) Fail(context + "/domain/devices", "expected an object");
        if (devices.contains("interfaces") || devices.contains("disks") || devices.contains("hostdevs"))
            Fail(context + "/domain/devices", "NICs, disks and PCI devices are managed by the generator");
    }
    return domain;
}

VmDefinition ReadVm(const Json& value, const std::filesystem::path& path) {
    const auto context = path.string();
    CheckFields(value, {"id", "name", "type", "vcpu", "ram_mib", "disks", "domain", "pci_devices"}, context);
    VmDefinition vm{ReadUuid(value, "id", context), ReadName(value, "name", context),
            ReadName(value, "type", context),
            static_cast<std::uint32_t>(ReadPositive(value, "vcpu", context, UINT32_MAX)),
            ReadPositive(value, "ram_mib", context, UINT64_MAX >> 20),
            ReadDisks(value, context), ReadDomainOverrides(value, context), path};
    if (value.contains("pci_devices")) vm.pci_devices = ReadStringArray(value, "pci_devices", context);
    return vm;
}

}  // namespace skeleton::input
