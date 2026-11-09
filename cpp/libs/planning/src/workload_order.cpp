#include "workloads.hpp"

#include <functional>

namespace skeleton {
namespace {

void CollectApplicationVmRequirements(InfrastructurePlan& plan) {
    for (auto& [name, application] : plan.applications)
        for (const auto& dependency : application.deployment_order) {
            const auto& required = plan.applications.at(dependency);
            application.required_vms.insert(required.vm);
        }
}

}  // namespace

void OrderApplications(InfrastructurePlan& plan) {
    std::map<std::string, unsigned> state;
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (!plan.applications.contains(name)) throw ConfigurationError("unknown application dependency " + name);
        if (state[name] == 1) throw ConfigurationError("application dependency cycle at " + name);
        if (state[name] == 2) return;
        state[name] = 1;
        auto& application = plan.applications.at(name);
        std::set<std::string> required{name};
        for (const auto& dependency : application.dependencies) {
            visit(dependency);
            if (application.enabled && !plan.applications.at(dependency).enabled)
                throw ConfigurationError("enabled application " + name + " requires disabled " + dependency);
            const auto& order = plan.applications.at(dependency).deployment_order;
            required.insert(order.begin(), order.end());
        }
        state[name] = 2;
        if (application.enabled) {
            plan.application_order.push_back(name);
            for (const auto& candidate : plan.application_order)
                if (required.contains(candidate)) application.deployment_order.push_back(candidate);
        }
    };
    for (const auto& [name, application] : plan.applications) visit(name);
    CollectApplicationVmRequirements(plan);
}

}  // namespace skeleton
