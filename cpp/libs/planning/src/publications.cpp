#include "workloads.hpp"

namespace skeleton {

unsigned ResolveFrontendPort(const InfrastructurePlan& plan, const std::string& service, const std::string& via) {
    if (!plan.topology.service_networks.contains(via)) throw ConfigurationError("unknown frontend service " + via);
    const auto& frontend = plan.topology.Service(via);
    const bool allowed = std::any_of(plan.policy.connections.begin(), plan.policy.connections.end(), [&](const auto& flow) {
        return flow.from == frontend.vm && flow.service_name == service && flow.protocol == "tcp";
    });
    if (!allowed || frontend.protocol != "tcp") throw ConfigurationError("frontend has no permitted path to the published service");
    return frontend.port;
}

}  // namespace skeleton
