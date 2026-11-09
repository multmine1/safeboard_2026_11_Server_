#include "skeleton/networks/configuration.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace skeleton {
namespace {

Json GenerateWakePacket(const Json& settings, const Ipv4Cidr& address) {
    auto mac = settings.at("ethernet").at("match").at("macaddress").get<std::string>();
    if (!std::regex_match(mac, std::regex("([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}")))
        throw ConfigurationError("Wake-on-LAN requires a six-byte Ethernet MAC address");
    std::erase(mac, ':');
    std::transform(mac.begin(), mac.end(), mac.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto& port = settings.at("wake_port");
    if (!port.is_number_integer() || port.get<std::int64_t>() < 1 || port.get<std::int64_t>() > 65535)
        throw ConfigurationError("Wake-on-LAN port must be an integer in 1..65535");
    std::string packet(12, 'f');
    for (unsigned repeat = 0; repeat < 16; ++repeat) packet += mac;
    return {{"packet_hex", packet}, {"port", port},
        {"address", FormatIpv4(static_cast<std::uint32_t>(address.Network() + address.Size() - 1))}};
}

}  // namespace

Json GenerateHypervisorHostNetwork(const SiteDefinition& site) {
    const auto& settings = site.hypervisor_settings.at("hypervisor_network");
    const auto ethernet = settings.at("ethernet");
    const auto name = ethernet.at("set-name").get<std::string>();
    const auto address = ParseCidr(site.hypervisor_address + "/" + std::to_string(settings.at("prefix").get<unsigned>()));
    const auto gateway = ParseIpv4(settings.at("gateway"));
    if (!address.Contains(gateway)) throw ConfigurationError("hypervisor gateway must belong to its physical subnet");
    auto bridge = settings.at("bridge_settings");
    bridge["interfaces"] = Json::array({name});
    bridge["addresses"] = Json::array({address.ToString()});
    bridge["routes"] = Json::array({{{"to", "default"}, {"via", FormatIpv4(gateway)}, {"metric", settings.at("route_metric")}}});
    const Json netplan{{"network", {{"version", 2}, {"renderer", settings.at("renderer")},
        {"ethernets", {{name, ethernet}}}, {"bridges", {{settings.at("bridge").get<std::string>(), bridge}}}}}};
    Json result{{"netplan", netplan}};
    if (ethernet.at("wakeonlan").get<bool>()) result["wake"] = GenerateWakePacket(settings, address);
    return result;
}

}  // namespace skeleton
