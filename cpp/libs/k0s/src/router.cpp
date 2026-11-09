#include "native.hpp"

namespace skeleton::k0s {
namespace {

Json ConfigMap(const std::string& name, const RouterRuntimePlan& router, const Json& data) {
    return {{"apiVersion", "v1"}, {"kind", "ConfigMap"},
        {"metadata", {{"name", name}, {"namespace", router.settings.at("namespace")}}}, {"data", data}};
}

Json VolumeMount(const std::string& name, const std::string& path, bool read_only) {
    Json mount{{"name", name}, {"mountPath", path}};
    if (read_only) mount["readOnly"] = true;
    return mount;
}

Json BuildDnsItems(const SiteDefinition& site, const RouterRuntimePlan& router) {
    const auto& dns = site.services.at("dns");
    const auto corefile = std::filesystem::path(dns.at("corefile_path").get<std::string>());
    const auto zones = std::filesystem::path(dns.at("zones_directory").get<std::string>()).lexically_relative(corefile.parent_path());
    if (zones.empty() || zones.is_absolute() || std::find(zones.begin(), zones.end(), "..") != zones.end())
        throw ConfigurationError("router DNS zones_directory must be within the Corefile directory");
    Json items = Json::array({{{"key", "Corefile"}, {"path", corefile.filename().string()}}});
    for (const auto& [name, value] : router.dns_data.items())
        if (name != "Corefile") items.push_back({{"key", name}, {"path", (zones / name).lexically_normal().generic_string()}});
    return items;
}

Json BuildInitContainers(const RouterRuntimePlan& router) {
    const auto& settings = router.settings;
    Json arguments = Json::array({"-L"});
    for (const auto& [name, value] : router.payload.items())
        arguments.push_back((std::filesystem::path(settings.at("payload_mount").get<std::string>()) / name).string());
    arguments.push_back(settings.at("state_mount"));
    Json stage{{"name", "stage"}, {"image", settings.at("init_image")}, {"imagePullPolicy", settings.at("pull_policy")},
        {"command", {"cp"}}, {"args", arguments}, {"securityContext", settings.at("copy_security")},
        {"resources", settings.at("init_resources")}, {"volumeMounts", {
            VolumeMount("payload", settings.at("payload_mount"), true), VolumeMount("state", settings.at("state_mount"), false)}}};
    Json apply{{"name", "apply"}, {"image", settings.at("init_image")}, {"imagePullPolicy", settings.at("pull_policy")},
        {"command", {"nsenter"}}, {"args", {"-t", "1", "-m", "-r", "--", "ansible-playbook", settings.at("apply_playbook")}},
        {"securityContext", settings.at("init_security")}, {"resources", settings.at("init_resources")}};
    return Json::array({stage, apply});
}

Json Probe(const SiteDefinition& site, const RouterRuntimePlan& router, const std::string& path, const std::string& port) {
    auto probe = router.settings.at("probe_settings");
    probe["httpGet"] = {{"host", site.services.at("dns_resolver").at("local_address")},
        {"path", path}, {"port", router.settings.at(port)}};
    return probe;
}

Json BuildDnsContainer(const SiteDefinition& site, const RouterRuntimePlan& router) {
    const auto path = std::filesystem::path(site.services.at("dns").at("corefile_path").get<std::string>());
    Json environment = Json::array();
    for (const auto& [name, value] : router.environment.items()) environment.push_back({{"name", name}, {"value", value}});
    return {{"name", "dns"}, {"image", router.settings.at("dns_image")},
        {"imagePullPolicy", router.settings.at("pull_policy")}, {"args", {"-conf", path.string()}},
        {"env", environment}, {"securityContext", router.settings.at("dns_security")},
        {"resources", router.settings.at("resources")},
        {"volumeMounts", {VolumeMount("dns", path.parent_path().string(), true)}},
        {"readinessProbe", Probe(site, router, "/ready", "ready_port")},
        {"livenessProbe", Probe(site, router, "/health", "health_port")}};
}

Json BuildDaemonSet(const InfrastructurePlan& plan, const RouterRuntimePlan& router) {
    const auto& settings = router.settings;
    const Json labels{{"skeleton.vm", router.id}, {"app.kubernetes.io/name", "router"}};
    Json tolerations = plan.kubernetes.nodes.at(router.id).taints;
    for (auto& item : tolerations) item["operator"] = "Equal";
    Json pod{{"hostNetwork", true}, {"hostPID", true}, {"nodeSelector", {{"skeleton.vm", router.id}}},
        {"serviceAccountName", settings.at("service_account")}, {"tolerations", tolerations},
        {"initContainers", BuildInitContainers(router)}, {"containers", {BuildDnsContainer(plan.topology.definitions.site, router)}},
        {"volumes", {
            {{"name", "payload"}, {"configMap", {{"name", "payload-" + router.id}}}},
            {{"name", "state"}, {"hostPath", {{"path", settings.at("state_directory")}, {"type", "DirectoryOrCreate"}}}},
            {{"name", "dns"}, {"configMap", {{"name", "dns-" + router.id},
                {"items", BuildDnsItems(plan.topology.definitions.site, router)}}}}}}};
    const Json metadata{{"labels", labels}, {"annotations", {{"skeleton.config-hash", router.fingerprint}}}};
    const Json pod_template{{"metadata", metadata}, {"spec", pod}};
    const Json spec{{"selector", {{"matchLabels", labels}}}, {"template", pod_template}};
    return {{"apiVersion", "apps/v1"}, {"kind", "DaemonSet"},
        {"metadata", {{"name", "router-" + router.id}, {"namespace", settings.at("namespace")}}}, {"spec", spec}};
}

Json BuildRouterRbac(const InfrastructurePlan& plan) {
    const auto& settings = plan.kubernetes.settings.at("router_runtime");
    const auto ns = settings.at("namespace").get<std::string>();
    const auto sa = settings.at("service_account").get<std::string>();
    const auto role = ns + "-" + sa;
    Json items = Json::array();
    items.push_back({{"apiVersion", "v1"}, {"kind", "Namespace"},
        {"metadata", {{"name", ns}, {"labels", settings.at("namespace_labels")}}}});
    items.push_back({{"apiVersion", "v1"}, {"kind", "ServiceAccount"},
        {"metadata", {{"name", sa}, {"namespace", ns}}}});
    items.push_back({{"apiVersion", "rbac.authorization.k8s.io/v1"}, {"kind", "ClusterRole"},
        {"metadata", {{"name", role}}}, {"rules", plan.topology.definitions.site.asset_data.at("dns_rbac")}});
    const Json subject{{"kind", "ServiceAccount"}, {"name", sa}, {"namespace", ns}};
    const Json reference{{"apiGroup", "rbac.authorization.k8s.io"}, {"kind", "ClusterRole"}, {"name", role}};
    items.push_back({{"apiVersion", "rbac.authorization.k8s.io/v1"}, {"kind", "ClusterRoleBinding"},
        {"metadata", {{"name", role}}}, {"roleRef", reference}, {"subjects", Json::array({subject})}});
    return items;
}

}  // namespace

void AddRouterConfigurations(GeneratedFiles& files, const InfrastructurePlan& plan) {
    files["k0s/router-rbac.json"] = Json{{"apiVersion", "v1"}, {"kind", "List"}, {"items", BuildRouterRbac(plan)}}.dump(2) + "\n";
    for (const auto& [id, router] : plan.routers) {
        const auto directory = std::filesystem::path("k0s/routers") / id;
        const auto items = Json::array({ConfigMap("payload-" + id, router, router.payload),
            ConfigMap("dns-" + id, router, router.dns_data), BuildDaemonSet(plan, router)});
        files[directory / "manifests.json"] = Json{{"apiVersion", "v1"}, {"kind", "List"}, {"items", items}}.dump(2) + "\n";
        for (const auto& [name, value] : router.payload.items()) files[directory / "payload" / name] = value.get<std::string>();
    }
}

}  // namespace skeleton::k0s
