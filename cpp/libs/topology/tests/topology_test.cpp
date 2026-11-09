#include <functional>

#include <gtest/gtest.h>

#include "fixture.hpp"

namespace skeleton::test {

TEST(HypervisorServices, KeepsALoopbackSiteNameSeparateFromTheLanSocketOnTheSamePort) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings["hypervisor_services"] = {
        {"cockpit", {{"port", 443}, {"scheme", "https"}, {"path", "/"}}},
        {"admin", {{"port", 443}, {"scheme", "https"}, {"path", "/"},
                   {"bind_address", "127.0.0.1"}, {"dns_zone", "site"}}}};

    const auto topology = BuildTopology(definitions);

    EXPECT_EQ(topology.host_services.at("admin").fqdn, "admin." + definitions.site.domain);
    EXPECT_EQ(topology.host_services.at("admin").socket, "127.0.0.1:443");
    EXPECT_EQ(topology.host_services.at("cockpit").address, definitions.site.hypervisor_address);
    EXPECT_EQ(topology.host_services.at("cockpit").fqdn, "cockpit.infra.example.test");
}

TEST(HypervisorServices, RejectsAmbiguousSocketsAndUnknownDnsZones) {
    for (const auto& fault : {"socket", "zone", "wildcard"}) {
        SCOPED_TRACE(fault);
        auto definitions = Definitions();
        definitions.site.hypervisor_settings["hypervisor_services"] = {
            {"first", {{"port", 443}, {"scheme", "https"}, {"path", "/"}}},
            {"second", {{"port", 8443}, {"scheme", "https"}, {"path", "/"}}}};
        auto& endpoint = definitions.site.hypervisor_settings["hypervisor_services"]["second"];
        if (std::string(fault) == "socket") endpoint["port"] = 443;
        if (std::string(fault) == "zone") endpoint["dns_zone"] = "unknown";
        if (std::string(fault) == "wildcard") endpoint["bind_address"] = "0.0.0.0";

        EXPECT_THROW(BuildTopology(definitions), ConfigurationError);
    }
}

TEST(Topology, BuildsTheTreeNamesAndRouterFirstOrder) {
    const auto topology = BuildTopology(Definitions());

    EXPECT_EQ(topology.root, kRoot);
    EXPECT_EQ(topology.parents.at(kChild), kRoot);
    EXPECT_EQ(topology.network_domains.at(kChild), "apps.infra.example.test");
    EXPECT_EQ(topology.vm_domains.at(kGate), "gate.apps.infra.example.test");
    EXPECT_EQ(topology.vm_domains.at(kApi), "api.apps.infra.example.test");
    EXPECT_EQ(topology.configuration_order.front(), kEdge);
    const auto gate = std::find(topology.configuration_order.begin(),
                              topology.configuration_order.end(), kGate);
    const auto api = std::find(topology.configuration_order.begin(),
                             topology.configuration_order.end(), kApi);
    EXPECT_LT(gate, api);
    EXPECT_EQ(topology.RouterPath(kChild, kRoot),
              (std::vector<std::string>{kChild, kRoot}));
}

TEST(Topology, ResolvesTheHypervisorSourceWithoutDuplicatingItsAddressInNetworks) {
    auto definitions = Definitions();
    definitions.site.hypervisor_address = "192.0.2.44";
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000009", "web", kDb, "tcp", 443, 443,
        "uplink", {"hypervisor", "198.51.100.0/24"}});

    const auto topology = BuildTopology(definitions);

    EXPECT_EQ(topology.Service("30000000-0000-4000-8000-000000000009").sources,
              (std::vector<std::string>{"192.0.2.44/32", "198.51.100.0/24"}));
}

struct InvalidModel {
    std::string name;
    std::function<void(SourceDefinitions&)> change;
};

class InvalidTopology : public testing::TestWithParam<InvalidModel> {};

TEST_P(InvalidTopology, RejectsAnAmbiguousOrUnroutableModel) {
    auto definitions = Definitions();
    GetParam().change(definitions);

    EXPECT_THROW(BuildTopology(std::move(definitions)), ConfigurationError);
}

INSTANTIATE_TEST_SUITE_P(Contract, InvalidTopology, testing::Values(
    InvalidModel{"MissingReference", [](auto& d) {
        d.networks.at(kRoot).members.push_back("30000000-0000-4000-8000-000000000001");
    }},
    InvalidModel{"TwoWorkingNetworks", [](auto& d) {
        d.networks.at(kRoot).members.push_back(kApi);
    }},
    InvalidModel{"Cycle", [](auto& d) {
        d.networks.at(kChild).members.push_back(kEdge);
    }},
    InvalidModel{"TwoRoots", [](auto& d) {
        d.networks.at(kRoot).members = {kDb};
    }},
    InvalidModel{"FqdnCollision", [](auto& d) {
        d.vms.at(kDb).name = "apps";
    }},
    InvalidModel{"RuleOwnedByAnotherNetwork", [](auto& d) {
        d.networks.at(kRoot).allow = d.networks.at(kChild).allow;
    }}), [](const auto& info) { return info.param.name; });

TEST(Topology, AllowsTheSameLocalVmNameInDifferentNetworks) {
    auto definitions = Definitions();
    definitions.vms.at(kApi).name = "db";

    const auto topology = BuildTopology(std::move(definitions));

    EXPECT_NE(topology.vm_domains.at(kApi), topology.vm_domains.at(kDb));
}

TEST(Topology, RejectsAliasesThatCollideWithServiceOrVmNames) {
    for (const std::string alias : {"https", "db"}) {
        SCOPED_TRACE(alias);
        auto definitions = Definitions();
        definitions.networks.at(kRoot).exports.push_back({
            "30000000-0000-4000-8000-000000000009", "https", kDb, "tcp", 443, 443,
            "uplink", {"hypervisor"}, {alias}});

        EXPECT_THROW(BuildTopology(definitions), ConfigurationError);
    }
}

TEST(Topology, KeepsExplicitInternalPermissionsForAnUplinkService) {
    auto definitions = Definitions();
    const std::string service = "30000000-0000-4000-8000-000000000009";
    definitions.networks.at(kRoot).exports.push_back({service, "git", kDb, "tcp", 13000, 3000,
        "uplink", {definitions.site.hypervisor_address + "/32"}});
    definitions.networks.at(kChild).allow = {{kApi, service, "", {}}};

    const auto topology = BuildTopology(definitions);

    EXPECT_EQ(topology.definitions.networks.at(kChild).allow.front().protocol, "tcp");
    EXPECT_EQ(topology.definitions.networks.at(kChild).allow.front().ports,
        (std::vector<std::uint16_t>{13000}));
    EXPECT_EQ(topology.Service(service).sources, (std::vector<std::string>{definitions.site.hypervisor_address + "/32"}));
}

}  // namespace skeleton::test
