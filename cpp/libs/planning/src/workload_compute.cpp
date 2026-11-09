#include "workloads.hpp"

namespace skeleton {
namespace {

Json NativeAmount(const ComputeAmount& amount) {
    const auto cpu = amount.cpu_millicores % 1000 ? std::to_string(amount.cpu_millicores) + "m" :
                     std::to_string(amount.cpu_millicores / 1000);
    const auto memory = amount.ram_mib % 1024 ? std::to_string(amount.ram_mib) + "Mi" :
                        std::to_string(amount.ram_mib / 1024) + "Gi";
    return {{"cpu", cpu}, {"memory", memory}};
}

Json NativeContainers(const std::map<std::string, ContainerResources>& containers) {
    Json native = Json::object();
    for (const auto& [name, container] : containers)
        native[name] = {{"requests", NativeAmount(container.Requests())}, {"limits", NativeAmount(container.Limits())}};
    return native;
}

}  // namespace

std::map<std::string, PodResourcePlan> BuildApplicationCompute(const WorkloadDefinition& definition) {
    std::map<std::string, PodResourcePlan> compute;
    for (const auto& [name, pod] : definition.compute) compute.emplace(name, BuildPodResourcePlan(pod));
    return compute;
}

Json BuildComputeContext(const std::map<std::string, PodResourcePlan>& compute) {
    Json context = Json::object();
    for (const auto& [name, pod] : compute)
        context[name] = {{"replicas", pod.definition.replicas}, {"containers", NativeContainers(pod.definition.containers)},
                         {"init_containers", NativeContainers(pod.definition.init_containers)}};
    return context;
}

void ValidateComputeBindings(const std::map<std::string, PodResourcePlan>& compute, const Json& patches) {
    std::set<std::string> bindings;
    for (const auto& patch : patches) bindings.insert(patch.at("from").get<std::string>());
    for (const auto& [name, pod] : compute) {
        const auto prefix = "/context/compute/" + name;
        if (!pod.definition.fixed_replicas && !bindings.contains(prefix + "/replicas"))
            throw ConfigurationError("unbound compute replica count: " + name);
        for (const auto& [container, resources] : pod.definition.containers)
            if (!bindings.contains(prefix + "/containers/" + container)) throw ConfigurationError("unbound compute container: " + name + "/" + container);
        for (const auto& [container, resources] : pod.definition.init_containers)
            if (!bindings.contains(prefix + "/init_containers/" + container)) throw ConfigurationError("unbound init container: " + name + "/" + container);
    }
}

void PrepareWorkloadResources(InfrastructurePlan& plan) {
    const auto& capacity = plan.topology.definitions.site.workload_capacity;
    std::map<std::string, WorkloadAllocation> allocations;
    for (const auto& [name, application] : plan.applications) {
        if (!capacity && !application.compute.empty()) throw ConfigurationError("declare workload_capacity before allocating Pod resources");
        allocations.emplace(name, WorkloadAllocation{application.vm, application.enabled, application.compute});
    }
    if (capacity) plan.workload_resources = BuildWorkloadResourcePlan(plan.resources, allocations, *capacity);
}

}  // namespace skeleton
