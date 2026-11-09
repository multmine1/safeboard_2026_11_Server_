#pragma once

#include "skeleton/vm/types.hpp"

namespace skeleton {

std::uint64_t ConvertBinaryUnits(std::uint64_t value, unsigned shift, const std::string& unit);
std::string FormatDiskDevice(std::size_t index, const std::string& bus);
void ValidateVmProfiles(const std::map<std::string, VmDefinition>& vms,
                        const Json& profiles, const std::vector<std::string>& storage_classes);

}  // namespace skeleton
