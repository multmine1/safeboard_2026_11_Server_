#include <gtest/gtest.h>

#include "fixture.hpp"
#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::test {
namespace {

SourceDefinitions BrowserDefinitions() {
    auto definitions = Definitions();
    definitions.site.services["local_access"] = {{"bind_address", "127.0.0.2"},
        {"socks_port", 2080}, {"browser_proxy", {{"port", 2081}, {"label", "org.example.browser-proxy"}}},
        {"services", Json::array()}};
    return definitions;
}

}  // namespace

TEST(BrowserProxy, PreparesSelectiveRemoteDnsAccessFromTheSiteAndSocksSettings) {
    const auto topology = BuildTopology(BrowserDefinitions());
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& proxy = plan.local_access.at("browser_proxy");
    EXPECT_EQ(proxy.at("url"), "http://127.0.0.2:2081/proxy.pac");
    EXPECT_EQ(proxy.at("label"), "org.example.browser-proxy");
    EXPECT_EQ(proxy.at("port"), 2081);
    const auto pac = proxy.at("pac").get<std::string>();
    EXPECT_NE(pac.find(Json(topology.definitions.site.domain).dump()), std::string::npos);
    EXPECT_NE(pac.find("SOCKS5 127.0.0.2:2080; SOCKS 127.0.0.2:2080"), std::string::npos);
    EXPECT_NE(pac.find("return \"DIRECT\""), std::string::npos);
    EXPECT_EQ(pac.find("dnsResolve"), std::string::npos);
    EXPECT_EQ(pac.find("; DIRECT"), std::string::npos);
}

TEST(BrowserProxy, RejectsMissingSocksCollidingPortsAndUnsafeLaunchAgentLabels) {
    for (const auto fault : {"missing_socks", "zero_port", "large_port", "collision", "label"}) {
        SCOPED_TRACE(fault);
        auto definitions = BrowserDefinitions();
        auto& access = definitions.site.services["local_access"];
        const std::string setting = fault;
        if (setting == "missing_socks") access.erase("socks_port");
        if (setting == "zero_port") access["browser_proxy"]["port"] = 0;
        if (setting == "large_port") access["browser_proxy"]["port"] = 65536;
        if (setting == "collision") access["browser_proxy"]["port"] = 2080;
        if (setting == "label") access["browser_proxy"]["label"] = "../outside";
        const auto topology = BuildTopology(definitions);
        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

}  // namespace skeleton::test
