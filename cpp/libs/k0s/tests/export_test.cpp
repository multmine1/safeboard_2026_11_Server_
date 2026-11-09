#include <gtest/gtest.h>

#include "kubernetes_fixture.hpp"
#include "skeleton/k0s/export.hpp"

namespace skeleton::test {

TEST(K0s, ExportsPreparedApplicationFilesWithoutAllowingThemToEscapeTheirDirectory) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    Json application{{"name", "delivery"}, {"vm", kApi}, {"enabled", true},
        {"resources", {{{"apiVersion", "v1"}, {"kind", "ConfigMap"}, {"metadata", {{"name", "delivery"}}}, {"data", Json::object()}}}},
        {"files", {{"delivery.yml", Json::object()}}},
        {"patches", {{{"op", "copy"}, {"from", "/context/namespace"}, {"path", "/files/delivery.yml/namespace"}}}},
        {"json_fields", {"/files/delivery.yml"}}};
    SetWorkloads(definitions, Json::array({application}));
    auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddK0sConfigurations(files, plan);

    EXPECT_EQ(Json::parse(files.at("k0s/applications/delivery/delivery.yml")).at("namespace"),
        plan.applications.at("delivery").network_namespace);
    application["files"]["../cluster.json"] = "escape";
    SetWorkloads(definitions, Json::array({application}));
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(K0s, ExportsCertificateSubjectsAndTheSealerNamespaceWithoutPrivateValues) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["pki"] = {{"sealing_application", "sealer"}};
    definitions.networks.at(kRoot).exports.push_back({
        "30000000-0000-4000-8000-000000000003", "web-api", kDb, "tcp", 8443, 8443, "internal", {}});
    const auto workload = [](const std::string& name, const std::string& vm) {
        return Json{{"name", name}, {"vm", vm}, {"enabled", true},
            {"resources", {{{"apiVersion", "v1"}, {"kind", "ConfigMap"},
                {"metadata", {{"name", name}}}, {"data", Json::object()}}}}};
    };
    auto web = workload("web", kDb);
    web["secrets"]["web-tls"] = {{"provider", "certificate"}, {"services", {"web-api"}}};
    SetWorkloads(definitions, Json::array({web, workload("sealer", kApi)}));
    auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddK0sConfigurations(files, plan);

    const auto secrets = Json::parse(files.at("k0s/secrets.json"));
    EXPECT_EQ(secrets.at("sealing").at("namespace"), plan.kubernetes.namespaces.at(kChild));
    EXPECT_EQ(secrets.at("certificates").at(0).at("common_name"), plan.topology.vm_domains.at(kDb));
    EXPECT_EQ(secrets.at("certificates").at(0).at("subject_alt_name"), Json({
        "DNS:" + plan.topology.vm_domains.at(kDb), "DNS:" + plan.topology.service_domains.at("web-api")}));
    EXPECT_EQ(secrets.at("certificates").at(0).at("namespace"), plan.kubernetes.namespaces.at(kRoot));
    EXPECT_FALSE(secrets.at("certificates").at(0).contains("private_key"));
    web["secrets"]["web-tls"]["services"] = {"foreign-service"};
    definitions.site.workloads->at(0) = ReadWorkloadDefinitions(Json::array({web}), Json::object()).front();
    topology = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
}

