#include <gtest/gtest.h>

#include <sstream>

#include "kubernetes_fixture.hpp"
#include "hypervisor_fixture.hpp"
#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::test {
namespace {

Json Application(const std::string& name, const std::string& vm) {
    return {{"name", name}, {"vm", vm}, {"enabled", true},
        {"resources", Json::array({{{"apiVersion", "v1"}, {"kind", "ConfigMap"},
            {"metadata", {{"name", name}}}, {"data", Json::object()}}})},
        {"patches", Json::array({
            {{"op", "copy"}, {"from", "/context/namespace"}, {"path", "/resources/0/metadata/namespace"}},
            {{"op", "copy"}, {"from", "/context/connections/infra-sql/address"}, {"path", "/resources/0/data/db"}}})}};
}

SourceDefinitions HostNfsDefinitions() {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["storage"] = {{"mount_root", "/srv"}, {"fstype", "ext4"}, {"opts", "defaults"}};
    definitions.vms.at(kDb).disks.push_back({"data", "ssd", 32});
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000001", "files", kDb, "tcp", 12049, 2049, "internal", {}});
    definitions.networks.at(kChild).allow = {{kApi, "30000000-0000-4000-8000-000000000001", "", {}}};
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000002", "backup-files", kDb, "tcp", 12050, 2049,
        "uplink", {definitions.site.hypervisor_address + "/32"}});
    auto server = Application("files-server", kDb);
    server["patches"] = {{{"op", "copy"}, {"from", "/context/storage/nfs_exports"}, {"path", "/guest/nfs_exports"}}};
    server["storage"]["nfs_exports"] = {{{"name", "cloud"}, {"service", "files"},
        {"mount", "data"}, {"directory", "cloud"}, {"clients", {kApi}},
        {"options", "rw,sync,root_squash,no_subtree_check"}, {"owner", "33"}, {"group", "33"}, {"mode", "0770"},
        {"hypervisor_access", {{"service", "backup-files"}, {"options", "ro,sync,root_squash,no_subtree_check"}}}}};
    SetWorkloads(definitions, Json::array({server}));
    return definitions;
}

}  // namespace

TEST(Workloads, SharesOnlyDeclaredSourceAssetsBetweenNativeValuesAndPublishedFiles) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.asset_data["rules"] = {{"groups", Json::array({{{"name", "example"}}})}};
    auto application = Application("alerts", kDb);
    application["chart"]["values"] = Json::object();
    application["assets"] = {{"rules", "rules"}};
    application["patches"] = {
        {{"op", "copy"}, {"from", "/context/assets/rules"}, {"path", "/chart/values/rules"}},
        {{"op", "copy"}, {"from", "/context/assets/rules"}, {"path", "/files/rules.json"}}};
    application["json_fields"] = {"/files/rules.json"};
    SetWorkloads(definitions, Json::array({application}));
    auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    const auto& prepared = plan.applications.at("alerts");
    EXPECT_EQ(Json::parse(prepared.files.at("rules.json")), definitions.site.asset_data.at("rules"));
    EXPECT_EQ(prepared.chart.at("values").at("rules"), definitions.site.asset_data.at("rules"));

    definitions.site.asset_data["unused"] = {{"unrelated", "change"}};
    topology = BuildTopology(definitions);
    EXPECT_EQ(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)).applications.at("alerts").fingerprint,
              prepared.fingerprint);
    definitions.site.workloads->front().specification["assets"]["rules"] = "missing";
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, BindsNavigationAndCertificateNamesToDeclaredPrivateHostEndpoints) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["pki"]["sealing_application"] = "sealer";
    definitions.site.hypervisor_settings = HypervisorJson();
    definitions.site.hypervisor_settings["hypervisor_services"] = {
        {"admin", {{"port", 443}, {"scheme", "https"}, {"path", "/"},
                   {"bind_address", "127.0.0.1"}, {"dns_zone", "site"}}}};
    definitions.site.hypervisor_settings["hypervisor_hosts"] = {{"path", "/etc/hosts"}, {"mode", "0644"}};
    definitions.site.services["local_access"] = {{"bind_address", "127.0.0.1"}, {"services", Json::array()}};
    auto portal = Application("portal", kDb), sealer = Application("sealer", kApi);
    portal["navigation"] = {{{"label", "Control"}, {"access", "admin"}}};
    portal["secrets"]["portal-tls"] = {{"provider", "certificate"}, {"host_services", {"admin"}}};
    portal["patches"] = {{{"op", "copy"}, {"from", "/context/navigation"}, {"path", "/resources/0/data/links"}}};
    sealer["patches"] = Json::array();
    SetWorkloads(definitions, Json::array({portal, sealer}));
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& link = plan.applications.at("portal").resources.at(0).at("data").at("links").at(0);
    EXPECT_EQ(link.at("url"), "https://admin." + definitions.site.domain + ":443/");
    EXPECT_EQ(link.at("label"), "Control");
    const auto& names = plan.secret_configuration.at("certificates").at(0).at("subject_alt_name");
    EXPECT_NE(std::find(names.begin(), names.end(), "DNS:admin." + definitions.site.domain), names.end());

    definitions.site.workloads->at(0).specification["navigation"][0]["access"] = "missing";
    const auto invalid_topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(invalid_topology, AllocateAddressPlan(invalid_topology)), ConfigurationError);
}

