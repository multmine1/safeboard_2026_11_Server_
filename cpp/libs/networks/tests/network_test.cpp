#include <gtest/gtest.h>

#include "fixture.hpp"
#include "skeleton/networks/configuration.hpp"
#include "skeleton/networks/connections.hpp"

namespace skeleton::test {
namespace {

bool ContainsRule(const std::set<std::string>& rules, const std::string& text) {
    return std::any_of(rules.begin(), rules.end(), [&](const auto& rule) {
        return rule.find(text) != std::string::npos;
    });
}

}  // namespace

TEST(Connections, PreservesEveryProtocolAndMergesPortsWithoutDependingOnRuleOrder) {
    auto definitions = Definitions();
    definitions.networks.at(kChild).allow.push_back({kApi, kDb, "tcp", {22}});
    definitions.networks.at(kChild).allow.push_back({kApi, kDb, "udp", {53}});
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    auto flows = ResolveConnections(topology, addresses);

    const auto connections = GenerateClientConnections(topology, flows, kApi);

    const auto& peer = connections.at(topology.vm_domains.at(kDb));
    EXPECT_EQ(peer.at("endpoints").at("tcp").at("ports"), Json({22, 5432}));
    EXPECT_EQ(peer.at("endpoints").at("udp").at("port"), 53);
    EXPECT_FALSE(peer.contains("port"));
    EXPECT_FALSE(peer.contains("protocol"));
    std::reverse(flows.begin(), flows.end());
    EXPECT_EQ(GenerateClientConnections(topology, flows, kApi), connections);
    flows.push_back(ResolveConnection(topology, addresses, {kApi, kDb, "tcp", {}}));
    EXPECT_TRUE(GenerateClientConnections(topology, flows, kApi)
        .at(topology.vm_domains.at(kDb)).at("endpoints").at("tcp").at("ports").empty());
}

TEST(Policies, CompilesDirectedAccessAcrossEveryRouterOnThePath) {
    const auto topology = BuildTopology(Definitions());
    const auto addresses = AllocateAddressPlan(topology);
    const auto source = FormatIpv4(addresses.vms.at(kApi).at("lan").address.address);
    const auto destination = FormatIpv4(addresses.vms.at(kDb).at("lan").address.address);

    const auto rules = CompileFirewallRules(topology, addresses);

    for (const auto& id : {kEdge, kGate}) {
        EXPECT_TRUE(ContainsRule(rules.at(id).forward,
            "ip saddr " + source + " ip daddr " + destination + " ct state new tcp dport 5432"));
        EXPECT_FALSE(ContainsRule(rules.at(id).forward,
            "ip saddr " + destination + " ip daddr " + source + " ct state new"));
        const auto text = GenerateNftablesConfiguration(topology, addresses, id, rules.at(id));
        EXPECT_NE(text.find("ct state established,related accept"), std::string::npos);
        EXPECT_NE(text.find("policy drop"), std::string::npos);
        EXPECT_TRUE(text.starts_with("destroy table inet infrastructure\ndestroy table ip infrastructure_nat\n"));
    }
}

TEST(Policies, AddsMasqueradeOnlyAtTheRootForExplicitInternetAccess) {
    auto definitions = Definitions();
    definitions.networks.at(kChild).allow.push_back({kApi, "internet", "tcp", {443}});
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);

    const auto rules = CompileFirewallRules(topology, addresses);

    EXPECT_TRUE(rules.at(kEdge).masquerade);
    EXPECT_FALSE(rules.at(kGate).masquerade);
    EXPECT_TRUE(ContainsRule(rules.at(kGate).forward, "tcp dport 443"));
    EXPECT_FALSE(ContainsRule(rules.at(kGate).forward, "ip daddr 192.0.2.0/24"));
    const auto root = GenerateNftablesConfiguration(topology, addresses, kEdge, rules.at(kEdge));
    EXPECT_NE(root.find("masquerade"), std::string::npos);
    EXPECT_NE(root.find("ip daddr !="), std::string::npos);
}

