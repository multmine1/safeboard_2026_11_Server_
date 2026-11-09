#include "workloads.hpp"

#include <regex>

namespace skeleton {

namespace {

unsigned ResolveEndpointPort(const InfrastructurePlan& plan, const Json& endpoint, const Json& connection) {
    if (!endpoint.contains("via")) return connection.at(endpoint.contains("service") ? "gateway_port" : "port").get<unsigned>();
    if (!endpoint.contains("service")) throw ConfigurationError("a frontend URI must name its owned service");
    return ResolveFrontendPort(plan, endpoint.at("service"), endpoint.at("via"));
}

void BuildEndpointUris(Json& context, const InfrastructurePlan& plan, const Json& definition) {
    const auto endpoints = definition.value("endpoints", Json::object());
    for (const auto& [name, endpoint] : endpoints.items()) {
        if (endpoint.contains("connection") == endpoint.contains("service"))
            throw ConfigurationError("an application endpoint requires exactly one connection or owned service");
        const bool owned = endpoint.contains("service");
        const auto& connection = context.at(owned ? "services" : "connections")
            .at(endpoint.at(owned ? "service" : "connection").get<std::string>());
        const auto port = ResolveEndpointPort(plan, endpoint, connection);
        context["endpoints"][name] = endpoint.at("scheme").get<std::string>() + "://" +
            connection.at("fqdn").get<std::string>() + ":" + std::to_string(port) +
            endpoint.value("path", std::string{});
    }
}

void BindRegistryReferences(Json& context, const Json& definition) {
    const auto references = definition.value("registry_references", Json::object());
    for (const auto& [name, reference] : references.items()) {
        const auto connection = reference.at("connection").get<std::string>();
        const auto path = reference.at("path").get<std::string>();
        if (!context.at("connections").contains(connection) || path.empty() || path.starts_with('/') ||
            path.find_first_of("\r\n") != std::string::npos)
            throw ConfigurationError("registry reference requires a permitted connection and relative repository path: " + name);
        context["registry_references"][name] = context.at("connections").at(connection).at("dns_socket").get<std::string>() + "/" + path;
    }
}

Json SelectBoundContext(const Json& context, const Json& patches) {
    Json selected = Json::object();
    for (const auto& patch : patches) {
        const auto from = patch.at("from").get<std::string>();
        if (!from.starts_with("/context/") || from == "/context/fingerprint" ||
            from == "/context/revision_annotations" || from.starts_with("/context/revision_annotations/")) continue;
        selected[from] = context.at(Json::json_pointer(from.substr(std::string("/context").size())));
    }
    return selected;
}

void AssignApplicationRevision(Json& context, const InfrastructurePlan& plan, const Json& definition, const Json& patches) {
    auto identity = definition;
    identity.erase("_source");
    auto content = SelectBoundContext(context, patches).dump() + identity.dump();
    if (definition.contains("component"))
        content += plan.topology.definitions.site.asset_data.at("components").at(definition.at("component").get<std::string>()).dump();
    context["fingerprint"] = std::to_string(StableHash(content));
    context["revision_annotations"] = {{"skeleton.config-hash", context.at("fingerprint")}};
}

}  // namespace

namespace {

Json BuildVmWorkloadContext(const InfrastructurePlan& plan, const std::string& id) {
    if (!plan.kubernetes.nodes.contains(id) || plan.topology.IsRouter(id))
        throw ConfigurationError("application target must be an ordinary Kubernetes node: " + id);
    const auto& vm = plan.vms.at(id).variables;
    const auto& node = plan.kubernetes.nodes.at(id);
    Json context{{"id", id}, {"namespace", plan.kubernetes.namespaces.at(node.network)},
        {"node_selector", {{"skeleton.vm", id}}}, {"pod_labels", {{"skeleton.vm", id}}},
        {"address", vm.at("address")}, {"fqdn", vm.at("fqdn")},
        {"site_domain", plan.topology.definitions.site.domain}, {"connections", vm.at("connections")},
        {"mounts", Json::object()}, {"services", Json::object()}, {"endpoints", Json::object()}};
    for (const auto& mount : vm.at("mounts"))
        context["mounts"][std::filesystem::path(mount.at("path").get<std::string>()).filename().string()] = mount.at("path");
    for (const auto& service : plan.topology.definitions.networks.at(node.network).exports)
        if (service.vm == id) {
            for (const auto& name : service.Names()) {
                const auto& fqdn = plan.topology.service_domains.at(name);
                context["services"][name] = {{"port", service.target_port}, {"gateway_port", service.port},
                    {"id", service.id}, {"fqdn", fqdn}, {"dns_socket", fqdn + ":" + std::to_string(service.target_port)}};
            }
        }
    return context;
}

void BindDelegatedTargets(Json& context, const InfrastructurePlan& plan, const Json& definition) {
    context["delegations"] = context["targets"] = Json::object();
    for (const auto& delegation : definition.value("delegated_resources", Json::array())) {
        const auto network = delegation.at("network").get<std::string>();
        if (!plan.kubernetes.namespaces.contains(network)) throw ConfigurationError("unknown delegated network " + network);
        context["delegations"][network] = plan.kubernetes.namespaces.at(network);
    }
    for (const auto& target : definition.value("targets", std::vector<std::string>{})) {
        if (!plan.kubernetes.nodes.contains(target) ||
            !context["delegations"].contains(plan.kubernetes.nodes.at(target).network))
            throw ConfigurationError("delivery target must belong to an explicitly delegated network: " + target);
        context["targets"][target] = BuildVmWorkloadContext(plan, target);
    }
}

void BindSourceAssets(Json& context, const InfrastructurePlan& plan, const Json& definition) {
    const auto requested = definition.value("assets", Json::object());
    if (!requested.is_object()) throw ConfigurationError("workload assets must map aliases to declared source assets");
    for (const auto& [alias, reference] : requested.items())
        context["assets"][alias] = plan.topology.definitions.site.asset_data.at(reference.get<std::string>());
}

void BindServiceClients(Json& context, const InfrastructurePlan& plan, const Json& definition) {
    const auto& vm = definition.at("vm").get<std::string>();
    for (const auto& name : definition.value("service_clients", std::vector<std::string>{})) {
        if (!context.at("services").contains(name))
            throw ConfigurationError("client sources require a service owned by the workload VM: " + name);
        context["service_clients"][name] = Json::array();
        if (!plan.policy.service_clients.contains(vm)) continue;
        const auto& clients = plan.policy.service_clients.at(vm);
        const auto& canonical = plan.topology.ServiceNamed(name).name;
        if (clients.contains(canonical)) context["service_clients"][name] = clients.at(canonical);
    }
}

void BindMailboxAddresses(Json& context, const Json& definition) {
    context["mailboxes"] = Json::object();
    for (const auto& name : definition.value("mailboxes", std::vector<std::string>{})) {
        if (name.size() > 64 || !std::regex_match(name, std::regex("[a-z0-9][a-z0-9._+-]*")) ||
            context["mailboxes"].contains(name))
            throw ConfigurationError("mailboxes require distinct simple local account names");
        context["mailboxes"][name] = name + "@" + context.at("site_domain").get<std::string>();
    }
}

}  // namespace

Json BuildApplicationContext(const InfrastructurePlan& plan, const Json& definition,
                             const std::map<std::string, PodResourcePlan>& compute, const Json& patches) {
    auto context = BuildVmWorkloadContext(plan, definition.at("vm"));
    context["name"] = definition.at("name");
    context["storage"] = BuildApplicationStorage(plan, definition);
    const auto& api = plan.kubernetes.configuration.at("spec").at("api");
    context["api_server"] = "https://" + api.at("address").get<std::string>() + ":" + std::to_string(api.at("port").get<unsigned>());
    BindDelegatedTargets(context, plan, definition);
    BindSourceAssets(context, plan, definition);
    BindServiceClients(context, plan, definition);
    BindMailboxAddresses(context, definition);
    context["navigation"] = BuildApplicationNavigation(plan, definition);
    if (definition.contains("component"))
        context["component"] = plan.topology.definitions.site.asset_data.at("components")
            .at(definition.at("component").get<std::string>());
    BuildEndpointUris(context, plan, definition);
    BindRegistryReferences(context, definition);
    if (!compute.empty()) context["compute"] = BuildComputeContext(compute);
    AssignApplicationRevision(context, plan, definition, patches);
    return context;
}

}  // namespace skeleton
