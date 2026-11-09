#include "workloads.hpp"
#include "browser_proxy.hpp"

namespace skeleton {
namespace {

void ValidateForwardService(const SiteDefinition& site, const GatewayService& service) {
    const bool allowed = std::any_of(service.sources.begin(), service.sources.end(), [&](const auto& source) {
        return ParseCidr(source).Contains(ParseIpv4(site.hypervisor_address));
    });
    if (service.protocol != "tcp" || service.scope != "uplink" || !allowed)
        throw ConfigurationError("local access requires an uplink TCP service permitting the hypervisor: " + service.name);
}

std::pair<std::string, Json> BuildForward(const InfrastructurePlan& plan, const GatewayService& service, const Json& entry) {
    const auto& site = plan.topology.definitions.site;
    const auto bind = site.services.at("local_access").at("bind_address").get<std::string>();
    const auto& root = plan.topology.definitions.networks.at(plan.topology.root).router;
    const auto port = entry.value("local_port", static_cast<unsigned>(service.port));
    auto name = service.name;
    if (entry.contains("url_service")) throw ConfigurationError("local URL names must use a declared service alias");
    if (entry.contains("alias")) {
        name = entry.at("alias").get<std::string>();
        if (std::find(service.aliases.begin(), service.aliases.end(), name) == service.aliases.end())
            throw ConfigurationError("local URL alias must belong to the selected service: " + name);
    }
    const auto& fqdn = plan.topology.service_domains.at(name);
    const auto key = entry.contains("alias") ? service.name + "-" + name : name;
    return {key, {{"remote_host", FormatIpv4(plan.addresses.vms.at(root).at("wan").address.address)},
        {"remote_name", fqdn},
        {"remote_url", entry.at("scheme").get<std::string>() + "://" + fqdn + ":" +
            std::to_string(service.port) + entry.value("path", std::string{})},
        {"remote_port", service.port}, {"local_port", port}, {"vm", service.vm},
        {"url", entry.at("scheme").get<std::string>() + "://" + fqdn + ":" + std::to_string(port) + entry.value("path", std::string{})},
        {"hosts_entry", bind + " " + fqdn}}};
}

}  // namespace

Json BuildLocalAccessPlan(const InfrastructurePlan& plan) {
    const auto& topology = plan.topology;
    const auto& site = topology.definitions.site;
    if (!site.services.contains("local_access")) return Json::object();
    const auto& settings = site.services.at("local_access");
    const auto bind = settings.at("bind_address").get<std::string>();
    if (!ParseCidr("127.0.0.0/8").Contains(ParseIpv4(bind)))
        throw ConfigurationError("local forwards must bind to a loopback address");
    Json result{{"ssh_host", site.hypervisor_alias}, {"bind_address", bind}, {"forwards", Json::object()}};
    if (settings.contains("socks_port")) {
        const auto port = settings.at("socks_port").get<unsigned>();
        if (port == 0 || port > 65535)
            throw ConfigurationError("SOCKS port must be within 1..65535");
        result["socks_address"] = bind + ":" + std::to_string(port);
    }
    std::set<unsigned> local_ports;
    for (const auto& entry : settings.at("services")) {
        const auto id = entry.at("service").get<std::string>();
        if (!topology.service_networks.contains(id)) throw ConfigurationError("unknown local access service " + id);
        const auto& service = topology.Service(id);
        ValidateForwardService(site, service);
        auto [name, forward] = BuildForward(plan, service, entry);
        const auto port = forward.at("local_port").get<unsigned>();
        if (port == 0 || port > 65535 || !local_ports.insert(port).second)
            throw ConfigurationError("local access ports must be unique and within 1..65535");
        if (result["forwards"].contains(name))
            throw ConfigurationError("duplicate local publication: " + name);
        result["forwards"][name] = std::move(forward);
    }
    for (const auto& [name, endpoint] : topology.host_services)
        result["forwards"][name] = {{"remote_host", endpoint.address}, {"remote_name", endpoint.fqdn},
            {"remote_port", endpoint.port}, {"remote_url", endpoint.url}};
    if (settings.contains("browser_proxy")) result["browser_proxy"] = BuildBrowserProxyPlan(site, result);
    return result;
}

}  // namespace skeleton
