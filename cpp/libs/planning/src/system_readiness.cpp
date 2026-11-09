#include "system_readiness.hpp"

namespace skeleton {
namespace {

SystemWorkloadReadiness PrepareSystemWorkload(const KubernetesPlan& kubernetes, const Json& definition) {
    SystemWorkloadReadiness readiness{definition.at("namespace"), definition.at("resource"), std::nullopt, {}};
    if (!readiness.resource.starts_with("daemonset/")) return readiness;
    readiness.pod_selector = definition.at("pod_selector").get<std::string>();
    const auto roles = definition.at("node_roles").get<std::set<std::string>>();
    if (readiness.pod_selector->empty() || roles.empty())
        throw ConfigurationError("DaemonSet readiness requires a Pod selector and explicit node roles");
    for (const auto& role : roles)
        if (role != "controller" && role != "worker" && role != "router")
            throw ConfigurationError("unknown DaemonSet readiness node role " + role);
    for (const auto& [id, node] : kubernetes.nodes)
        if (roles.contains(node.role)) readiness.nodes.push_back(id);
    return readiness;
}

}  // namespace

std::vector<SystemWorkloadReadiness> BuildSystemReadiness(const KubernetesPlan& kubernetes) {
    std::vector<SystemWorkloadReadiness> readiness;
    if (kubernetes.nodes.empty()) return readiness;
    for (const auto& definition : kubernetes.settings.at("deployment").at("system_rollouts"))
        readiness.push_back(PrepareSystemWorkload(kubernetes, definition));
    return readiness;
}

}  // namespace skeleton
