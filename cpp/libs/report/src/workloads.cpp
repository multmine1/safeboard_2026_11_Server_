#include "skeleton/report/export.hpp"

namespace skeleton {
namespace {

Json Amount(const ComputeAmount& amount) {
    return {{"cpu_millicores", amount.cpu_millicores}, {"ram_mib", amount.ram_mib}};
}

Json Totals(const ComputeTotals& total) {
    return {{"pods", total.pods}, {"requests", Amount(total.requests)}, {"limits", Amount(total.limits)}};
}

Json CapacityPolicy(const WorkloadCapacityPolicy& policy) {
    Json overrides = Json::object();
    for (const auto& [id, reserve] : policy.vm_reserves) overrides[id] = Amount(reserve);
    return {{"non_application_reserve", Amount(policy.non_application_reserve)},
            {"permit_limit_overcommit", policy.permit_limit_overcommit}, {"vm_reserves", overrides}};
}

Json Containers(const std::map<std::string, ContainerResources>& containers) {
    Json values = Json::object();
    for (const auto& [name, container] : containers)
        values[name] = {{"requests", Amount(container.Requests())}, {"limits", Amount(container.Limits())}};
    return values;
}

Json Pods(const std::map<std::string, PodResourcePlan>& pods) {
    Json values = Json::object();
    for (const auto& [name, pod] : pods)
        values[name] = {{"replicas", pod.definition.replicas}, {"replicas_are_fixed", pod.definition.fixed_replicas},
            {"per_pod", Totals(pod.per_pod)},
            {"total", Totals(pod.total)}, {"containers", Containers(pod.definition.containers)},
            {"init_containers", Containers(pod.definition.init_containers)}};
    return values;
}

Json VmBudgets(const InfrastructurePlan& plan) {
    if (!plan.workload_resources) return nullptr;
    Json values = Json::object();
    for (const auto& [id, vm] : plan.workload_resources->vms)
        values[id] = {{"fqdn", plan.topology.vm_domains.at(id)}, {"capacity", Amount(vm.capacity)},
            {"reserved", Amount(vm.reserved)}, {"budget", Amount(vm.budget)}, {"enabled", Totals(vm.enabled)},
            {"request_headroom", Amount(vm.request_headroom)}, {"cpu_limits_overcommitted", vm.cpu_limits_overcommitted},
            {"memory_limits_overcommitted", vm.memory_limits_overcommitted}};
    return values;
}

Json Applications(const InfrastructurePlan& plan) {
    Json values = Json::object();
    for (const auto& [name, application] : plan.applications) {
        Json value{{"vm", application.vm}, {"enabled", application.enabled}, {"initially_running", application.initially_running},
            {"pods", Pods(application.compute)}, {"dependencies", application.dependencies},
            {"required_vms", application.required_vms}, {"required_secrets", application.required_secrets},
            {"readiness", application.readiness}, {"storage", application.storage},
            {"pod_allocation_declared", !application.compute.empty()}};
        if (application.component) value["component"] = *application.component;
        if (!application.chart.empty())
            for (const auto key : {"chartName", "version"})
                if (application.chart.contains(key)) value["chart"][key] = application.chart.at(key);
        if (plan.workload_resources) {
            const auto& total = plan.workload_resources->applications.at(name);
            value["pod_count"] = total.pods;
            value["requests"] = Amount(total.requests);
            value["limits"] = Amount(total.limits);
        }
        values[name] = std::move(value);
    }
    return values;
}

}  // namespace

void AddWorkloadResourceReport(GeneratedFiles& files, const InfrastructurePlan& plan) {
    const auto& site = plan.topology.definitions.site;
    if (!site.workloads && !site.workload_capacity) return;
    Json totals = nullptr, policy = nullptr;
    if (plan.workload_resources) {
        totals = {{"configured", Totals(plan.workload_resources->configured)}, {"enabled", Totals(plan.workload_resources->enabled)},
                  {"source_initially_running", Totals(plan.workload_resources->source_initially_running)}};
        policy = CapacityPolicy(*plan.topology.definitions.site.workload_capacity);
    }
    files["reports/workloads.json"] = Json{{"scope", "declared_application_pods"}, {"power_basis", "source_initial_running"}, {"policy", policy},
        {"totals", totals}, {"vms", VmBudgets(plan)}, {"applications", Applications(plan)}}.dump(2) + "\n";
}

}  // namespace skeleton
