#include <gtest/gtest.h>

#include "skeleton/cleanup/boundary.hpp"

namespace skeleton::test {

TEST(Cleanup, ProtectsPlatformAnchorsAndWholeControllerTreesWithoutBlockingVmChildren) {
    const CleanupBoundary boundary({"/srv/pools", "/srv/pools/ssd", "/etc/netplan/host.yaml"},
                                   {"/home/operator/project", "/home/operator/.ssh", "/etc/ssh"});

    EXPECT_NO_THROW(boundary.ValidateTarget({"/srv/pools/ssd/volumes/id", RemovalKind::Directory}));
    EXPECT_NO_THROW(boundary.ValidateTarget({"/etc/cockpit/ws-certs.d/managed.key", RemovalKind::File}));
    for (const auto path : {"/", "/etc", "/srv/pools", "/srv/pools/ssd", "/srv/pools/ssd/", "/home/operator",
                            "/home/operator/project/cache", "/etc/ssh/sshd_config", "relative", "/srv/pools/../other"}) {
        SCOPED_TRACE(path);
        EXPECT_THROW(boundary.ValidateTarget({path, RemovalKind::Directory}), ConfigurationError);
    }
}

TEST(Cleanup, PreparesCompleteRemoteQueriesAndAllowsMissingTargetsAndOwnedLeafLinks) {
    const CleanupBoundary boundary({}, {"/home/operator/project"});
    const std::vector<RemovalTarget> targets{{"/data/owned", RemovalKind::Directory}, {"/data/reference", RemovalKind::Link}};
    EXPECT_EQ(boundary.BuildInspectionPaths(targets), (std::vector<std::filesystem::path>{
        "/", "/data", "/data/owned", "/data/reference", "/home", "/home/operator", "/home/operator/project"}));
    FilesystemSnapshot snapshot{{}, {"/"}};
    for (const auto& path : boundary.BuildInspectionPaths(targets)) snapshot.paths[path] = FilesystemKind::Directory;
    snapshot.paths["/data/owned"] = FilesystemKind::Missing;
    snapshot.paths["/data/reference"] = FilesystemKind::Link;

    EXPECT_NO_THROW(boundary.ValidateSnapshotTargets(targets, snapshot));
    auto incomplete = snapshot;
    incomplete.mounts.clear();
    EXPECT_THROW(boundary.ValidateSnapshotTargets(targets, incomplete), ConfigurationError);
    for (auto& [path, kind] : incomplete.paths) kind = FilesystemKind::Missing;
    incomplete.mounts = {"/"};
    EXPECT_THROW(boundary.ValidateSnapshotTargets(targets, incomplete), ConfigurationError);
    snapshot.paths.erase("/data");
    EXPECT_THROW(boundary.ValidateSnapshotTargets(targets, snapshot), ConfigurationError);
}

TEST(Cleanup, RejectsRemoteTypeChangesAliasAncestorsAndNestedMounts) {
    const CleanupBoundary boundary({}, {"/home/operator/project"});
    const std::vector<RemovalTarget> targets{{"/data/owned", RemovalKind::Directory}};
    FilesystemSnapshot valid{{}, {"/"}};
    for (const auto& path : boundary.BuildInspectionPaths(targets)) valid.paths[path] = FilesystemKind::Directory;
    EXPECT_NO_THROW(boundary.ValidateSnapshotTargets(targets, valid));
    for (const auto& [path, kind] : std::vector<std::pair<std::filesystem::path, FilesystemKind>>{
            {"/data/owned", FilesystemKind::Link}, {"/data", FilesystemKind::Link},
            {"/data", FilesystemKind::Missing}, {"/home/operator/project", FilesystemKind::Link}}) {
        auto changed = valid;
        changed.paths[path] = kind;
        EXPECT_THROW(boundary.ValidateSnapshotTargets(targets, changed), ConfigurationError);
    }
    valid.mounts.push_back("/data/owned/mounted");
    EXPECT_THROW(boundary.ValidateSnapshotTargets(targets, valid), ConfigurationError);
}

TEST(Cleanup, ProtectsNativeResolvedLeafLinksWithoutFollowingControllerFilesystemLinks) {
    const CleanupBoundary boundary({"/etc/systemd/system/default.target"}, {});
    const std::vector<RemovalTarget> targets{{"/data/owned", RemovalKind::Directory}};
    FilesystemSnapshot snapshot{{}, {"/"}};
    for (const auto& path : boundary.BuildInspectionPaths(targets)) snapshot.paths[path] = FilesystemKind::Directory;
    snapshot.paths["/etc/systemd/system/default.target"] = FilesystemKind::Link;
    snapshot.link_targets["/etc/systemd/system/default.target"] = "/usr/lib/systemd/system/multi-user.target";
    EXPECT_NO_THROW(boundary.ValidateSnapshotTargets(targets, snapshot));
    snapshot.link_targets["/etc/systemd/system/default.target"] = "/data/owned/preserved.target";
    EXPECT_THROW(boundary.ValidateSnapshotTargets(targets, snapshot), ConfigurationError);
}

}  // namespace skeleton::test
