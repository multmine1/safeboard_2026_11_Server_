#include "readers.hpp"

namespace skeleton::input {

void ReadPciDevices(const Json& value, SiteDefinition& site) {
    if (!value.contains("pci_devices")) return;
    const auto context = site.source.string() + "/pci_devices";
    const auto& hardware = value.at("pci_devices");
    if (!hardware.is_object()) Fail(context, "expected named physical PCI addresses");
    for (const auto& [name, address] : hardware.items()) {
        ReadName(Json{{"name", name}}, "name", context);
        try {
            site.pci_devices.emplace(name, ParsePciAddress(ReadString(hardware, name, context)));
        } catch (const ConfigurationError& error) {
            Fail(context + "/" + name, error.what());
        }
    }
}

}  // namespace skeleton::input
