#include <gtest/gtest.h>

#include "hypervisor_fixture.hpp"
#include "kubernetes_fixture.hpp"
#include "proxy_fixture.hpp"
#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::test {

TEST(HypervisorPlan, KeepsAdditionalControllerKeysOnTheHostWithoutChangingGuestKeys) {
    auto definitions = Definitions();
    auto host = HypervisorJson();
    host["controller_user"] = definitions.site.services.at("ssh").at("users").at(0).at("name");
    host["hypervisor_sshd"] = SshPolicyJson();
    const auto original = definitions.site.services.at("ssh").at("users").at(0).at("authorized_keys");
    host["hypervisor_sshd"]["accounts"]["controller"]["additional_authorized_keys"] = {
        original.at(0), "ssh-ed25519 AAAACONTROLLER temporary", "ssh-ed25519 AAAACONTROLLER another-comment"};
    definitions.site.hypervisor_settings = host;
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& keys = plan.hypervisor.at("sshd").at("authorized_keys").at(0).at("keys");
    EXPECT_EQ(keys.size(), original.size() + 1);
    EXPECT_EQ(keys.back(), "ssh-ed25519 AAAACONTROLLER temporary");
    EXPECT_EQ(plan.topology.definitions.site.services.at("ssh").at("users").at(0).at("authorized_keys"), original);
    for (const auto& [id, vm] : plan.vms)
        EXPECT_EQ(vm.variables.dump().find("AAAACONTROLLER"), std::string::npos);
}

