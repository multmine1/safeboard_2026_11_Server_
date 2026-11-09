#include "hypervisor_proxy.hpp"

#include <regex>

namespace skeleton {
namespace {

std::filesystem::path ProxyPath(const Json& value) {
    const auto path = std::filesystem::path(value.get<std::string>());
    if (!path.is_absolute() || std::find(path.begin(), path.end(), "..") != path.end())
        throw ConfigurationError("socket proxy paths must be absolute without parent traversal");
    return path;
}

const GatewayService& ResolveProxyBackend(const InfrastructurePlan& plan, const std::string& host_service, const Json& reference) {
    const auto& topology = plan.topology;
    if (!topology.host_services.contains(host_service) ||
        !ParseCidr("127.0.0.0/8").Contains(ParseIpv4(topology.host_services.at(host_service).address)))
        throw ConfigurationError("private socket proxy requires a declared loopback host service: " + host_service);
    const auto& service = topology.Service(reference.at("service").get<std::string>());
    const std::vector<std::string> allowed{topology.definitions.site.hypervisor_address + "/32"};
    if (service.protocol != "tcp" || service.scope != "uplink" || service.sources != allowed)
        throw ConfigurationError("socket proxy backend must be an uplink TCP service permitting only the hypervisor");
    return service;
}

Json BuildSocketProxy(const InfrastructurePlan& plan, const std::string& name, const Json& reference, const Json& settings) {
    const auto& backend = ResolveProxyBackend(plan, name, reference);
    const auto prefix = settings.at("unit_prefix").get<std::string>();
    if (!std::regex_match(prefix, std::regex("[A-Za-z0-9_-]+")))
        throw ConfigurationError("socket proxy unit prefix must be a simple unit name prefix");
    const auto unit = prefix + name;
    const auto directory = ProxyPath(settings.at("unit_directory"));
    const auto binary = ProxyPath(settings.at("binary"));
    auto socket = settings.at("socket"), service = settings.at("service");
    if (socket.value("Socket", Json::object()).contains("ListenStream") ||
        service.value("Service", Json::object()).contains("ExecStart"))
        throw ConfigurationError("socket proxy endpoints are calculated and must not be overridden");
    const auto& root = plan.topology.definitions.networks.at(plan.topology.root).router;
    const auto target = FormatIpv4(plan.addresses.vms.at(root).at("wan").address.address) + ":" + std::to_string(backend.port);
    socket["Socket"]["ListenStream"] = plan.topology.host_services.at(name).socket;
    service["Service"]["ExecStart"] = Json(binary.string()).dump() + " " + target;
    return {{"name", name}, {"socket_unit", unit + ".socket"}, {"service_unit", unit + ".service"},
        {"socket_path", (directory / (unit + ".socket")).string()},
        {"service_path", (directory / (unit + ".service")).string()}, {"socket", socket}, {"service", service}};
}

}  // namespace

void PrepareHypervisorProxies(InfrastructurePlan& plan) {
    const auto& host = plan.topology.definitions.site.hypervisor_settings;
    if (!host.contains("hypervisor_proxies")) return;
    auto proxies = Json::array();
    for (const auto& [name, reference] : host.at("hypervisor_proxies").items())
        proxies.push_back(BuildSocketProxy(plan, name, reference, host.at("hypervisor_proxy")));
    plan.hypervisor["bootstrap"]["hypervisor_socket_proxies"] = std::move(proxies);
}

}  // namespace skeleton
