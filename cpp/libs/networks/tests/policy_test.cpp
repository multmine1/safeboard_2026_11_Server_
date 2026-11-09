#include <gtest/gtest.h>

#include "kubernetes_fixture.hpp"
#include "skeleton/networks/policy.hpp"
#include "skeleton/networks/access.hpp"

namespace skeleton::test {
namespace {

bool Contains(const std::set<std::string>& rules, const std::string& text) {
    return std::any_of(rules.begin(), rules.end(), [&](const auto& rule) {
        return rule.find(text) != std::string::npos;
    });
}

}  // namespace

TEST(PolicyPlan, AllowsBgpOnlyBetweenAdjacentNicAddresses) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    const auto cluster = BuildKubernetesPlan(topology, addresses);

    const auto policy = BuildNetworkPolicyPlan(topology, addresses, cluster);

    const auto& peer = *std::find_if(cluster.peers.begin(), cluster.peers.end(), [&](const auto& item) {
        return item.from == kApi && item.to == kGate;
    });
    const auto match = "ip saddr " + peer.source_address + " ip daddr " + peer.target_address;
    EXPECT_TRUE(Contains(policy.firewalls.at(kApi).output, match + " ct state new tcp dport 179"));
    EXPECT_TRUE(Contains(policy.firewalls.at(kGate).input, match + " ct state new tcp dport 179"));
    EXPECT_FALSE(Contains(policy.firewalls.at(kEdge).input, match));
    EXPECT_FALSE(Contains(policy.firewalls.at(kGate).forward, "tcp dport 179"));
}

TEST(PolicyPlan, CompilesAnExplicitHypervisorServiceAlongTheWholeRouterTree) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    const auto file = directory.path / "nested/child.network.json";
    auto network = Json::parse(std::ifstream(file));
    network["allow"].push_back({{"from", kApi}, {"to", "hypervisor"},
                                {"protocol", "tcp"}, {"ports", {9177}}});
    WriteJson(file, network);
    auto definitions = ReadSourceDefinitions(directory.path);
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    const auto cluster = BuildKubernetesPlan(topology, addresses);

    const auto policy = BuildNetworkPolicyPlan(topology, addresses, cluster);

    const auto destination = definitions.site.hypervisor_address;
    const auto match = "ip daddr " + destination + " ct state new tcp dport 9177";
    for (const auto& router : {kGate, kEdge})
        EXPECT_TRUE(Contains(policy.firewalls.at(router).forward, match));
    EXPECT_TRUE(Contains(policy.firewalls.at(kApi).output, match));
    EXPECT_FALSE(Contains(policy.firewalls.at(kDb).output, "dport 9177"));
    const auto& outgoing = policy.pod_access.at(kApi);
    EXPECT_TRUE(std::any_of(outgoing.begin(), outgoing.end(), [&](const auto& rule) {
        return rule.direction == "out" && rule.ports == std::vector<std::uint16_t>{9177} &&
               rule.peers == std::vector<std::string>{destination + "/32"};
    }));
    EXPECT_EQ(GenerateClientConnections(topology, policy.connections, kApi)
                  .at("hypervisor").at("socket"), destination + ":9177");

    network["allow"].back().erase("ports");
    WriteJson(file, network);
    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
}

TEST(PolicyPlan, CarriesDirectedPodPermissionsThroughEveryRouter) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    const auto cluster = BuildKubernetesPlan(topology, addresses);

    const auto policy = BuildNetworkPolicyPlan(topology, addresses, cluster);

    const auto source = cluster.nodes.at(kApi).pods.ToString();
    const auto target = cluster.nodes.at(kDb).pods.ToString();
    for (const auto& id : {kGate, kEdge}) {
        EXPECT_TRUE(Contains(policy.firewalls.at(id).forward,
            "ip saddr " + source + " ip daddr " + target + " ct state new tcp dport 5432"));
        EXPECT_FALSE(Contains(policy.firewalls.at(id).forward,
            "ip saddr " + target + " ip daddr " + source + " ct state new tcp dport 5432"));
    }
    EXPECT_TRUE(Contains(policy.firewalls.at(kApi).forward, "iifname \"cali*\" oifname \"lan0\""));
    EXPECT_TRUE(Contains(policy.firewalls.at(kDb).forward, "iifname \"lan0\" oifname \"cali*\""));
    const auto& ingress = policy.pod_access.at(kDb);
    EXPECT_TRUE(std::any_of(ingress.begin(), ingress.end(), [&](const auto& rule) {
        return rule.direction == "in" && rule.ports == std::vector<std::uint16_t>{5432} &&
            std::find(rule.peers.begin(), rule.peers.end(), source) != rule.peers.end();
    }));
}

