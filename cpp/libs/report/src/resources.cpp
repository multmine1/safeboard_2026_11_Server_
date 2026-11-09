#include "skeleton/report/export.hpp"

namespace skeleton {
namespace {

Json SerializeTotals(const ResourceTotals& resources) {
    return {{"vm_count", resources.vm_count}, {"vcpu", resources.vcpu},
            {"ram_mib", resources.ram_mib}, {"disk_gib_by_storage", resources.disk_gib_by_storage}};
}

Json SerializeMemory(const std::optional<MemoryAvailability>& memory) {
    if (!memory) return nullptr;
    return {{"hypervisor_memory_kib", memory->budget.HypervisorMemoryKiB()},
            {"active_vm_limit_mib", memory->budget.ActiveVmLimitMiB()},
            {"hypervisor_reserve_kib", memory->budget.HypervisorReserveKiB()},
            {"vm_headroom_mib", memory->vm_headroom_mib}};
}

Json SerializeStartupMemory(const std::optional<StartupMemoryAssessment>& startup) {
    if (!startup) return nullptr;
    return {{"within_budget", startup->within_budget}, {"additional_mib", startup->additional_mib},
            {"remaining_mib", startup->remaining_mib}, {"shortfall_mib", startup->shortfall_mib}};
}

Json SerializeVms(const InfrastructurePlan& plan) {
    Json vms = Json::object();
    for (const auto& [id, vm] : plan.resources.vms)
        vms[id] = {{"fqdn", plan.topology.vm_domains.at(id)},
            {"initially_running", vm.initially_running}, {"kubernetes_node", vm.kubernetes_node},
            {"resources", SerializeTotals(vm.resources)}, {"startup_memory", SerializeStartupMemory(vm.startup_memory)}};
    return vms;
}

}  // namespace

void AddResourceReport(GeneratedFiles& files, const InfrastructurePlan& plan) {
    const auto& resources = plan.resources;
    const Json totals{{"configured", SerializeTotals(resources.configured)},
        {"initially_running", SerializeTotals(resources.initially_running)},
        {"kubernetes_configured", SerializeTotals(resources.kubernetes_configured)},
        {"kubernetes_initially_running", SerializeTotals(resources.kubernetes_initially_running)}};
    files["reports/resources.json"] = Json{{"power_basis", "source_initial_running"}, {"totals", totals},
        {"memory", SerializeMemory(resources.memory)}, {"vms", SerializeVms(plan)}}.dump(2) + "\n";
}

}  // namespace skeleton
