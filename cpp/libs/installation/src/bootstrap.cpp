#include "skeleton/installation/bootstrap.hpp"

#include <regex>

namespace skeleton {
namespace {

std::string ReadSafePath(const std::string& value) {
    const auto path = std::filesystem::path(value);
    if (!path.is_absolute() || path == path.root_path() || path != path.lexically_normal() ||
        !std::regex_match(value, std::regex("[a-zA-Z0-9_./:-]+")))
        throw ConfigurationError("installer paths must be normalized absolute paths without shell metacharacters");
    return value;
}

void ValidateMode(const std::string& mode) {
    if (!std::regex_match(mode, std::regex("0[0-7]{3}")) || (std::stoul(mode, nullptr, 8) & 0133) != 0)
        throw ConfigurationError("installer target files must not be executable or writable by group/others");
}

std::string JoinCommand(const std::vector<std::string>& arguments) {
    std::string result;
    for (const auto& argument : arguments) result += (result.empty() ? "" : " ") + ("'" + argument + "'");
    return result;
}

void AppendTargetFile(InstallerBootstrapPlan& plan, std::set<std::string>& targets,
    const InstallerTargetFile& file, const std::string& payload_name) {
    ReadSafePath(file.path);
    ValidateMode(file.mode);
    if (!targets.insert(file.path).second) throw ConfigurationError("installer target files must be unique");
    if (file.content.find("PRIVATE KEY-----") != std::string::npos)
        throw ConfigurationError("private keys must not be embedded in installer files");
    plan.overlay["files"] += file.mode + " " + file.path + " " + payload_name + "\n";
    plan.overlay[payload_name] = file.content;
}

void PreparePublicKeys(InstallerBootstrapPlan& plan, std::set<std::string>& targets,
    const Json& settings, const InstallerBootstrapContext& context) {
    if (context.authorized_keys.empty() || settings.at("authorized_keys_mode") != "0600")
        throw ConfigurationError("installer bootstrap requires public keys with mode0600");
    std::string keys;
    for (const auto& key : context.authorized_keys) {
        if ((!key.starts_with("ssh-") && !key.starts_with("sk-") && !key.starts_with("ecdsa-")) ||
            key.find_first_of("\r\n") != std::string::npos || key.find(' ') == std::string::npos)
            throw ConfigurationError("only public OpenSSH keys may enter the installer payload");
        keys += key + "\n";
    }
    const auto root_home = ReadSafePath(settings.at("root_home").get<std::string>());
    AppendTargetFile(plan, targets, {root_home + "/.ssh/authorized_keys", "0600", keys}, "authorized_keys");
    AppendTargetFile(plan, targets, {context.user_home + "/.ssh/authorized_keys", "0600", keys}, "authorized_keys");
}

std::string SerializeSshOptions(const Json& options) {
    if (options.at("PermitRootLogin") != "prohibit-password" || options.at("PasswordAuthentication") != "no" ||
        options.at("KbdInteractiveAuthentication") != "no")
        throw ConfigurationError("installer SSH bootstrap must allow root keys and forbid password login");
    std::string content;
    for (const auto& [name, value] : options.items()) {
        if (!value.is_string() || name.empty() || name.find_first_of(" \t\r\n") != std::string::npos ||
            value.get_ref<const std::string&>().find_first_of("\r\n") != std::string::npos)
            throw ConfigurationError("installer SSH options require one directive per entry");
        content += name + " " + value.get<std::string>() + "\n";
    }
    return content;
}

void PrepareTargetFiles(InstallerBootstrapPlan& plan, const Json& settings, const InstallerBootstrapContext& context) {
    std::set<std::string> targets;
    for (const auto& file : context.files)
        AppendTargetFile(plan, targets, file, "file-" + std::to_string(targets.size()));
    const auto& sshd = settings.at("sshd");
    AppendTargetFile(plan, targets, {sshd.at("path"), sshd.at("mode"), SerializeSshOptions(sshd.at("options"))}, "sshd.conf");
    const auto& interfaces = settings.at("ifupdown");
    AppendTargetFile(plan, targets, {interfaces.at("path"), interfaces.at("mode"), interfaces.at("content")}, "interfaces");
    PreparePublicKeys(plan, targets, settings, context);
}

void PrepareRetainedHostKeys(InstallerBootstrapPlan& plan, const InstallerBootstrapContext& context) {
    if (context.host_keys.empty()) throw ConfigurationError("installer must explicitly retain the existing SSH host identity");
    std::set<std::string> paths;
    for (const auto& file : context.host_keys) {
        ReadSafePath(file.path);
        ValidateMode(file.mode);
        if (!file.path.ends_with(".pub") && file.mode != "0600")
            throw ConfigurationError("retained private host keys require mode0600");
        if (!paths.insert(file.path).second) throw ConfigurationError("retained host key paths must be unique");
        plan.overlay["hostkeys"] += file.mode + " " + file.path + "\n";
    }
}

std::string FindSystemDisk(const HostDiskPlan& disks) {
    for (const auto& disk : disks.disks)
        for (const auto& partition : disk.partitions)
            if (partition.mountpoint == "/") return ReadSafePath(disk.device);
    throw ConfigurationError("installer has no resolved system disk");
}

void PrepareNativeServices(InstallerBootstrapPlan& plan, const Json& services) {
    if (!services.is_object()) throw ConfigurationError("installer services must be a native unit/action map");
    for (const auto& [unit, action] : services.items()) {
        if (!std::regex_match(unit, std::regex("[a-zA-Z0-9_@.-]+")) || (action != "enable" && action != "disable"))
            throw ConfigurationError("installer service actions must be enable/disable for explicit unit names");
        plan.overlay["services"] += action.get<std::string>() + " " + unit + "\n";
    }
}

InstallerBootstrapPlan PrepareBootstrap(const HostDiskPlan& disks, const Json& settings,
    const std::string& previous_root, const InstallerBootstrapContext& context) {
    if (!std::regex_match(context.user, std::regex("[a-z_][a-z0-9_-]*")))
        throw ConfigurationError("installer bootstrap requires a regular Linux account name");
    ReadSafePath(context.user_home);
    ReadSafePath(previous_root);
    if (!previous_root.starts_with("/dev/disk/by-uuid/"))
        throw ConfigurationError("installer previous root must use its existing filesystem UUID");
    if (context.retain_script.empty() || context.finish_script.empty())
        throw ConfigurationError("installer bootstrap requires both native callback scripts");
    const auto ram = ReadSafePath(settings.at("ram_directory").get<std::string>());
    if (!ram.starts_with("/run/")) throw ConfigurationError("retained host keys must remain in installer RAM under /run");
    InstallerBootstrapPlan plan;
    plan.payload_directory = ReadSafePath(settings.at("payload_directory").get<std::string>());
    if (ram == plan.payload_directory || ram.starts_with(plan.payload_directory + "/") || plan.payload_directory.starts_with(ram + "/"))
        throw ConfigurationError("installer public payload and private retention RAM must not overlap");
    plan.previous_root = previous_root;
    if (settings.at("previous_root_filesystem") != "ext4")
        throw ConfigurationError("installer identity retention currently requires an ext4 previous root");
    plan.overlay["previous_root_filesystem"] = settings.at("previous_root_filesystem").get<std::string>() + "\n";
    PrepareTargetFiles(plan, settings, context);
    PrepareRetainedHostKeys(plan, context);
    PrepareNativeServices(plan, settings.at("services"));
    plan.overlay["retain.sh"] = context.retain_script;
    plan.overlay["finish.sh"] = context.finish_script;
    plan.early_command = JoinCommand({"/bin/sh", plan.payload_directory + "/retain.sh", ram, previous_root,
        FindSystemDisk(disks), plan.payload_directory});
    plan.late_command = JoinCommand({"/bin/sh", plan.payload_directory + "/finish.sh", ram,
        plan.payload_directory, context.user, context.user_home});
    return plan;
}

}  // namespace

InstallerBootstrapPlan BuildInstallerBootstrapPlan(const HostDiskPlan& disks, const Json& settings,
    const std::string& previous_root, const InstallerBootstrapContext& context) {
    try {
        return PrepareBootstrap(disks, settings, previous_root, context);
    } catch (const Json::exception& error) {
        throw ConfigurationError(std::string("invalid installer bootstrap: ") + error.what());
    }
}

}  // namespace skeleton