TEST(Policies, RoutesSameSubnetAccessThroughItsRouter) {
    auto definitions = Definitions();
    definitions.networks.at(kChild).members.clear();
    definitions.networks.at(kRoot).members.push_back(kApi);
    definitions.networks.at(kRoot).allow = std::move(definitions.networks.at(kChild).allow);
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);

    const auto rules = CompileFirewallRules(topology, addresses);

    EXPECT_TRUE(ContainsRule(rules.at(kEdge).forward,
                            "iifname \"lan0\" oifname \"lan0\""));
    EXPECT_FALSE(ContainsRule(rules.at(kGate).forward, "5432"));
}

TEST(NetworkConfig, GeneratesGatewayAndChildRoutesWithoutHypervisorForwarding) {
    const auto topology = BuildTopology(Definitions());
    const auto addresses = AllocateAddressPlan(topology);
    const auto gateway = FormatIpv4(addresses.vms.at(kGate).at("lan").address.address);

    const auto api = GenerateNetplan(topology, addresses, kApi).at("network").at("ethernets").at("lan0");
    const auto root = GenerateNetplan(topology, addresses, kEdge).at("network").at("ethernets").at("lan0");

    EXPECT_EQ(api.at("routes").at(0).at("to"), gateway + "/32");
    EXPECT_EQ(api.at("routes").at(1).at("via"), gateway);
    ASSERT_EQ(root.at("routes").size(), 1);
    EXPECT_EQ(root.at("routes").at(0).at("to"), addresses.subnets.at(kChild).ToString());
    EXPECT_EQ(root.at("routes").at(0).at("via"),
              FormatIpv4(addresses.vms.at(kGate).at("wan").address.address));
}

TEST(Dns, GeneratesRouterAliasesChildForwardingAndHypervisorAccess) {
    const auto topology = BuildTopology(Definitions());
    const auto addresses = AllocateAddressPlan(topology);
    const auto child_dns = FormatIpv4(addresses.vms.at(kGate).at("wan").address.address);

    const auto root = GenerateCoreDnsConfiguration(topology, addresses, kEdge);
    const auto child = GenerateCoreDnsConfiguration(topology, addresses, kGate);
    const auto hypervisor = GenerateHypervisorConfiguration(topology, addresses);
    const auto rules = CompileFirewallRules(topology, addresses);

    EXPECT_NE(root.corefile.find("apps.infra.example.test:53"), std::string::npos);
    EXPECT_NE(root.corefile.find("forward . " + child_dns), std::string::npos);
    EXPECT_NE(root.zones.begin()->second.find("@ IN A "), std::string::npos);
    EXPECT_NE(root.zones.begin()->second.find("edge IN A "), std::string::npos);
    EXPECT_NE(child.corefile.find("forward . " +
        FormatIpv4(addresses.vms.at(kEdge).at("lan").address.address)), std::string::npos);
    EXPECT_EQ(hypervisor.at("resolver").at("servers"), Json::array({"192.0.2.10"}));
    EXPECT_EQ(hypervisor.at("routes").at(0).at("via"), "192.0.2.10");
    EXPECT_EQ(hypervisor.at("activate_after"), kEdge);
    EXPECT_TRUE(ContainsRule(rules.at(kEdge).input, "ip saddr 192.0.2.2 udp dport 53"));
    EXPECT_TRUE(ContainsRule(rules.at(kEdge).input, "ip saddr 192.0.2.2 tcp dport 53"));
}

TEST(Dns, PublishesDeclaredPhysicalHostServicesOnlyAtTheRootAndRejectsNameConflicts) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings["hypervisor_services"] = {
        {"cockpit", {{"port", 443}, {"scheme", "https"}, {"path", "/"}}}};
    auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    const std::string name = "cockpit." + topology.network_domains.at(topology.root);

    const auto root = GenerateCoreDnsConfiguration(topology, addresses, kEdge);
    const auto child = GenerateCoreDnsConfiguration(topology, addresses, kGate);

    EXPECT_NE(root.corefile.find(definitions.site.hypervisor_address + " " + name), std::string::npos);
    EXPECT_EQ(child.corefile.find(name), std::string::npos);
    definitions.site.hypervisor_settings["hypervisor_services"]["edge"] =
        {{"port", 8443}, {"scheme", "https"}, {"path", "/"}};
    EXPECT_THROW(BuildTopology(definitions), ConfigurationError);
    definitions.site.hypervisor_settings["hypervisor_services"].erase("edge");
    definitions.site.hypervisor_settings["hypervisor_services"]["cockpit"]["port"] = 0;
    EXPECT_THROW(BuildTopology(definitions), ConfigurationError);
}

