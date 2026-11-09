#include "skeleton/placement/resources.hpp"

#include "arithmetic.hpp"

namespace skeleton {
namespace {

using placement::AddChecked;

ResourceTotals SummarizeVmResources(const VmAllocation& allocation) {
    ResourceTotals resources{1, allocation.vcpu, allocation.ram_mib, {}};
    for (const auto& disk : allocation.disks)
        AddChecked(resources.disk_gib_by_storage[disk.storage], disk.size_gib);
    return resources;
}

void AddResources(ResourceTotals& total, const ResourceTotals& resources) {
    AddChecked(total.vm_count, resources.vm_count);
    AddChecked(total.vcpu, resources.vcpu);
    AddChecked(total.ram_mib, resources.ram_mib);
    for (const auto& [storage, size] : resources.disk_gib_by_storage)
        AddChecked(total.disk_gib_by_storage[storage], size);
}

StartupMemoryAssessment AssessStartupMemory(const VmResourcePlan& vm, std::uint64_t headroom_mib) {
    const auto additional_mib = vm.initially_running ? 0 : vm.resources.ram_mib;
    const bool fits = additional_mib <= headroom_mib;
    return {fits, additional_mib, fits ? headroom_mib - additional_mib : 0,
            fits ? 0 : additional_mib - headroom_mib};
}

void PrepareMemoryAssessments(ResourcePlan& plan, const MemoryBudget& budget) {
    if (plan.initially_running.ram_mib > budget.ActiveVmLimitMiB())
        throw ConfigurationError("active VM memory exceeds the declared budget");
    const auto headroom_mib = budget.ActiveVmLimitMiB() - plan.initially_running.ram_mib;
    plan.memory = MemoryAvailability{budget, headroom_mib};
    for (auto& [id, vm] : plan.vms)
        vm.startup_memory = AssessStartupMemory(vm, headroom_mib);
}

}  // namespace

MemoryBudget::MemoryBudget(std::uint64_t hypervisor_memory_kib, std::uint64_t active_vm_limit_mib)
    : hypervisor_memory_kib_(hypervisor_memory_kib), active_vm_limit_mib_(active_vm_limit_mib) {
    if (hypervisor_memory_kib == 0 || active_vm_limit_mib == 0 ||
        active_vm_limit_mib > hypervisor_memory_kib / 1024)
        throw ConfigurationError("active VM memory budget must be positive and fit the hypervisor capacity");
    hypervisor_reserve_kib_ = hypervisor_memory_kib - active_vm_limit_mib * 1024;
}

ResourcePlan BuildResourcePlan(const std::map<std::string, VmAllocation>& allocations,
                               const std::optional<MemoryBudget>& budget) {
    ResourcePlan plan;
    for (const auto& [id, allocation] : allocations) {
        const auto resources = SummarizeVmResources(allocation);
        AddResources(plan.configured, resources);
        if (allocation.initially_running) AddResources(plan.initially_running, resources);
        if (allocation.kubernetes_node) {
            AddResources(plan.kubernetes_configured, resources);
            if (allocation.initially_running) AddResources(plan.kubernetes_initially_running, resources);
        }
        plan.vms.emplace(id, VmResourcePlan{resources, allocation.initially_running, allocation.kubernetes_node, std::nullopt});
    }
    if (budget) PrepareMemoryAssessments(plan, *budget);
    return plan;
}

}  // namespace skeleton
