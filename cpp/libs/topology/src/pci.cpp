#include "skeleton/topology/model.hpp"

namespace skeleton {

void ValidatePciAssignments(const SourceDefinitions& definitions) {
    std::set<std::string> addresses;
    for (const auto& [name, address] : definitions.site.pci_devices)
        if (!addresses.insert(address.text).second)
            throw ConfigurationError("physical PCI address has multiple logical names: " + address.text);
    std::map<std::string, std::string> owners;
    for (const auto& [id, vm] : definitions.vms) {
        std::set<std::string> requests;
        for (const auto& name : vm.pci_devices) {
            if (!definitions.site.pci_devices.contains(name))
                throw ConfigurationError(vm.source.string() + "/pci_devices: unknown device " + name);
            if (!requests.insert(name).second)
                throw ConfigurationError(vm.source.string() + "/pci_devices: repeated device " + name);
            if (!owners.emplace(name, id).second)
                throw ConfigurationError(vm.source.string() + "/pci_devices: device already assigned to another VM: " + name);
        }
    }
}

}  // namespace skeleton
