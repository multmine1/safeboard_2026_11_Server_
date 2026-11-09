#include <gtest/gtest.h>

#include <algorithm>

#include "kubernetes_fixture.hpp"
#include "skeleton/graphviz/export.hpp"

namespace skeleton::test {

TEST(Graph, ShowsOnlyAllowedVmPairsWithProtocolsInterfacesAndEveryRouter) {
    auto definitions = Definitions();
    const std::string data_network = "10000000-0000-4000-8000-000000000003";
    const std::string data_router = "20000000-0000-4000-8000-000000000005";
    auto router = definitions.vms.at(kGate);
    router.id = data_router;
    router.name = "store";
    definitions.vms.emplace(data_router, router);
    auto& root_members = definitions.networks.at(kRoot).members;
    std::erase(root_members, kDb);
    root_members.push_back(data_router);
    definitions.networks.emplace(data_network,
        NetworkDefinition{data_network, "data", data_router, {kDb}, {}, {}});
    auto& rules = definitions.networks.at(kChild).allow;
    rules.push_back({kApi, kDb, "udp", {53}});
    rules.push_back({kGate, kDb, "tcp", {22}});
    const auto topology = BuildTopology(definitions);
    const auto addresses = AllocateAddressPlan(topology);

    GeneratedFiles files;
    AddGraphvizConfiguration(files, BuildInfrastructurePlan(topology, addresses));

    const auto stem = "graphviz/paths/" + kApi + "/" + kDb;
    ASSERT_TRUE(files.contains(stem + ".dot"));
    EXPECT_TRUE(files.contains(stem + ".svg"));
    EXPECT_FALSE(files.contains("graphviz/paths/" + kDb + "/" + kApi + ".dot"));
    EXPECT_FALSE(files.contains("graphviz/paths/" + kGate + "/" + kDb + ".dot"));
    const auto& dot = files.at(stem + ".dot");
    const auto first = dot.find(Json(kApi).dump() + " -> " + Json(kGate).dump());
    const auto second = dot.find(Json(kGate).dump() + " -> " + Json(kEdge).dump());
    const auto third = dot.find(Json(kEdge).dump() + " -> " + Json(data_router).dump());
    const auto fourth = dot.find(Json(data_router).dump() + " -> " + Json(kDb).dump());
    EXPECT_NE(first, std::string::npos);
    EXPECT_LT(first, second);
    EXPECT_LT(second, third);
    EXPECT_NE(third, std::string::npos);
    EXPECT_LT(third, fourth);
    EXPECT_NE(fourth, std::string::npos);
    EXPECT_NE(dot.find("tcp/5432"), std::string::npos);
    EXPECT_NE(dot.find("udp/53"), std::string::npos);
    EXPECT_NE(dot.find("wan0"), std::string::npos);
    EXPECT_NE(dot.find(addresses.vms.at(kGate).at("wan").address.ToString()), std::string::npos);
    EXPECT_EQ(std::count_if(files.begin(), files.end(), [](const auto& item) {
        return item.first.generic_string().starts_with("graphviz/paths/") && item.first.extension() == ".dot";
    }), 1);
}

TEST(Graph, IncludesTheGatewayAndPodPoolsForAllowedVmsInTheSameNetwork) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto& root = definitions.networks.at(kRoot);
    auto& child = definitions.networks.at(kChild);
    root.members.push_back(kApi);
    root.allow = child.allow;
    child.allow.clear();
    child.members.clear();
    const auto topology = BuildTopology(definitions);

    GeneratedFiles files;
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    AddGraphvizConfiguration(files, plan);

    const auto path = "graphviz/paths/" + kApi + "/" + kDb + ".dot";
    ASSERT_TRUE(files.contains(path));
    const auto& dot = files.at(path);
    EXPECT_NE(dot.find(Json(kApi).dump() + " -> " + Json(kEdge).dump()), std::string::npos);
    EXPECT_NE(dot.find(Json(kEdge).dump() + " -> " + Json(kDb).dump()), std::string::npos);
    EXPECT_EQ(dot.find(kGate), std::string::npos);
    EXPECT_NE(dot.find(plan.kubernetes.nodes.at(kApi).pods.ToString()), std::string::npos);
    EXPECT_NE(dot.find(plan.kubernetes.nodes.at(kDb).pods.ToString()), std::string::npos);
}

}  // namespace skeleton::test
