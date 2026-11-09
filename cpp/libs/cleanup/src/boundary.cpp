#include "paths.hpp"

namespace skeleton {
namespace cleanup {

bool IsWithin(const std::filesystem::path& child, const std::filesystem::path& parent) {
    auto part = child.begin();
    for (const auto& expected : parent) {
        if (part == child.end() || *part != expected) return false;
        ++part;
    }
    return true;
}

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    if (!path.is_absolute() || std::find(path.begin(), path.end(), "..") != path.end())
        throw ConfigurationError("cleanup paths must be absolute without parent traversal: " + path.string());
    const auto normalized = path.lexically_normal();
    return normalized != normalized.root_path() && normalized.filename().empty() ? normalized.parent_path() : normalized;
}

}  // namespace cleanup

using cleanup::IsWithin;
using cleanup::NormalizePath;

CleanupBoundary::CleanupBoundary(std::vector<std::filesystem::path> preserved_paths,
                                 std::vector<std::filesystem::path> preserved_trees)
    : preserved_paths_(std::move(preserved_paths)), preserved_trees_(std::move(preserved_trees)) {
    for (auto& path : preserved_paths_) path = NormalizePath(path);
    for (auto& path : preserved_trees_) path = NormalizePath(path);
}

void CleanupBoundary::ValidateTarget(const RemovalTarget& target) const {
    const auto path = NormalizePath(target.path);
    if (path == path.root_path() || path.parent_path() == path.root_path())
        throw ConfigurationError("cleanup cannot remove a filesystem root or top-level entry: " + path.string());
    for (const auto& preserved : preserved_paths_)
        if (IsWithin(preserved, path)) throw ConfigurationError("cleanup would remove a preserved path: " + path.string());
    for (const auto& preserved : preserved_trees_)
        if (IsWithin(preserved, path) || IsWithin(path, preserved))
            throw ConfigurationError("cleanup overlaps a preserved tree: " + path.string());
}

}  // namespace skeleton
