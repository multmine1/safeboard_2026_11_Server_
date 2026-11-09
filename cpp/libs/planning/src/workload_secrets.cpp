#include "workloads.hpp"

#include <regex>

namespace skeleton {
namespace {

void ValidateSecretRequest(const std::string& name, const Json& request) {
    if (!std::regex_match(name, std::regex("[a-z0-9]([a-z0-9.-]{0,251}[a-z0-9])?")))
        throw ConfigurationError("invalid Secret name " + name);
    const std::set<std::string> fields{"provider", "services", "connections", "dns_names", "host_services"};
    for (const auto& [key, value] : request.items())
        if (!fields.contains(key)) throw ConfigurationError("unsupported Secret request field " + key);
    const auto provider = request.at("provider").get<std::string>();
    if (provider != "vault" && provider != "certificate" && provider != "ca")
        throw ConfigurationError("unknown Secret provider " + provider);
    if (provider != "certificate" && (request.contains("services") || request.contains("connections") ||
        request.contains("dns_names") || request.contains("host_services")))
        throw ConfigurationError("only certificates declare service names");
}

Json PrepareCertificate(const InfrastructurePlan& plan, const ApplicationPlan& application,
                        const std::string& name, const Json& request) {
    const auto& fqdn = plan.topology.vm_domains.at(application.vm);
    std::vector<std::string> names{"DNS:" + fqdn};
    const auto& services = plan.topology.definitions.networks.at(plan.topology.vm_networks.at(application.vm)).exports;
    for (const auto& service_name : request.value("services", std::vector<std::string>{})) {
        const auto service = std::find_if(services.begin(), services.end(), [&](const auto& value) {
            const auto names = value.Names();
            return std::find(names.begin(), names.end(), service_name) != names.end() && value.vm == application.vm;
        });
        if (service == services.end())
            throw ConfigurationError("a certificate service must belong to its workload VM: " + service_name);
        names.push_back("DNS:" + plan.topology.service_domains.at(service_name));
    }
    for (const auto& dns_name : request.value("dns_names", std::vector<std::string>{}))
        names.push_back("DNS:" + dns_name);
    for (const auto& host_service : request.value("host_services", std::vector<std::string>{})) {
        if (!plan.topology.host_services.contains(host_service))
            throw ConfigurationError("certificate refers to an unknown host service " + host_service);
        names.push_back("DNS:" + plan.topology.host_services.at(host_service).fqdn);
    }
    const auto& connections = plan.vms.at(application.vm).variables.at("connections");
    for (const auto& connection : request.value("connections", std::vector<std::string>{})) {
        if (!connections.contains(connection)) throw ConfigurationError("certificate refers to an unpermitted connection " + connection);
        names.push_back("DNS:" + connections.at(connection).at("fqdn").get<std::string>());
    }
    std::set<std::string> seen;
    std::erase_if(names, [&](const auto& value) { return !seen.insert(value).second; });
    return {{"application", application.name}, {"name", name}, {"namespace", application.network_namespace},
        {"common_name", fqdn}, {"subject_alt_name", names}};
}

void AddManagedSecrets(InfrastructurePlan& plan, ApplicationPlan& application,
                       std::set<std::pair<std::string, std::string>>& producers) {
    if (!application.managed_secrets.is_object()) throw ConfigurationError("workload secrets must be an object");
    for (const auto& [name, request] : application.managed_secrets.items()) {
        ValidateSecretRequest(name, request);
        if (!producers.emplace(application.network_namespace, name).second)
            throw ConfigurationError("multiple producers of Secret " + application.network_namespace + "/" + name);
        if (request.at("provider") == "certificate")
            plan.secret_configuration["certificates"].push_back(PrepareCertificate(plan, application, name, request));
        application.required_secrets.push_back(name);
    }
    std::sort(application.required_secrets.begin(), application.required_secrets.end());
    auto& names = application.required_secrets;
    names.erase(std::unique(names.begin(), names.end()), names.end());
}

}  // namespace

void PrepareApplicationSecrets(InfrastructurePlan& plan) {
    std::set<std::pair<std::string, std::string>> producers;
    for (auto& [name, application] : plan.applications) {
        if (application.managed_secrets.empty()) continue;
        if (plan.secret_configuration.empty()) {
            const auto& pki = plan.kubernetes.settings.at("pki");
            const auto sealer = pki.at("sealing_application").get<std::string>();
            if (!plan.applications.contains(sealer)) throw ConfigurationError("unknown sealing application " + sealer);
            plan.secret_configuration = {{"settings", pki}, {"certificates", Json::array()},
                {"sealing", {{"application", sealer}, {"namespace", plan.applications.at(sealer).network_namespace}}}};
        }
        const auto sealer = plan.secret_configuration.at("sealing").at("application").get<std::string>();
        if (name == sealer) throw ConfigurationError("the sealing controller cannot depend on its own sealed Secrets");
        if (std::find(application.dependencies.begin(), application.dependencies.end(), sealer) == application.dependencies.end())
            application.dependencies.push_back(sealer);
        AddManagedSecrets(plan, application, producers);
    }
}

}  // namespace skeleton
