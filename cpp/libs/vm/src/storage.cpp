#include "skeleton/vm/storage.hpp"

namespace skeleton {
namespace {

void ValidateComponent(const std::string& value) {
    const auto path = std::filesystem::path(value);
    if (value.empty() || path.filename() != path || value == "." || value == "..")
        throw ConfigurationError("VM storage components must remain single path names");
}

}  // namespace

VmStoragePlan BuildVmStoragePlan(const VmDefinition& vm, const VmStorageSettings& settings) {
    if (vm.disks.empty() || vm.disks.front().name != "os")
        throw ConfigurationError("VM storage requires the OS disk first");
    ValidateComponent(vm.id);
    ValidateComponent(settings.volume_format);
    VmStoragePlan plan;
    for (const auto& disk : vm.disks) {
        ValidateComponent(disk.name);
        ValidateComponent(disk.storage);
        auto& directory = plan.directories[disk.storage];
        directory = settings.storage_root / disk.storage / "volumes" / vm.id;
        plan.files.emplace(disk.name, directory / (disk.name + "." + settings.volume_format));
        plan.links[disk.storage] = settings.libvirt_home / "VMs" / disk.storage / vm.id;
    }
    plan.seed = plan.directories.at(vm.disks.front().storage) / "seed.iso";
    return plan;
}

}  // namespace skeleton
