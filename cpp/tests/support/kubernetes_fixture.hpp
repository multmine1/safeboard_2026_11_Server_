#pragma once

#include "fixture.hpp"

namespace skeleton::test {

inline void EnableKubernetes(SourceDefinitions& definitions) {
    const auto path = std::filesystem::path(SITE_FIXTURE_FILE).parent_path() / "kubernetes.json";
    definitions.site.services["kubernetes"] = Json::parse(std::ifstream(path));
    definitions.site.profiles["application"]["kubernetes"]["role"] = "controller";
    definitions.site.profiles["postgres"]["kubernetes"]["role"] = "worker";
}

inline void SetWorkloads(SourceDefinitions& definitions, const Json& workloads) {
    definitions.site.workloads = ReadWorkloadDefinitions(workloads, definitions.site.asset_data.value("components", Json::object()));
}

inline Json ComputeWorkloadJson(const std::string& name, const std::string& vm) {
    Json definition{{"name", name}, {"vm", vm}, {"enabled", true}};
    definition["compute"]["main"]["replicas"] = 2;
    definition["compute"]["main"]["containers"]["worker"] = {
        {"requests", {{"cpu_millicores", 200}, {"ram_mib", 128}}},
        {"limits", {{"cpu_millicores", 600}, {"ram_mib", 512}}}};
    Json deployment{{"apiVersion", "apps/v1"}, {"kind", "Deployment"}, {"metadata", {{"name", name}}}};
    deployment["spec"]["template"]["spec"]["containers"] = Json::array({{{"name", "worker"}}});
    definition["resources"] = Json::array({deployment});
    definition["patches"] = {
        {{"op", "copy"}, {"from", "/context/compute/main/replicas"}, {"path", "/resources/0/spec/replicas"}},
        {{"op", "copy"}, {"from", "/context/compute/main/containers/worker"}, {"path", "/resources/0/spec/template/spec/containers/0/resources"}}};
    return definition;
}

}  // namespace skeleton::test