TEST(HypervisorPlan, RejectsPrivateMultilineAndMalformedAdditionalControllerKeys) {
    for (const auto fault : {"private", "multiline", "empty", "not_array", "not_string"}) {
        SCOPED_TRACE(fault);
        auto definitions = Definitions();
        definitions.site.hypervisor_settings = HypervisorJson();
        definitions.site.hypervisor_settings["hypervisor_sshd"] = SshPolicyJson();
        auto& keys = definitions.site.hypervisor_settings["hypervisor_sshd"]["accounts"]["controller"]["additional_authorized_keys"];
        keys = Json::array();
        if (std::string(fault) == "private") keys.push_back("-----BEGIN OPENSSH PRIVATE KEY-----");
        if (std::string(fault) == "multiline") keys.push_back("ssh-ed25519 AAAACONTROLLER\nPermitRootLogin yes");
        if (std::string(fault) == "empty") keys.push_back("");
        if (std::string(fault) == "not_array") keys = "ssh-ed25519 AAAACONTROLLER";
        if (std::string(fault) == "not_string") keys.push_back(1);
        const auto topology = BuildTopology(definitions);
        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(HypervisorProxy, PreparesALoopbackSocketAndRoutesTheBackendThroughTheRootRouter) {
    auto definitions = Definitions();
    EnableHostProxy(definitions);
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& proxy = plan.hypervisor.at("bootstrap").at("hypervisor_socket_proxies").at(0);
    EXPECT_EQ(proxy.at("socket_path"), "/etc/systemd/system/stand-proxy-admin.socket");
    EXPECT_EQ(proxy.at("socket").at("Socket").at("ListenStream"), "127.0.0.1:443");
    EXPECT_EQ(proxy.at("service").at("Service").at("ExecStart"),
              "\"/usr/lib/systemd/systemd-socket-proxyd\" 192.0.2.10:18445");
    EXPECT_EQ(proxy.at("service").at("Service").at("DynamicUser"), "yes");
    EXPECT_EQ(proxy.at("service_path"), "/etc/systemd/system/stand-proxy-admin.service");
}

TEST(HypervisorProxy, RejectsExposedListenersPermissiveBackendsAndOverriddenEndpoints) {
    for (const auto fault : {"listener", "sources", "scope", "override", "unit_path"}) {
        SCOPED_TRACE(fault);
        auto definitions = Definitions();
        EnableHostProxy(definitions);
        auto& host = definitions.site.hypervisor_settings;
        if (std::string(fault) == "listener") host["hypervisor_services"]["admin"].erase("bind_address");
        if (std::string(fault) == "sources") definitions.networks.at(kChild).exports.back().sources = {"0.0.0.0/0"};
        if (std::string(fault) == "scope") definitions.networks.at(kChild).exports.back().scope = "internal";
        if (std::string(fault) == "override") host["hypervisor_proxy"]["socket"]["Socket"]["ListenStream"] = "127.0.0.1:9443";
        if (std::string(fault) == "unit_path") host["hypervisor_proxy"]["unit_directory"] = "/etc/../tmp";
        const auto topology = BuildTopology(definitions);
        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(HypervisorPlan, PreparesRestrictedSshAccountsAndBackupPathsFromTheCommonPlan) {
    auto definitions = Definitions();
    auto host = HypervisorJson();
    host["controller_user"] = "operator";
    host["hypervisor_user"] = "virtctl";
    host["hypervisor_sshd"] = SshPolicyJson();
    definitions.site.hypervisor_settings = host;
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000009", "git", kDb, "tcp", 13000, 3000,
        "uplink", {definitions.site.hypervisor_address + "/32"}});
    definitions.site.services["local_access"] = {{"bind_address", "127.0.0.1"}, {"services", {
        {{"service", "30000000-0000-4000-8000-000000000009"}, {"scheme", "https"}, {"local_port", 9443}}}}};
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& ssh = plan.hypervisor.at("sshd");
    EXPECT_EQ(ssh.at("global_options").at("AllowUsers"), "operator virtctl");
    EXPECT_EQ(ssh.at("matches").at(0).at("user"), "operator");
    EXPECT_EQ(ssh.at("matches").at(0).at("options").at("PermitOpen"), "127.0.0.1:8888");
    EXPECT_EQ(ssh.at("matches").at(1).at("options").at("PermitOpen"),
              "192.0.2.10:13000 " + plan.topology.service_domains.at("git") + ":13000");
    EXPECT_EQ(ssh.at("authorized_keys").at(0).at("keys"),
              definitions.site.services.at("ssh").at("users").at(0).at("authorized_keys"));
    EXPECT_NE(ssh.at("content").get<std::string>().find("Match User virtctl\n"), std::string::npos);
    EXPECT_EQ(ssh.at("backup").at("files").size(), 2);
    EXPECT_EQ(ssh.at("backup").at("files").at(0).at("mode"), "0600");
    EXPECT_EQ(ssh.at("backup").at("files").at(1).at("path"), "/etc/ssh/ssh_host_ed25519_key.pub");
    EXPECT_EQ(ssh.at("backup").at("files").at(1).at("mode"), "0644");
}

TEST(HypervisorPlan, KeepsSshServiceForwardingClosedWhenNoServiceIsPublished) {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings = HypervisorJson();
    definitions.site.hypervisor_settings["hypervisor_sshd"] = SshPolicyJson();
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.hypervisor.at("sshd").at("matches").at(1).at("options").at("PermitOpen"), "none");
}

TEST(HypervisorPlan, ResolvesDeclaredHostForwardingSettingsWithoutDuplicatingTheirAddresses) {
    auto definitions = Definitions();
    auto host = HypervisorJson();
    host["hypervisor_cockpit"] = {{"listen", "127.0.0.1:19090"}};
    host["hypervisor_sshd"] = SshPolicyJson();
    host["hypervisor_sshd"]["accounts"]["controller"]["forward_settings"] = {"/hypervisor_cockpit/listen"};
    definitions.site.hypervisor_settings = host;
    auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.hypervisor.at("sshd").at("matches").at(0).at("options").at("PermitOpen"),
              "127.0.0.1:19090 127.0.0.1:8888");
    EXPECT_EQ(plan.hypervisor.at("sshd").at("matches").at(1).at("options").at("PermitOpen"), "none");
    definitions.site.hypervisor_settings.erase("hypervisor_cockpit");
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(HypervisorPlan, RejectsUnsafeSshOverridesHostKeyPathsAndAccountCollisions) {
    for (const auto key : {"forward_override", "host_key_path", "migration_path", "account_collision"}) {
        SCOPED_TRACE(key);
        auto definitions = Definitions();
        definitions.site.hypervisor_settings = HypervisorJson();
        auto profile = SshPolicyJson();
        if (std::string(key) == "forward_override") profile["accounts"]["libvirt"]["options"]["PermitOpen"] = "any";
        else if (std::string(key) == "host_key_path") profile["host_keys"] = {"relative-host-key"};
        else if (std::string(key) == "migration_path") profile["obsolete_paths"] = {profile.at("path")};
        else definitions.site.hypervisor_settings["controller_user"] = "vmctl";
        definitions.site.hypervisor_settings["hypervisor_sshd"] = profile;
        const auto topology = BuildTopology(definitions);

        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(HypervisorPlan, PreparesLocalForwardsThroughTheRootRouterAndRequiresAnExplicitPermission) {
    auto definitions = Definitions();
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000009", "git", kDb, "tcp", 13000, 3000,
        "uplink", {definitions.site.hypervisor_address + "/32"}});
    definitions.site.services["local_access"] = {{"bind_address", "127.0.0.1"}, {"services", {
        {{"service", "30000000-0000-4000-8000-000000000009"}, {"scheme", "https"}, {"path", "/"}}}}};
    auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& access = plan.local_access;
    const auto& entry = access.at("forwards").at("git");
    EXPECT_EQ(access.at("ssh_host"), definitions.site.hypervisor_alias);
    EXPECT_EQ(entry.at("remote_host"), FormatIpv4(plan.addresses.vms.at(kEdge).at("wan").address.address));
    EXPECT_EQ(entry.at("remote_port"), 13000);
    EXPECT_EQ(entry.at("local_port"), 13000);
    EXPECT_EQ(entry.at("url"), "https://" + plan.topology.service_domains.at("git") + ":13000/");
    EXPECT_EQ(entry.at("hosts_entry"), "127.0.0.1 " + plan.topology.service_domains.at("git"));

    definitions.networks.at(kRoot).exports.back().scope = "internal";
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(HypervisorPlan, PreparesSocksAccessWithRemoteDnsAndTheRemoteServicePort) {
    auto definitions = Definitions();
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000009", "git", kDb, "tcp", 443, 3000,
        "uplink", {definitions.site.hypervisor_address + "/32"}});
    definitions.site.services["local_access"] = {{"bind_address", "127.0.0.1"},
        {"socks_port", 1080}, {"services", {
        {{"service", "30000000-0000-4000-8000-000000000009"},
         {"scheme", "https"}, {"path", "/"}, {"local_port", 9443}}}}};
    auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.local_access.at("socks_address"), "127.0.0.1:1080");
    const auto& service = plan.local_access.at("forwards").at("git");
    EXPECT_EQ(service.at("remote_name"), topology.service_domains.at("git"));
    EXPECT_EQ(service.at("remote_url"), "https://" + topology.service_domains.at("git") + ":443/");
    EXPECT_EQ(service.at("url"), "https://" + topology.service_domains.at("git") + ":9443/");
    for (const auto port : {0, 65536}) {
        definitions.site.services["local_access"]["socks_port"] = port;
        topology = BuildTopology(definitions);
        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(HypervisorPlan, SharesThePhysicalHostEndpointBetweenCockpitSshAndCertificatePlans) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["pki"] = Json::object();
    auto host = HypervisorJson();
    host["hypervisor_sshd"] = SshPolicyJson();
    host["hypervisor_services"] = {{"cockpit", {{"port", 443}, {"scheme", "https"}, {"path", "/"}}}};
    host["hypervisor_hosts"] = {{"path", "/etc/hosts"}, {"mode", "0644"}};
    host["hypervisor_cockpit"] = {{"service", "cockpit"},
        {"tls", {{"namespace", "hypervisor"}, {"name", "cockpit-tls"}}}};
    definitions.site.hypervisor_settings = host;
    definitions.site.services["local_access"] = {{"bind_address", "127.0.0.1"}, {"socks_port", 1080}, {"services", Json::array()}};
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const std::string fqdn = "cockpit." + topology.network_domains.at(topology.root);
    const auto& endpoint = plan.local_access.at("forwards").at("cockpit");
    EXPECT_EQ(endpoint.at("remote_name"), fqdn);
    EXPECT_EQ(endpoint.at("remote_host"), definitions.site.hypervisor_address);
    EXPECT_EQ(endpoint.at("remote_url"), "https://" + fqdn + ":443/");
    EXPECT_EQ(plan.hypervisor.at("bootstrap").at("hypervisor_cockpit").at("listen"),
              definitions.site.hypervisor_address + ":443");
    EXPECT_EQ(plan.hypervisor.at("bootstrap").at("hypervisor_hosts").at("content"),
              definitions.site.hypervisor_address + " " + fqdn + "\n");
    EXPECT_EQ(plan.hypervisor.at("sshd").at("matches").at(1).at("options").at("PermitOpen"),
              definitions.site.hypervisor_address + ":443 " + fqdn + ":443");
    ASSERT_EQ(plan.secret_configuration.at("certificates").size(), 1);
    EXPECT_EQ(plan.secret_configuration.at("certificates").at(0).at("subject_alt_name"),
              Json::array({"DNS:" + fqdn}));
}

