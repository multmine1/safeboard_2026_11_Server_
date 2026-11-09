#pragma once

#include "skeleton/vm/types.hpp"

namespace skeleton {

struct VmStorageSettings {
    std::filesystem::path storage_root;
    std::filesystem::path libvirt_home;
    std::string volume_format;
};

struct VmStoragePlan {
    std::map<std::string, std::filesystem::path> directories;
    std::map<std::string, std::filesystem::path> files;
    std::map<std::string, std::filesystem::path> links;
    std::filesystem::path seed;
};

VmStoragePlan BuildVmStoragePlan(const VmDefinition& vm, const VmStorageSettings& settings);

}  // namespace skeleton
