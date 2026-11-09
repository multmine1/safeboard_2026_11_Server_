#pragma once

#include <filesystem>
#include <fstream>
#include "temporary_directory.hpp"

#include "skeleton/input/source.hpp"

namespace skeleton::test {

inline const std::string kRoot = "10000000-0000-4000-8000-000000000001";
inline const std::string kChild = "10000000-0000-4000-8000-000000000002";
inline const std::string kEdge = "20000000-0000-4000-8000-000000000001";
inline const std::string kGate = "20000000-0000-4000-8000-000000000002";
inline const std::string kDb = "20000000-0000-4000-8000-000000000003";
inline const std::string kApi = "20000000-0000-4000-8000-000000000004";


inline void WriteJson(const std::filesystem::path& path, const Json& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << value.dump(2) << '\n';
}

inline Json SiteJson() {
    std::ifstream stream(SITE_FIXTURE_FILE);
    auto site = Json::parse(stream);
    site["provider"]["lock_file"] =
        (std::filesystem::path(SITE_FIXTURE_FILE).parent_path() /
         site.at("provider").at("lock_file").get<std::string>()).lexically_normal().string();
    if (site.contains("assets"))
        for (auto& [name, reference] : site["assets"].items())
            reference = (std::filesystem::path(SITE_FIXTURE_FILE).parent_path() /
                reference.get<std::string>()).lexically_normal().string();
    return site;
}

inline Json VmJson(const std::string& id, const std::string& name,
                   const std::string& type = "application") {
    return {{"id", id}, {"name", name}, {"type", type}, {"vcpu", 2},
            {"ram_mib", 2048},
            {"disks", {{{"name", "os"}, {"storage", "nvme"}, {"size_gib", 32}}}}};
}

inline void WriteSources(const std::filesystem::path& root) {
    WriteJson(root / "site.json", SiteJson());
    WriteJson(root / "routers/edge.vm.json", VmJson(kEdge, "edge", "router"));
    WriteJson(root / "routers/gate.vm.json", VmJson(kGate, "gate", "router"));
    WriteJson(root / "machines/db.vm.json", VmJson(kDb, "db", "postgres"));
    WriteJson(root / "machines/api.vm.json", VmJson(kApi, "api"));
    WriteJson(root / "root.network.json",
              {{"id", kRoot}, {"name", "infra"}, {"router", kEdge},
               {"members", {kDb, kGate}}});
    WriteJson(root / "nested/child.network.json",
              {{"id", kChild}, {"name", "apps"}, {"router", kGate},
               {"members", {kApi}},
               {"allow", {{{"from", kApi}, {"to", kDb},
                            {"protocol", "tcp"}, {"ports", {5432}}}}}});
}

inline SourceDefinitions Definitions() {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    return ReadSourceDefinitions(directory.path);
}

}  // namespace skeleton::test
