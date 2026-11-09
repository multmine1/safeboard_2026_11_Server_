#include "readers.hpp"
#include "skeleton/input/cleanup.hpp"

namespace skeleton {
namespace {

FilesystemKind ReadFilesystemKind(const Json& stat) {
    const bool exists = stat.at("exists").get<bool>();
    FilesystemKind kind = FilesystemKind::Other;
    unsigned types = 0;
    for (const auto& [name, candidate] : std::map<std::string, FilesystemKind>{
            {"isreg", FilesystemKind::File}, {"isdir", FilesystemKind::Directory}, {"islnk", FilesystemKind::Link}})
        if (stat.value(name, false)) { kind = candidate; ++types; }
    if (types > 1 || (!exists && types > 0))
        throw ConfigurationError("cleanup filesystem entry contains conflicting types");
    return exists ? kind : FilesystemKind::Missing;
}

}  // namespace

Json ReadJsonFile(const std::filesystem::path& path) {
    return input::ReadJson(path);
}

CleanupConfiguration ReadCleanupConfiguration(const std::filesystem::path& path) {
    try {
        const auto reset = ReadJsonFile(path).at("reset");
        CleanupBoundary boundary(reset.at("boundary").at("paths").get<std::vector<std::filesystem::path>>(),
                                  reset.at("boundary").at("trees").get<std::vector<std::filesystem::path>>());
        std::vector<RemovalTarget> targets;
        for (const auto& name : {"remove_files", "remove_links", "volume_directories", "remove_directories"}) {
            const auto kind = std::string(name) == "remove_files" ? RemovalKind::File :
                              std::string(name) == "remove_links" ? RemovalKind::Link : RemovalKind::Directory;
            for (const auto& target : reset.at(name)) targets.push_back({target.get<std::string>(), kind});
        }
        targets.push_back({reset.at("account").at("home").get<std::string>(), RemovalKind::Directory});
        return {std::move(boundary), std::move(targets)};
    } catch (const Json::exception& error) {
        throw ConfigurationError(path.string() + ": " + error.what());
    }
}

FilesystemSnapshot ReadFilesystemSnapshot(const std::filesystem::path& path) {
    try {
        FilesystemSnapshot snapshot;
        const auto document = ReadJsonFile(path);
        if (!document.at("filesystems").is_array() || !document.at("paths").is_array())
            throw ConfigurationError(path.string() + ": mount targets and path observations must be arrays");
        for (const auto& mount : document.at("filesystems")) {
            if (mount.contains("children"))
                throw ConfigurationError(path.string() + ": cleanup requires flat mount observations from findmnt --list");
            snapshot.mounts.emplace_back(mount.at("target").get<std::string>());
        }
        for (const auto& entry : document.at("paths")) {
            const auto observed = std::filesystem::path(entry.at("item").get<std::string>());
            const auto kind = ReadFilesystemKind(entry.at("stat"));
            if (!snapshot.paths.emplace(observed, kind).second)
                throw ConfigurationError(path.string() + ": duplicate filesystem path observation");
            if (kind == FilesystemKind::Link)
                snapshot.link_targets.emplace(observed, entry.at("stat").at("lnk_source").get<std::string>());
        }
        return snapshot;
    } catch (const Json::exception& error) {
        throw ConfigurationError(path.string() + ": " + error.what());
    }
}

}  // namespace skeleton
