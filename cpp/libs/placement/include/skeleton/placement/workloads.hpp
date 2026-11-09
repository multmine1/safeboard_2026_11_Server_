#pragma once

#include "skeleton/placement/resources.hpp"

namespace skeleton {

struct ComputeAmount {
    std::uint64_t cpu_millicores = 0;
    std::uint64_t ram_mib = 0;
    bool operator==(const ComputeAmount&) const = default;
};

class ContainerResources {
public:
    ContainerResources(ComputeAmount requests, ComputeAmount limits);
    const ComputeAmount& Requests() const { return requests_; }
    const ComputeAmount& Limits() const { return limits_; }
private:
    ComputeAmount requests_;
    ComputeAmount limits_;
};

struct PodDefinition {
    std::uint64_t replicas;
    std::map<std::string, ContainerResources> containers;
    std::map<std::string, ContainerResources> init_containers;
    bool fixed_replicas = false;
};

struct ComputeTotals {
    std::uint64_t pods = 0;
    ComputeAmount requests;
    ComputeAmount limits;
};

struct PodResourcePlan {
    PodDefinition definition;
    ComputeTotals per_pod;
    ComputeTotals total;
};

struct WorkloadAllocation {
    std::string vm;
    bool enabled;
    std::map<std::string, PodResourcePlan> pods;
};

struct WorkloadCapacityPolicy {
    ComputeAmount non_application_reserve;
    bool permit_limit_overcommit;
    std::map<std::string, ComputeAmount> vm_reserves = {};
};

struct VmWorkloadResources {
    ComputeAmount capacity;
    ComputeAmount reserved;
    ComputeAmount budget;
    ComputeTotals enabled;
    ComputeAmount request_headroom;
    bool cpu_limits_overcommitted;
    bool memory_limits_overcommitted;
};

struct WorkloadResourcePlan {
    std::map<std::string, VmWorkloadResources> vms;
    std::map<std::string, ComputeTotals> applications;
    ComputeTotals configured;
    ComputeTotals enabled;
    ComputeTotals source_initially_running;
};

PodResourcePlan BuildPodResourcePlan(PodDefinition definition);
WorkloadResourcePlan BuildWorkloadResourcePlan(const ResourcePlan& vm_resources,
    const std::map<std::string, WorkloadAllocation>& applications, const WorkloadCapacityPolicy& policy);

}  // namespace skeleton
