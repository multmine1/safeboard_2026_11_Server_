#include <gtest/gtest.h>

#include "fixture.hpp"
#include "skeleton/input/cleanup.hpp"

namespace skeleton::test {

TEST(CleanupInput, ReadsNativeMountAndPathFactsAndRejectsIncompleteOrAmbiguousSnapshots) {
    TemporaryDirectory directory;
    const auto path = directory.path / "filesystem.json";
    Json document{{"filesystems", {{{"target", "/"}}, {{"target", "/data/ssd"}}}}, {"paths", {
        {{"item", "/data/ssd/file"}, {"stat", {{"exists", true}, {"isreg", true}}}},
        {{"item", "/data/ssd/reference"}, {"stat", {{"exists", true}, {"islnk", true}, {"lnk_source", "/archive/data"}}}},
        {{"item", "/data/ssd/missing"}, {"stat", {{"exists", false}}}}}}};
    WriteJson(path, document);

    const auto snapshot = ReadFilesystemSnapshot(path);
    EXPECT_EQ(snapshot.mounts, (std::vector<std::filesystem::path>{"/", "/data/ssd"}));
    EXPECT_EQ(snapshot.paths.at("/data/ssd/file"), FilesystemKind::File);
    EXPECT_EQ(snapshot.paths.at("/data/ssd/reference"), FilesystemKind::Link);
    EXPECT_EQ(snapshot.link_targets.at("/data/ssd/reference"), "/archive/data");
    EXPECT_EQ(snapshot.paths.at("/data/ssd/missing"), FilesystemKind::Missing);
    document["filesystems"][0]["children"] = {{{"target", "/data/ssd/mounted"}}};
    WriteJson(path, document);
    EXPECT_THROW(ReadFilesystemSnapshot(path), ConfigurationError);
    document["filesystems"][0].erase("children");
    document["paths"].push_back(document["paths"].back());
    WriteJson(path, document);
    EXPECT_THROW(ReadFilesystemSnapshot(path), ConfigurationError);
    WriteJson(path, Json::object());
    EXPECT_THROW(ReadFilesystemSnapshot(path), ConfigurationError);
    document["paths"] = {{{"item", "/bad"}, {"stat", {{"exists", true}, {"isdir", true}, {"islnk", true}}}}};
    WriteJson(path, document);
    EXPECT_THROW(ReadFilesystemSnapshot(path), ConfigurationError);
}

}  // namespace skeleton::test
