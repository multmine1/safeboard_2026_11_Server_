#include "readers.hpp"

#include <fstream>
#include <limits>
#include <regex>
#include <sstream>

namespace skeleton::input {

std::vector<std::uint16_t> ReadPorts(const Json& value, const std::string& context) {
    if (!value.contains("ports")) return {};
    const auto& array = value.at("ports");
    if (!array.is_array() || array.empty()) Fail(context + "/ports", "expected a nonempty array");
    std::set<std::uint16_t> ports;
    for (const auto& port : array) {
        const auto number = ReadPositive(Json{{"port", port}}, "port", context, 65535);
        ports.insert(static_cast<std::uint16_t>(number));
    }
    return {ports.begin(), ports.end()};
}

AccessRule ReadAccessRule(const Json& value, const std::string& context, bool require_source) {
    CheckFields(value, {"from", "to", "protocol", "ports"}, context);
    if (!require_source && value.contains("from")) Fail(context + "/from", "common rules apply to every VM");
    AccessRule rule{require_source ? ReadUuid(value, "from", context) : "", ReadString(value, "to", context),
                    value.contains("protocol") ? ReadString(value, "protocol", context) : "", ReadPorts(value, context)};
    if (rule.to.starts_with("hypervisor:")) {
        if (rule.to.size() == std::string("hypervisor:").size() || !rule.protocol.empty() || !rule.ports.empty())
            Fail(context, "named hypervisor access derives protocol and ports from its service");
    } else if (rule.to != "internet" && rule.to != "hypervisor") rule.to = ReadUuid(value, "to", context);
    if (rule.to == "hypervisor" && (rule.protocol.empty() || rule.ports.empty()))
        Fail(context, "hypervisor access requires an explicit protocol and ports");
    if (!require_source && (rule.to != "internet" || rule.protocol.empty()))
        Fail(context, "common egress requires internet and an explicit protocol");
    if (!rule.protocol.empty() && rule.protocol != "tcp" && rule.protocol != "udp" && rule.protocol != "icmp")
        Fail(context + "/protocol", "expected tcp, udp or icmp");
    if (rule.protocol == "icmp" && !rule.ports.empty())
        Fail(context + "/ports", "ICMP has no ports");
    return rule;
}

GatewayService ReadGatewayService(const Json& value, const std::string& context) {
    CheckFields(value, {"id", "name", "vm", "protocol", "port", "target_port", "scope", "sources", "aliases"}, context);
    GatewayService service{ReadUuid(value, "id", context), ReadName(value, "name", context),
        ReadUuid(value, "vm", context), ReadString(value, "protocol", context),
        static_cast<std::uint16_t>(ReadPositive(value, "port", context, 65535)), {},
        ReadString(value, "scope", context), {}};
    service.target_port = value.contains("target_port")
        ? ReadPositive(value, "target_port", context, 65535) : service.port;
    if (service.protocol != "tcp" && service.protocol != "udp") Fail(context, "exports require TCP or UDP");
    if (service.scope != "internal" && service.scope != "uplink") Fail(context, "scope must be internal or uplink");
    if (service.scope == "uplink") service.sources = ReadStringArray(value, "sources", context);
    if (service.scope == "uplink" && service.sources.empty()) Fail(context, "uplink sources must be explicit");
    if (value.contains("aliases"))
        for (const auto& alias : ReadStringArray(value, "aliases", context))
            service.aliases.push_back(ReadName(Json{{"name", alias}}, "name", context + "/aliases"));
    return service;
}

NetworkDefinition ReadNetwork(const Json& value, const std::filesystem::path& path) {
    const auto context = path.string();
    CheckFields(value, {"id", "name", "router", "members", "allow", "type", "exports", "dns_upstreams"}, context);
    if (value.contains("type") && ReadString(value, "type", context) != "isolated")
        Fail(context + "/type", "only isolated networks are supported");
    NetworkDefinition network{ReadUuid(value, "id", context), ReadName(value, "name", context),
                              ReadUuid(value, "router", context), {}, {}, path};
    const auto members = ReadStringArray(value, "members", context);
    for (const auto& member : members)
        network.members.push_back(ReadUuid(Json{{"id", member}}, "id", context + "/members"));
    std::sort(network.members.begin(), network.members.end());
    if (std::adjacent_find(network.members.begin(), network.members.end()) != network.members.end())
        Fail(context + "/members", "duplicate member");
    const auto rules = value.value("allow", Json::array());
    if (!rules.is_array()) Fail(context + "/allow", "expected an array");
    for (std::size_t index = 0; index < rules.size(); ++index)
        network.allow.push_back(ReadAccessRule(rules[index], context + "/allow/" + std::to_string(index), true));
    const auto exports = value.value("exports", Json::array());
    if (!exports.is_array()) Fail(context + "/exports", "expected an array");
    for (std::size_t index = 0; index < exports.size(); ++index)
        network.exports.push_back(ReadGatewayService(exports[index], context + "/exports/" + std::to_string(index)));
    if (value.contains("dns_upstreams")) {
        network.dns_upstreams = ReadStringArray(value, "dns_upstreams", context);
        if (network.dns_upstreams.empty()) Fail(context + "/dns_upstreams", "at least one upstream is required");
    }
    return network;
}

}  // namespace skeleton::input
