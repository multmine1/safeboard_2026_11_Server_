#pragma once

#include "skeleton/contracts/types.hpp"

namespace skeleton {

enum class RemovalKind { File, Directory, Link };

struct RemovalTarget {
    std::filesystem::path path;
    RemovalKind kind;
};

enum class FilesystemKind { Missing, File, Directory, Link, Other };

struct FilesystemSnapshot {
    std::map<std::filesystem::path, FilesystemKind> paths;
    std::vector<std::filesystem::path> mounts;
    std::map<std::filesystem::path, std::filesystem::path> link_targets;
};

class CleanupBoundary {
public:
    CleanupBoundary(std::vector<std::filesystem::path> preserved_paths,
                    std::vector<std::filesystem::path> preserved_trees);
    void ValidateTarget(const RemovalTarget& target) const;
    std::vector<std::filesystem::path> BuildInspectionPaths(const std::vector<RemovalTarget>& targets) const;
    void ValidateSnapshotTargets(const std::vector<RemovalTarget>& targets,
                                 const FilesystemSnapshot& snapshot) const;

private:
    std::vector<std::filesystem::path> preserved_paths_;
    std::vector<std::filesystem::path> preserved_trees_;
};

}  // namespace skeleton
