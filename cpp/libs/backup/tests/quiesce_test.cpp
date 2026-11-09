#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "host_fixture.hpp"
#include "temporary_directory.hpp"
#include "../src/host_units.hpp"

namespace skeleton::test {
namespace {

std::string DecodeShell(const std::string& command) {
    std::istringstream stream(command);
    Json executable, option, script;
    stream >> executable >> option >> script;
    auto result = script.get<std::string>();
    for (const auto& token : {"$$", "%%"})
        for (std::size_t at = 0; (at = result.find(token, at)) != std::string::npos; ++at)
            result.erase(at, 1);
    return result;
}

int RunShell(const std::filesystem::path& directory, const std::string& command) {
    const auto path = directory.string() + ":" + std::getenv("PATH");
    const auto script = "cd " + backup::ShellArgument(directory.string()) + " && PATH=" + backup::ShellArgument(path) + " " +
        backup::ShellCommand({"sh", "-c", DecodeShell(command)}) + " > result.log 2>&1";
    return std::system(script.c_str());
}

Json ReadState(const std::filesystem::path& directory) {
    return Json::parse(std::ifstream(directory / "state.json"));
}

void WriteState(const std::filesystem::path& directory, bool enabled) {
    std::ofstream(directory / "state.json") << Json{{"maintenance", enabled}};
}

void PrepareTransportStub(const std::filesystem::path& directory) {
    std::ofstream(directory / "sudo-controller") << "fixture\n";
    const auto path = directory / "ssh";
    std::ofstream(path) << R"(#!/bin/sh
for command; do :; done
case "$command" in
  *"'status'"*) cat state.json; test ! -e fail-status ;;
  *"'enter'"*) printf '{"maintenance":true}' > state.json; test ! -e fail-enter ;;
  *"'leave'"*) test ! -e fail-leave || exit 1; printf '{"maintenance":false}' > state.json ;;
  *) exit 99 ;;
esac
)";
    std::filesystem::permissions(path, std::filesystem::perms::owner_all);
}

class BackupQuiesceTest : public ::testing::TestWithParam<std::string> {
protected:
    void SetUp() override {
        PrepareTransportStub(directory.path);
        const auto& name = GetParam();
        WriteState(directory.path, name == "operator");
        if (name == "status" || name == "enter" || name == "leave") std::ofstream(directory.path / ("fail-" + name));
        auto definition = HostDefinition();
        definition.quiesce = {"cloud"};
        definition.host->capture_directory = directory.path / "capture";
        std::filesystem::create_directory(definition.host->capture_directory);
        auto context = HostContext();
        context.quiesce_sources["cloud"] = {"controller", {"status"}, {"enter"}, {"leave"}, {"controller"}};
        plan = BuildBackupPlan(definition, context);
        const auto& service = plan.host->units.at("project-backup-quiesce.service").at("Service");
        start = service.at("ExecStart").at(0).get<std::string>();
        stop = service.at("ExecStopPost").get<std::string>();
        marker = plan.quiesce.front().marker;
    }
    TemporaryDirectory directory;
    BackupPlan plan;
    std::string start, stop;
    std::filesystem::path marker;
};

}  // namespace

TEST_P(BackupQuiesceTest, ReleasesOnlyOwnedMaintenanceAndKeepsFailedCleanupRecoverable) {
    const auto& name = GetParam();
    const auto entered = RunShell(directory.path, start);
    EXPECT_EQ(entered == 0, name == "none" || name == "leave");
    EXPECT_EQ(std::filesystem::exists(marker), name != "operator" && name != "status");
    const auto released = RunShell(directory.path, stop);
    EXPECT_EQ(released == 0, name != "leave");
    EXPECT_EQ(ReadState(directory.path).at("maintenance"), name == "operator" || name == "leave");
    EXPECT_EQ(std::filesystem::exists(marker), name == "leave");
    if (name != "leave") return;
    std::filesystem::remove(directory.path / "fail-leave");
    EXPECT_EQ(RunShell(directory.path, start), 0);
    EXPECT_EQ(RunShell(directory.path, stop), 0);
    EXPECT_FALSE(std::filesystem::exists(marker));
    EXPECT_EQ(ReadState(directory.path).at("maintenance"), false);
}

INSTANTIATE_TEST_SUITE_P(Ownership, BackupQuiesceTest, ::testing::Values("none", "operator", "status", "enter", "leave"));

TEST(BackupFilesystem, AcceptsCompletionOnlyWhileItsFreezeIsActiveAndStatusTransportSucceeds) {
    TemporaryDirectory directory;
    auto definition = HostDefinition();
    definition.quiesce = {"mail"};
    definition.filesystem_watchdog = {{"OnActiveSec", 120}, {"AccuracySec", 1}};
    auto context = HostContext();
    context.quiesce_sources["mail"] = {"controller", {}, {}, {}, {"controller"},
        BackupFilesystemSource{"/srv/messages", "/etc/systemd/system"}};
    const auto plan = BuildBackupPlan(definition, context);
    const auto command = plan.host->units.at("project-backup-freeze-check.service").at("Service").at("ExecStart").at(0).get<std::string>();
    const auto ssh = directory.path / "ssh";
    std::ofstream(ssh) << "#!/bin/sh\ncat active-state; test ! -e fail-status\n";
    std::filesystem::permissions(ssh, std::filesystem::perms::owner_all);
    std::ofstream(directory.path / "sudo-controller") << "fixture\n";
    for (const auto state : {"active", "inactive", "transport"}) {
        SCOPED_TRACE(state);
        const std::string name = state;
        std::ofstream(directory.path / "active-state") << (name == "inactive" ? "inactive" : "active") << '\n';
        if (name == "transport") std::ofstream(directory.path / "fail-status");
        EXPECT_EQ(RunShell(directory.path, command) == 0, name == "active");
    }
}

}  // namespace skeleton::test
