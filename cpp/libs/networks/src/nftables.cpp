#include "firewall_internal.hpp"

#include <sstream>

namespace skeleton {
namespace {

void WriteChain(std::ostringstream& output, const std::string& name, const std::string& policy,
                 const std::set<std::string>& rules, const std::set<std::string>& guards) {
    output << "    chain " << name << " {\n        type filter hook " << name
           << " priority 0; policy " << policy << ";\n";
    if (name != "output")
        for (const auto& guard : guards) output << "        " << guard << '\n';
    output << "        ct state established,related accept\n";
    if (name == "input") output << "        iifname \"lo\" accept\n";
    if (name == "output") output << "        oifname \"lo\" accept\n";
    for (const auto& rule : rules) output << "        " << rule << '\n';
    output << "    }\n";
}

void WriteNatTable(std::ostringstream& output, const SiteDefinition& site,
                   const std::string& table, const FirewallRules& rules) {
    if (rules.masquerade || !rules.prerouting.empty() || !rules.postrouting.empty()) {
        output << "table ip " << table << "_nat {\n    chain prerouting {\n"
               << "        type nat hook prerouting priority dstnat; policy accept;\n";
        for (const auto& rule : rules.prerouting) output << "        " << rule << '\n';
        output << "    }\n    chain postrouting {\n        type nat hook postrouting priority srcnat; policy accept;\n";
        for (const auto& rule : rules.postrouting) output << "        " << rule << '\n';
        if (rules.masquerade)
            output << "        oifname " << Json(InterfaceName(site, "wan")).dump()
                   << " ip saddr " << site.ipv4_pool << ' ' << FormatInternetDestination(site) << " masquerade\n";
        output << "    }\n}\n";
    }
}

}  // namespace

std::string GenerateNftablesConfiguration(const Topology& topology, const AddressPlan&,
                                         const std::string& vm_id, const FirewallRules& rules) {
    const auto& site = topology.definitions.site;
    const auto& firewall = site.services.at("firewall");
    const auto table = firewall.at("table").get<std::string>();
    auto policy = firewall.at("policy");
    const auto& profile = site.profiles.at(topology.definitions.vms.at(vm_id).type);
    if (!topology.IsRouter(vm_id) && profile.contains("firewall"))
        policy.merge_patch(profile.at("firewall").at("policy"));
    std::ostringstream output;
    output << "destroy table inet " << table << "\ndestroy table ip " << table << "_nat\n"
           << "table inet " << table << " {\n";
    WriteChain(output, "input", policy.at("input"), rules.input, rules.ingress_guards);
    WriteChain(output, "forward", policy.at("forward"), rules.forward, rules.ingress_guards);
    WriteChain(output, "output", policy.at("output"), rules.output, rules.ingress_guards);
    output << "}\n";
    WriteNatTable(output, site, table, rules);
    return output.str();
}

}  // namespace skeleton
