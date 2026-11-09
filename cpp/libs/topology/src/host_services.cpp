#include "skeleton/topology/model.hpp"

#include <arpa/inet.h>
#include <regex>

namespace skeleton {
namespace {

std::string ResolveBindAddress(const Topology& topology, const Json& definition) {
    const auto& host = topology.definitions.site.hypervisor_address;
    if (!definition.contains("bind_address")) return host;
    const auto address = definition.at("bind_address").get<std::string>();
    in_addr parsed{};
    if (inet_pton(AF_INET, address.c_str(), &parsed) != 1 ||
        (address != host && (ntohl(parsed.s_addr) >> 24) != 127))
        throw ConfigurationError("hypervisor service must bind to the declared host address or IPv4 loopback");
    return address;
}

std::string ResolveDnsZone(const Topology& topology, const Json& definition) {
    if (!definition.contains("dns_zone")) return topology.network_domains.at(topology.root);
    const auto zone = definition.at("dns_zone").get<std::string>();
    if (zone == "site") return topology.definitions.site.domain;
    if (zone == "root") return topology.network_domains.at(topology.root);
    throw ConfigurationError("unknown hypervisor service DNS zone: " + zone);
}

HostServiceEndpoint BuildHostEndpoint(const Topology& topology, const std::string& name, const Json& definition) {
    if (!std::regex_match(name, std::regex("[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?")))
        throw ConfigurationError("hypervisor service name must be a DNS label: " + name);
    const auto port = definition.at("port").get<unsigned>();
    const auto scheme = definition.at("scheme").get<std::string>();
    const auto path = definition.at("path").get<std::string>();
    if (port == 0 || port > 65535 || (scheme != "http" && scheme != "https") || !path.starts_with('/'))
        throw ConfigurationError("hypervisor service requires a TCP port, HTTP(S) scheme and absolute URL path: " + name);
    const auto address = ResolveBindAddress(topology, definition);
    const auto fqdn = name + "." + ResolveDnsZone(topology, definition);
    if (fqdn.size() > 253) throw ConfigurationError("hypervisor service DNS name exceeds 253 characters");
    return {address, fqdn, address + ":" + std::to_string(port),
        scheme + "://" + fqdn + ":" + std::to_string(port) + path, static_cast<std::uint16_t>(port)};
}

}  // namespace

void IndexHypervisorServices(Topology& topology) {
    const auto& settings = topology.definitions.site.hypervisor_settings;
    if (!settings.contains("hypervisor_services")) return;
    std::set<std::string> domains;
    for (const auto& [id, domain] : topology.network_domains) domains.insert(domain);
    for (const auto& [id, domain] : topology.vm_domains) domains.insert(domain);
    for (const auto& [id, domain] : topology.service_domains) domains.insert(domain);
    std::set<std::string> sockets;
    for (const auto& [name, definition] : settings.at("hypervisor_services").items()) {
        auto endpoint = BuildHostEndpoint(topology, name, definition);
        if (!domains.insert(endpoint.fqdn).second || !sockets.insert(endpoint.socket).second)
            throw ConfigurationError("conflicting hypervisor service name or listening socket: " + name);
        topology.host_services.emplace(name, std::move(endpoint));
    }
}

}  // namespace skeleton
