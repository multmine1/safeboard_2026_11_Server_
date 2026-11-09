#include "skeleton/planning/infrastructure.hpp"

namespace skeleton {
namespace {

Json BuildRegistryTrust(const Json& settings, const Json& endpoint) {
    const auto authority = endpoint.at("dns_socket").get<std::string>();
    const auto directory = std::filesystem::path(settings.at("directory").get<std::string>()) / authority;
    const auto ca_name = settings.at("ca_filename").get<std::string>();
    const auto ca_file = directory / ca_name;
    const auto url = Json("https://" + authority).dump();
    const auto content = "server = " + url + "\n[host." + url + "]\n" +
        "capabilities = [\"pull\", \"resolve\"]\nca = " + Json(ca_file.string()).dump() + "\n";
    return {{"authority", authority}, {"directory", directory.string()}, {"ca_file", ca_file.string()},
        {"hosts_content", content}, {"system_ca", settings.at("system_ca")},
        {"docker_directory", (std::filesystem::path(settings.at("docker_directory").get<std::string>()) / authority).string()},
        {"docker_ca_file", (std::filesystem::path(settings.at("docker_directory").get<std::string>()) / authority / ca_name).string()},
        {"dropin", settings.at("dropin")}, {"dropin_content",
            "version = 3\n[plugins.\"io.containerd.cri.v1.images\".registry]\nconfig_path = " + settings.at("directory").dump() + "\n"}};
}

}  // namespace

void PrepareRegistryClients(InfrastructurePlan& plan) {
    if (!plan.kubernetes.settings.contains("registry")) return;
    const auto& settings = plan.kubernetes.settings.at("registry");
    const auto id = settings.at("service").get<std::string>();
    if (!plan.topology.service_networks.contains(id)) throw ConfigurationError("unknown Registry service " + id);
    const auto& service = plan.topology.Service(id);
    for (auto& [vm, guest] : plan.vms) {
        const auto& connections = guest.variables.at("connections");
        if (!connections.contains(service.name)) continue;
        const auto& endpoint = connections.at(service.name);
        if (endpoint.value("protocol", std::string{}) != "tcp" || !endpoint.contains("dns_socket"))
            throw ConfigurationError("Registry clients require one permitted TCP endpoint");
        guest.variables["registry_trust"] = BuildRegistryTrust(settings, endpoint);
    }
}

}  // namespace skeleton
