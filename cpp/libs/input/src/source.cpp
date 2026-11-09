#include "readers.hpp"

namespace skeleton {
namespace {
using namespace input;

std::vector<std::filesystem::path> DiscoverSourceFiles(const std::filesystem::path& directory) {
    if (!std::filesystem::is_directory(directory)) Fail(directory.string(), "expected a source directory");
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_symlink() && entry.is_regular_file() && entry.path().extension() == ".json")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

void ReadSourceFile(const std::filesystem::path& path, SourceDefinitions& definitions,
                    std::set<std::string>& ids, bool& found_site,
                    const std::filesystem::path& source_root) {
    const auto filename = path.filename().string();
    try {
        const auto value = ReadJson(path);
        if (filename == "site.json" || filename.ends_with(".site.json")) {
            if (found_site) Fail(path.string(), "only one site configuration is allowed");
            definitions.site = ReadSite(value, path, source_root);
            found_site = true;
        } else if (filename.ends_with(".vm.json")) {
            auto vm = ReadVm(value, path);
            if (!ids.insert(vm.id).second) Fail(path.string(), "duplicate UUID " + vm.id);
            definitions.vms.emplace(vm.id, std::move(vm));
        } else if (filename.ends_with(".network.json")) {
            auto network = ReadNetwork(value, path);
            if (!ids.insert(network.id).second) Fail(path.string(), "duplicate UUID " + network.id);
            definitions.networks.emplace(network.id, std::move(network));
        } else Fail(path.string(), "expected *.vm.json, *.network.json or site.json");
    } catch (const Json::exception& error) {
        Fail(path.string(), error.what());
    }
}

}  // namespace


SourceDefinitions ReadSourceDefinitions(const std::filesystem::path& directory) {
    using namespace input;
    SourceDefinitions definitions;
    std::set<std::string> ids;
    bool found_site = false;
    for (const auto& path : DiscoverSourceFiles(directory))
        ReadSourceFile(path, definitions, ids, found_site, directory);
    if (!found_site) Fail(directory.string(), "site.json is required");
    return definitions;
}

}  // namespace skeleton
