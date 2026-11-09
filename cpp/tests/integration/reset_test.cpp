#include <gtest/gtest.h>

#include "fixture.hpp"
#include "skeleton/pipeline/generator.hpp"

namespace skeleton::test {

TEST(ResetValidation, ValidatesTheHypervisorFilesystemSnapshotWithoutInspectingControllerPaths) {
    TemporaryDirectory directory;
    const std::filesystem::path home = "/srv/remote-host/vmctl";
    const std::filesystem::path repository = "/srv/remote-host/backups";
    WriteJson(directory.path / "reset.json", {{"reset", {
        {"boundary", {{"paths", Json::array()}, {"trees", {"/srv/remote-host/project"}}}},
        {"remove_files", Json::array()}, {"remove_links", Json::array()}, {"volume_directories", Json::array()},
        {"remove_directories", {repository.string()}},
        {"account", {{"home", home.string()}}}}}});
    const std::string address = "module.vm.libvirt_domain.domains[\"id\"]";
    WriteJson(directory.path / "scope.json", {{"format", 1}, {"resources", {{address, {{"/name", "vm-id"}}}}}});
    WriteJson(directory.path / "snapshot.json", {{"state_addresses", {address}}, {"plan", {
        {"format_version", "1.2"}, {"planned_values", Json::object()}, {"resource_changes", {
            {{"address", address}, {"mode", "managed"}, {"type", "libvirt_domain"},
             {"change", {{"actions", {"delete"}}, {"before", {{"name", "vm-id"}, {"private", "native-private-value"}}}}}}
        }}}}});
    Json filesystem{{"filesystems", {{{"target", "/"}}}}, {"paths", Json::array()}};
    for (const auto path : {"/", "/srv", "/srv/remote-host", "/srv/remote-host/project",
                            "/srv/remote-host/vmctl", "/srv/remote-host/backups"})
        filesystem["paths"].push_back({{"item", path}, {"stat", {{"exists", true}, {"isdir", true}}}});
    WriteJson(directory.path / "filesystem.json", filesystem);
    const auto validate = [&] { return RunResetValidation(directory.path / "reset.json", directory.path / "scope.json",
                                                       directory.path / "snapshot.json", directory.path / "filesystem.json"); };

    const auto preview = validate();
    EXPECT_EQ(preview.at("delete_count"), 1);
    EXPECT_EQ(preview.at("host_removals"), Json::array({
        {{"path", repository.string()}, {"kind", "directory"}}, {{"path", home.string()}, {"kind", "directory"}}}));
    EXPECT_EQ(preview.dump().find("native-private-value"), std::string::npos);
    filesystem["paths"].back()["stat"] = {{"exists", true}, {"islnk", true}, {"lnk_source", "/archives/shared"}};
    WriteJson(directory.path / "filesystem.json", filesystem);
    EXPECT_THROW(validate(), ConfigurationError);

    filesystem["paths"].back()["stat"] = {{"exists", true}, {"isdir", true}};
    filesystem["filesystems"].push_back({{"target", (repository / "mounted").string()}});
    WriteJson(directory.path / "filesystem.json", filesystem);
    EXPECT_THROW(validate(), ConfigurationError);
}

}  // namespace skeleton::test