TEST(Workloads, BindsRegistryReferencesThroughAPermittedConnectionAndKeepsCredentialBindingsLiteral) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const std::string service = "30000000-0000-4000-8000-000000000009";
    definitions.networks.at(kRoot).exports.push_back({service, "registry", kDb, "tcp", 15000, 5000, "internal", {}});
    definitions.networks.at(kChild).allow = {{kApi, service, "", {}}};
    auto app = Application("web", kApi);
    app["registry_references"] = {{"image", {{"connection", "registry"}, {"path", "{{ registry_user }}/web"}}}};
    app["patches"] = {{{"op", "copy"}, {"from", "/context/registry_references/image"}, {"path", "/files/delivery.json"}}};
    SetWorkloads(definitions, Json::array({app}));
    auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    EXPECT_EQ(plan.applications.at("web").files.at("delivery.json"),
        plan.vms.at(kApi).variables.at("connections").at("registry").at("dns_socket").get<std::string>() + "/{{ registry_user }}/web");

    definitions.networks.at(kChild).allow.clear();
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, RefusesToFormatPreparedContextAsAPublishedJsonString) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto application = Application("storage", kDb);
    application["patches"] = Json::array();
    application["json_fields"] = {"/context/storage"};
    SetWorkloads(definitions, Json::array({application}));
    const auto topology = BuildTopology(definitions);

    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, SerializesPreparedNativeOperationsAsOneJsonObjectPerLine) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto application = Application("mail", kApi);
    application["files"]["plan.ndjson"] = Json::array({{{"message", "first\nsecond"}}, {{"operation", "update"}}});
    application["patches"] = {{{"op", "copy"}, {"from", "/context/fqdn"}, {"path", "/files/plan.ndjson/1/hostname"}}};
    application["ndjson_fields"] = {"/files/plan.ndjson"};
    SetWorkloads(definitions, Json::array({application}));
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    std::istringstream stream(plan.applications.at("mail").files.at("plan.ndjson"));
    std::string line;
    ASSERT_TRUE(std::getline(stream, line));
    EXPECT_EQ(Json::parse(line).at("message"), "first\nsecond");
    ASSERT_TRUE(std::getline(stream, line));
    EXPECT_EQ(Json::parse(line).at("hostname"), topology.vm_domains.at(kApi));
    EXPECT_FALSE(std::getline(stream, line));
}

TEST(Workloads, BindsTheDeclaredSiteDomainIndependentlyOfVmAndNetworkNames) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto application = Application("mail", kApi);
    application["mailboxes"] = {"postmaster", "forgejo"};
    application["patches"] = {
        {{"op", "copy"}, {"from", "/context/site_domain"}, {"path", "/resources/0/data/mail_domain"}},
        {{"op", "copy"}, {"from", "/context/mailboxes"}, {"path", "/files/mailboxes.json"}}};
    application["json_fields"] = {"/files/mailboxes.json"};
    SetWorkloads(definitions, Json::array({application}));
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.applications.at("mail").resources.at(0).at("data").at("mail_domain"),
              definitions.site.domain);
    const auto addresses = Json::parse(plan.applications.at("mail").files.at("mailboxes.json"));
    EXPECT_EQ(addresses.at("forgejo"), "forgejo@" + definitions.site.domain);
    EXPECT_EQ(addresses.at("postmaster"), "postmaster@" + definitions.site.domain);
}

