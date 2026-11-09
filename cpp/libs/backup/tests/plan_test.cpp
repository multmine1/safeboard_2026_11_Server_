#include <gtest/gtest.h>

#include "skeleton/backup/plan.hpp"

namespace skeleton::test {
namespace {

BackupDefinition Definition() {
    return {"hd", "backups/project", "environment/.tools/anaconda/envs/skeleton/bin",
        "environment/.tools/backup/cache", "environment/.tools/backup/restore", 64,
        {{"daily", 7}, {"weekly", 4}},
        {{"project", BackupSetKind::Files, {"."}, {"environment/.tools", "configs/generated"}},
         {"tofu-state", BackupSetKind::TofuState, {}, {}}}};
}

BackupContext Context() {
    BackupContext context{"/work/project", "/work/project/configs/generated/tofu",
        {{"hd", {"/data/hd", "UUID=archive"}}}, {"/data/hd/volumes", "/home/vmctl"},
        "/work/project/configs/generated/tofu/terraform.tfstate"};
    context.repository_ssh_alias = "hypervisor";
    return context;
}

}  // namespace

TEST(BackupPlan, CompilesFilesAndNativeStateCaptureWithoutASecretInItsEnvironmentOrArguments) {
    const auto plan = BuildBackupPlan(Definition(), Context());

    EXPECT_EQ(plan.repository, "/data/hd/backups/project");
    EXPECT_EQ(plan.repository_url, "sftp:hypervisor:/data/hd/backups/project");
    EXPECT_EQ(plan.repository_parents, (std::vector<std::filesystem::path>{"/data/hd", "/data/hd/backups"}));
    EXPECT_EQ(plan.cache, "/work/project/environment/.tools/backup/cache");
    EXPECT_EQ(plan.mount_check_argv, (std::vector<std::string>{"findmnt", "--mountpoint", "/data/hd", "--source", "UUID=archive"}));
    EXPECT_EQ(plan.sets.at("project").working_directory, "/work/project");
    EXPECT_EQ(plan.sets.at("project").argv.back(), "/work/project");
    const auto& files_argv = plan.sets.at("project").argv;
    EXPECT_NE(std::find(files_argv.begin(), files_argv.end(), "/work/project/environment/.tools"), files_argv.end());
    const auto& state = plan.sets.at("tofu-state");
    EXPECT_EQ(state.working_directory, "/work/project/configs/generated/tofu");
    EXPECT_NE(std::find(state.argv.begin(), state.argv.end(), "--stdin-from-command"), state.argv.end());
    EXPECT_EQ(state.argv.back(), "pull");
    EXPECT_EQ(state.required_files, (std::vector<std::filesystem::path>{"/work/project/configs/generated/tofu/terraform.tfstate"}));
    EXPECT_EQ(plan.retention_plan_argv.back(), "--dry-run");
    EXPECT_EQ(plan.restore_argv.back(), "--");
}

TEST(BackupPlan, KeepsIdenticalControllerAndHypervisorPathsInSeparateFilesystemContexts) {
    auto context = Context();
    context.project_directory = "/data/hd/backups/project";
    context.tofu_directory = context.project_directory / "configs/generated/tofu";
    context.tofu_state_file = *context.tofu_directory / "terraform.tfstate";

    const auto plan = BuildBackupPlan(Definition(), context);

    EXPECT_EQ(plan.repository, plan.project_directory);
    EXPECT_EQ(plan.repository_url, "sftp:hypervisor:/data/hd/backups/project");
    EXPECT_EQ(plan.sets.at("project").working_directory, context.project_directory);
}

TEST(BackupPlan, RefusesRepositoryOverlapAndPathsOutsideTheDeclaredWorkspace) {
    for (const auto fault : {"parent", "absolute", "volumes", "project", "unknown_storage", "cache", "restore", "alias", "exclude"}) {
        SCOPED_TRACE(fault);
        auto definition = Definition();
        auto context = Context();
        const auto name = std::string(fault);
        if (name == "parent") definition.repository_directory = "../escape";
        if (name == "absolute") definition.repository_directory = "/elsewhere";
        if (name == "volumes") definition.repository_directory = "volumes/backup";
        if (name == "project") context.repository_excluded_paths.push_back("/data/hd/backups");
        if (name == "unknown_storage") definition.storage = "missing";
        if (name == "cache") definition.cache_directory = "../outside";
        if (name == "restore") definition.restore_directory = definition.bin_directory;
        if (name == "alias") context.repository_ssh_alias = "invalid:alias";
        if (name == "exclude") definition.sets.front().excludes = {"../outside"};
        EXPECT_THROW(BuildBackupPlan(definition, context), ConfigurationError);
    }
}

TEST(BackupPlan, RejectsAmbiguousSetsAndMissingNativeStateContextBeforeAnyBackupRuns) {
    for (const auto fault : {"duplicate", "empty", "state_context", "state_file", "state_files", "retention"}) {
        SCOPED_TRACE(fault);
        auto definition = Definition();
        auto context = Context();
        const auto name = std::string(fault);
        if (name == "duplicate") definition.sets.push_back(definition.sets.front());
        if (name == "empty") definition.sets.front().paths.clear();
        if (name == "state_context") context.tofu_directory.reset();
        if (name == "state_file") context.tofu_state_file.reset();
        if (name == "state_files") definition.sets.back().paths = {"."};
        if (name == "retention") definition.retention["daily"] = 0;
        EXPECT_THROW(BuildBackupPlan(definition, context), ConfigurationError);
    }
}

}  // namespace skeleton::test
