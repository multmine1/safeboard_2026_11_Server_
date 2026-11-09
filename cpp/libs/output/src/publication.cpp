#include "skeleton/output/publication.hpp"
#include "skeleton/contracts/types.hpp"

#include <fstream>
#include <random>

namespace skeleton {
namespace {

void ValidateRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) throw ConfigurationError("invalid generated path: " + path.string());
    for (const auto& component : path)
        if (component == ".." || component == ".") throw ConfigurationError("unsafe generated path: " + path.string());
}

void ValidateParents(const std::filesystem::path& directory, const std::filesystem::path& relative) {
    auto path = directory;
    for (const auto& component : relative.parent_path()) {
        path /= component;
        if (std::filesystem::is_symlink(path))
            throw ConfigurationError("generated directory must not be a symlink: " + path.string());
    }
}

class StagingDirectory {
public:
    explicit StagingDirectory(const std::filesystem::path& output) {
        std::random_device random;
        do {
            path = output.parent_path() / ("." + output.filename().string() + ".staging-" +
                                           std::to_string(random()) + "-" + std::to_string(random()));
        } while (!std::filesystem::create_directory(path));
    }
    ~StagingDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    StagingDirectory(const StagingDirectory&) = delete;
    StagingDirectory& operator=(const StagingDirectory&) = delete;

    std::filesystem::path path;
};

void PreserveUnmanagedFiles(const std::filesystem::path& output,
                             const std::filesystem::path& staging) {
    if (!std::filesystem::exists(output) || std::filesystem::is_empty(output)) return;
    std::ifstream stream(output / "manifest.json");
    if (!stream) throw ConfigurationError(output.string() + ": refuses to replace an unmanaged directory");
    const auto manifest = Json::parse(stream);
    if (manifest.at("generator") != "skeleton" || manifest.at("format") != 1)
        throw ConfigurationError(output.string() + ": unknown manifest format");
    std::filesystem::copy(output, staging, std::filesystem::copy_options::recursive |
                                           std::filesystem::copy_options::copy_symlinks);
    for (const auto& file : manifest.at("files")) {
        const std::filesystem::path relative = file.get<std::string>();
        ValidateRelativePath(relative);
        ValidateParents(staging, relative);
        std::filesystem::remove(staging / relative);
    }
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(path, std::ios::binary);
    stream << content;
    stream.close();
}

void PublishDirectory(const std::filesystem::path& staging, const std::filesystem::path& output) {
    const auto backup = std::filesystem::path(staging.string() + ".previous");
    const bool had_output = std::filesystem::exists(output);
    if (had_output) std::filesystem::rename(output, backup);
    try {
        std::filesystem::rename(staging, output);
    } catch (...) {
        if (had_output) std::filesystem::rename(backup, output);
        throw;
    }
    std::error_code ignored;
    if (had_output) std::filesystem::remove_all(backup, ignored);
}

}  // namespace

void WriteGeneratedFiles(const GeneratedFiles& files, const std::filesystem::path& output) {
    const auto destination = std::filesystem::absolute(output).lexically_normal();
    std::filesystem::create_directories(destination.parent_path());
    StagingDirectory staging(destination);
    PreserveUnmanagedFiles(destination, staging.path);
    std::filesystem::remove(staging.path / "manifest.json");
    Json paths = Json::array();
    for (const auto& [path, content] : files) {
        ValidateRelativePath(path);
        ValidateParents(staging.path, path);
        if (std::filesystem::is_symlink(staging.path / path))
            throw ConfigurationError("generated file must not replace an unmanaged symlink: " + path.string());
        WriteFile(staging.path / path, content);
        paths.push_back(path.generic_string());
    }
    WriteFile(staging.path / "manifest.json", Json{{"generator", "skeleton"}, {"format", 1},
                                                   {"files", paths}}.dump(2) + "\n");
    PublishDirectory(staging.path, destination);
}

}  // namespace skeleton
