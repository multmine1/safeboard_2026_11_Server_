#include "guest.hpp"

namespace skeleton {

std::optional<std::string> ResolveGuestSshGateway(const SiteDefinition& site) {
    const auto& ssh = site.services.at("ssh");
    if (!ssh.contains("gateway") || !ssh.at("gateway").is_string())
        throw ConfigurationError("services/ssh/gateway must explicitly select direct or hypervisor");
    const auto mode = ssh.at("gateway").get<std::string>();
    if (mode == "hypervisor") return site.hypervisor_alias;
    if (mode == "direct") return std::nullopt;
    throw ConfigurationError("services/ssh/gateway must select direct or hypervisor");
}

Json BuildGuestSshConnection(const SiteDefinition& site, const std::string& id,
                             const std::string& address, const std::string& python,
                             const std::optional<std::string>& gateway) {
    const auto& ssh = site.services.at("ssh");
    Json connection{{"host", address}, {"port", ssh.at("port")},
        {"user", ssh.at("bootstrap_user")}, {"type", ssh.at("transport")}, {"python", python}};
    auto arguments = ssh.value("client_args", std::string{});
    if (!arguments.empty()) arguments += ' ';
    arguments += "-o HostKeyAlias=" + id;
    if (gateway) {
        connection["gateway"] = *gateway;
        arguments += " -o ProxyJump=" + *gateway;
    }
    connection["ssh_args"] = std::move(arguments);
    return connection;
}

}  // namespace skeleton