TEST(Workloads, RejectsNativeLineFormatsWithoutObjectsOrInsideThePreparedContext) {
    for (const auto fault : {"not-array", "scalar-record", "context"}) {
        SCOPED_TRACE(fault);
        auto definitions = Definitions();
        EnableKubernetes(definitions);
        auto application = Application("mail", kApi);
        application["patches"] = Json::array();
        application["files"]["plan.ndjson"] = Json::array({Json::object()});
        application["ndjson_fields"] = {"/files/plan.ndjson"};
        if (std::string(fault) == "not-array") application["files"]["plan.ndjson"] = "raw";
        if (std::string(fault) == "scalar-record") application["files"]["plan.ndjson"] = Json::array({"raw"});
        if (std::string(fault) == "context") application["ndjson_fields"] = {"/context/storage"};
        SetWorkloads(definitions, Json::array({application}));
        const auto topology = BuildTopology(definitions);
        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(Workloads, RejectsAmbiguousOrNonLocalMailboxNames) {
    for (const auto& names : {std::vector<std::string>{"mail", "mail"}, {"mail@example.test"}, {""}}) {
        auto definitions = Definitions();
        EnableKubernetes(definitions);
        auto application = Application("mail", kApi);
        application["patches"] = Json::array();
        application["mailboxes"] = names;
        SetWorkloads(definitions, Json::array({application}));
        const auto topology = BuildTopology(definitions);
        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(Workloads, IgnoresUnusedServiceContextWhenPreparingTheWorkloadRevision) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto application = Application("web", kApi);
    application["patches"] = {
        {{"op", "copy"}, {"from", "/context/address"}, {"path", "/resources/0/data/address"}},
        {{"op", "copy"}, {"from", "/context/revision_annotations"}, {"path", "/resources/0/metadata/annotations"}}};
    SetWorkloads(definitions, Json::array({application}));
    auto topology = BuildTopology(definitions);
    const auto before = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    definitions.networks.at(kChild).exports.push_back({"30000000-0000-4000-8000-000000000003",
        "unused", kApi, "tcp", 18444, 8444, "internal", {}});
    topology = BuildTopology(definitions);
    const auto after = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(before.applications.at("web").fingerprint, after.applications.at("web").fingerprint);
    EXPECT_EQ(before.applications.at("web").resources, after.applications.at("web").resources);
    definitions.site.workloads->front().specification["resources"][0]["data"]["purpose"] = "changed";
    topology = BuildTopology(definitions);
    EXPECT_NE(after.applications.at("web").fingerprint,
        BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)).applications.at("web").fingerprint);
}

TEST(Workloads, AddsDeclaredSecretRequirementsAndTheSealingDependency) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["pki"]["sealing_application"] = "sealer";
    auto web = Application("web", kDb), sealer = Application("sealer", kApi);
    web["patches"] = sealer["patches"] = Json::array();
    web["secrets"] = {{"web-tls", {{"provider", "certificate"}}},
        {"web-admin", {{"provider", "vault"}}}, {"web-ca", {{"provider", "ca"}}}};
    SetWorkloads(definitions, Json::array({web, sealer}));
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.applications.at("web").required_secrets, (std::vector<std::string>{"web-admin", "web-ca", "web-tls"}));
    EXPECT_EQ(plan.applications.at("web").dependencies, (std::vector<std::string>{"sealer"}));
    EXPECT_EQ(plan.application_order, (std::vector<std::string>{"sealer", "web"}));
    EXPECT_EQ(plan.applications.at("web").deployment_order, (std::vector<std::string>{"sealer", "web"}));
    EXPECT_EQ(plan.applications.at("web").required_vms, (std::set<std::string>{kDb, kApi}));
}

