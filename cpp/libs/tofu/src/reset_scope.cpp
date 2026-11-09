#include "reset_scope.hpp"

namespace skeleton::tofu {
namespace {

Json AddNames(Json& records, const std::string& module, const std::string& type,
              const std::string& label, const Json& values) {
    Json names = Json::array();
    for (const auto& [key, value] : values.items()) {
        records[ResourceAddress(module, type, label, key)] = {{"/name", value.at("name")}};
        names.push_back(value.at("name"));
    }
    return names;
}

void AddStoragePaths(Json& records, const std::map<std::string, VmPlan>& guests) {
    for (const auto& [id, guest] : guests) {
        for (const auto& [storage, path] : guest.storage.directories)
            records.at(ResourceAddress("vm", "libvirt_pool", "storage", id + "/" + storage))["/target/path"] = path.string();
        for (const auto& [disk, path] : guest.storage.files)
            records.at(ResourceAddress("vm", "libvirt_volume", "volumes", id + "/" + disk))["/path"] = path.string();
        records.at(ResourceAddress("vm", "libvirt_volume", "volumes", id + "/seed.iso"))["/path"] = guest.storage.seed.string();
    }
}

}  // namespace

std::string ResourceAddress(const std::string& module, const std::string& type,
                            const std::string& name, const std::string& key) {
    return "module." + module + "." + type + "." + name + "[" + Json(key).dump() + "]";
}

Json BuildResetScope(const Json& networks, const Json& vm_data,
                     const std::map<std::string, VmPlan>& guests, const SiteDefinition& site) {
    Json records = Json::object();
    const auto network_names = AddNames(records, "network", "libvirt_network", "networks", networks);
    for (const auto& [key, value] : networks.items())
        records.at(ResourceAddress("network", "libvirt_network", "networks", key))["/bridge/name"] = value.at("bridge").at("name");
    const auto vm_names = AddNames(records, "vm", "libvirt_domain", "domains", vm_data.at("domains"));
    const auto pool_names = AddNames(records, "vm", "libvirt_pool", "storage", vm_data.at("storage"));
    AddNames(records, "vm", "libvirt_volume", "volumes", vm_data.at("volumes"));
    AddNames(records, "vm", "libvirt_cloudinit_disk", "seeds", vm_data.at("seeds"));
    AddStoragePaths(records, guests);
    if (!vm_data.at("image").empty())
        records["module.vm.libvirt_volume.image"] = {{"/name", vm_data.at("image").at("name")}, {"/pool", site.image_pool}};
    return {{"format", 1}, {"resources", records}, {"vm_names", vm_names},
            {"network_names", network_names}, {"pool_names", pool_names}};
}

}  // namespace skeleton::tofu
