#pragma once

#include "skeleton/cleanup/boundary.hpp"

namespace skeleton {

struct CleanupConfiguration {
    CleanupBoundary boundary;
    std::vector<RemovalTarget> targets;
};

Json ReadJsonFile(const std::filesystem::path& path);
CleanupConfiguration ReadCleanupConfiguration(const std::filesystem::path& path);
FilesystemSnapshot ReadFilesystemSnapshot(const std::filesystem::path& path);

}  // namespace skeleton