TEST(Workloads, BindsFrontendCertificatesOnlyToPermittedServiceAliases) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["pki"]["sealing_application"] = "sealer";
    const std::string service = "30000000-0000-4000-8000-000000000003";
    definitions.networks.at(kChild).exports.push_back({
        "30000000-0000-4000-8000-000000000004", "https", kApi, "tcp", 443, 443,
        "uplink", {"hypervisor"}, {"git"}});
    definitions.networks.at(kRoot).exports.push_back({service, "backend", kDb, "tcp", 13000, 3000, "internal", {}});
    definitions.networks.at(kChild).allow = {{kApi, service, "", {}}};
    auto frontend = Application("frontend", kApi), sealer = Application("sealer", kDb);
    frontend["patches"] = sealer["patches"] = Json::array();
    frontend["secrets"]["frontend-tls"] = {{"provider", "certificate"}, {"connections", {"backend"}}};
    frontend["secrets"]["public-tls"] = {{"provider", "certificate"}, {"services", {"git"}}};
    SetWorkloads(definitions, Json::array({frontend, sealer}));
    auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.secret_configuration.at("certificates").at(0).at("subject_alt_name"), Json({
        "DNS:" + plan.topology.vm_domains.at(kApi), "DNS:" + plan.topology.service_domains.at("backend")}));
    EXPECT_EQ(plan.secret_configuration.at("certificates").at(1).at("subject_alt_name"), Json({
        "DNS:" + plan.topology.vm_domains.at(kApi), "DNS:" + plan.topology.service_domains.at("git")}));
    definitions.networks.at(kChild).allow.clear();
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, DerivesTheCanonicalUrlFromAPermittedFrontend) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const std::string backend = "30000000-0000-4000-8000-000000000003";
    const std::string frontend = "30000000-0000-4000-8000-000000000004";
    const std::string cloud = "30000000-0000-4000-8000-000000000005";
    definitions.networks.at(kRoot).exports.push_back({backend, "git-backend", kDb, "tcp", 13000, 3000, "internal", {}});
    definitions.networks.at(kRoot).exports.push_back({cloud, "cloud-backend", kDb, "tcp", 18080, 8080, "internal", {}});
    definitions.networks.at(kChild).exports.push_back({frontend, "https", kApi, "tcp", 443, 443,
        "uplink", {"0.0.0.0/0"}, {"git", "cloud"}});
    definitions.networks.at(kChild).allow = {{kApi, backend, "", {}}, {kApi, cloud, "", {}}};
    definitions.networks.at(kRoot).allow.push_back({kDb, frontend, "", {}});
    definitions.site.services["local_access"] = {{"bind_address", "127.0.0.1"},
        {"services", {
            {{"service", frontend}, {"alias", "git"}, {"scheme", "https"}, {"path", "/"}},
            {{"service", frontend}, {"alias", "cloud"}, {"scheme", "https"}, {"local_port", 9443}, {"path", "/"}}}}};
    auto application = Application("git", kDb);
    application["endpoints"]["web"] = {{"connection", "git"}, {"scheme", "https"}, {"path", "/"}};
    application["patches"] = {{{"op", "copy"}, {"from", "/context/endpoints/web"}, {"path", "/resources/0/data/url"}}};
    SetWorkloads(definitions, Json::array({application}));
    auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.applications.at("git").resources.at(0).at("data").at("url"),
        "https://" + plan.topology.service_domains.at("git") + ":443/");
    ASSERT_TRUE(plan.local_access.at("forwards").contains("https-git"));
    ASSERT_TRUE(plan.local_access.at("forwards").contains("https-cloud"));
    EXPECT_EQ(plan.local_access.at("forwards").at("https-git").at("url"),
        plan.applications.at("git").resources.at(0).at("data").at("url"));
    EXPECT_EQ(plan.local_access.at("forwards").at("https-cloud").at("remote_url"),
        "https://" + plan.topology.service_domains.at("cloud") + ":443/");
    definitions.networks.at(kRoot).allow.pop_back();
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, RejectsRepeatedPublicationsOfTheSameService) {
    auto definitions = Definitions();
    const std::string backend = "30000000-0000-4000-8000-000000000003";
    const std::string frontend = "30000000-0000-4000-8000-000000000004";
    definitions.networks.at(kRoot).exports.push_back({backend, "git-backend", kDb, "tcp", 13000, 3000, "internal", {}});
    definitions.networks.at(kChild).exports.push_back({frontend, "https", kApi, "tcp", 443, 443,
        "uplink", {"0.0.0.0/0"}, {"git"}});
    definitions.networks.at(kChild).allow = {{kApi, backend, "", {}}};
    definitions.site.services["local_access"] = {{"bind_address", "127.0.0.1"},
        {"services", {
            {{"service", frontend}, {"alias", "git"}, {"scheme", "https"}},
            {{"service", frontend}, {"alias", "git"}, {"scheme", "https"}, {"local_port", 9443}}}}};
    const auto topology = BuildTopology(definitions);

    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, BindsOnlyPermittedClientSourcesForAnOwnedService) {
    const std::string service = "30000000-0000-4000-8000-000000000003";
    for (const bool cross_network : {false, true}) {
        SCOPED_TRACE(cross_network);
        auto definitions = Definitions();
        EnableKubernetes(definitions);
        if (!cross_network) {
            definitions.networks.at(kChild).members.clear();
            definitions.networks.at(kRoot).members.push_back(kApi);
        }
        definitions.networks.at(kChild).allow.clear();
        definitions.networks.at(cross_network ? kChild : kRoot).allow = {{kApi, service, "", {}}};
        definitions.networks.at(kRoot).exports.push_back({service, "backend", kDb, "tcp", 18443, 8443, "internal", {}});
        auto application = Application("web", kDb);
        application["service_clients"] = {"backend"};
        application["patches"] = {{{"op", "copy"}, {"from", "/context/service_clients/backend"},
            {"path", "/resources/0/data/proxies"}}};
        SetWorkloads(definitions, Json::array({application}));
        auto topology = BuildTopology(definitions);
        const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
        std::set<std::string> expected;
        if (cross_network) expected.insert(FormatIpv4(plan.addresses.vms.at(kEdge).at("lan").address.address) + "/32");
        else expected = {plan.kubernetes.nodes.at(kApi).address + "/32", plan.kubernetes.nodes.at(kApi).pods.ToString()};
        EXPECT_EQ(plan.applications.at("web").resources.at(0).at("data").at("proxies"), Json(expected));

        definitions.networks.at(cross_network ? kChild : kRoot).allow.clear();
        topology = BuildTopology(definitions);
        EXPECT_EQ(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)).applications.at("web")
            .resources.at(0).at("data").at("proxies"), Json::array());
    }
}

