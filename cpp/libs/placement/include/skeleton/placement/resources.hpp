#pragma once

#include "skeleton/contracts/types.hpp"

namespace skeleton {

struct DiskCapacity {
    std::string storage;
    std::uint64_t size_gib;
};

struct VmAllocation {
    std::uint64_t vcpu;
    std::uint64_t ram_mib;
    std::vector<DiskCapacity> disks;
    bool initially_running;
    bool kubernetes_node;
};

struct ResourceTotals {
    std::uint64_t vm_count = 0;
    std::uint64_t vcpu = 0;
    std::uint64_t ram_mib = 0;
    std::map<std::string, std::uint64_t> disk_gib_by_storage;
};

class MemoryBudget {
public:
    MemoryBudget(std::uint64_t hypervisor_memory_kib, std::uint64_t active_vm_limit_mib);
    std::uint64_t HypervisorMemoryKiB() const { return hypervisor_memory_kib_; }
    std::uint64_t ActiveVmLimitMiB() const { return active_vm_limit_mib_; }
    std::uint64_t HypervisorReserveKiB() const { return hypervisor_reserve_kib_; }

private:
    std::uint64_t hypervisor_memory_kib_;
    std::uint64_t active_vm_limit_mib_;
    std::uint64_t hypervisor_reserve_kib_;
};

struct MemoryAvailability {
    MemoryBudget budget;
    std::uint64_t vm_headroom_mib;
};

struct StartupMemoryAssessment {
    bool within_budget;
    std::uint64_t additional_mib;
    std::uint64_t remaining_mib;
    std::uint64_t shortfall_mib;
};

struct VmResourcePlan {
    ResourceTotals resources;
    bool initially_running;
    bool kubernetes_node;
    std::optional<StartupMemoryAssessment> startup_memory;
};

struct ResourcePlan {
    ResourceTotals configured;
    ResourceTotals initially_running;
    ResourceTotals kubernetes_configured;
    ResourceTotals kubernetes_initially_running;
    std::map<std::string, VmResourcePlan> vms;
    std::optional<MemoryAvailability> memory;
};

ResourcePlan BuildResourcePlan(const std::map<std::string, VmAllocation>& allocations,
                               const std::optional<MemoryBudget>& budget);

}  // namespace skeleton
