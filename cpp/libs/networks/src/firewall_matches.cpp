#include "firewall_internal.hpp"

namespace skeleton {

std::string FormatNftSet(const std::vector<std::string>& values) {
    if (values.size() == 1) return values.front();
    std::string result = "{ ";
    for (const auto& value : values) result += (result.size() > 2 ? ", " : "") + value;
    return result + " }";
}

std::string FormatPortMatch(const std::string& protocol, const std::vector<std::uint16_t>& values) {
    if (protocol == "any") return {};
    if (protocol == "icmp") return "ip protocol icmp";
    if (values.empty()) return "meta l4proto " + protocol;
    std::vector<std::string> ports;
    for (const auto port : values) ports.push_back(std::to_string(port));
    return protocol + " dport " + FormatNftSet(ports);
}

std::string RouteRole(const Topology& topology, const std::string& router_network,
                       const std::string& target_network) {
    return !target_network.empty() && topology.IsDescendant(target_network, router_network) ? "lan" : "wan";
}

std::string ParentMacGuard(const Topology& topology, const AddressPlan& plan,
                           const std::string& router_network, const std::string& ingress_role) {
    if (ingress_role != "wan" || router_network == topology.root) return {};
    const auto& parent = topology.definitions.networks.at(topology.parents.at(router_network)).router;
    return "ether saddr " + plan.vms.at(parent).at("lan").mac + " ";
}

std::set<std::string>& SelectFirewallChain(FirewallRules& rules, bool local_source, bool local_target) {
    if (local_source) return rules.output;
    if (local_target) return rules.input;
    return rules.forward;
}

std::string FormatFlowInterfaces(const Topology& topology, const AddressPlan& plan,
                           const std::string& router_network, const std::string& source_network,
                           const std::string& target_network, bool local_source, bool local_target) {
    const auto& site = topology.definitions.site;
    std::string result;
    const auto ingress = RouteRole(topology, router_network, source_network);
    if (!local_source) result += "iifname " + Json(InterfaceName(site, ingress)).dump() + " ";
    if (!local_target)
        result += "oifname " + Json(InterfaceName(site, RouteRole(topology, router_network, target_network))).dump() + " ";
    if (!local_source) result += ParentMacGuard(topology, plan, router_network, ingress);
    return result;
}

std::vector<std::string> InternetExclusions(const SiteDefinition& site) {
    auto exclusions = site.services.at("firewall").at("internet_exclusions").get<std::vector<std::string>>();
    exclusions.push_back(site.ipv4_pool);
    if (site.services.contains("kubernetes")) {
        const auto& network = site.services.at("kubernetes").at("config").at("spec").at("network");
        exclusions.push_back(network.at("podCIDR"));
        exclusions.push_back(network.at("serviceCIDR"));
    }
    const auto uplink = ParseCidr(site.uplink_address);
    exclusions.push_back(Ipv4Cidr{uplink.Network(), uplink.prefix}.ToString());
    std::vector<Ipv4Cidr> ranges;
    for (const auto& value : exclusions) {
        const auto parsed = ParseCidr(value);
        const Ipv4Cidr range{parsed.Network(), parsed.prefix};
        if (std::any_of(ranges.begin(), ranges.end(), [&](const auto& existing) {
                return existing.prefix <= range.prefix && existing.Contains(range.address);
            })) continue;
        std::erase_if(ranges, [&](const auto& existing) {
            return range.prefix <= existing.prefix && range.Contains(existing.address);
        });
        ranges.push_back(range);
    }
    exclusions.clear();
    for (const auto& range : ranges) exclusions.push_back(range.ToString());
    std::sort(exclusions.begin(), exclusions.end());
    return exclusions;
}


std::string FormatInternetDestination(const SiteDefinition& site) {
    return "ip daddr != " + FormatNftSet(InternetExclusions(site));
}

}  // namespace skeleton
