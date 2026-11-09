#include "paths.hpp"

namespace skeleton {
namespace {

void AddAncestors(std::set<std::filesystem::path>& paths, const std::filesystem::path& path) {
    for (auto current = cleanup::NormalizePath(path);; current = current.parent_path()) {
        paths.insert(current);
        if (current == current.root_path()) break;
    }
}

FilesystemKind RequireEntry(const FilesystemSnapshot& snapshot, const std::filesystem::path& path) {
    const auto entry = snapshot.paths.find(path);
    if (entry == snapshot.paths.end())
        throw ConfigurationError("cleanup filesystem snapshot is missing a path: " + path.string());
    return entry->second;
}

void ValidateWalk(const FilesystemSnapshot& snapshot, const std::filesystem::path& path) {
    std::vector<std::filesystem::path> ancestors;
    for (auto current = path;; current = current.parent_path()) {
        ancestors.push_back(current);
        if (current == current.root_path()) break;
    }
    bool missing = false;
    for (auto entry = ancestors.rbegin(); entry != ancestors.rend(); ++entry) {
        const auto kind = RequireEntry(snapshot, *entry);
        if (kind == FilesystemKind::Missing) { missing = true; continue; }
        if (missing) throw ConfigurationError("cleanup snapshot has an existing child under a missing path: " + entry->string());
        if (*entry != path && kind != FilesystemKind::Directory)
            throw ConfigurationError("cleanup path has a non-directory ancestor: " + entry->string());
    }
}

std::vector<std::filesystem::path> ResolvePreservedPaths(const FilesystemSnapshot& snapshot,
                                                       const std::vector<std::filesystem::path>& paths) {
    std::vector<std::filesystem::path> resolved;
    for (const auto& path : paths) {
        ValidateWalk(snapshot, path);
        if (RequireEntry(snapshot, path) != FilesystemKind::Link) { resolved.push_back(path); continue; }
        const auto target = snapshot.link_targets.find(path);
        if (target == snapshot.link_targets.end() || target->second == path)
            throw ConfigurationError("cleanup snapshot is missing a resolved preserved symlink: " + path.string());
        resolved.push_back(cleanup::NormalizePath(target->second));
    }
    return resolved;
}

void ValidateRemovalType(const RemovalTarget& target, FilesystemKind kind) {
    if (kind == FilesystemKind::Missing) return;
    const bool valid = target.kind == RemovalKind::Directory ? kind == FilesystemKind::Directory :
                       target.kind == RemovalKind::Link ? kind == FilesystemKind::Link :
                       kind == FilesystemKind::File || kind == FilesystemKind::Link;
    if (!valid) throw ConfigurationError("cleanup target has an unexpected filesystem type: " + target.path.string());
}

}  // namespace

std::vector<std::filesystem::path> CleanupBoundary::BuildInspectionPaths(const std::vector<RemovalTarget>& targets) const {
    std::set<std::filesystem::path> paths;
    for (const auto& path : preserved_paths_) AddAncestors(paths, path);
    for (const auto& path : preserved_trees_) AddAncestors(paths, path);
    for (const auto& target : targets) {
        ValidateTarget(target);
        AddAncestors(paths, target.path);
    }
    return {paths.begin(), paths.end()};
}

void CleanupBoundary::ValidateSnapshotTargets(const std::vector<RemovalTarget>& targets,
                                             const FilesystemSnapshot& snapshot) const {
    if (std::find(snapshot.mounts.begin(), snapshot.mounts.end(), std::filesystem::path("/")) == snapshot.mounts.end())
        throw ConfigurationError("cleanup snapshot is missing the filesystem root mount");
    for (const auto& path : BuildInspectionPaths(targets)) RequireEntry(snapshot, path);
    if (RequireEntry(snapshot, "/") != FilesystemKind::Directory)
        throw ConfigurationError("cleanup snapshot is missing the filesystem root directory");
    const CleanupBoundary resolved(ResolvePreservedPaths(snapshot, preserved_paths_),
                                   ResolvePreservedPaths(snapshot, preserved_trees_));
    for (const auto& target : targets) {
        const auto path = cleanup::NormalizePath(target.path);
        ValidateWalk(snapshot, path);
        ValidateRemovalType(target, RequireEntry(snapshot, path));
        resolved.ValidateTarget({path, target.kind});
        for (const auto& mount : snapshot.mounts)
            if (cleanup::IsWithin(cleanup::NormalizePath(mount), path))
                throw ConfigurationError("cleanup target contains a mounted filesystem: " + path.string());
    }
}

}  // namespace skeleton
