#pragma once

#include "fixture.hpp"

namespace skeleton::test {

inline Json HypervisorJson() {
    return Json::parse(std::ifstream(std::filesystem::path(SITE_FIXTURE_FILE).parent_path() / "hypervisor.json"));
}

inline Json SshPolicyJson() {
    return Json::parse(std::ifstream(std::filesystem::path(SITE_FIXTURE_FILE).parent_path() / "sshd.json"));
}

inline Json PciPolicyJson() {
    return {{"firmware_table", "/sys/firmware/acpi/tables/DMAR"},
        {"sysfs_devices", "/sys/bus/pci/devices"}, {"allowed_classes", {"0x030000", "0x040300"}}};
}

inline void UseHypervisorFile(const std::filesystem::path& root, Json hypervisor) {
    auto site = SiteJson();
    site["hypervisor"] = {{"file", "../hypervisor.json"}};
    for (const auto& key : {"storage_root", "storage_classes", "vmctl_home"}) site.erase(key);
    site["services"].erase("hypervisor");
    WriteJson(root.parent_path() / "hypervisor.json", hypervisor);
    WriteJson(root / "site.json", site);
}

}  // namespace skeleton::test