TEST(PolicyPlan, RoutesRouterSystemPodsToTheControlPlaneThroughTheSameTree) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    const auto cluster = BuildKubernetesPlan(topology, addresses);
    ResolveCommonPermissions(topology, cluster);

    const auto policy = BuildNetworkPolicyPlan(topology, addresses, cluster);

    const auto pods = cluster.nodes.at(kEdge).pods.ToString();
    const auto api = cluster.nodes.at(kApi).address;
    const auto match = "ip saddr " + pods + " ip daddr " + api + " ct state new tcp dport 6443";
    EXPECT_TRUE(Contains(policy.firewalls.at(kEdge).forward, "iifname \"cali*\" oifname \"lan0\" " + match));
    EXPECT_FALSE(Contains(policy.firewalls.at(kEdge).output, match));
    EXPECT_TRUE(Contains(policy.firewalls.at(kGate).forward, match));
    EXPECT_TRUE(Contains(policy.firewalls.at(kApi).input, match));
    const auto reverse = "ip saddr " + api + " ip daddr " + pods + " ct state new tcp dport 10250";
    EXPECT_TRUE(Contains(policy.firewalls.at(kEdge).forward, reverse));
    EXPECT_FALSE(Contains(policy.firewalls.at(kEdge).input, reverse));
}

TEST(PolicyPlan, AllowsServiceDnatToOnlyTheNodesOwnHostAndLocalPods) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    const auto cluster = BuildKubernetesPlan(topology, addresses);

    const auto policy = BuildNetworkPolicyPlan(topology, addresses, cluster);

    const auto& node = cluster.nodes.at(kApi);
    const auto match = "ct status dnat ip saddr " + node.address + " ip daddr ";
    const auto& output = policy.firewalls.at(kApi).output;
    EXPECT_TRUE(output.contains(match + node.address + " accept"));
    EXPECT_TRUE(output.contains(match + node.pods.ToString() + " accept"));
    EXPECT_FALSE(output.contains(match + cluster.nodes.at(kDb).pods.ToString() + " accept"));
}

TEST(PolicyPlan, RejectsSpoofedIngressBeforeAcceptingEstablishedConnections) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    const auto cluster = BuildKubernetesPlan(topology, addresses);

    const auto policy = BuildNetworkPolicyPlan(topology, addresses, cluster);
    const auto router = GenerateNftablesConfiguration(topology, addresses, kEdge, policy.firewalls.at(kEdge));
    const auto guest = GenerateNftablesConfiguration(topology, addresses, kApi, policy.firewalls.at(kApi));

    const auto mac = addresses.vms.at(kDb).at("lan").mac;
    const auto guard = "ether saddr " + mac + " ip saddr != { " +
        FormatIpv4(addresses.vms.at(kDb).at("lan").address.address) + ", " +
        cluster.nodes.at(kDb).pods.ToString() + " } drop";
    const auto position = router.find(guard);
    ASSERT_NE(position, std::string::npos);
    EXPECT_LT(position, router.find("ct state established,related accept"));
    const auto parent_guard = "ether saddr != " + addresses.vms.at(kGate).at("lan").mac + " drop";
    const auto guest_position = guest.find(parent_guard);
    ASSERT_NE(guest_position, std::string::npos);
    EXPECT_LT(guest_position, guest.find("ct state established,related accept"));
}

TEST(PolicyPlan, PreservesTheGatewayNatBoundaryForPodServiceAccess) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const std::string service = "30000000-0000-4000-8000-000000000001";
    definitions.networks.at(kRoot).exports.push_back({service, "sql", kDb, "tcp", 15432, 5432, "internal", {}});
    definitions.networks.at(kChild).allow = {{kApi, service, "", {}}};
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);
    const auto cluster = BuildKubernetesPlan(topology, addresses);

    const auto policy = BuildNetworkPolicyPlan(topology, addresses, cluster);

    const auto gateway = FormatIpv4(addresses.vms.at(kEdge).at("lan").address.address);
    const auto source = cluster.nodes.at(kApi).pods.ToString();
    const auto target = cluster.nodes.at(kDb).pods.ToString();
    EXPECT_TRUE(Contains(policy.firewalls.at(kEdge).prerouting, "ip saddr " + source + " ip daddr " + gateway));
    EXPECT_TRUE(Contains(policy.firewalls.at(kDb).forward, "ip saddr " + gateway + " ip daddr " + target));
    EXPECT_FALSE(Contains(policy.firewalls.at(kEdge).forward, "ip saddr " + source + " ip daddr " + target));
    for (const auto& rule : policy.pod_access.at(kDb))
        if (rule.direction == "in" && rule.ports == std::vector<std::uint16_t>{5432})
            EXPECT_EQ(rule.peers, std::vector<std::string>{gateway + "/32"});
}

}  // namespace skeleton::test
