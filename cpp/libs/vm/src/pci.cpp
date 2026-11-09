#include "skeleton/vm/pci.hpp"
#include "skeleton/vm/types.hpp"

#include <charconv>
#include <cctype>
#include <regex>

namespace skeleton {

PciAddress ParsePciAddress(const std::string& text) {
    if (!std::regex_match(text, std::regex("[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\\.[0-7]")))
        throw ConfigurationError("PCI address requires domain:bus:slot.function in hexadecimal");
    auto read = [&](std::size_t offset, std::size_t size) {
        unsigned value{};
        std::from_chars(text.data() + offset, text.data() + offset + size, value, 16);
        return value;
    };
    PciAddress address{text, read(0, 4), read(5, 2), read(8, 2), read(11, 1)};
    if (address.slot > 31) throw ConfigurationError("PCI slot exceeds five bits");
    std::transform(address.text.begin(), address.text.end(), address.text.begin(),
        [](unsigned char character) { return std::tolower(character); });
    return address;
}

std::vector<PciAssignment> BuildVmPciPlan(const VmDefinition& vm,
    const std::map<std::string, PciAddress>& hardware, const Json& policy) {
    if (vm.pci_devices.empty()) return {};
    if (!policy.is_object() || !policy.contains("managed") || !policy.at("managed").is_boolean())
        throw ConfigurationError(vm.source.string() + "/pci_devices: defaults/hostdev/managed must be explicit boolean policy");
    std::vector<PciAssignment> result;
    auto names = vm.pci_devices;
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        const auto device = hardware.find(name);
        if (device == hardware.end()) throw ConfigurationError(vm.source.string() + "/pci_devices: unknown device " + name);
        result.push_back({name, device->second, policy.at("managed").get<bool>()});
    }
    return result;
}

}  // namespace skeleton