TEST(K0s, SerializesPreparedPoolsPhysicalPeersAndNodeInstallation) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["paths"]["data_directory"] = "/var/lib/example-cluster";
    definitions.site.services["kubernetes"]["bgp"]["port"] = 1179;
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddK0sConfigurations(files, plan);

    const auto calico = Json::parse(files.at("k0s/calico.json"));
    std::size_t pools = 0, peers = 0;
    bool ipam = false;
    for (const auto& item : calico.at("items")) {
        if (item.at("kind") == "BGPConfiguration") {
            EXPECT_EQ(item.at("spec").at("nodeToNodeMeshEnabled"), false);
            EXPECT_EQ(item.at("spec").at("listenPort"), 1179);
        }
        if (item.at("kind") == "IPAMConfig") {
            ipam = true;
            EXPECT_EQ(item.at("spec").at("strictAffinity"), true);
        }
        if (item.at("kind") == "IPPool") {
            ++pools;
            EXPECT_EQ(item.at("spec").at("ipipMode"), "Never");
            EXPECT_EQ(item.at("spec").at("vxlanMode"), "Never");
        }
        if (item.at("kind") == "BGPPeer") {
            ++peers;
            EXPECT_EQ(item.at("spec").at("sourceAddress"), "None");
            EXPECT_EQ(item.at("spec").at("nextHopMode"), "Self");
            EXPECT_TRUE(item.at("spec").at("peerIP").get<std::string>().ends_with(":1179"));
        }
    }
    EXPECT_EQ(pools, 4);
    EXPECT_EQ(peers, 6);
    EXPECT_TRUE(ipam);
    const auto controller = Json::parse(files.at("k0s/nodes/" + kApi + ".json"));
    const auto dns = FormatIpv4(plan.addresses.vms.at(kGate).at("lan").address.address);
    EXPECT_EQ(controller.at("role"), "controller");
    EXPECT_EQ(controller.at("dns_address"), dns);
    EXPECT_NE(controller.at("install_argv").dump().find("--cluster-dns=" + dns), std::string::npos);
    EXPECT_EQ(controller.at("install_argv").at(2), "controller");
    EXPECT_NE(controller.at("install_argv").dump().find("/var/lib/example-cluster"), std::string::npos);
    EXPECT_NE(controller.at("install_argv").dump().find("--disable-components=coredns"), std::string::npos);
    const auto router = Json::parse(files.at("k0s/nodes/" + kGate + ".json"));
    EXPECT_NE(router.at("install_argv").dump().find("skeleton.router=true:NoSchedule"), std::string::npos);
    EXPECT_EQ(router.at("preload_argv").size(), 2);
    EXPECT_EQ(router.at("preload_argv").at(0).back(), plan.kubernetes.settings.at("router_runtime").at("init_image"));
    EXPECT_EQ(Json::parse(files.at("k0s/configuration.json")), plan.kubernetes.configuration);
    for (const auto& profile : plan.kubernetes.configuration.at("spec").at("workerProfiles")) {
        EXPECT_FALSE(profile.at("values").contains("clusterDNS"));
        EXPECT_FALSE(profile.at("values").contains("clusterDomain"));
    }
}

TEST(K0s, RejectsAsnsThatTheNativeCalicoSchemaCannotRepresent) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["asn_pool"] = {{"first", 4200000000ULL}, {"last", 4294967294ULL}};
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    EXPECT_THROW(AddK0sConfigurations(files, plan), ConfigurationError);
}

TEST(K0s, BootstrapsCalicoAgainstTheRoutedApiWithoutDependingOnServiceNat) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    const auto& calico = plan.kubernetes.configuration.at("spec").at("network").at("calico");
    const auto& environment = plan.routers.at(kEdge).environment;

    EXPECT_EQ(calico.at("envVars").at("KUBERNETES_SERVICE_HOST"), environment.at("KUBERNETES_SERVICE_HOST"));
    EXPECT_EQ(calico.at("envVars").at("KUBERNETES_SERVICE_PORT"), environment.at("KUBERNETES_SERVICE_PORT"));
    const auto& patch = calico.at("patches").back();
    EXPECT_EQ(patch.at("target").at("name"), "calico-node");
    const auto content = Json::parse(patch.at("patch").at("content").get<std::string>());
    const auto& init = content.at("spec").at("template").at("spec").at("initContainers").at(0);
    EXPECT_EQ(init.at("name"), "install-cni");
    for (const auto& variable : init.at("env"))
        EXPECT_EQ(variable.at("value"), environment.at(variable.at("name").get<std::string>()));
    EXPECT_EQ(init.at("env").size(), 2);
}

