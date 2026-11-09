#include "readers.hpp"

namespace skeleton::input {

Json ReadWorkloads(const Json& value, const std::filesystem::path& root) {
    if (value.is_array()) return value;
    CheckFields(value, {"directory", "file"}, "services/workloads");
    if (value.contains("directory") == value.contains("file"))
        Fail("services/workloads", "provide exactly one directory or file");
    if (value.contains("file")) return ReadJson(root / ReadString(value, "file", "services/workloads"));
    const auto directory = root / ReadString(value, "directory", "services/workloads");
    if (!std::filesystem::is_directory(directory)) Fail(directory.string(), "expected a workload directory");
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_symlink() || !entry.is_regular_file() || entry.path().extension() != ".json") continue;
        if (!entry.path().filename().string().ends_with(".workload.json"))
            Fail(entry.path().string(), "expected *.workload.json");
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    Json definitions = Json::array();
    for (const auto& file : files) {
        auto definition = ReadJson(file);
        if (!definition.is_object()) Fail(file.string(), "expected one workload object");
        definition["_source"] = file.string();
        definitions.push_back(std::move(definition));
    }
    return definitions;
}

}  // namespace skeleton::input

namespace skeleton {
namespace {

void ValidateFixedReplicas(const Json& component, const Json& compute, const std::string& context) {
    const auto inherited = component.value("compute", Json::object());
    for (const auto& [name, pod] : inherited.items())
        if (pod.contains("fixed_replicas") && (!compute.contains(name) ||
            compute.at(name).value("fixed_replicas", Json()) != pod.at("fixed_replicas")))
            input::Fail(context, "cannot override the fixed replica count of component allocation " + name);
}

WorkloadDefinition ReadWorkload(const Json& definition, const Json& components) {
    const auto context = definition.value("_source", std::string("workload"));
    const auto name = input::ReadName(definition, "name", context);
    if (name != input::ReadString(definition, "name", context)) input::Fail(context, "workload names must be lowercase");
    const auto vm = input::ReadUuid(definition, "vm", context);
    const auto& enabled = input::Require(definition, "enabled", context);
    if (!enabled.is_boolean()) input::Fail(context + "/enabled", "expected a boolean");
    Json component = Json::object();
    if (definition.contains("component")) component = components.at(input::ReadString(definition, "component", context));
    auto compute = component.value("compute", Json::object());
    compute.merge_patch(definition.value("compute", Json::object()));
    ValidateFixedReplicas(component, compute, context);
    auto patches = component.value("patches", Json::array());
    const auto extra = definition.value("patches", Json::array());
    if (!patches.is_array() || !extra.is_array()) input::Fail(context + "/patches", "expected a JSON Patch array");
    for (const auto& patch : extra) patches.push_back(patch);
    auto specification = definition;
    specification["vm"] = vm;
    return {name, vm, enabled.get<bool>(), std::move(specification), std::move(patches),
            input::ReadPodDefinitions(compute, context + "/compute")};
}

}  // namespace

std::vector<WorkloadDefinition> ReadWorkloadDefinitions(const Json& definitions, const Json& components) {
    if (!definitions.is_array()) throw ConfigurationError("workloads must be an array");
    std::vector<WorkloadDefinition> result;
    for (const auto& definition : definitions) {
        try {
            result.push_back(ReadWorkload(definition, components));
        } catch (const Json::exception& error) {
            throw ConfigurationError(std::string("invalid workload definition: ") + error.what());
        }
    }
    return result;
}

}  // namespace skeleton
