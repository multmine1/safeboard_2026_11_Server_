#pragma once

#include "skeleton/contracts/types.hpp"
#include "skeleton/vm/pci.hpp"

namespace skeleton {

struct DiskDefinition {
    std::string name;
    std::string storage;
    std::uint64_t size_gib;
};

struct VmDefinition {
    std::string id;
    std::string name;
    std::string type;
    std::uint32_t vcpu;
    std::uint64_t ram_mib;
    std::vector<DiskDefinition> disks;
    Json domain = Json::object();
    std::filesystem::path source;
    std::vector<std::string> pci_devices;
};

}  // namespace skeleton
