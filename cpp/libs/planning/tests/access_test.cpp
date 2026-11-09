#include <gtest/gtest.h>
#include <iterator>
#include <sstream>

#include "kubernetes_fixture.hpp"
#include "hypervisor_fixture.hpp"
#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::test {

TEST(ControllerAccess, PreparesOneHypervisorJumpAndOnlyThePreparedGuestSshSockets) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings = HypervisorJson();
    definitions.site.hypervisor_settings["hypervisor_sshd"] = SshPolicyJson();
    definitions.site.hypervisor_alias = "workstation-gateway";
    definitions.site.services["ssh"]["gateway"] = "hypervisor";
    definitions.site.services["ssh"]["port"] = 2222;
    definitions.vms.at(kApi).domain["running"] = false;
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    std::set<std::string> expected;
    for (const auto& [id, vm] : plan.vms) {
        EXPECT_EQ(vm.connection.at("gateway"), "workstation-gateway");
        EXPECT_EQ(vm.connection.at("port"), 2222);
        const auto arguments = vm.connection.at("ssh_args").get<std::string>();
        EXPECT_NE(arguments.find("-o ProxyJump=workstation-gateway"), std::string::npos);
        EXPECT_NE(arguments.find("-o HostKeyAlias=" + id), std::string::npos);
        expected.insert(vm.connection.at("host").get<std::string>() + ":" +
                        std::to_string(vm.connection.at("port").get<unsigned>()));
    }
    const auto targets = plan.hypervisor.at("sshd").at("matches").at(1).at("options").at("PermitOpen").get<std::string>();
    std::istringstream stream(targets);
    const std::set<std::string> actual(std::istream_iterator<std::string>{stream}, std::istream_iterator<std::string>{});
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(plan.hypervisor.at("sshd").at("matches").at(0).at("options").at("PermitOpen"), "127.0.0.1:8888");
}

TEST(ControllerAccess, RequiresAnExplicitSupportedGuestGatewayMode) {
    for (const auto mode : {Json(), Json("router-chain"), Json(true)}) {
        auto definitions = Definitions();
        if (mode.is_null()) definitions.site.services["ssh"].erase("gateway");
        else definitions.site.services["ssh"]["gateway"] = mode;
        const auto topology = BuildTopology(definitions);

        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(Planning, PreparesRegistryTrustOnlyForPermittedClients) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const std::string service = "30000000-0000-4000-8000-000000000009";
    definitions.networks.at(kRoot).exports.push_back({service, "registry", kDb, "tcp", 15000, 5000, "internal", {}});
    definitions.networks.at(kChild).allow = {{kApi, service, "", {}}};
    definitions.site.services["kubernetes"]["registry"] = {{"service", service},
        {"directory", "/etc/k0s/containerd.d/certs.d"}, {"dropin", "/etc/k0s/containerd.d/registry.toml"},
        {"system_ca", "/usr/local/share/ca-certificates/infra.crt"}, {"docker_directory", "/etc/docker/certs.d"},
        {"ca_filename", "infrastructure.crt"}};
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& trust = plan.vms.at(kApi).variables.at("registry_trust");
    EXPECT_EQ(trust.at("authority"), plan.topology.service_domains.at("registry") + ":15000");
    EXPECT_EQ(trust.at("directory"), "/etc/k0s/containerd.d/certs.d/" + trust.at("authority").get<std::string>());
    EXPECT_EQ(trust.at("ca_file"), trust.at("directory").get<std::string>() + "/infrastructure.crt");
    EXPECT_NE(trust.at("hosts_content").get<std::string>().find("capabilities = [\"pull\", \"resolve\"]"), std::string::npos);
    EXPECT_EQ(plan.vms.at(kDb).variables.count("registry_trust"), 0);
}

TEST(Planning, RejectsADeclaredMemoryBudgetAboveTheHypervisorCapacity) {
    auto definitions = Definitions();
    definitions.site.services["capacity"] = {
        {"hypervisor_memory_kib", 8 * 1024 * 1024}, {"active_vm_limit_mib", 8193}};
    const auto topology = BuildTopology(std::move(definitions));

    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Planning, GivesDemoUfwTheSameRouterTransitAndLocalPermissions) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.profiles["router"]["ufw"] = {
        {"state", "enabled"}, {"defaults", {{"incoming", "deny"}, {"outgoing", "deny"}, {"routed", "deny"}}}};
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& rules = plan.vms.at(kGate).variables.at("network").at("ufw").at("rules");
    bool transit = false, bgp = false, dns = false;
    for (const auto& rule : rules) {
        const auto port = rule.value("port", std::string());
        if (port == "5432" && rule.value("route", false)) transit = true;
        if (port == "179" && !rule.value("route", false)) bgp = true;
        if (port == "53" && rule.at("direction") == "in") dns = true;
    }
    EXPECT_TRUE(transit);
    EXPECT_TRUE(bgp);
    EXPECT_TRUE(dns);
}

TEST(Planning, AddsClusterControlAndOnlyTheCommonEgressDeclaredInJson) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto site = SiteJson();
    site["services"]["kubernetes"] = definitions.site.services.at("kubernetes");
    site["profiles"] = definitions.site.profiles;
    site["services"]["firewall"]["common_allow"] = {{{"to", "internet"}, {"protocol", "tcp"}, {"ports", {443}}}};
    WriteJson(directory.path / "site.json", site);
    const auto topology = BuildTopology(ReadSourceDefinitions(directory.path));

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& worker = plan.vms.at(kDb).variables.at("network").at("files").at("/etc/nftables.conf").get<std::string>();
    const auto& controller = plan.vms.at(kApi).variables.at("network").at("files").at("/etc/nftables.conf").get<std::string>();
    EXPECT_NE(worker.find("6443"), std::string::npos);
    EXPECT_EQ(worker.find("8132"), std::string::npos);
    EXPECT_NE(controller.find("10250"), std::string::npos);
    EXPECT_NE(worker.find("tcp dport 443 accept"), std::string::npos);
    EXPECT_EQ(worker.find("tcp dport 80 accept"), std::string::npos);
    const auto source = plan.kubernetes.nodes.at(kDb).address;
    const auto target = plan.kubernetes.nodes.at(kApi).address;
    EXPECT_EQ(worker.find("ip saddr " + source + " ip daddr " + target + " ct state new tcp dport 5432"), std::string::npos);
}

}  // namespace skeleton::test
