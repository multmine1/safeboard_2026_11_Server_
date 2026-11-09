#include "host_units.hpp"

namespace skeleton::backup {
namespace {

void AddProducer(BackupHostPlan& host, const std::string& name, const BackupCapturePlan& capture,
                 const BackupHostContext& context, const std::string& previous) {
    const auto target = host.settings.capture_directory / capture.filename;
    const auto staging = target.string() + ".preparing";
    auto service = host.settings.service_options;
    service.update({{"Type", "oneshot"}, {"WorkingDirectory", "%d"},
        {"LoadCredentialEncrypted", Json::array({Credential(host.settings, "guest-" + capture.vm), Credential(host.settings, "sudo-" + capture.vm)})},
        {"StandardOutput", "file:" + staging}, {"StandardError", "journal"},
        {"LimitFSIZE", capture.max_size_bytes}, {"ExecStartPre", UnitCommand({"rm", "-f", "--", staging})},
        {"ExecStart", Json::array({UnitCommand({"sh", "-c", "exec " + GuestShellCommand(host, capture.vm, capture.argv, context)})})},
        {"ExecStartPost", Json::array({UnitCommand({"test", "-s", staging}),
            UnitCommand({"chown", host.repository_user, staging}), UnitCommand({"mv", "--", staging, target.string()})})},
        {"ExecStopPost", UnitCommand({"rm", "-f", "--", staging})}});
    host.units[name] = {{"Unit", BoundToGate(host, previous)}, {"Service", service}};
}

std::string NfsBackupCommand(const BackupHostPlan& host, const std::string& name, const BackupNfsPlan& nfs) {
    const auto directory = nfs.directory.string();
    const auto script = ShellCommand({"mount", "-t", "nfs", "-o", nfs.options, nfs.source, directory}) +
        " || exit $?; exec " +
        ShellCommand({"runuser", "-u", host.repository_user, "--", host.settings.binary.string(),
            "backup", "--tag", name}) + " --tag \"batch-${INVOCATION_ID}\" " + ShellCommand({"--", directory});
    return "!" + UnitCommand({"sh", "-c", script});
}

Json BackupCommands(const BackupHostPlan& host, const BackupPlan& plan) {
    Json commands = Json::array();
    for (const auto& [name, set] : plan.sets) {
        if (set.nfs) commands.push_back(NfsBackupCommand(host, name, *set.nfs));
        if (set.capture) commands.push_back(UnitCommand({host.settings.binary.string(), "backup", "--tag", name}) +
            " \"--tag\" \"batch-${INVOCATION_ID}\" " + UnitCommand({
            "--stdin-from-command", "--stdin-filename", set.capture->filename, "--", "cat",
            (host.settings.capture_directory / set.capture->filename).string()}));
    }
    return commands;
}

Json RepositoryService(const BackupHostPlan& host, const BackupPlan& plan) {
    const auto& settings = host.settings;
    auto service = settings.service_options;
    service.update({{"Type", "oneshot"}, {"User", host.repository_user},
        {"LoadCredentialEncrypted", Json::array({Credential(settings, "repository-password")})},
        {"Environment", Json::array({Json("RESTIC_REPOSITORY=" + plan.repository.string()).dump(),
            Json("RESTIC_CACHE_DIR=" + settings.cache_directory.string()).dump(), "RESTIC_PASSWORD_FILE=%d/repository-password"})}});
    return service;
}

std::string AddCompletionCheck(BackupHostPlan& host, const BackupPlan& plan, const BackupHostContext& context) {
    Json commands = Json::array();
    std::set<std::string> credentials;
    for (const auto& application : plan.quiesce) {
        if (!application.commands.filesystem) continue;
        const auto& source = application.commands;
        commands.push_back(UnitCommand({"sh", "-c", QuiesceStatusCommand(host, source, true, context)}));
        for (const auto prefix : {"guest-", "sudo-"})
            credentials.insert(Credential(host.settings, prefix + source.vm));
    }
    if (commands.empty()) return {};
    auto service = host.settings.service_options;
    service.update({{"Type", "oneshot"}, {"WorkingDirectory", "%d"},
        {"LoadCredentialEncrypted", credentials}, {"ExecStart", commands}});
    const auto unit = host.settings.name + "-freeze-check.service";
    host.units[unit] = {{"Unit", BoundToGate(host, {})}, {"Service", service}};
    return unit;
}

void AddWork(BackupHostPlan& host, const BackupPlan& plan, const std::string& previous, const BackupHostContext& context) {
    const auto& settings = host.settings;
    auto service = RepositoryService(host, plan);
    service["ExecStart"] = BackupCommands(host, plan);
    const auto captured = UnitCommand({settings.binary.string(), "tag", "--add", "captured", "--tag"}) + " \"batch-${INVOCATION_ID}\"";
    const auto check = AddCompletionCheck(host, plan, context);
    service["ExecStartPost"] = check.empty() ? Json(captured) :
        Json::array({"!" + UnitCommand({"systemctl", "start", "--wait", check}), captured});
    if (std::any_of(plan.sets.begin(), plan.sets.end(), [](const auto& set) { return set.second.nfs.has_value(); }))
        service["PrivateMounts"] = true;
    host.units[settings.name + "-work.service"] = {{"Unit", BoundToGate(host, previous)}, {"Service", service}};
}

void AddVerification(BackupHostPlan& host, const BackupPlan& plan) {
    auto service = RepositoryService(host, plan);
    Json commands = Json::array();
    for (auto arguments : {plan.check_argv, plan.retention_argv}) {
        arguments.front() = host.settings.binary.string();
        commands.push_back(UnitCommand(arguments));
    }
    service["ExecStart"] = commands;
    host.units[host.settings.name + "-verify.service"] = {{"Unit", BoundToGate(host, {})}, {"Service", service}};
}

std::string TransactionCleanup(const BackupHostPlan& host, const BackupPlan& plan) {
    const auto& settings = host.settings;
    std::vector<std::string> cleanup{"rm", "-f", "--"};
    std::vector<std::string> stop{"systemctl", "stop", settings.name + "-work.service", settings.name + "-verify.service"};
    if (host.units.contains(settings.name + "-freeze-check.service")) stop.push_back(settings.name + "-freeze-check.service");
    for (const auto& [name, set] : plan.sets)
        if (set.capture) {
            const auto path = (settings.capture_directory / set.capture->filename).string();
            cleanup.insert(cleanup.end(), {path, path + ".preparing"});
            stop.push_back(settings.name + "-" + name + ".service");
        }
    if (!plan.quiesce.empty()) stop.push_back(settings.name + "-quiesce.service");
    return "result=$?; " + ShellCommand(stop) + " || result=1; " +
        (cleanup.size() == 3 ? ":" : ShellCommand(cleanup)) + " || result=1; exit $result";
}

std::string TransactionScript(const BackupHostPlan& host, const BackupPlan& plan) {
    const auto& settings = host.settings;
    const auto work = settings.name + "-work.service";
    std::string stages;
    if (!plan.quiesce.empty()) stages = ShellCommand({"systemctl", "start", settings.name + "-quiesce.service"}) + " || exit $?; ";
    stages += ShellCommand({"systemctl", "start", "--wait", work}) + " || exit $?; ";
    if (!plan.quiesce.empty()) stages += ShellCommand({"systemctl", "stop", settings.name + "-quiesce.service"}) + " || exit $?; ";
    stages += ShellCommand({"systemctl", "start", "--wait", settings.name + "-verify.service"});
    return "flock -n 0 || exit $?; trap " + ShellArgument(TransactionCleanup(host, plan)) +
        " EXIT; trap 'exit 143' HUP INT TERM; " + stages;
}

void AddGateAndTimer(BackupHostPlan& host, const BackupPlan& plan, const BackupHostContext& context) {
    const auto& settings = host.settings;
    host.units[host.gate_unit] = {{"Service", {{"Type", "exec"}, {"UMask", settings.service_options.at("UMask")},
        {"RuntimeMaxSec", settings.service_options.at("TimeoutStartSec")},
        {"StandardInput", "file:" + context.lock_file.string()},
        {"ExecStartPre", UnitCommand(plan.mount_check_argv)},
        {"ExecStart", UnitCommand({"sh", "-c", TransactionScript(host, plan)})}}}};
    auto timer = settings.timer;
    timer["Timer"]["Unit"] = host.gate_unit;
    host.units[host.timer_unit] = std::move(timer);
}

}  // namespace

void BuildHostUnits(BackupHostPlan& host, const BackupPlan& plan, const BackupHostContext& context) {
    std::string previous = BuildQuiesceUnit(host, plan, context);
    for (const auto& [name, set] : plan.sets) {
        if (!set.capture) continue;
        const auto unit = host.settings.name + "-" + name + ".service";
        if (name == "work" || name == "verify" || name == "quiesce" || name == "freeze-check")
            throw ConfigurationError("host backup set collides with a control unit");
        AddProducer(host, unit, *set.capture, context, previous);
        previous = unit;
    }
    AddWork(host, plan, previous, context);
    AddVerification(host, plan);
    AddGateAndTimer(host, plan, context);
}

}  // namespace skeleton::backup
