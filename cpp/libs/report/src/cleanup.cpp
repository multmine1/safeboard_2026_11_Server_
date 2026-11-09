#include "skeleton/report/export.hpp"

namespace skeleton {

Json AddCleanupPreview(Json native_preview, const std::vector<RemovalTarget>& targets) {
    Json removals = Json::array();
    for (const auto& target : targets) {
        const auto kind = target.kind == RemovalKind::Directory ? "directory" :
                          target.kind == RemovalKind::Link ? "link" : "file";
        removals.push_back({{"path", target.path.string()}, {"kind", kind}});
    }
    native_preview["host_removals"] = std::move(removals);
    return native_preview;
}

}  // namespace skeleton
