#include "skeleton/placement/workloads.hpp"
#include "arithmetic.hpp"

namespace skeleton {
namespace {

void AddAmount(ComputeAmount& total, const ComputeAmount& amount) {
    placement::AddChecked(total.cpu_millicores, amount.cpu_millicores);
    placement::AddChecked(total.ram_mib, amount.ram_mib);
}

ComputeAmount Maximum(const ComputeAmount& left, const ComputeAmount& right) {
    return {std::max(left.cpu_millicores, right.cpu_millicores), std::max(left.ram_mib, right.ram_mib)};
}

ComputeAmount Multiply(const ComputeAmount& amount, std::uint64_t replicas) {
    return {placement::MultiplyChecked(amount.cpu_millicores, replicas),
            placement::MultiplyChecked(amount.ram_mib, replicas)};
}

void AddTotals(ComputeTotals& total, const ComputeTotals& amount) {
    placement::AddChecked(total.pods, amount.pods);
    AddAmount(total.requests, amount.requests);
    AddAmount(total.limits, amount.limits);
}

bool Exceeds(const ComputeAmount& amount, const ComputeAmount& capacity) {
    return amount.cpu_millicores > capacity.cpu_millicores || amount.ram_mib > capacity.ram_mib;
}

VmWorkloadResources BuildVmBudget(const VmResourcePlan& vm, const ComputeAmount& reserve) {
    const ComputeAmount capacity{placement::MultiplyChecked(vm.resources.vcpu, 1000), vm.resources.ram_mib};
    if (Exceeds(reserve, capacity))
        throw ConfigurationError("non-application reserve exceeds VM capacity");
    const ComputeAmount budget{capacity.cpu_millicores - reserve.cpu_millicores, capacity.ram_mib - reserve.ram_mib};
    return {capacity, reserve, budget, {}, budget, false, false};
}

void AssessVmRequests(VmWorkloadResources& vm, const std::string& id, const WorkloadCapacityPolicy& policy) {
    if (Exceeds(vm.enabled.requests, vm.budget))
        throw ConfigurationError("enabled workload requests exceed the declared application budget of VM " + id);
    vm.request_headroom = {vm.budget.cpu_millicores - vm.enabled.requests.cpu_millicores,
                          vm.budget.ram_mib - vm.enabled.requests.ram_mib};
    vm.cpu_limits_overcommitted = vm.enabled.limits.cpu_millicores > vm.budget.cpu_millicores;
    vm.memory_limits_overcommitted = vm.enabled.limits.ram_mib > vm.budget.ram_mib;
    if (!policy.permit_limit_overcommit && (vm.cpu_limits_overcommitted || vm.memory_limits_overcommitted))
        throw ConfigurationError("workload limits exceed VM application budget and overcommit is forbidden: " + id);
}

}  // namespace

ContainerResources::ContainerResources(ComputeAmount requests, ComputeAmount limits)
    : requests_(requests), limits_(limits) {
    if (!requests.cpu_millicores || !requests.ram_mib || Exceeds(requests, limits))
        throw ConfigurationError("container CPU/RAM requests must be positive and no greater than limits");
}

PodResourcePlan BuildPodResourcePlan(PodDefinition definition) {
    if (!definition.replicas || definition.containers.empty())
        throw ConfigurationError("a Pod allocation requires positive replicas and application containers");
    ComputeTotals per_pod{1, {}, {}};
    for (const auto& [name, container] : definition.containers) {
        AddAmount(per_pod.requests, container.Requests());
        AddAmount(per_pod.limits, container.Limits());
    }
    for (const auto& [name, container] : definition.init_containers) {
        per_pod.requests = Maximum(per_pod.requests, container.Requests());
        per_pod.limits = Maximum(per_pod.limits, container.Limits());
    }
    const ComputeTotals total{definition.replicas, Multiply(per_pod.requests, definition.replicas),
                             Multiply(per_pod.limits, definition.replicas)};
    return {std::move(definition), per_pod, total};
}

WorkloadResourcePlan BuildWorkloadResourcePlan(const ResourcePlan& vm_resources,
    const std::map<std::string, WorkloadAllocation>& applications, const WorkloadCapacityPolicy& policy) {
    WorkloadResourcePlan plan;
    for (const auto& [id, vm] : vm_resources.vms) {
        if (!vm.kubernetes_node) continue;
        const auto& reserve = policy.vm_reserves.contains(id) ? policy.vm_reserves.at(id) : policy.non_application_reserve;
        plan.vms.emplace(id, BuildVmBudget(vm, reserve));
    }
    for (const auto& [id, reserve] : policy.vm_reserves)
        if (!plan.vms.contains(id)) throw ConfigurationError("resource reserve targets an unknown Kubernetes VM: " + id);
    for (const auto& [name, application] : applications) {
        if (!plan.vms.contains(application.vm)) throw ConfigurationError("workload target is not a declared Kubernetes VM: " + name);
        ComputeTotals total;
        for (const auto& [pod, allocation] : application.pods) AddTotals(total, allocation.total);
        plan.applications.emplace(name, total);
        AddTotals(plan.configured, total);
        if (application.enabled) {
            AddTotals(plan.enabled, total);
            AddTotals(plan.vms.at(application.vm).enabled, total);
            if (vm_resources.vms.at(application.vm).initially_running) AddTotals(plan.source_initially_running, total);
        }
    }
    for (auto& [id, vm] : plan.vms) AssessVmRequests(vm, id, policy);
    return plan;
}

}  // namespace skeleton
