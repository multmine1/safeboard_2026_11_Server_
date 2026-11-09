#include "pci.hpp"

#include <regex>

namespace skeleton {
namespace {

const Json& RequirePciField(const Json& policy, const std::string& key) {
    if (!policy.contains(key)) throw ConfigurationError("hypervisor_pci/" + key + ": required field");
    return policy.at(key);
}

std::string ReadPciString(const Json& policy, const std::string& key) {
    const auto& value = RequirePciField(policy, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty())
        throw ConfigurationError("hypervisor_pci/" + key + ": expected a nonempty string");
    return value.get<std::string>();
}

std::filesystem::path ReadPciPath(const Json& policy, const std::string& key) {
    const auto path = std::filesystem::path(ReadPciString(policy, key));
    if (!path.is_absolute() || path == path.root_path() || path != path.lexically_normal() ||
        path.string().find_first_of("\r\n") != std::string::npos)
        throw ConfigurationError("hypervisor_pci/" + key + ": expected a normalized absolute location");
    return path;
}

void ValidatePciClasses(const Json& policy) {
    const auto& values = RequirePciField(policy, "allowed_classes");
    if (!values.is_array() || values.empty()) throw ConfigurationError("hypervisor_pci/allowed_classes: expected nonempty classes");
    const std::regex token("0x[0-9a-f]{6}");
    for (const auto& value : values) {
        if (!value.is_string() || !std::regex_match(value.get<std::string>(), token))
            throw ConfigurationError("hypervisor_pci/allowed_classes: invalid class");
    }
}

std::map<std::string, std::set<std::string>> BuildPciGroupBoundaries(const InfrastructurePlan& plan) {
    std::map<std::string, std::set<std::string>> allowed;
    for (const auto& [id, vm] : plan.vms) {
        if (vm.pci_devices.empty()) continue;
        std::set<std::string> members;
        for (const auto& device : vm.pci_devices) members.insert(device.address.text);
        for (const auto& device : vm.pci_devices) allowed[device.name] = members;
    }
    return allowed;
}

void PreparePciDevices(Json& pci, const InfrastructurePlan& plan) {
    const auto root = ReadPciPath(pci, "sysfs_devices");
    ReadPciPath(pci, "firmware_table");
    const auto allowed = BuildPciGroupBoundaries(plan);
    std::set<std::string> catalog;
    for (const auto& [name, address] : plan.topology.definitions.site.pci_devices) catalog.insert(address.text);
    pci["devices"] = Json::array();
    for (const auto& [name, address] : plan.topology.definitions.site.pci_devices) {
        const auto path = root / address.text;
        pci["devices"].push_back({{"name", name}, {"address", address.text},
            {"class_path", (path / "class").string()}, {"iommu_link", (path / "iommu_group").string()},
            {"allowed_group_members", allowed.contains(name) ? allowed.at(name) : catalog}});
    }
}

}  // namespace

void PrepareHypervisorPci(InfrastructurePlan& plan) {
    if (!plan.hypervisor.contains("bootstrap")) return;
    auto& bootstrap = plan.hypervisor["bootstrap"];
    if (plan.topology.definitions.site.pci_devices.empty()) {
        bootstrap.erase("hypervisor_pci");
        return;
    }
    if (!bootstrap.contains("hypervisor_pci") || !bootstrap.at("hypervisor_pci").is_object())
        throw ConfigurationError("physical PCI devices require an explicit hypervisor_pci policy");
    auto& pci = bootstrap["hypervisor_pci"];
    ValidatePciClasses(pci);
    PreparePciDevices(pci, plan);
}

}  // namespace skeleton
