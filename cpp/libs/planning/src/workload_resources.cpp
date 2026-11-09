#include "workloads.hpp"

namespace skeleton {
namespace {

void AddDelegatedRbac(Json& resources, const Json& delegations,
                     const std::map<std::string, std::string>& namespaces) {
    for (const auto& delegation : delegations) {
        const auto network = delegation.at("network").get<std::string>();
        if (!namespaces.contains(network)) throw ConfigurationError("RBAC delegation targets an unknown network " + network);
        for (auto resource : delegation.at("resources")) {
            if (resource.at("apiVersion") != "rbac.authorization.k8s.io/v1" ||
                (resource.at("kind") != "Role" && resource.at("kind") != "RoleBinding"))
                throw ConfigurationError("only namespaced RBAC can be delegated to another network");
            if (resource.at("metadata").contains("namespace"))
                throw ConfigurationError("delegated RBAC derives its namespace from the network UUID");
            resource["metadata"]["namespace"] = namespaces.at(network);
            resources.push_back(std::move(resource));
        }
    }
}

}  // namespace

std::map<std::string, std::string> ResolveApplicationFiles(const Json& payload) {
    std::map<std::string, std::string> files;
    if (!payload.at("files").is_object()) throw ConfigurationError("application files must be a path to content object");
    for (const auto& [name, content] : payload.at("files").items()) {
        const auto path = std::filesystem::path(name);
        if (path.empty() || path.is_absolute() || path.filename().empty() || path.filename() == "." ||
            std::find(path.begin(), path.end(), "..") != path.end() ||
            path.lexically_normal() == "manifests.json" || path.lexically_normal() == "values.json")
            throw ConfigurationError("application file must remain in its directory and preserve generated manifests: " + name);
        if (!files.emplace(path.lexically_normal().generic_string(), content.get<std::string>()).second)
            throw ConfigurationError("duplicate normalized application file " + name);
    }
    return files;
}

Json ResolveApplicationResources(const Json& payload, const std::string& network_namespace,
                                 const std::map<std::string, std::string>& namespaces) {
    auto resources = payload.at("resources");
    if (!resources.is_array() || !payload.at("cluster_resources").is_array())
        throw ConfigurationError("workload resources and cluster_resources must be arrays");
    for (auto& resource : resources) {
        auto& metadata = resource.at("metadata");
        if (metadata.contains("namespace") && metadata.at("namespace") != network_namespace)
            throw ConfigurationError("application resource targets a foreign network namespace");
        metadata["namespace"] = network_namespace;
    }
    for (const auto& resource : payload.at("cluster_resources")) {
        if (resource.at("metadata").contains("namespace"))
            throw ConfigurationError("cluster_resources must not declare a namespace");
        resources.push_back(resource);
    }
    AddDelegatedRbac(resources, payload.at("delegated_resources"), namespaces);
    return resources;
}

}  // namespace skeleton