TEST(Workloads, RefusesClientSourcesForAServiceOwnedByAnotherVm) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.networks.at(kRoot).exports.push_back({"30000000-0000-4000-8000-000000000003",
        "backend", kDb, "tcp", 18443, 8443, "internal", {}});
    auto application = Application("web", kApi);
    application["patches"] = Json::array();
    application["service_clients"] = {"backend"};
    SetWorkloads(definitions, Json::array({application}));
    const auto topology = BuildTopology(definitions);

    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, DelegatesOnlyNamespacedRbacToAnExistingNetwork) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto application = Application("delivery", kApi);
    application["targets"] = {kDb};
    application["patches"] = {{{"op", "copy"}, {"from", "/context/targets/" + kDb + "/node_selector"},
        {"path", "/resources/0/data/target"}}};
    application["delegated_resources"] = {{{"network", kRoot}, {"resources", {
        {{"apiVersion", "rbac.authorization.k8s.io/v1"}, {"kind", "Role"},
            {"metadata", {{"name", "delivery"}}}, {"rules", Json::array()}}}}}};
    SetWorkloads(definitions, Json::array({application}));
    auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.applications.at("delivery").resources.size(), 2);
    EXPECT_EQ(plan.applications.at("delivery").resources.back().at("metadata").at("namespace"),
        plan.kubernetes.namespaces.at(kRoot));
    EXPECT_EQ(plan.applications.at("delivery").resources.at(0).at("data").at("target"), Json({{"skeleton.vm", kDb}}));
    EXPECT_EQ(plan.applications.at("delivery").required_vms, (std::set<std::string>{kApi}));
    definitions.site.workloads->at(0).specification["delegated_resources"][0]["resources"][0]["kind"] = "Deployment";
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, RejectsPrivateSourceFieldsAndConflictingSecretProducers) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["pki"]["sealing_application"] = "sealer";
    auto web = Application("web", kDb), sealer = Application("sealer", kApi);
    web["patches"] = sealer["patches"] = Json::array();
    web["secrets"]["admin"] = {{"provider", "vault"}, {"password", "must-stay-out-of-source"}};
    SetWorkloads(definitions, Json::array({web, sealer}));
    auto topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    web["secrets"]["admin"].erase("password");
    auto duplicate = web;
    duplicate["name"] = "another-web";
    SetWorkloads(definitions, Json::array({web, duplicate, sealer}));
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, BindsPreparedNamespaceAndOnlyAnAllowedConnection) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000001", "infra-sql", kDb, "tcp", 15432, 5432, "internal", {}});
    definitions.networks.at(kChild).allow = {{kApi, "30000000-0000-4000-8000-000000000001", "", {}}};
    definitions.site.asset_data["components"]["client"] = {{"image", "registry.invalid/client@sha256:fixture"}};
    auto definition = Application("database-client", kApi);
    definition["component"] = "client";
    definition["patches"].push_back({{"op", "copy"}, {"from", "/context/component/image"}, {"path", "/resources/0/data/image"}});
    SetWorkloads(definitions, Json::array({definition}));
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& application = plan.applications.at("database-client");
    EXPECT_TRUE(application.initially_running);
    EXPECT_EQ(application.resources.at(0).at("metadata").at("namespace"), plan.kubernetes.namespaces.at(kChild));
    EXPECT_EQ(application.resources.at(0).at("data").at("db"), plan.vms.at(kApi).variables.at("connections").at("infra-sql").at("address"));
    EXPECT_EQ(application.resources.at(0).at("data").at("image"), "registry.invalid/client@sha256:fixture");
}

