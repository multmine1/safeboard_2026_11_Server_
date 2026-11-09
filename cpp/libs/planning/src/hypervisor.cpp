#include "skeleton/planning/infrastructure.hpp"

namespace skeleton {
namespace {

void PrepareControllerAccess(Json& variables) {
    if (!variables.contains("controller_access")) return;
    const auto home = std::filesystem::path(variables.at("controller_home").get<std::string>());
    if (!home.is_absolute()) throw ConfigurationError("controller_home must be an absolute path");
    for (const auto key : {"ssh_config", "known_hosts"}) {
        const auto relative = std::filesystem::path(variables.at("controller_access").at(key).get<std::string>());
        if (relative.is_absolute() || std::find(relative.begin(), relative.end(), "..") != relative.end())
            throw ConfigurationError("controller access paths must remain below controller_home");
        variables["controller_access"][key] = (home / relative).string();
    }
}

void PrepareHypervisorNetwork(Json& variables, const SiteDefinition& site, const Json& network) {
    const auto host_network = GenerateHypervisorHostNetwork(site);
    variables["hypervisor_netplan"] = host_network.at("netplan");
    if (host_network.contains("wake")) variables["wake"] = host_network.at("wake");
    auto runtime = variables.at("hypervisor_netplan");
    auto& bridge = runtime["network"]["bridges"][site.uplink_bridge];
    for (const auto& route : network.at("routes")) bridge["routes"].push_back(route);
    bridge["nameservers"]["addresses"] = network.at("resolver").at("servers");
    variables["hypervisor_runtime_netplan"] = std::move(runtime);
}

void PrepareHypervisorStorage(Json& variables, const SiteDefinition& site) {
    variables["storage_mounts"] = Json::array();
    variables["storage_pools"] = Json::array();
    variables["vmctl_storage_directories"] = Json::array();
    for (const auto& [name, definition] : site.hypervisor_settings.at("generator").at("storage_classes").items()) {
        const auto path = std::filesystem::path(site.storage_root) / name;
        variables["storage_pools"].push_back({{"name", name}, {"path", (path / "volumes").string()}});
        variables["vmctl_storage_directories"].push_back((std::filesystem::path(site.vmctl_home) / "VMs" / name).string());
        if (definition.empty()) continue;
        auto mount = definition;
        mount["src"] = "UUID=" + mount.at("uuid").get<std::string>();
        mount["path"] = path.string();
        mount.erase("uuid");
        variables["storage_mounts"].push_back(std::move(mount));
    }
}

void PrepareHypervisorHostNames(Json& variables, const Topology& topology) {
    if (topology.host_services.empty()) return;
    auto hosts = topology.definitions.site.hypervisor_settings.at("hypervisor_hosts");
    std::string content;
    for (const auto& [name, endpoint] : topology.host_services)
        content += endpoint.address + " " + endpoint.fqdn + "\n";
    hosts["content"] = std::move(content);
    variables["hypervisor_hosts"] = std::move(hosts);
}

void PrepareCockpitConfiguration(Json& variables, const Topology& topology) {
    if (!variables.contains("hypervisor_cockpit") || !variables.at("hypervisor_cockpit").contains("service")) return;
    auto& cockpit = variables["hypervisor_cockpit"];
    const auto service = cockpit.at("service").get<std::string>();
    if (!topology.host_services.contains(service)) throw ConfigurationError("unknown Cockpit host service: " + service);
    const auto& endpoint = topology.host_services.at(service);
    cockpit["listen"] = endpoint.socket;
    cockpit["certificate"] = {{"namespace", cockpit.at("tls").at("namespace")},
        {"name", cockpit.at("tls").at("name")}, {"common_name", endpoint.fqdn},
        {"subject_alt_name", Json::array({"DNS:" + endpoint.fqdn})}};
}

}  // namespace

Json BuildHypervisorBootstrap(const Topology& topology, const Json& network) {
    const auto& site = topology.definitions.site;
    if (site.hypervisor_settings.empty()) return Json::object();
    auto variables = site.hypervisor_settings;
    for (const auto& key : {"generator", "ansible_become_exe", "ansible_python_interpreter", "hypervisor_sshd", "hypervisor_services", "hypervisor_installation"}) variables.erase(key);
    variables["hypervisor_home"] = site.vmctl_home;
    PrepareControllerAccess(variables);
    PrepareHypervisorNetwork(variables, site, network);
    PrepareHypervisorStorage(variables, site);
    PrepareHypervisorHostNames(variables, topology);
    PrepareCockpitConfiguration(variables, topology);
    return variables;
}

}  // namespace skeleton