TEST(Dns, ResolvesPublishedNamesForTheHypervisorToTheRootUplink) {
    auto definitions = Definitions();
    definitions.networks.at(kChild).exports.push_back({
        "30000000-0000-4000-8000-000000000010", "published", kApi, "tcp", 8443, 443,
        "uplink", {definitions.site.hypervisor_address + "/32"}});
    definitions.networks.at(kChild).exports.push_back({
        "30000000-0000-4000-8000-000000000011", "other-client", kApi, "tcp", 9443, 443,
        "uplink", {"192.0.2.1/32"}});
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);

    const auto root = GenerateCoreDnsConfiguration(topology, addresses, kEdge);

    const auto& name = topology.service_domains.at("published");
    const auto uplink = FormatIpv4(addresses.vms.at(kEdge).at("wan").address.address);
    EXPECT_NE(root.corefile.find(uplink + " " + name), std::string::npos);
    EXPECT_EQ(root.corefile.find(topology.service_domains.at("other-client")), std::string::npos);
    EXPECT_EQ(root.corefile.find(FormatIpv4(addresses.vms.at(kApi).at("lan").address.address)), std::string::npos);
}

TEST(Dns, ResolvesFrontendAliasesWithoutGrantingAnotherServiceOrChangingItsPort) {
    auto definitions = Definitions();
    const std::string frontend = "30000000-0000-4000-8000-000000000010";
    definitions.networks.at(kRoot).exports.push_back({frontend, "https", kDb, "tcp", 443, 443,
        "uplink", {"hypervisor"}, {"cloud"}});
    definitions.networks.at(kChild).allow = {{kApi, frontend, "", {}}};
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    const auto flows = ResolveConnections(topology, addresses);
    const auto connections = GenerateClientConnections(topology, flows, kApi);
    const auto root = GenerateCoreDnsConfiguration(topology, addresses, flows, kEdge);
    const auto child = GenerateCoreDnsConfiguration(topology, addresses, flows, kGate);

    ASSERT_TRUE(topology.service_domains.contains("cloud"));
    ASSERT_TRUE(connections.contains("cloud"));
    const auto& fqdn = topology.service_domains.at("cloud");
    EXPECT_EQ(connections.at("cloud").at("fqdn"), fqdn);
    EXPECT_EQ(connections.at("cloud").at("address"), connections.at("https").at("address"));
    EXPECT_EQ(connections.at("cloud").at("port"), 443);
    EXPECT_EQ(flows.size(), 1);
    EXPECT_NE(child.corefile.find(connections.at("cloud").at("address").get<std::string>() + " " + fqdn), std::string::npos);
    EXPECT_NE(root.corefile.find(FormatIpv4(addresses.vms.at(kEdge).at("wan").address.address) + " " + fqdn), std::string::npos);
}

TEST(NetworkConfig, UsesInterfaceNamesAndPortsFromJson) {
    auto definitions = Definitions();
    definitions.site.services["network"]["interfaces"] = {{"lan", "inside0"}, {"wan", "outside0"}};
    definitions.site.services["ssh"]["port"] = 2222;
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);

    const auto netplan = GenerateNetplan(topology, addresses, kApi);
    const auto rules = CompileFirewallRules(topology, addresses);

    EXPECT_TRUE(netplan.at("network").at("ethernets").contains("inside0"));
    EXPECT_TRUE(ContainsRule(rules.at(kEdge).input, "tcp dport 2222"));
    EXPECT_FALSE(ContainsRule(rules.at(kEdge).input, "tcp dport 22 accept"));
}

}  // namespace skeleton::test
