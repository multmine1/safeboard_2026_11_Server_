#include "paths.hpp"

namespace skeleton::backup {
namespace {

void RequireAbsolute(const std::filesystem::path& path) {
    if (!path.is_absolute() || path == path.root_path() ||
        std::find(path.begin(), path.end(), "..") != path.end())
        throw ConfigurationError("backup requires an absolute path below the filesystem root");
}

std::filesystem::path RelativePath(const std::filesystem::path& root, const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute() ||
        std::find(relative.begin(), relative.end(), "..") != relative.end())
        throw ConfigurationError("backup relative paths cannot escape their declared root");
    return NormalizePath(root / relative);
}

std::filesystem::path ToolPath(const BackupContext& context, const std::filesystem::path& relative) {
    const auto path = ProjectPath(context, relative);
    const auto tools = ProjectPath(context, "environment/.tools");
    if (path == tools || !ContainsPath(tools, path))
        throw ConfigurationError("backup tooling, cache and restore paths must remain in environment/.tools");
    return path;
}

void ValidateRepository(const std::filesystem::path& repository, const BackupContext& context) {
    for (const auto& path : context.repository_excluded_paths) {
        RequireAbsolute(path);
        if (PathsOverlap(repository, NormalizePath(path)))
            throw ConfigurationError("backup repository cannot overlap an excluded hypervisor path");
    }
}

void PrepareCapturePaths(BackupPlan& plan, const BackupDefinition& definition, const BackupContext& context,
                         const std::filesystem::path& bin) {
    if (!definition.capture) return;
    const auto& capture = *definition.capture;
    RequireAbsolute(capture.guest_directory);
    const auto local = ToolPath(context, capture.local_directory);
    for (const auto& other : {bin, plan.cache, plan.restore_directory})
        if (PathsOverlap(local, other))
            throw ConfigurationError("backup capture cannot overlap binaries, cache or restore directories");
    plan.capture = BackupCapturePaths{NormalizePath(capture.guest_directory), local};
}

}  // namespace

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    auto result = path.lexically_normal();
    while (result != result.root_path() && result.filename().empty()) result = result.parent_path();
    return result;
}

bool ContainsPath(const std::filesystem::path& parent, const std::filesystem::path& child) {
    return std::mismatch(parent.begin(), parent.end(), child.begin(), child.end()).first == parent.end();
}

bool PathsOverlap(const std::filesystem::path& left, const std::filesystem::path& right) {
    return ContainsPath(left, right) || ContainsPath(right, left);
}

std::filesystem::path ProjectPath(const BackupContext& context, const std::filesystem::path& relative) {
    RequireAbsolute(context.project_directory);
    return RelativePath(context.project_directory, relative);
}

void PreparePaths(BackupPlan& plan, const BackupDefinition& definition, const BackupContext& context) {
    const auto mount = context.mounts.find(definition.storage);
    if (mount == context.mounts.end() || mount->second.source.empty())
        throw ConfigurationError("backup storage requires a declared physical mount and source");
    RequireAbsolute(mount->second.path);
    plan.mount = NormalizePath(mount->second.path);
    plan.project_directory = ProjectPath(context, ".");
    plan.repository = RelativePath(plan.mount, definition.repository_directory);
    if (plan.repository == plan.mount) throw ConfigurationError("backup repository must be below its mount");
    ValidateRepository(plan.repository, context);
    plan.repository_config = plan.repository / "config";
    for (auto parent = plan.repository.parent_path(); parent != plan.mount; parent = parent.parent_path())
        plan.repository_parents.push_back(parent);
    plan.repository_parents.push_back(plan.mount);
    std::reverse(plan.repository_parents.begin(), plan.repository_parents.end());
    if (context.repository_ssh_alias.empty() || context.repository_ssh_alias.starts_with('-') ||
        context.repository_ssh_alias.find_first_of(" \t\r\n:/") != std::string::npos)
        throw ConfigurationError("backup requires the declared hypervisor SSH alias for its SFTP backend");
    plan.repository_url = "sftp:" + context.repository_ssh_alias + ":" + plan.repository.string();
    const auto bin = ToolPath(context, definition.bin_directory);
    plan.cache = ToolPath(context, definition.cache_directory);
    plan.restore_directory = ToolPath(context, definition.restore_directory);
    if (PathsOverlap(plan.cache, plan.restore_directory) || PathsOverlap(bin, plan.restore_directory) || PathsOverlap(bin, plan.cache))
        throw ConfigurationError("backup binary, cache and restore directories must not overlap");
    plan.mount_check_argv = {"findmnt", "--mountpoint", plan.mount.string(), "--source", mount->second.source};
    plan.repository_realpath_argv = {"realpath", "--canonicalize-missing", plan.repository.string()};
    PrepareCapturePaths(plan, definition, context, bin);
}

}  // namespace skeleton::backup
