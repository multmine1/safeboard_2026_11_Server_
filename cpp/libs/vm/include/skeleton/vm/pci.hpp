#pragma once

#include "skeleton/contracts/types.hpp"

namespace skeleton {

struct VmDefinition;

struct PciAddress {
    std::string text;
    unsigned domain;
    unsigned bus;
    unsigned slot;
    unsigned function;
};

struct PciAssignment {
    std::string name;
    PciAddress address;
    bool managed;
};

PciAddress ParsePciAddress(const std::string& text);
std::vector<PciAssignment> BuildVmPciPlan(const VmDefinition& vm,
    const std::map<std::string, PciAddress>& hardware, const Json& policy);

}  // namespace skeleton
