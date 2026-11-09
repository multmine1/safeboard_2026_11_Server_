#include "skeleton/planning/infrastructure.hpp"

#include <sstream>
#include <regex>

namespace skeleton {
namespace {

std::string JoinSshValues(const std::set<std::string>& values) {
    std::string result;
    for (const auto& value : values) result += (result.empty() ? "" : " ") + value;
    return result;
}

void AppendSshOptions(std::ostringstream& content, const Json& options, const std::string& indent) {
    for (const auto& [option, value] : options.items()) {
        const auto text = value.get<std::string>();
        if (option.find_first_of(" \t\r\n") != std::string::npos || text.find_first_of("\r\n") != std::string::npos)
            throw ConfigurationError("hypervisor_sshd/options: expected one SSH directive per entry");
        content << indent << option << ' ' << text << '\n';
    }
}

std::string ResolveSshAccount(const Json& host, const std::string& role) {
    if (role != "controller" && role != "libvirt")
        throw ConfigurationError("hypervisor_sshd/accounts: unknown account role " + role);
    const auto user = host.at(role == "controller" ? "controller_user" : "hypervisor_user").get<std::string>();
    if (user.empty() || user.find_first_of(" \t\r\n") != std::string::npos)
        throw ConfigurationError("hypervisor_sshd/accounts: expected a single account name");
    return user;
}

std::set<std::string> ResolveSshForwardTargets(const Json& host, const Json& definition) {
    auto targets = definition.at("forward_targets").get<std::set<std::string>>();
    for (const auto& reference : definition.value("forward_settings", Json::array())) {
        const auto pointer = Json::json_pointer(reference.get<std::string>());
        if (!host.contains(pointer))
            throw ConfigurationError("hypervisor_sshd: unknown forwarding setting " + pointer.to_string());
        targets.insert(host.at(pointer).get<std::string>());
    }
    return targets;
}

Json PrepareSshMatch(const InfrastructurePlan& plan, const std::string& role, const Json& definition) {
    auto options = definition.at("options");
    if (options.contains("AllowUsers") || options.contains("PermitOpen"))
        throw ConfigurationError("hypervisor_sshd/accounts/options: AllowUsers and PermitOpen are generated");
    auto targets = ResolveSshForwardTargets(plan.topology.definitions.site.hypervisor_settings, definition);
    if (role == "libvirt" && !targets.empty())
        throw ConfigurationError("hypervisor_sshd/accounts/libvirt: forwarding targets come from the service plan");
    if (role == "libvirt")
        for (const auto& [id, vm] : plan.vms)
            if (vm.connection.contains("gateway"))
                targets.insert(vm.connection.at("host").get<std::string>() + ":" +
                               std::to_string(vm.connection.at("port").get<unsigned>()));
    if (definition.at("service_forwards").get<bool>() && !plan.local_access.empty())
        for (const auto& entry : plan.local_access.at("forwards")) {
            targets.insert(entry.at("remote_host").get<std::string>() + ":" + std::to_string(entry.at("remote_port").get<unsigned>()));
            targets.insert(entry.at("remote_name").get<std::string>() + ":" + std::to_string(entry.at("remote_port").get<unsigned>()));
        }
    options["PermitOpen"] = targets.empty() ? "none" : JoinSshValues(targets);
    return {{"user", ResolveSshAccount(plan.topology.definitions.site.hypervisor_settings, role)}, {"options", options}};
}

Json PrepareHostKeyBackup(const Json& settings) {
    Json backup;
    for (const auto& field : {"private_file_mode", "public_file_mode"})
        backup[field] = settings.at("backup").at(field);
    backup["files"] = Json::array();
    for (const auto& value : settings.at("host_keys")) {
        const auto key = value.get<std::string>();
        if (!std::filesystem::path(key).is_absolute())
            throw ConfigurationError("hypervisor_sshd/host_keys: expected an absolute host path");
        backup["files"].push_back({{"path", key}, {"mode", backup.at("private_file_mode")}});
        backup["files"].push_back({{"path", key + ".pub"}, {"mode", backup.at("public_file_mode")}});
    }
    return backup;
}

void ValidateSshMigrationPaths(const Json& settings) {
    const auto path = std::filesystem::path(settings.at("path").get<std::string>()).lexically_normal();
    if (!path.is_absolute() || path.extension() != ".conf")
        throw ConfigurationError("hypervisor_sshd/path: expected an absolute .conf path");
    for (const auto& value : settings.at("obsolete_paths")) {
        const auto old = std::filesystem::path(value.get<std::string>()).lexically_normal();
        if (old == path || old.parent_path() != path.parent_path() || old.extension() != ".conf")
            throw ConfigurationError("hypervisor_sshd/obsolete_paths: expected another .conf in the managed directory");
    }
}

void AppendControllerPublicKeys(Json& keys, std::set<std::string>& identities, const Json& values) {
    if (!values.is_array()) throw ConfigurationError("controller authorized keys must be an array");
    for (const auto& value : values) {
        if (!value.is_string()) throw ConfigurationError("controller authorized keys must be public key strings");
        const auto text = value.get<std::string>();
        if (text.find_first_of("\r\n") != std::string::npos || !std::regex_match(text,
            std::regex(R"((ssh-ed25519|ssh-rsa|ecdsa-sha2-nistp(256|384|521)|sk-ssh-ed25519@openssh.com|sk-ecdsa-sha2-nistp256@openssh.com) [^ \t]+( .*)?)")))
            throw ConfigurationError("controller authorized keys require one OpenSSH public key per entry");
        std::istringstream fields(text);
        std::string type, blob;
        fields >> type >> blob;
        if (identities.insert(type + " " + blob).second) keys.push_back(text);
    }
}

Json PrepareControllerPublicKeys(const SiteDefinition& site) {
    Json keys = Json::array();
    std::set<std::string> identities;
    const auto user = site.hypervisor_settings.at("controller_user");
    for (const auto& account : site.services.at("ssh").at("users"))
        if (account.at("name") == user)
            AppendControllerPublicKeys(keys, identities, account.at("authorized_keys"));
    const auto& controller = site.hypervisor_settings.at("hypervisor_sshd").at("accounts").at("controller");
    if (controller.contains("additional_authorized_keys"))
        AppendControllerPublicKeys(keys, identities, controller.at("additional_authorized_keys"));
    return keys.empty() ? Json::array() : Json::array({{{"user", user}, {"keys", keys}}});
}

}  // namespace

Json BuildHypervisorSshPlan(const InfrastructurePlan& plan) {
    const auto& settings = plan.topology.definitions.site.hypervisor_settings.at("hypervisor_sshd");
    ValidateSshMigrationPaths(settings);
    Json result;
    for (const auto& field : {"path", "file_mode", "owner", "group", "obsolete_paths", "global_options", "host_keys"})
        result[field] = settings.at(field);
    if (settings.at("global_options").contains("AllowUsers"))
        throw ConfigurationError("hypervisor_sshd/global_options: AllowUsers is generated from the declared accounts");
    if (settings.at("accounts").size() != 2 || !settings.at("accounts").contains("controller") || !settings.at("accounts").contains("libvirt"))
        throw ConfigurationError("hypervisor_sshd/accounts: controller and libvirt are required");
    result["matches"] = Json::array();
    std::set<std::string> users;
    for (const auto& [role, definition] : settings.at("accounts").items()) {
        auto match = PrepareSshMatch(plan, role, definition);
        if (!users.insert(match.at("user").get<std::string>()).second)
            throw ConfigurationError("hypervisor_sshd/accounts: controller and libvirt must be different users");
        result["matches"].push_back(std::move(match));
    }
    result["global_options"]["AllowUsers"] = JoinSshValues(users);
    std::ostringstream content;
    AppendSshOptions(content, result.at("global_options"), "");
    for (const auto& path : settings.at("host_keys")) content << "HostKey " << path.get<std::string>() << '\n';
    for (const auto& match : result.at("matches")) {
        content << "Match User " << match.at("user").get<std::string>() << '\n';
        AppendSshOptions(content, match.at("options"), "    ");
    }
    content << "Match all\n";
    result["content"] = content.str();
    result["backup"] = PrepareHostKeyBackup(settings);
    result["authorized_keys"] = PrepareControllerPublicKeys(plan.topology.definitions.site);
    return result;
}

}  // namespace skeleton
