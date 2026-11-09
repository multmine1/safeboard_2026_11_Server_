#include "readers.hpp"

namespace skeleton::input {
namespace {

ComputeAmount ReadComputeAmount(const Json& value, const std::string& context) {
    CheckFields(value, {"cpu_millicores", "ram_mib"}, context);
    return {ReadPositive(value, "cpu_millicores", context, UINT64_MAX),
            ReadPositive(value, "ram_mib", context, UINT64_MAX)};
}

std::map<std::string, ContainerResources> ReadContainers(const Json& value, const std::string& context) {
    if (!value.is_object()) Fail(context, "expected a name to container resources object");
    std::map<std::string, ContainerResources> containers;
    for (const auto& [name, specification] : value.items()) {
        if (ReadName(Json{{"name", name}}, "name", context) != name) Fail(context, "container names must be lowercase");
        CheckFields(specification, {"requests", "limits"}, context + "/" + name);
        containers.emplace(name, ContainerResources(ReadComputeAmount(Require(specification, "requests", context), context + "/requests"),
                                                   ReadComputeAmount(Require(specification, "limits", context), context + "/limits")));
    }
    return containers;
}

std::map<std::string, ComputeAmount> ReadVmReserves(const Json& value, const Json& global_reserve,
                                                  const std::string& context) {
    if (!value.is_object()) Fail(context, "expected a VM UUID to resource reserve object");
    std::map<std::string, ComputeAmount> reserves;
    for (const auto& [reference, partial] : value.items()) {
        const auto entry_context = context + "/" + reference;
        const auto id = ReadUuid(Json{{"id", reference}}, "id", entry_context);
        CheckFields(partial, {"cpu_millicores", "ram_mib"}, entry_context);
        auto resolved = global_reserve;
        resolved.merge_patch(partial);
        if (!reserves.emplace(id, ReadComputeAmount(resolved, entry_context)).second)
            Fail(entry_context, "duplicate normalized VM UUID");
    }
    return reserves;
}

}  // namespace

std::map<std::string, PodDefinition> ReadPodDefinitions(const Json& value, const std::string& context) {
    if (!value.is_object()) Fail(context, "expected a name to Pod allocation object");
    std::map<std::string, PodDefinition> pods;
    for (const auto& [name, specification] : value.items()) {
        if (ReadName(Json{{"name", name}}, "name", context) != name) Fail(context, "Pod allocation names must be lowercase");
        CheckFields(specification, {"replicas", "fixed_replicas", "containers", "init_containers"}, context + "/" + name);
        const bool fixed = specification.contains("fixed_replicas");
        if (fixed == specification.contains("replicas")) Fail(context, "declare replicas or fixed_replicas, not both");
        auto containers = ReadContainers(Require(specification, "containers", context), context + "/containers");
        if (containers.empty()) Fail(context, "a Pod allocation requires application containers");
        pods.emplace(name, PodDefinition{ReadPositive(specification, fixed ? "fixed_replicas" : "replicas", context, UINT64_MAX),
            std::move(containers), ReadContainers(specification.value("init_containers", Json::object()), context + "/init_containers"), fixed});
    }
    return pods;
}

WorkloadCapacityPolicy ReadWorkloadCapacity(const Json& value, const std::string& context) {
    CheckFields(value, {"non_application_reserve", "permit_limit_overcommit", "vm_reserves"}, context);
    const auto& overcommit = Require(value, "permit_limit_overcommit", context);
    if (!overcommit.is_boolean()) Fail(context, "permit_limit_overcommit must be a boolean");
    const auto& reserve = Require(value, "non_application_reserve", context);
    const auto amount = ReadComputeAmount(reserve, context + "/non_application_reserve");
    return {amount, overcommit.get<bool>(),
            ReadVmReserves(value.value("vm_reserves", Json::object()), reserve, context + "/vm_reserves")};
}

}  // namespace skeleton::input
