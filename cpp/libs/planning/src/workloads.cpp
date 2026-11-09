#include "workloads.hpp"

#include <regex>

namespace skeleton {
namespace {

void ValidateApplicationPatches(const Json& payload, const Json& patches) {
    for (const auto& patch : patches) {
        const auto path = patch.at("path").get<std::string>();
        const auto from = patch.at("from").get<std::string>();
        if (patch.at("op") != "copy" || !from.starts_with("/context/") ||
            (!path.starts_with("/chart/") && !path.starts_with("/resources/") &&
                !path.starts_with("/cluster_resources/") && !path.starts_with("/guest/") &&
                !path.starts_with("/delegated_resources/") && !path.starts_with("/files/")))
            throw ConfigurationError("workload patches may only copy prepared context into the application payload");
        const auto pointer = Json::json_pointer(path);
        if (from.starts_with("/context/compute/") && payload.contains(pointer))
            throw ConfigurationError("compute binding duplicates a native resource or replica declaration at " + path);
    }
}

Json& ResolveFormattedField(Json& payload, const Json& field) {
    const auto path = field.get<std::string>();
    if (path == "/context" || path.starts_with("/context/"))
        throw ConfigurationError("native field formatting cannot modify the prepared workload context");
    return payload.at(Json::json_pointer(path));
}

std::string SerializeJsonLines(const Json& records) {
    if (!records.is_array()) throw ConfigurationError("NDJSON fields require an array of objects");
    std::string result;
    for (const auto& record : records) {
        if (!record.is_object()) throw ConfigurationError("each NDJSON record must be an object");
        result += record.dump() + "\n";
    }
    return result;
}

void FormatNativeFields(Json& payload, const Json& definition) {
    for (const auto& field : definition.value("json_fields", Json::array())) {
        auto& value = ResolveFormattedField(payload, field);
        value = value.dump(2) + "\n";
    }
    for (const auto& field : definition.value("ndjson_fields", Json::array())) {
        auto& value = ResolveFormattedField(payload, field);
        value = SerializeJsonLines(value);
    }
}

Json BindApplication(const InfrastructurePlan& plan, const WorkloadDefinition& source,
                     const std::map<std::string, PodResourcePlan>& compute) {
    const auto& definition = source.specification;
    Json chart = Json::object();
    if (definition.contains("component"))
        chart = plan.topology.definitions.site.asset_data.at("components").at(definition.at("component").get<std::string>()).value("chart", Json::object());
    chart.merge_patch(definition.value("chart", Json::object()));
    Json payload{{"context", BuildApplicationContext(plan, definition, compute, source.patches)},
        {"chart", chart},
        {"resources", definition.value("resources", Json::array())},
        {"cluster_resources", definition.value("cluster_resources", Json::array())},
        {"delegated_resources", definition.value("delegated_resources", Json::array())},
        {"files", definition.value("files", Json::object())},
        {"guest", definition.value("guest", Json::object())}};
    const auto& patches = source.patches;
    ValidateComputeBindings(compute, patches);
    ValidateApplicationPatches(payload, patches);
    payload = payload.patch(patches);
    FormatNativeFields(payload, definition);
    return payload;
}

void PrepareApplicationChart(ApplicationPlan& application, const InfrastructurePlan& plan, const Json& definition) {
    if (definition.contains("component"))
        application.chart_archive = plan.topology.definitions.site.asset_data.at("components")
            .at(definition.at("component").get<std::string>()).value("chart_archive", Json::object());
    application.chart_archive.merge_patch(definition.value("chart_archive", Json::object()));
    if (!application.chart.empty()) {
        application.chart["namespace"] = application.network_namespace;
        application.chart["releaseName"] = application.name;
        if (!application.chart_archive.empty()) {
            const auto directory = plan.kubernetes.settings.at("paths").at("chart_directory").get<std::string>();
            const auto checksum = application.chart_archive.at("checksum").get<std::string>();
            if (!std::regex_match(checksum, std::regex("sha256:[a-f0-9]{64}")))
                throw ConfigurationError("chart archives require a pinned SHA-256 checksum");
            application.chart_archive["path"] = (std::filesystem::path(directory) / (application.name + "-" + checksum.substr(7) + ".tgz")).string();
            application.chart["chartName"] = application.chart_archive.at("path");
        }
    }
}

ApplicationPlan BuildApplication(const InfrastructurePlan& plan, const WorkloadDefinition& source) {
    const auto& definition = source.specification;
    ApplicationPlan application;
    application.name = source.name;
    if (definition.contains("component")) application.component = definition.at("component").get<std::string>();
    application.compute = BuildApplicationCompute(source);
    const auto payload = BindApplication(plan, source, application.compute);
    application.vm = source.vm;
    application.network_namespace = payload.at("context").at("namespace");
    application.fingerprint = payload.at("context").at("fingerprint");
    application.storage = payload.at("context").at("storage");
    application.enabled = source.enabled;
    application.initially_running = plan.kubernetes.nodes.at(application.vm).initially_running;
    application.resources = ResolveApplicationResources(payload, application.network_namespace, plan.kubernetes.namespaces);
    application.files = ResolveApplicationFiles(payload);
    application.chart = payload.at("chart");
    application.guest = payload.at("guest");
    PrepareApplicationChart(application, plan, definition);
    if (application.resources.empty() && application.chart.empty() && application.guest.empty())
        throw ConfigurationError("workload " + application.name + " has no chart, resources or guest configuration");
    application.readiness = definition.value("readiness", Json::array());
    application.dependencies = definition.value("depends_on", std::vector<std::string>{});
    application.required_secrets = definition.value("required_secrets", std::vector<std::string>{});
    application.managed_secrets = definition.value("secrets", Json::object());
    return application;
}

}  // namespace

void BuildApplicationPlans(InfrastructurePlan& plan) {
    const auto& workloads = plan.topology.definitions.site.workloads;
    if (!workloads) return;
    for (const auto& definition : *workloads) {
        const auto& name = definition.name;
        try {
            auto application = BuildApplication(plan, definition);
            if (!plan.applications.emplace(application.name, std::move(application)).second)
                throw ConfigurationError("duplicate application name " + name);
        } catch (const Json::exception& error) {
            throw ConfigurationError("workload " + name + ": " + error.what());
        }
    }
    PrepareApplicationSecrets(plan);
    OrderApplications(plan);
}

}  // namespace skeleton
