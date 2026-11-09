#include <gtest/gtest.h>

#include "kubernetes_fixture.hpp"
#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::test {

TEST(RouterPlan, SeparatesBootstrapDnsFromTheApiDependentRuntime) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& router = plan.routers.at(kGate);
    const auto& guest = plan.vms.at(kGate).variables;
    EXPECT_EQ(guest.at("network_owner").at("owner"), "k0s");
    EXPECT_EQ(guest.at("network_owner").at("marker"), router.host_configuration.at("owner_marker"));
    EXPECT_EQ(router.host_configuration.at("files"), guest.at("network").at("files"));
    EXPECT_EQ(plan.vms.at(kGate).dns.corefile.find("kubernetes "), std::string::npos);
    EXPECT_NE(router.corefile.find("kubernetes cluster.local"), std::string::npos);
    EXPECT_NE(router.corefile.find("net-" + kChild), std::string::npos);
    EXPECT_EQ(router.corefile.find("net-" + kRoot), std::string::npos);
    EXPECT_EQ(router.environment.at("KUBERNETES_SERVICE_HOST"), plan.kubernetes.nodes.at(kApi).address);
    EXPECT_EQ(router.host_configuration.dump().find("password"), std::string::npos);
}

TEST(RouterPlan, ChangesTheRuntimeFingerprintWhenThePreparedPolicyChanges) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto first_topology = BuildTopology(definitions);
    const auto first = BuildInfrastructurePlan(first_topology, AllocateAddressPlan(first_topology));
    definitions.networks.at(kChild).allow.push_back({kApi, kDb, "tcp", {8443}});
    const auto next_topology = BuildTopology(definitions);

    const auto next = BuildInfrastructurePlan(next_topology, AllocateAddressPlan(next_topology));

    EXPECT_NE(first.routers.at(kGate).fingerprint, next.routers.at(kGate).fingerprint);
    EXPECT_NE(first.routers.at(kEdge).fingerprint, next.routers.at(kEdge).fingerprint);
    EXPECT_EQ(first.vms.at(kGate).connection, next.vms.at(kGate).connection);
}

TEST(RouterPlan, PackagesTheSharedFirewallTaskAndTracksItsRevision) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto first_topology = BuildTopology(definitions);
    const auto first = BuildInfrastructurePlan(first_topology, AllocateAddressPlan(first_topology));
    const auto& router = first.routers.at(kGate);

    ASSERT_TRUE(router.payload.contains("firewall.yml"));
    EXPECT_EQ(router.payload.at("firewall.yml"), definitions.site.asset_data.at("firewall_tasks"));

    definitions.site.asset_data["firewall_tasks"] = "- name: Updated firewall task\n";
    const auto next_topology = BuildTopology(definitions);
    const auto next = BuildInfrastructurePlan(next_topology, AllocateAddressPlan(next_topology));

    EXPECT_NE(router.fingerprint, next.routers.at(kGate).fingerprint);
}

}  // namespace skeleton::test
