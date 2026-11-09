#include <gtest/gtest.h>

#include "skeleton/backup/plan.hpp"

namespace skeleton::test {
namespace {

BackupDefinition CaptureDefinition() {
    BackupDefinition definition{"hd", "backups/project", "environment/.tools/env/bin",
        "environment/.tools/backup/cache", "environment/.tools/backup/restore", 64,
        {{"daily", 7}}, {{"database", BackupSetKind::Postgresql, {}, {}, "postgres", 16}}};
    definition.capture = BackupCaptureDefinition{"/var/tmp", "environment/.tools/backup/capture", 32};
    return definition;
}

BackupContext CaptureContext() {
    BackupContext context{"/work/project", {}, {{"hd", {"/data/hd", "UUID=archive"}}}, {"/data/hd/volumes"}};
    context.repository_ssh_alias = "hypervisor";
    context.remote_sources["database"] = {BackupSetKind::Postgresql, "controller",
        {"k0s", "kubectl", "exec", "service/database", "--", "pg_dumpall", "--no-password"},
        {"root", "controller", "database"}, {"/var/lib/k0s", "/etc/k0s"}};
    context.domain_names = {{"root", "vm-root"}, {"controller", "vm-controller"}, {"database", "vm-database"}};
    return context;
}

}  // namespace

TEST(BackupCapture, CompilesBoundedNativeProducersAndStableArchiveNamesWithoutPuttingDataInArgv) {
    const auto plan = BuildBackupPlan(CaptureDefinition(), CaptureContext());

    ASSERT_TRUE(plan.capture);
    EXPECT_EQ(plan.capture->local_directory, "/work/project/environment/.tools/backup/capture");
    const auto& set = plan.sets.at("database");
    ASSERT_TRUE(set.capture);
    const auto& capture = *set.capture;
    EXPECT_EQ(capture.vm, "controller");
    EXPECT_EQ(capture.filename, "database.sql");
    EXPECT_EQ(capture.max_size_bytes, 16 * 1024 * 1024);
    EXPECT_EQ(capture.local_required_free_bytes, 48 * 1024 * 1024);
    EXPECT_EQ(capture.argv.front(), "prlimit");
    EXPECT_EQ(set.argv.back(), "cat");
    EXPECT_TRUE(capture.required_vms.contains("database"));
    EXPECT_EQ(plan.required_domains, (std::set<std::string>{"vm-root", "vm-controller", "vm-database"}));
}

TEST(BackupCapture, RejectsUnresolvedProducersUnboundedSizesAndDestructiveStagingPaths) {
    for (const auto fault : {"missing", "kind", "size", "paths", "local", "guest", "overflow", "domain"}) {
        SCOPED_TRACE(fault);
        auto definition = CaptureDefinition();
        auto context = CaptureContext();
        const auto name = std::string(fault);
        if (name == "missing") context.remote_sources.clear();
        if (name == "kind") context.remote_sources.at("database").kind = BackupSetKind::K0s;
        if (name == "size") definition.sets.front().max_size_mib.reset();
        if (name == "paths") definition.sets.front().paths = {"."};
        if (name == "local") definition.capture->local_directory = definition.bin_directory;
        if (name == "guest") definition.capture->guest_directory = "/var/lib/k0s/capture";
        if (name == "overflow") definition.sets.front().max_size_mib = UINT64_MAX;
        if (name == "domain") context.domain_names.clear();
        EXPECT_THROW(BuildBackupPlan(definition, context), ConfigurationError);
    }
}

}  // namespace skeleton::test
