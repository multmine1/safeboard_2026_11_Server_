#include "host_units.hpp"

namespace skeleton::backup {

std::string ShellArgument(const std::string& argument) {
    std::string result = "'";
    for (const auto character : argument) result += character == '\'' ? "'\\''" : std::string(1, character);
    return result + "'";
}

std::string ShellCommand(const std::vector<std::string>& arguments) {
    std::string command;
    for (const auto& argument : arguments) command += (command.empty() ? "" : " ") + ShellArgument(argument);
    return command;
}

std::string UnitCommand(const std::vector<std::string>& arguments) {
    std::string result;
    for (const auto& argument : arguments) {
        std::string escaped;
        for (const auto character : argument) {
            if (character == '%' || character == '$') escaped += character;
            escaped += character;
        }
        if (!result.empty()) result += ' ';
        result += Json(escaped).dump();
    }
    return result;
}

std::string Credential(const BackupHostDefinition& settings, const std::string& name) {
    return name + ":" + (settings.credential_directory / (name + ".cred")).string();
}

Json BoundToGate(const BackupHostPlan& host, const std::string& previous) {
    Json result{{"BindsTo", host.gate_unit}, {"After", host.gate_unit}};
    if (!previous.empty()) {
        result["Requires"] = previous;
        result["After"] = host.gate_unit + " " + previous;
    }
    return result;
}

std::string GuestShellCommand(BackupHostPlan& host, const std::string& vm, const std::vector<std::string>& arguments,
                              const BackupHostContext& context) {
    if (!context.connections.contains(vm)) throw ConfigurationError("host backup has no resolved guest connection");
    const auto& connection = context.connections.at(vm);
    if (!host.connections.contains(vm)) {
        if (connection.address.empty() || connection.user.empty() || !connection.port)
            throw ConfigurationError("host backup requires a complete guest SSH endpoint");
        const auto known = connection.port == 22 ? connection.address : "[" + connection.address + "]:" + std::to_string(connection.port);
        host.connections[vm] = {{"address", connection.address}, {"user", connection.user},
            {"port", connection.port}, {"known_host", known}};
    }
    std::vector<std::string> ssh{"ssh", "-F", "/dev/null", "-i", "guest-" + vm, "-p", std::to_string(connection.port),
        "-o", "UserKnownHostsFile=" + host.settings.known_hosts.string()};
    for (const auto& [key, value] : host.settings.ssh_options.items())
        ssh.insert(ssh.end(), {"-o", key + "=" + (value.is_string() ? value.get<std::string>() : value.dump())});
    ssh.insert(ssh.end(), {connection.user + "@" + connection.address, "sudo -S -p '' -- " + ShellCommand(arguments)});
    return ShellCommand(ssh) + " < " + ShellArgument("sudo-" + vm);
}

}  // namespace skeleton::backup
