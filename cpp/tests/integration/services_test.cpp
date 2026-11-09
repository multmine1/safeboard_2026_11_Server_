#include <gtest/gtest.h>

#include "fixture.hpp"
#include "skeleton/pipeline/generator.hpp"

namespace skeleton::test {
namespace {

const std::string kService = "30000000-0000-4000-8000-000000000001";

void WriteServiceSources(const std::filesystem::path& directory) {
    WriteSources(directory);
    auto site = SiteJson();
    site["services"]["firewall"]["inter_network_snat"] = true;
    WriteJson(directory / "site.json", site);
    auto root = Json::parse(std::ifstream(directory / "root.network.json"));
    root["exports"] = {{{"id", kService}, {"name", "sql"}, {"vm", kDb},
                         {"protocol", "tcp"}, {"port", 15432}, {"target_port", 5432},
                         {"scope", "internal"}}};
    WriteJson(directory / "root.network.json", root);
    auto child = Json::parse(std::ifstream(directory / "nested/child.network.json"));
    child["allow"] = {{{"from", kApi}, {"to", kService}}};
    child["dns_upstreams"] = {"1.1.1.1"};
    WriteJson(directory / "nested/child.network.json", child);
}

}  // namespace

TEST(Services, PublishesThroughTheGatewayWithoutLeakingThePrivateBackendToTheClient) {
    TemporaryDirectory directory;
    WriteServiceSources(directory.path);
    const auto topology = BuildTopology(ReadSourceDefinitions(directory.path));
    const auto addresses = AllocateAddressPlan(topology);
    const auto gateway = FormatIpv4(addresses.vms.at(kEdge).at("lan").address.address);
    const auto backend = FormatIpv4(addresses.vms.at(kDb).at("lan").address.address);

    const auto files = GenerateConfiguration(topology, addresses);

    const auto client = Json::parse(files.at("ansible/host_vars/" + kApi + ".json"));
    EXPECT_EQ(client.at("connections").at("sql").at("address"), gateway);
    EXPECT_EQ(client.at("connections").at("sql").at("port"), 15432);
    EXPECT_EQ(client.dump().find(backend), std::string::npos);
    const auto root = Json::parse(files.at("ansible/host_vars/" + kEdge + ".json"));
    const auto& nft = root.at("network").at("files").at("/etc/nftables.conf").get<std::string>();
    EXPECT_NE(nft.find("dnat to " + backend + ":5432"), std::string::npos);
    EXPECT_NE(nft.find("snat to " + gateway), std::string::npos);
    const auto db = Json::parse(files.at("ansible/host_vars/" + kDb + ".json"));
    EXPECT_NE(db.at("network").at("files").at("/etc/nftables.conf").get<std::string>().find("ip saddr " + gateway), std::string::npos);
    const auto child = Json::parse(files.at("ansible/host_vars/" + kGate + ".json"));
    EXPECT_NE(child.at("network").at("files").at("/etc/coredns/Corefile").get<std::string>().find("forward . 1.1.1.1"), std::string::npos);
    EXPECT_NE(files.at("graphviz/paths/" + kApi + "/" + kDb + ".dot").find("DNAT"), std::string::npos);
}

TEST(Services, FiltersUplinkPublicationAtEveryHopAndPreservesTheClientSource) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto child = Json::parse(std::ifstream(directory.path / "nested/child.network.json"));
    child["exports"] = {{{"id", kService}, {"name", "web"}, {"vm", kApi},
                          {"protocol", "tcp"}, {"port", 443}, {"target_port", 8443},
                          {"scope", "uplink"}, {"sources", {"203.0.113.0/24"}}}};
    WriteJson(directory.path / "nested/child.network.json", child);
    const auto topology = BuildTopology(ReadSourceDefinitions(directory.path));
    const auto addresses = AllocateAddressPlan(topology);

    const auto files = GenerateConfiguration(topology, addresses);

    for (const auto& id : {kEdge, kGate, kApi}) {
        const auto host = Json::parse(files.at("ansible/host_vars/" + id + ".json"));
        const auto& nft = host.at("network").at("files").at("/etc/nftables.conf").get<std::string>();
        EXPECT_NE(nft.find("203.0.113.0/24"), std::string::npos);
        EXPECT_NE(nft.find("tcp dport 8443"), std::string::npos);
    }
}

TEST(Services, RejectsAnExportOfAnotherNetworksVm) {
    TemporaryDirectory directory;
    WriteServiceSources(directory.path);
    auto root = Json::parse(std::ifstream(directory.path / "root.network.json"));
    root["exports"][0]["vm"] = kApi;
    WriteJson(directory.path / "root.network.json", root);

    EXPECT_THROW(BuildTopology(ReadSourceDefinitions(directory.path)), ConfigurationError);
}

}  // namespace skeleton::test
