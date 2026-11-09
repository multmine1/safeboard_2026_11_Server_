#include <gtest/gtest.h>

#include "monitoring_fixture.hpp"
#include "kubernetes_fixture.hpp"
#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::test {

TEST(HostMonitoring, SharesTheHostEndpointAndIncludesPoweredOffDomainsWithoutOpeningLibvirtWriteAccess) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings = HypervisorJson();
    definitions.vms.at(kApi).domain["running"] = false;
    EnableHostMonitoring(definitions);
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& monitor = plan.hypervisor.at("bootstrap").at("hypervisor_monitoring");
    const auto& config = monitor.at("configuration");
    EXPECT_EQ(config.at("inputs").at("libvirt").at(0).at("libvirt_uri"),
              "qemu+unix:///system?socket=/run/libvirt/libvirt-sock-ro");
    EXPECT_EQ(config.at("inputs").at("libvirt").at(0).at("domains"),
              Json::array({"vm-" + kEdge, "vm-" + kGate, "vm-" + kDb, "vm-" + kApi}));
    const auto& output = config.at("outputs").at("prometheus_client").at(0);
    EXPECT_EQ(output.at("listen"), definitions.site.hypervisor_address + ":19273");
    EXPECT_EQ(output.at("ip_range"), Json::array({definitions.site.hypervisor_address + "/32",
        FormatIpv4(plan.addresses.vms.at(kEdge).at("wan").address.address) + "/32"}));
    EXPECT_EQ(monitor.at("installation").at("archive"),
              "/home/operator/project/environment/.tools/artifacts/telegraf/telegraf.tar.gz");
    EXPECT_EQ(monitor.at("systemd").at("Service").at("DynamicUser"), "yes");
    EXPECT_EQ(monitor.at("systemd").at("Service").at("ExecStart"),
              "\"/usr/local/bin/telegraf\" --config \"/etc/skeleton/host-metrics.toml\"");
}

TEST(HostMonitoring, RejectsMissingEndpointsUnsafeCachePathsAndDuplicatedGeneratedConfiguration) {
    for (const auto fault : {"endpoint", "cache", "checksum", "domains", "listener", "command"}) {
        SCOPED_TRACE(fault);
        auto definitions = Definitions();
        definitions.site.hypervisor_settings = HypervisorJson();
        EnableHostMonitoring(definitions);
        auto& monitor = definitions.site.hypervisor_settings["hypervisor_monitoring"];
        if (std::string(fault) == "endpoint") monitor["service"] = "missing";
        if (std::string(fault) == "cache") monitor["cache_directory"] = "../outside";
        if (std::string(fault) == "checksum") definitions.site.asset_data["host_monitoring"]["telegraf"]["checksum"] = "unpinned";
        if (std::string(fault) == "domains") monitor["configuration"]["inputs"]["libvirt"][0]["domains"] = Json::array();
        if (std::string(fault) == "listener") monitor["configuration"]["outputs"]["prometheus_client"][0]["listen"] = ":1";
        if (std::string(fault) == "command") monitor["systemd"]["Service"]["ExecStart"] = "/other";
        const auto topology = BuildTopology(definitions);
        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(HostMonitoring, ResolvesNamedHostServicePermissionsFromJsonWithoutDuplicatingPorts) {
    TemporaryDirectory directory;
    WriteSources(directory.path / "source");
    auto host = HypervisorJson();
    host["hypervisor_hosts"] = {{"path", "/etc/hosts"}, {"mode", "0644"}};
    host["hypervisor_services"] = {{"host-metrics", {{"port", 19273}, {"scheme", "http"}, {"path", "/metrics"}}}};
    UseHypervisorFile(directory.path / "source", host);
    auto network = Json::parse(std::ifstream(directory.path / "source/nested/child.network.json"));
    network["allow"] = {{{"from", kApi}, {"to", "hypervisor:host-metrics"}}};
    WriteJson(directory.path / "source/nested/child.network.json", network);
    auto definitions = ReadSourceDefinitions(directory.path / "source");
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& connection = plan.vms.at(kApi).variables.at("connections").at("host-metrics");
    EXPECT_EQ(connection.at("dns_socket"), "host-metrics." + topology.network_domains.at(kRoot) + ":19273");
    EXPECT_EQ(connection.at("address"), topology.definitions.site.hypervisor_address);
    const auto found = std::find_if(plan.policy.connections.begin(), plan.policy.connections.end(), [](const auto& flow) {
        return flow.from == kApi && flow.to == "hypervisor" && flow.service_name == "host-metrics";
    });
    ASSERT_NE(found, plan.policy.connections.end());
    const auto& flow = *found;
    EXPECT_EQ(flow.to, "hypervisor");
    EXPECT_EQ(flow.target_ports, std::vector<std::uint16_t>{19273});
    EXPECT_EQ(flow.routers, std::vector<std::string>({kChild, kRoot}));
    EXPECT_EQ(flow.nat_network, kRoot);
    EXPECT_EQ(flow.nat_address, FormatIpv4(plan.addresses.vms.at(kEdge).at("wan").address.address));
    EXPECT_NE(plan.vms.at(kGate).dns.corefile.find(connection.at("fqdn").get<std::string>()), std::string::npos);
    const auto pods = plan.kubernetes.nodes.at(kApi).pods.ToString();
    const auto& nat = plan.policy.firewalls.at(kEdge).postrouting;
    EXPECT_TRUE(std::any_of(nat.begin(), nat.end(), [&](const auto& rule) {
        return rule.find("ip saddr " + pods) != std::string::npos &&
               rule.find("snat to " + flow.nat_address) != std::string::npos;
    }));

    network["allow"][0]["ports"] = {19273};
    WriteJson(directory.path / "source/nested/child.network.json", network);
    EXPECT_THROW(ReadSourceDefinitions(directory.path / "source"), ConfigurationError);
}

}  // namespace skeleton::test