TEST(Workloads, RejectsAnUnpermittedBindingAndDependencyCycles) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    SetWorkloads(definitions, Json::array({Application("client", kApi)}));
    auto topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);

    auto first = Application("first", kApi), second = Application("second", kDb);
    first["patches"] = second["patches"] = Json::array();
    first["depends_on"] = {"second"}; second["depends_on"] = {"first"};
    SetWorkloads(definitions, Json::array({first, second}));
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, RetainsTheConfigurationOfAnEnabledApplicationOnAStoppedVm) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto application = Application("paused", kDb);
    application["patches"] = Json::array();
    SetWorkloads(definitions, Json::array({application}));
    definitions.vms.at(kDb).domain["running"] = false;
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_TRUE(plan.applications.at("paused").enabled);
    EXPECT_FALSE(plan.applications.at("paused").initially_running);
    EXPECT_EQ(plan.application_order, (std::vector<std::string>{"paused"}));
    EXPECT_EQ(plan.applications.at("paused").resources.at(0).at("metadata").at("namespace"),
        plan.kubernetes.namespaces.at(kRoot));
}

TEST(Workloads, IgnoresSourceFileLocationWhenComputingTheRuntimeRevision) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto application = Application("placed", kApi);
    application["patches"] = Json::array();
    application["_source"] = "/old/layout/workload.json";
    SetWorkloads(definitions, Json::array({application}));
    const auto first_topology = BuildTopology(definitions);
    const auto first = BuildInfrastructurePlan(first_topology, AllocateAddressPlan(first_topology));
    definitions.site.workloads->at(0).specification["_source"] = "/new/workload.json";
    const auto moved_topology = BuildTopology(definitions);

    const auto moved = BuildInfrastructurePlan(moved_topology, AllocateAddressPlan(moved_topology));

    EXPECT_EQ(first.applications.at("placed").fingerprint, moved.applications.at("placed").fingerprint);
    EXPECT_EQ(first.applications.at("placed").resources, moved.applications.at("placed").resources);
}

