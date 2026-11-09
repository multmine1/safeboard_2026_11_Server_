#include "host_units.hpp"

#include <regex>

namespace skeleton::backup {
namespace {

std::string ReleaseOwned(BackupHostPlan& host, const BackupQuiescePlan& application, const BackupHostContext& context) {
    const auto marker = application.marker.string();
    return "if " + ShellCommand({"test", "-e", marker}) + "; then " +
        GuestShellCommand(host, application.commands.vm, application.commands.leave, context) + " && " +
        ShellCommand({"rm", "--", marker}) + "; fi";
}

std::string EnterQuiesce(BackupHostPlan& host, const BackupQuiescePlan& application, const BackupHostContext& context) {
    const auto& commands = application.commands;
    return ReleaseOwned(host, application, context) + " || exit $?; " +
        QuiesceStatusCommand(host, commands, false, context) + " || exit $?; " +
        ShellCommand({"touch", "--", application.marker.string()}) + " || exit $?; " +
        GuestShellCommand(host, commands.vm, commands.enter, context);
}

}  // namespace

std::string QuiesceStatusCommand(BackupHostPlan& host, const BackupQuiesceSource& source,
                                 bool active, const BackupHostContext& context) {
    const auto test = source.filesystem ? "test \"$status\" = " + std::string(active ? "active" : "inactive") :
        "printf '%s' \"$status\" | " + ShellCommand({"python3", "-c",
            "import json,sys; sys.exit(json.load(sys.stdin).get('maintenance') is not " + std::string(active ? "True" : "False") + ")"});
    return "status=$(" + GuestShellCommand(host, source.vm, source.status, context) + ") || exit $?; " + test;
}

void PrepareQuiesce(BackupPlan& plan, const BackupDefinition& definition, const BackupContext& context) {
    std::set<std::string> names;
    for (const auto& name : definition.quiesce) {
        if (!definition.host || !context.host || !names.insert(name).second ||
            !std::regex_match(name, std::regex("[a-z0-9][a-z0-9-]{0,62}")) || !context.quiesce_sources.contains(name))
            throw ConfigurationError("backup quiesce requires distinct resolved workload names and native host settings");
        auto source = context.quiesce_sources.at(name);
        if (source.filesystem) PrepareFilesystemQuiesce(plan, source, name, definition);
        if (source.status.empty() || source.enter.empty() || source.leave.empty() || !source.required_vms.contains(source.vm))
            throw ConfigurationError("backup quiesce requires status enter leave commands and its complete access path");
        for (const auto& vm : source.required_vms) {
            if (!context.domain_names.contains(vm)) throw ConfigurationError("quiesced backup references an unknown VM");
            plan.required_domains.insert(context.domain_names.at(vm));
        }
        plan.quiesce.push_back({name, definition.host->capture_directory / (name + ".maintenance"), source});
    }
    if (definition.filesystem_watchdog && plan.guest_quiesce_units.empty())
        throw ConfigurationError("filesystem watchdog requires a filesystem-quiesced workload");
}

std::string BuildQuiesceUnit(BackupHostPlan& host, const BackupPlan& plan, const BackupHostContext& context) {
    if (plan.quiesce.empty()) return {};
    Json enter = Json::array();
    std::set<std::string> credentials;
    for (const auto& application : plan.quiesce) {
        enter.push_back(UnitCommand({"sh", "-c", EnterQuiesce(host, application, context)}));
        credentials.insert(Credential(host.settings, "guest-" + application.commands.vm));
        credentials.insert(Credential(host.settings, "sudo-" + application.commands.vm));
    }
    std::string leave = "result=0; ";
    for (auto application = plan.quiesce.rbegin(); application != plan.quiesce.rend(); ++application)
        leave += ReleaseOwned(host, *application, context) + " || result=1; ";
    leave += "exit $result";
    auto service = host.settings.service_options;
    service.update({{"Type", "oneshot"}, {"RemainAfterExit", true}, {"WorkingDirectory", "%d"},
        {"LoadCredentialEncrypted", credentials}, {"ExecStart", enter},
        {"ExecStopPost", UnitCommand({"sh", "-c", leave})}});
    const auto name = host.settings.name + "-quiesce.service";
    host.units[name] = {{"Unit", BoundToGate(host, {})}, {"Service", service}};
    return name;
}

}  // namespace skeleton::backup