TEST(HypervisorPlan, DerivesMountsPoolsAndThePhysicalBridgeFromSharedSettings) {
    TemporaryDirectory directory;
    const auto root = directory.path / "source";
    WriteSources(root);
    auto host = HypervisorJson();
    host["generator"]["storage_root"] = "/data/libvirt";
    UseHypervisorFile(root, host);
    const auto topology = BuildTopology(ReadSourceDefinitions(root));

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& vars = plan.hypervisor.at("bootstrap");
    EXPECT_EQ(vars.at("storage_mounts").size(), 2);
    EXPECT_EQ(vars.at("storage_pools").size(), 3);
    EXPECT_EQ(vars.at("storage_mounts").at(0).at("path"), "/data/libvirt/hd");
    const auto& network = vars.at("hypervisor_netplan").at("network");
    EXPECT_EQ(network.at("ethernets").at("enp1s0").at("wakeonlan"), true);
    const auto& bridge = network.at("bridges").at("br-wan");
    EXPECT_EQ(bridge.at("interfaces"), Json::array({"enp1s0"}));
    EXPECT_EQ(bridge.at("addresses"), Json::array({"192.0.2.2/24"}));
    EXPECT_EQ(bridge.at("routes").at(0).at("via"), "192.0.2.1");
    const auto& runtime = vars.at("hypervisor_runtime_netplan").at("network").at("bridges").at("br-wan");
    EXPECT_EQ(runtime.at("routes").size(), 2);
    EXPECT_EQ(runtime.at("nameservers").at("addresses"), plan.hypervisor.at("resolver").at("servers"));
    EXPECT_EQ(bridge.at("nameservers").at("addresses"), Json::array({"192.0.2.1"}));
    EXPECT_EQ(vars.at("storage_mounts").at(0).at("src"), "UUID=10000000-0000-4000-8000-000000000011");
    const auto& wake = vars.at("wake");
    EXPECT_EQ(wake.at("address"), "192.0.2.255");
    EXPECT_EQ(wake.at("port"), 9);
    EXPECT_EQ(wake.at("packet_hex").get<std::string>(),
        "ffffffffffff"
        "020000000001020000000001020000000001020000000001"
        "020000000001020000000001020000000001020000000001"
        "020000000001020000000001020000000001020000000001"
        "020000000001020000000001020000000001020000000001");
}

TEST(HypervisorPlan, ResolvesTheRemoteAdministratorInsteadOfDelegatingHostChangesToTheMac) {
    TemporaryDirectory directory;
    const auto root = directory.path / "source";
    WriteSources(root);
    auto host = HypervisorJson();
    host["hypervisor_admin_alias"] = "localhost";
    host["hypervisor_bootstrap_alias"] = "physical-host";
    UseHypervisorFile(root, host);
    auto site = Json::parse(std::ifstream(root / "site.json"));
    site["services"]["ssh"]["gateway"] = "hypervisor";
    WriteJson(root / "site.json", site);
    const auto topology = BuildTopology(ReadSourceDefinitions(root));

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.hypervisor.at("controller").at("admin_alias"), "physical-host");
    EXPECT_EQ(plan.hypervisor.at("controller").at("local_libvirt_uri"), "qemu:///system");
}

}  // namespace skeleton::test