TEST(Workloads, RejectsAnEnabledApplicationDependingOnADisabledService) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    auto consumer = Application("consumer", kApi), backend = Application("backend", kDb);
    consumer["patches"] = backend["patches"] = Json::array();
    consumer["depends_on"] = {"backend"};
    backend["enabled"] = false;
    SetWorkloads(definitions, Json::array({consumer, backend}));
    const auto topology = BuildTopology(definitions);

    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, BindsStorageToMountedDisksAndThePermittedNfsSourceAfterNat) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["storage"] = {{"mount_root", "/srv"}, {"fstype", "ext4"}, {"opts", "defaults"}};
    definitions.vms.at(kDb).disks.push_back({"fast", "nvme", 16});
    definitions.vms.at(kDb).disks.push_back({"data", "ssd", 32});
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000002", "files", kDb, "tcp", 12049, 2049, "internal", {}});
    definitions.networks.at(kChild).allow = {{kApi, "30000000-0000-4000-8000-000000000002", "", {}}};
    auto application = Application("data", kDb);
    application["storage"] = {
        {"local_paths", {{{"vm", kDb}, {"mount", "fast"}, {"directory", "kubernetes"}}}},
        {"volumes", {{"metrics", {{"mount", "fast"}, {"directory", "metrics"}}}}},
        {"nfs_exports", {{{"name", "forgejo"}, {"service", "files"}, {"mount", "data"}, {"directory", "forgejo"}, {"clients", {kApi}},
            {"options", "rw,sync,root_squash,no_subtree_check"}, {"owner", "1000"}, {"group", "1000"}, {"mode", "0750"}}}}};
    application["patches"] = {
        {{"op", "copy"}, {"from", "/context/storage/local_paths"}, {"path", "/resources/0/data/paths"}},
        {{"op", "copy"}, {"from", "/context/storage/volumes/metrics"}, {"path", "/resources/0/data/metrics"}},
        {{"op", "copy"}, {"from", "/context/storage/nfs_exports"}, {"path", "/guest/nfs_exports"}}};
    SetWorkloads(definitions, Json::array({application}));
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& storage = plan.applications.at("data");
    EXPECT_EQ(storage.storage.at("local_paths"), storage.resources.at(0).at("data").at("paths"));
    EXPECT_EQ(storage.storage.at("volumes").at("metrics"), storage.resources.at(0).at("data").at("metrics"));
    EXPECT_EQ(storage.storage.at("nfs_exports"), storage.guest.at("nfs_exports"));
    EXPECT_EQ(storage.resources.at(0).at("data").at("paths"), Json::array({{{"node", kDb}, {"paths", {"/srv/fast/kubernetes"}}}}));
    EXPECT_EQ(storage.resources.at(0).at("data").at("metrics"), "/srv/fast/metrics");
    const auto& exported = storage.guest.at("nfs_exports").at(0);
    EXPECT_EQ(exported.at("path"), "/srv/data/forgejo");
    EXPECT_EQ(exported.at("clients"), Json::array({{{"address", plan.vms.at(kEdge).variables.at("address")},
        {"options", "rw,sync,root_squash,no_subtree_check"}}}));

    definitions.networks.at(kChild).allow.clear();
    const auto forbidden = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(forbidden, AllocateAddressPlan(forbidden)), ConfigurationError);
    definitions.networks.at(kChild).allow = {{kApi, "30000000-0000-4000-8000-000000000002", "", {}}};
    definitions.site.workloads->at(0).specification["storage"]["local_paths"][0]["directory"] = "../../escape";
    const auto escaped = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(escaped, AllocateAddressPlan(escaped)), ConfigurationError);
    definitions.site.workloads->at(0).specification["storage"]["local_paths"][0] = {{"vm", kDb}, {"mount", "os"}, {"directory", "kubernetes"}};
    const auto unmounted = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(unmounted, AllocateAddressPlan(unmounted)), ConfigurationError);
}

