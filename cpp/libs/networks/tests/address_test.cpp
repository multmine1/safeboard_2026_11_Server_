#include <gtest/gtest.h>

#include "fixture.hpp"
#include "skeleton/networks/address.hpp"

namespace skeleton::test {

TEST(Allocation, ResolvesCollisionsAndFailsAfterTheLastFreeSlot) {
    // SHA-256 candidates for a and b both select slot zero in a two-slot pool.
    SlotAllocator allocator(2, "small pool");

    EXPECT_EQ(allocator.Allocate("a"), 0);
    EXPECT_EQ(allocator.Allocate("b"), 1);
    EXPECT_THROW(allocator.Allocate("c"), ConfigurationError);
}

TEST(Allocation, ReservesNetworkGatewayAndBroadcastAddresses) {
    auto definitions = Definitions();
    definitions.vms.erase(kGate);
    definitions.vms.erase(kApi);
    definitions.networks.erase(kChild);
    definitions.networks.at(kRoot).members = {kDb};
    definitions.site.ipv4_pool = "10.80.0.0/30";
    definitions.site.network_prefix = 30;

    const auto plan = AllocateAddressPlan(BuildTopology(definitions));

    EXPECT_EQ(plan.vms.at(kEdge).at("lan").address.ToString(), "10.80.0.1/30");
    EXPECT_EQ(plan.vms.at(kDb).at("lan").address.ToString(), "10.80.0.2/32");
    auto api = definitions.vms.at(kDb);
    api.id = kApi;
    api.name = "api";
    definitions.vms.emplace(kApi, api);
    definitions.networks.at(kRoot).members.push_back(kApi);
    EXPECT_THROW(AllocateAddressPlan(BuildTopology(definitions)), ConfigurationError);
}

TEST(Allocation, AssignsUniqueAddressesAndStableRoleBasedMacs) {
    const auto plan = AllocateAddressPlan(BuildTopology(Definitions()));
    std::set<std::uint32_t> ips;
    std::set<std::string> macs;

    for (const auto& [vm_id, nics] : plan.vms) {
        EXPECT_EQ(nics.size(), vm_id == kEdge || vm_id == kGate ? 2 : 1);
        for (const auto& [role, nic] : nics) {
            EXPECT_TRUE(ips.insert(nic.address.address).second);
            EXPECT_TRUE(macs.insert(nic.mac).second);
            EXPECT_TRUE(nic.mac.starts_with("02:"));
            EXPECT_EQ(nic.key, vm_id + "/" + role);
        }
    }
    EXPECT_EQ(plan.vms.at(kEdge).at("lan").mac, "02:be:79:81:66:81");
    EXPECT_FALSE(plan.vms.at(kGate).at("lan").isolated);
    EXPECT_TRUE(plan.vms.at(kGate).at("wan").isolated);
    EXPECT_TRUE(plan.vms.at(kApi).at("lan").isolated);
}

TEST(Allocation, KeepsMacButChangesAddressWhenVmMovesToAnotherNetwork) {
    auto definitions = Definitions();
    const auto before = AllocateAddressPlan(BuildTopology(definitions));
    definitions.networks.at(kChild).members.clear();
    definitions.networks.at(kRoot).members.push_back(kApi);
    definitions.networks.at(kRoot).allow = std::move(definitions.networks.at(kChild).allow);

    const auto topology = BuildTopology(definitions);
    const auto after = AllocateAddressPlan(topology);

    EXPECT_EQ(before.vms.at(kApi).at("lan").mac, after.vms.at(kApi).at("lan").mac);
    EXPECT_NE(before.vms.at(kApi).at("lan").address.address,
              after.vms.at(kApi).at("lan").address.address);
    EXPECT_EQ(topology.vm_domains.at(kApi), "api.infra.example.test");
}

TEST(Allocation, KeepsSubnetsAndMacsWhenNetworkIsReparented) {
    auto definitions = Definitions();
    const std::string peer = "10000000-0000-4000-8000-000000000003";
    const std::string router = "20000000-0000-4000-8000-000000000005";
    auto peer_router = definitions.vms.at(kGate);
    peer_router.id = router;
    peer_router.name = "border";
    definitions.vms.emplace(router, peer_router);
    definitions.networks.emplace(peer, NetworkDefinition{peer, "peer", router, {}, {}, {}});
    definitions.networks.at(kRoot).members.push_back(router);
    const auto before = AllocateAddressPlan(BuildTopology(definitions));
    definitions.networks.at(kRoot).members = {kDb, router};
    definitions.networks.at(peer).members = {kGate};

    const auto topology = BuildTopology(definitions);
    const auto after = AllocateAddressPlan(topology);

    for (const auto& [id, subnet] : before.subnets)
        EXPECT_EQ(subnet.ToString(), after.subnets.at(id).ToString());
    EXPECT_EQ(before.vms.at(kGate).at("wan").mac, after.vms.at(kGate).at("wan").mac);
    EXPECT_EQ(topology.vm_domains.at(kApi), "api.apps.peer.infra.example.test");
}

TEST(Allocation, RejectsAnInternalPoolOverlappingThePhysicalLan) {
    auto definitions = Definitions();
    definitions.site.ipv4_pool = "192.0.2.0/24";

    EXPECT_THROW(AllocateAddressPlan(BuildTopology(definitions)), ConfigurationError);
}

TEST(Allocation, UsesThePoolGatewayAndMacPrefixDeclaredInJson) {
    auto definitions = Definitions();
    definitions.site.ipv4_pool = "172.20.0.0/16";
    definitions.site.gateway_offset = 7;
    definitions.site.mac_prefix = "0a";

    const auto plan = AllocateAddressPlan(BuildTopology(definitions));

    for (const auto& [id, subnet] : plan.subnets) {
        EXPECT_TRUE(ParseCidr("172.20.0.0/16").Contains(subnet.address));
        const auto& router = definitions.networks.at(id).router;
        EXPECT_EQ(plan.vms.at(router).at("lan").address.address, subnet.address + 7);
    }
    EXPECT_TRUE(plan.vms.at(kApi).at("lan").mac.starts_with("0a:"));
}

TEST(Allocation, KeepsNetworkIdentityAndGatewayWhenItsRouterIsReplaced) {
    auto definitions = Definitions();
    const auto before = AllocateAddressPlan(BuildTopology(definitions));
    const std::string replacement = "20000000-0000-4000-8000-000000000005";
    auto router = definitions.vms.at(kEdge);
    router.id = replacement;
    definitions.vms.erase(kEdge);
    definitions.vms.emplace(replacement, router);
    definitions.networks.at(kRoot).router = replacement;

    const auto after = AllocateAddressPlan(BuildTopology(definitions));

    EXPECT_EQ(before.subnets.at(kRoot).ToString(), after.subnets.at(kRoot).ToString());
    EXPECT_EQ(before.vms.at(kEdge).at("lan").address.ToString(),
              after.vms.at(replacement).at("lan").address.ToString());
    EXPECT_NE(before.vms.at(kEdge).at("lan").mac, after.vms.at(replacement).at("lan").mac);
}

}  // namespace skeleton::test
