#include "resources.hpp"

namespace skeleton {

ResourcePlan BuildInfrastructureResources(const Topology& topology, const KubernetesPlan& kubernetes) {
    const auto& site = topology.definitions.site;
    std::optional<MemoryBudget> budget;
    if (site.services.contains("capacity")) {
        const auto& capacity = site.services.at("capacity");
        budget.emplace(capacity.at("hypervisor_memory_kib").get<std::uint64_t>(),
                       capacity.at("active_vm_limit_mib").get<std::uint64_t>());
    }
    std::map<std::string, VmAllocation> allocations;
    const bool running_default = site.defaults.at("domain").at("running").get<bool>();
    for (const auto& [id, vm] : topology.definitions.vms) {
        VmAllocation allocation{vm.vcpu, vm.ram_mib, {}, vm.domain.value("running", running_default),
                                kubernetes.nodes.contains(id)};
        for (const auto& disk : vm.disks) allocation.disks.push_back({disk.storage, disk.size_gib});
        allocations.emplace(id, std::move(allocation));
    }
    return BuildResourcePlan(allocations, budget);
}

}  // namespace skeleton