TEST(Workloads, SharesOneNfsDefinitionWithDeclaredClientsBeforeBindingAnyWorkload) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["storage"] = {{"mount_root", "/srv"}, {"fstype", "ext4"}, {"opts", "defaults"}};
    definitions.vms.at(kDb).disks.push_back({"data", "ssd", 32});
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000002", "files", kDb, "tcp", 12049, 2049, "internal", {}});
    definitions.networks.at(kChild).allow = {{kApi, "30000000-0000-4000-8000-000000000002", "", {}}};
    auto server = Application("files-server", kDb), client = Application("client", kApi);
    server["endpoints"]["nfs"] = {{"service", "files"}, {"scheme", "nfs"}};
    server["patches"] = {{{"op", "copy"}, {"from", "/context/endpoints/nfs"}, {"path", "/resources/0/data/uri"}}};
    server["storage"]["nfs_exports"] = {{{"name", "repositories"}, {"service", "files"}, {"mount", "data"},
        {"directory", "repositories"}, {"clients", {kApi}}, {"options", "rw,sync,root_squash,no_subtree_check"},
        {"owner", "1000"}, {"group", "1000"}, {"mode", "0750"}}};
    client["patches"] = {
        {{"op", "copy"}, {"from", "/context/storage/nfs/files/repositories/volume"}, {"path", "/resources/0/data/volume"}},
        {{"op", "copy"}, {"from", "/context/storage/nfs/files/repositories/port_option"}, {"path", "/resources/0/data/port"}},
        {{"op", "copy"}, {"from", "/context/connections/files/dns_socket"}, {"path", "/resources/0/data/socket"}}};
    SetWorkloads(definitions, Json::array({client, server}));
    auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& data = plan.applications.at("client").resources.at(0).at("data");
    EXPECT_EQ(data.at("volume").at("path"), "/srv/data/repositories");
    EXPECT_EQ(data.at("volume").at("server"), plan.vms.at(kApi).variables.at("connections").at("files").at("fqdn"));
    EXPECT_EQ(data.at("port"), "port=12049");
    EXPECT_EQ(data.at("socket"), data.at("volume").at("server").get<std::string>() + ":12049");
    EXPECT_EQ(plan.applications.at("files-server").resources.at(0).at("data").at("uri"),
        "nfs://" + data.at("volume").at("server").get<std::string>() + ":12049");
    SetWorkloads(definitions, Json::array({server, client}));
    topology = BuildTopology(definitions);
    EXPECT_EQ(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)).applications.at("client").resources,
        plan.applications.at("client").resources);
    client["vm"] = kDb;
    SetWorkloads(definitions, Json::array({client, server}));
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    auto duplicate = server;
    duplicate["name"] = "another-files-server";
    SetWorkloads(definitions, Json::array({server, duplicate}));
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(Workloads, PreparesReadOnlyHypervisorNfsThroughThePublishedRouterEndpoint) {
    const auto topology = BuildTopology(HostNfsDefinitions());
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& exported = plan.applications.at("files-server").guest.at("nfs_exports").at(0);
    const auto host_client = std::find_if(exported.at("clients").begin(), exported.at("clients").end(), [&](const auto& client) {
        return client.at("address") == topology.definitions.site.hypervisor_address;
    });
    ASSERT_NE(host_client, exported.at("clients").end());
    EXPECT_EQ(host_client->at("options"), "ro,sync,root_squash,no_subtree_check");
    EXPECT_EQ(exported.at("clients").size(), 2);
    const auto& source = plan.hypervisor.at("nfs_sources").at("backup-files").at("cloud");
    EXPECT_EQ(source.at("src"), FormatIpv4(plan.addresses.vms.at(kEdge).at("wan").address.address) + ":/srv/data/cloud");
    EXPECT_EQ(source.at("port"), 12050);
    EXPECT_EQ(source.at("server_vm"), kDb);
    EXPECT_EQ(source.at("required_vms"), Json(topology.host_access_paths.at(kDb)));
}

TEST(Workloads, RejectsHostNfsWithoutAnExactHostGrantReadOnlyAccessAndMatchingServer) {
    for (const auto fault : {"internal", "unpublished", "broad-source", "writable", "unsquashed", "backend"}) {
        SCOPED_TRACE(fault);
        auto definitions = HostNfsDefinitions();
        auto& service = definitions.networks.at(kRoot).exports.back();
        auto& workload = definitions.site.workloads->front().specification;
        const auto name = std::string(fault);
        if (name == "internal") service.scope = "internal";
        if (name == "unpublished") service.sources.clear();
        if (name == "broad-source") service.sources = {"0.0.0.0/0"};
        if (name == "writable") workload["storage"]["nfs_exports"][0]["hypervisor_access"]["options"] = "rw,root_squash";
        if (name == "unsquashed") workload["storage"]["nfs_exports"][0]["hypervisor_access"]["options"] = "ro,no_root_squash";
        if (name == "backend") service.target_port = 2050;
        const auto topology = BuildTopology(definitions);
        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(Workloads, AllowsHostOnlyNfsWithoutInventingAnOrdinaryVmClient) {
    auto definitions = HostNfsDefinitions();
    auto& exports = definitions.networks.at(kRoot).exports;
    std::erase_if(exports, [](const auto& service) { return service.name == "files"; });
    definitions.networks.at(kChild).allow.clear();
    auto& exported = definitions.site.workloads->front().specification["storage"]["nfs_exports"][0];
    exported["service"] = "backup-files";
    exported["clients"] = Json::array();
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    const auto& clients = plan.applications.at("files-server").guest.at("nfs_exports").at(0).at("clients");
    ASSERT_EQ(clients.size(), 1);
    EXPECT_EQ(clients.front().at("address"), definitions.site.hypervisor_address);
    EXPECT_TRUE(plan.nfs_exports.begin()->second.clients.empty());
    EXPECT_FALSE(plan.vms.at(kApi).variables.at("connections").contains("backup-files"));

    exported.erase("hypervisor_access");
    const auto ungranted = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(ungranted, AllocateAddressPlan(ungranted)), ConfigurationError);
}

}  // namespace skeleton::test
