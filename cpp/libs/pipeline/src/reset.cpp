#include "skeleton/pipeline/generator.hpp"
#include "skeleton/input/cleanup.hpp"
#include "skeleton/tofu/export.hpp"
#include "skeleton/report/export.hpp"

namespace skeleton {

Json RunResetInspection(const std::filesystem::path& configuration) {
    const auto cleanup = ReadCleanupConfiguration(configuration);
    return {{"paths", cleanup.boundary.BuildInspectionPaths(cleanup.targets)}};
}

Json RunResetValidation(const std::filesystem::path& configuration, const std::filesystem::path& scope,
                        const std::filesystem::path& snapshot, const std::filesystem::path& filesystem) {
    const auto cleanup = ReadCleanupConfiguration(configuration);
    auto preview = ValidateResetSnapshot(ReadJsonFile(scope), ReadJsonFile(snapshot));
    cleanup.boundary.ValidateSnapshotTargets(cleanup.targets, ReadFilesystemSnapshot(filesystem));
    return AddCleanupPreview(std::move(preview), cleanup.targets);
}

}  // namespace skeleton
