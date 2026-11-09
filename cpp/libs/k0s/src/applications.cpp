#include "native.hpp"

namespace skeleton::k0s {
namespace {

Json ApplicationResources(const ApplicationPlan& application) {
    auto items = application.resources;
    if (!application.chart.empty()) {
        auto specification = application.chart;
        if (specification.contains("values")) specification["values"] = specification.at("values").dump(2);
        // The k0s Helm controller accepts Chart objects only in kube-system.
        items.push_back({{"apiVersion", "helm.k0sproject.io/v1beta1"}, {"kind", "Chart"},
            {"metadata", {{"name", application.name}, {"namespace", "kube-system"}}},
            {"spec", specification}});
    }
    return {{"apiVersion", "v1"}, {"kind", "List"}, {"items", items}};
}

Json ApplicationDeployment(const InfrastructurePlan& plan, const ApplicationPlan& application) {
    const auto stack = plan.kubernetes.settings.at("deployment").at("stack_prefix").get<std::string>() + "-app-" + application.name;
    const auto directory = std::filesystem::path(plan.kubernetes.settings.at("paths").at("manifest_directory").get<std::string>()) / stack;
    Json stack_files = Json::object();
    if (!application.resources.empty() || !application.chart.empty())
        stack_files["application.yaml"] = "k0s/applications/" + application.name + "/manifests.json";
    return {{"vm", application.vm}, {"enabled", application.enabled}, {"initially_running", application.initially_running},
        {"namespace", application.network_namespace}, {"chart_archive", application.chart_archive},
        {"fingerprint", application.fingerprint},
        {"has_chart", !application.chart.empty()}, {"guest", application.guest},
        {"readiness", application.readiness}, {"required_secrets", application.required_secrets},
        {"managed_secrets", application.managed_secrets},
        {"order", application.deployment_order},
        {"required_vms", application.required_vms},
        {"stack", {{"name", stack}, {"directory", directory.string()},
            {"files", stack_files}}}};
}

}  // namespace

void AddApplicationConfigurations(GeneratedFiles& files, const InfrastructurePlan& plan) {
    if (!plan.topology.definitions.site.workloads) return;
    Json applications = Json::object();
    for (const auto& [name, application] : plan.applications) {
        const auto root = std::filesystem::path("k0s/applications") / name;
        if (!application.resources.empty() || !application.chart.empty())
            files[root / "manifests.json"] = ApplicationResources(application).dump(2) + "\n";
        if (application.chart.contains("values")) files[root / "values.json"] = application.chart.at("values").dump(2) + "\n";
        for (const auto& [path, content] : application.files) files[root / path] = content;
        applications[name] = ApplicationDeployment(plan, application);
    }
    files["k0s/applications/deployment.json"] = Json{{"controller", plan.kubernetes.controller},
        {"order", plan.application_order}, {"applications", applications}}.dump(2) + "\n";
    files["k0s/secrets.json"] = plan.secret_configuration.dump(2) + "\n";
}

}  // namespace skeleton::k0s
