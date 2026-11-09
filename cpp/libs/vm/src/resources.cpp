#include "skeleton/vm/resources.hpp"

namespace skeleton {

std::uint64_t ConvertBinaryUnits(std::uint64_t value, unsigned shift, const std::string& unit) {
    const std::map<std::string, std::uint64_t> factors{
        {"bytes", 1}, {"b", 1}, {"KiB", 1024}, {"k", 1024}, {"KB", 1000},
        {"MiB", 1048576}, {"M", 1048576}, {"MB", 1000000},
        {"GiB", 1073741824}, {"G", 1073741824}, {"GB", 1000000000}};
    const auto factor = factors.find(unit);
    if (factor == factors.end()) throw ConfigurationError("unsupported libvirt unit: " + unit);
    if (value > UINT64_MAX >> shift) throw ConfigurationError("resource size overflows bytes");
    const auto bytes = value << shift;
    if (bytes % factor->second != 0)
        throw ConfigurationError("resource size cannot be represented exactly in " + unit);
    return bytes / factor->second;
}

std::string FormatDiskDevice(std::size_t index, const std::string& bus) {
    const std::map<std::string, std::string> prefixes{
        {"virtio", "vd"}, {"scsi", "sd"}, {"sata", "sd"}, {"usb", "sd"},
        {"ide", "hd"}, {"xen", "xvd"}, {"fdc", "fd"}};
    if (!prefixes.contains(bus)) throw ConfigurationError("cannot derive disk device for bus: " + bus);
    std::string suffix;
    do {
        suffix.insert(suffix.begin(), static_cast<char>('a' + index % 26));
        if (index < 26) break;
        index = index / 26 - 1;
    } while (true);
    return prefixes.at(bus) + suffix;
}

void ValidateVmProfiles(const std::map<std::string, VmDefinition>& vms,
                        const Json& profiles, const std::vector<std::string>& storage_classes) {
    const std::set<std::string> classes(storage_classes.begin(), storage_classes.end());
    for (const auto& [id, vm] : vms) {
        if (!profiles.contains(vm.type))
            throw ConfigurationError(vm.source.string() + "/type: unknown profile " + vm.type);
        for (const auto& disk : vm.disks)
            if (!classes.contains(disk.storage))
                throw ConfigurationError(vm.source.string() + "/disks/storage: unknown class " + disk.storage);
    }
}

}  // namespace skeleton
