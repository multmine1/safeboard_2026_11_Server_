#include "skeleton/networks/configuration.hpp"
#include "skeleton/networks/connections.hpp"

#include <sstream>

namespace skeleton {
namespace {

std::string Join(const std::vector<std::string>& values) {
    std::string joined;
    for (const auto& value : values) joined += (joined.empty() ? "" : " ") + value;
    return joined;
}

std::string GenerateZone(const Topology& topology, const AddressPlan& plan,
                          const std::string& network_id) {
    const auto& network = topology.definitions.networks.at(network_id);
    const auto& router = topology.definitions.vms.at(network.router);
    const auto& dns = topology.definitions.site.services.at("dns");
    const auto& domain = topology.network_domains.at(network_id);
    const auto& router_domain = topology.vm_domains.at(network.router);
    std::map<std::string, std::string> records;
    records["@"] = records[router.name] = FormatIpv4(plan.vms.at(network.router).at("lan").address.address);
    for (const auto& member : network.members)
        if (!topology.IsRouter(member))
            records[topology.definitions.vms.at(member).name] = FormatIpv4(plan.vms.at(member).at("lan").address.address);
    std::string data;
    for (const auto& [name, address] : records) data += name + " IN A " + address + "\n";
    const auto serial = static_cast<std::uint32_t>(StableHash(domain + data + dns.dump()));
    std::ostringstream zone;
    zone << "$ORIGIN " << domain << ".\n$TTL " << dns.at("ttl") << "\n"
         << "@ IN SOA " << router_domain << ". " << dns.at("soa_mailbox").get<std::string>()
         << " (" << serial << ' ' << dns.at("soa_refresh") << ' ' << dns.at("soa_retry")
         << ' ' << dns.at("soa_expire") << ' ' << dns.at("soa_minimum") << ")\n"
         << "@ IN NS " << router_domain << ".\n" << data;
    return zone.str();
}

std::string DnsBlock(const std::string& domain, const std::string& binding,
                     const std::string& directive, unsigned port) {
    return domain + ":" + std::to_string(port) + " {\n    bind " + binding +
           "\n    " + directive + "\n}\n";
}

void AddPublishedHypervisorNames(const Topology& topology, const AddressPlan& plan,
                                 std::map<std::string, std::string>& hosts) {
    const auto hypervisor = ParseIpv4(topology.definitions.site.hypervisor_address);
    const auto& router = topology.definitions.networks.at(topology.root).router;
    const auto uplink = FormatIpv4(plan.vms.at(router).at("wan").address.address);
    for (const auto& [id, network] : topology.definitions.networks)
        for (const auto& service : network.exports) {
            if (service.scope != "uplink") continue;
            const bool allowed = std::any_of(service.sources.begin(), service.sources.end(), [&](const auto& source) {
                return ParseCidr(source).Contains(hypervisor);
            });
            if (allowed)
                for (const auto& name : service.Names()) hosts[topology.service_domains.at(name)] = uplink;
        }
}

}  // namespace

DnsConfiguration GenerateCoreDnsConfiguration(const Topology& topology, const AddressPlan& plan,
                                              const std::string& router_id) {
    return GenerateCoreDnsConfiguration(topology, plan, ResolveConnections(topology, plan), router_id);
}

DnsConfiguration GenerateCoreDnsConfiguration(const Topology& topology, const AddressPlan& plan,
    const std::vector<ConnectionPlan>& connections, const std::string& router_id) {
    const auto& site = topology.definitions.site;
    const auto& dns = site.services.at("dns");
    const auto port = dns.at("port").get<unsigned>();
    const auto& network_id = topology.router_networks.at(router_id);
    const auto& domain = topology.network_domains.at(network_id);
    const auto directory = dns.at("zones_directory").get<std::string>();
    const auto path = (std::filesystem::path(directory) / (domain + ".zone")).string();
    std::vector<std::string> bindings{site.services.at("dns_resolver").at("local_address")};
    for (const auto& [role, nic] : plan.vms.at(router_id)) bindings.push_back(FormatIpv4(nic.address.address));
    const auto binding = Join(bindings);
    DnsConfiguration result;
    result.bindings = bindings;
    result.zones.emplace(path, GenerateZone(topology, plan, network_id));
    result.corefile = DnsBlock(domain, binding, "file " + Json(path).dump(), port);
    for (const auto& [child, parent] : topology.parents) {
        if (parent != network_id) continue;
        const auto& child_router = topology.definitions.networks.at(child).router;
        const auto server = FormatIpv4(plan.vms.at(child_router).at("wan").address.address);
        result.corefile += DnsBlock(topology.network_domains.at(child), binding,
                                    "forward . " + server + ":" + std::to_string(port), port);
    }
    auto upstreams = site.dns_upstreams;
    const auto& network = topology.definitions.networks.at(network_id);
    if (!network.dns_upstreams.empty()) {
        upstreams = network.dns_upstreams;
        for (const auto& upstream : upstreams) ParseIpv4(upstream);
    } else if (network_id != topology.root) {
        const auto& parent = topology.definitions.networks.at(topology.parents.at(network_id)).router;
        upstreams = {FormatIpv4(plan.vms.at(parent).at("lan").address.address)};
    }
    std::map<std::string, std::string> service_hosts;
    for (const auto& flow : connections) {
        if (flow.source_network != network_id || flow.service_name.empty()) continue;
        const auto domain = flow.to == "hypervisor" ? topology.host_services.at(flow.service_name).fqdn
                                                   : topology.service_domains.at(flow.service_name);
        service_hosts[domain] = flow.frontend_address;
        if (flow.to != "hypervisor")
            for (const auto& name : topology.ServiceNamed(flow.service_name).aliases)
                service_hosts[topology.service_domains.at(name)] = flow.frontend_address;
    }
    if (network_id == topology.root) {
        AddPublishedHypervisorNames(topology, plan, service_hosts);
        for (const auto& [name, endpoint] : topology.host_services)
            service_hosts[endpoint.fqdn] = endpoint.address;
    }
    for (const auto& [name, address] : service_hosts)
        result.corefile += DnsBlock(name, binding, "hosts {\n        " + address + " " + name + "\n    }", port);
    result.corefile += DnsBlock(".", binding, "forward . " + Join(upstreams), port);
    return result;
}

std::string BuildClusterCorefile(const Topology& topology, const KubernetesPlan& cluster,
    const DnsConfiguration& dns, const std::string& network) {
    const auto& services = topology.definitions.site.services;
    const auto& settings = cluster.settings.at("router_runtime");
    const auto domain = cluster.configuration.at("spec").at("network").at("clusterDomain").get<std::string>();
    auto namespaces = settings.at("shared_namespaces").get<std::vector<std::string>>();
    if (network == topology.root)
        for (const auto& [id, name] : cluster.namespaces) namespaces.push_back(name);
    else namespaces.push_back(cluster.namespaces.at(network));
    const auto localhost = services.at("dns_resolver").at("local_address").get<std::string>();
    return dns.corefile + domain + ":" + std::to_string(services.at("dns").at("port").get<unsigned>()) + " {\n    bind " +
        Join(dns.bindings) + "\n    kubernetes " + domain + " {\n        namespaces " + Join(namespaces) +
        "\n    }\n    health " + localhost + ":" + std::to_string(settings.at("health_port").get<unsigned>()) +
        "\n    ready " + localhost + ":" + std::to_string(settings.at("ready_port").get<unsigned>()) + "\n}\n";
}

}  // namespace skeleton
