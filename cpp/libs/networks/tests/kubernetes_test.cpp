#include <gtest/gtest.h>

#include "kubernetes_fixture.hpp"
#include "skeleton/networks/kubernetes.hpp"

namespace skeleton::test {

struct InvalidTreeSetting {
    std::string name;
    Json patch;
};

class InvalidTreeSettings : public testing::TestWithParam<InvalidTreeSetting> {};

TEST_P(InvalidTreeSettings, RejectsConfigurationThatBreaksRoutedVmIdentity) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"].merge_patch(GetParam().patch);
    const auto topology = BuildTopology(definitions);

    EXPECT_THROW(BuildKubernetesPlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

INSTANTIATE_TEST_SUITE_P(RoutedTree, InvalidTreeSettings, testing::Values(
    InvalidTreeSetting{"PodAddressBorrowing", {{"ipam", {{"strictAffinity", false}}}}},
    InvalidTreeSetting{"NonAdjacentSource", {{"bgp", {{"sourceAddress", "UseNodeIP"}}}}},
    InvalidTreeSetting{"UnchangedNextHop", {{"bgp", {{"nextHopMode", "Keep"}}}}},
    InvalidTreeSetting{"PortOverflow", {{"bgp", {{"port", 65536}}}}}),
    [](const auto& info) { return info.param.name; });

TEST(KubernetesPlan, AllocatesNodePoolsAndPeersOnlyAlongThePhysicalTree) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);

    const auto cluster = BuildKubernetesPlan(topology, addresses);

    EXPECT_EQ(cluster.controller, kApi);
    ASSERT_EQ(cluster.nodes.size(), 4);
    ASSERT_EQ(cluster.peers.size(), 6);
    std::set<std::uint32_t> asns;
    for (const auto& [id, node] : cluster.nodes) {
        EXPECT_TRUE(asns.insert(node.asn).second);
        for (const auto& [other_id, other] : cluster.nodes)
            if (id != other_id) EXPECT_FALSE(node.pods.Overlaps(other.pods));
    }
    bool child_wan_peer = false;
    for (const auto& peer : cluster.peers) {
        EXPECT_EQ(addresses.vms.at(peer.from).at(peer.source_role).network, peer.network);
        EXPECT_EQ(addresses.vms.at(peer.to).at(peer.target_role).network, peer.network);
        EXPECT_FALSE(peer.from == kDb && peer.to == kApi);
        if (peer.from == kEdge && peer.to == kGate) {
            child_wan_peer = true;
            EXPECT_EQ(peer.target_role, "wan");
            EXPECT_EQ(peer.target_address, FormatIpv4(addresses.vms.at(kGate).at("wan").address.address));
        }
    }
    EXPECT_TRUE(child_wan_peer);
    EXPECT_EQ(cluster.configuration.at("spec").at("api").at("address"), cluster.nodes.at(kApi).address);
}

TEST(KubernetesPlan, RejectsUnsafeOrMultiplyDeclaredDataDirectoriesBeforeExport) {
    for (const auto fault : {"/", "../data", "argument", "split_argument"}) {
        SCOPED_TRACE(fault);
        auto definitions = Definitions();
        EnableKubernetes(definitions);
        auto& settings = definitions.site.services["kubernetes"];
        settings["paths"]["data_directory"] = "/var/lib/example";
        const auto name = std::string(fault);
        if (name == "argument") settings["controller_args"].push_back("--data-dir=/elsewhere");
        else if (name == "split_argument") settings["worker_args"].push_back("--data-dir");
        else settings["paths"]["data_directory"] = name;
        const auto topology = BuildTopology(definitions);
        EXPECT_THROW(BuildKubernetesPlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(KubernetesPlan, PreservesNodePoolsAndAsnsWhenAVmMovesToAnotherNetwork) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto original_topology = BuildTopology(definitions);
    const auto original = BuildKubernetesPlan(original_topology, AllocateAddressPlan(original_topology));
    std::erase(definitions.networks.at(kRoot).members, kDb);
    definitions.networks.at(kChild).members.push_back(kDb);
    const auto moved_topology = BuildTopology(definitions);

    const auto moved = BuildKubernetesPlan(moved_topology, AllocateAddressPlan(moved_topology));

    EXPECT_EQ(moved.nodes.at(kDb).pods.ToString(), original.nodes.at(kDb).pods.ToString());
    EXPECT_EQ(moved.nodes.at(kDb).asn, original.nodes.at(kDb).asn);
    EXPECT_EQ(moved.nodes.at(kDb).network, kChild);
}

TEST(KubernetesPlan, KeepsThePhysicalPeerConfigurationIndependentOfInitialVmPower) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto original_topology = BuildTopology(definitions);
    const auto original = BuildKubernetesPlan(original_topology, AllocateAddressPlan(original_topology));
    definitions.vms.at(kDb).domain["running"] = false;
    const auto topology = BuildTopology(definitions);

    const auto stopped = BuildKubernetesPlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(stopped.nodes.at(kDb).pods.ToString(), original.nodes.at(kDb).pods.ToString());
    EXPECT_FALSE(stopped.nodes.at(kDb).initially_running);
    EXPECT_EQ(stopped.peers.size(), original.peers.size());
    EXPECT_TRUE(std::any_of(stopped.peers.begin(), stopped.peers.end(), [](const auto& peer) {
        return peer.from == kDb || peer.to == kDb;
    }));
}

TEST(KubernetesPlan, RejectsOverlappingPoolsAndInsufficientAsnCapacity) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto& settings = definitions.site.services["kubernetes"];
    settings["config"]["spec"]["network"]["podCIDR"] = definitions.site.ipv4_pool;
    auto topology = BuildTopology(definitions);
    EXPECT_THROW(BuildKubernetesPlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    settings["config"]["spec"]["network"]["podCIDR"] = "10.244.0.0/16";
    settings["asn_pool"]["last"] = settings.at("asn_pool").at("first").get<std::uint64_t>() + 1;
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildKubernetesPlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

}  // namespace skeleton::test