TEST(K0s, SerializesDefaultDenyAndDirectedPodRulesFromTheCommonPlan) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddK0sConfigurations(files, plan);

    const auto policies = Json::parse(files.at("k0s/policies.json"));
    bool denied = false, allowed = false;
    for (const auto& item : policies.at("items")) {
        if (item.at("kind") != "NetworkPolicy") continue;
        const auto& spec = item.at("spec");
        if (item.at("metadata").at("name") == "default-deny") {
            denied = true;
            EXPECT_EQ(spec.at("types"), Json::array({"Ingress", "Egress"}));
            EXPECT_EQ(spec.at("selector"), "all()");
        }
        if (item.at("metadata").at("name") != "vm-" + kDb) continue;
        const auto text = spec.at("ingress").dump();
        EXPECT_NE(text.find(plan.kubernetes.nodes.at(kApi).pods.ToString()), std::string::npos);
        EXPECT_NE(text.find("5432"), std::string::npos);
        allowed = true;
    }
    EXPECT_TRUE(denied);
    EXPECT_TRUE(allowed);
}

TEST(K0s, PreservesDeclaredIcmpInPodPolicies) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.networks.at(kChild).allow.push_back({kApi, kDb, "icmp", {}});
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    EXPECT_NO_THROW(AddK0sConfigurations(files, plan));

    ASSERT_TRUE(files.contains("k0s/policies.json"));
    const auto policies = Json::parse(files.at("k0s/policies.json"));
    bool found = false;
    for (const auto& item : policies.at("items"))
        if (item.at("kind") == "NetworkPolicy" && item.at("metadata").at("name") == "vm-" + kApi)
            found = item.at("spec").at("egress").dump().find("ICMP") != std::string::npos;
    EXPECT_TRUE(found);
}

TEST(K0s, BindsEachRouterToItsNodeAndThePreparedHostPayload) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddK0sConfigurations(files, plan);

    const auto manifests = Json::parse(files.at("k0s/routers/" + kGate + "/manifests.json"));
    bool found = false;
    for (const auto& item : manifests.at("items")) {
        if (item.at("kind") != "DaemonSet") continue;
        found = true;
        const auto& pod = item.at("spec").at("template");
        EXPECT_EQ(pod.at("spec").at("hostNetwork"), true);
        EXPECT_EQ(pod.at("spec").at("hostPID"), true);
        EXPECT_EQ(pod.at("spec").at("nodeSelector").at("skeleton.vm"), kGate);
        EXPECT_EQ(pod.at("metadata").at("annotations").at("skeleton.config-hash"), plan.routers.at(kGate).fingerprint);
        EXPECT_EQ(pod.at("spec").at("initContainers").size(), 2);
        EXPECT_EQ(pod.at("spec").at("containers").size(), 1);
    }
    EXPECT_TRUE(found);
    const auto payload = Json::parse(files.at("k0s/routers/" + kGate + "/payload/configuration.json"));
    EXPECT_EQ(payload.at("network"), plan.routers.at(kGate).host_configuration);
    for (const auto& [filename, content] : plan.routers.at(kGate).payload.items())
        EXPECT_EQ(files.at("k0s/routers/" + kGate + "/payload/" + filename), content.get<std::string>());
    EXPECT_EQ(manifests.dump().find("PRIVATE KEY"), std::string::npos);
}

TEST(K0s, RetainsStoppedNodesAndEnabledPoolsForManualPowerManagement) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.vms.at(kDb).domain["running"] = false;
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddK0sConfigurations(files, plan);

    const auto cluster = Json::parse(files.at("k0s/cluster.json"));
    EXPECT_EQ(std::count(cluster.at("order").begin(), cluster.at("order").end(), kDb), 1);
    EXPECT_EQ(Json::parse(files.at("k0s/nodes/" + kDb + ".json")).at("initially_running"), false);
    bool reserved = false;
    const auto calico = Json::parse(files.at("k0s/calico.json"));
    for (const auto& item : calico.at("items"))
        if (item.at("kind") == "IPPool" && item.at("metadata").at("name") == "vm-" + kDb) {
            reserved = true;
            EXPECT_FALSE(item.at("spec").value("disabled", false));
        }
    EXPECT_TRUE(reserved);
}

TEST(K0s, PreparesIndependentManifestStacksAndTheNativeInstallRecord) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddK0sConfigurations(files, plan);

    const auto cluster = Json::parse(files.at("k0s/cluster.json"));
    ASSERT_EQ(cluster.at("stacks").at("base").size(), 2);
    const auto& stack = cluster.at("stacks").at("routers").at(0);
    EXPECT_EQ(stack.at("id"), cluster.at("routers").at(0));
    EXPECT_EQ(stack.at("name"), "skeleton-router-" + stack.at("id").get<std::string>());
    const auto node = Json::parse(files.at("k0s/nodes/" + kGate + ".json"));
    EXPECT_EQ(node.at("unit_path"), "/etc/systemd/system/k0sworker.service");
    EXPECT_EQ(node.at("record_path"), "/etc/k0s/install.json");
    EXPECT_EQ(node.at("install_argv").back(), "skeleton.router=true:NoSchedule");
}

TEST(K0s, PreparesDaemonsetWaitsByNodeRoleIncludingInitiallyStoppedWorkers) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.vms.at(kDb).domain["running"] = false;
    definitions.site.services["kubernetes"]["deployment"]["system_rollouts"] = {
        {{"namespace", "kube-system"}, {"resource", "daemonset/kube-proxy"},
         {"pod_selector", "k8s-app=kube-proxy"}, {"node_roles", {"controller", "worker"}}},
        {{"namespace", "kube-system"}, {"resource", "deployment/metrics-server"}}};
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddK0sConfigurations(files, plan);

    const auto readiness = Json::parse(files.at("k0s/cluster.json")).at("system_readiness");
    EXPECT_EQ(readiness.at(0).at("nodes"), Json::array({kDb, kApi}));
    EXPECT_EQ(readiness.at(0).at("pod_selector"), "k8s-app=kube-proxy");
    EXPECT_TRUE(readiness.at(1).at("nodes").empty());
}

TEST(K0s, SerializesChartsAndKeepsTheirDeploymentSeparateFromTheRouterStacks) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    SetWorkloads(definitions, Json::array({{
        {"name", "operator"}, {"vm", kApi}, {"enabled", true},
        {"chart", {{"chartName", "/charts/operator.tgz"}, {"version", "1.2.3"},
            {"values", {{"nodeSelector", {{"skeleton.vm", kApi}}}}}}},
        {"required_secrets", {"operator-identity"}}}}));
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddK0sConfigurations(files, plan);

    const auto resource = Json::parse(files.at("k0s/applications/operator/manifests.json")).at("items").at(0);
    EXPECT_EQ(resource.at("kind"), "Chart");
    EXPECT_EQ(resource.at("spec").at("namespace"), plan.kubernetes.namespaces.at(kChild));
    EXPECT_EQ(Json::parse(resource.at("spec").at("values").get<std::string>()), plan.applications.at("operator").chart.at("values"));
    const auto deployment = Json::parse(files.at("k0s/applications/deployment.json"));
    EXPECT_EQ(deployment.at("order"), Json::array({"operator"}));
    EXPECT_EQ(deployment.at("applications").at("operator").at("required_secrets"), Json::array({"operator-identity"}));

    SetWorkloads(definitions, Json::array());
    const auto retired_topology = BuildTopology(definitions);
    const auto retired = BuildInfrastructurePlan(retired_topology, AllocateAddressPlan(retired_topology));
    GeneratedFiles retired_files;
    AddK0sConfigurations(retired_files, retired);
    const auto empty = Json::parse(retired_files.at("k0s/applications/deployment.json"));
    EXPECT_TRUE(empty.at("applications").empty());
    EXPECT_TRUE(empty.at("order").empty());
}

}  // namespace skeleton::test
