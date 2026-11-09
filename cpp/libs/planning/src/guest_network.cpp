#include "guest.hpp"

namespace skeleton {
namespace {

void MoveNetworkFile(Json& variables, Json& network, const std::string& path) {
    network["files"][path] = variables.at("files").at(path);
    variables["files"].erase(path);
    if (variables.contains("file_modes") && variables.at("file_modes").contains(path)) {
        network["file_modes"][path] = variables.at("file_modes").at(path);
        variables["file_modes"].erase(path);
    }
}

Json NetworkDirectories(const Json& files) {
    std::set<std::string> directories;
    for (const auto& [path, data] : files.items()) directories.insert(std::filesystem::path(path).parent_path().string());
    return directories;
}

}  // namespace

void SplitGuestNetwork(VmPlan& guest, const Topology& topology, const std::string& vm_id) {
    auto& variables = guest.variables;
    const auto& services = topology.definitions.site.services;
    Json network{{"files", Json::object()}, {"file_modes", Json::object()}};
    for (const auto& key : {"network_path", "firewall_path", "sysctl", "sysctl_file"}) {
        network[key] = variables.at(key);
        variables.erase(key);
    }
    for (const auto& key : {"file_mode", "directory_mode", "python"}) network[key] = variables.at(key);
    network["firewall_service"] = services.at("firewall").at("unit");
    network["nft_validate"] = services.at("firewall").at("validate");
    network["remove_files"] = services.at("network").at("remove_files");
    network["disable_services"] = Json::array();
    MoveNetworkFile(variables, network, network.at("network_path"));
    MoveNetworkFile(variables, network, network.at("firewall_path"));
    if (topology.IsRouter(vm_id)) {
        network["bootstrap_service"] = services.at("dns").at("unit");
        network["disable_services"] = services.at("dns_resolver").at("disable_units");
        MoveNetworkFile(variables, network, services.at("dns").at("corefile_path"));
        MoveNetworkFile(variables, network, services.at("dns_resolver").at("path"));
        for (const auto& [path, data] : guest.dns.zones) MoveNetworkFile(variables, network, path);
    }
    if (variables.contains("ufw")) { network["ufw"] = variables.at("ufw"); variables.erase("ufw"); }
    std::erase_if(variables["services"].get_ref<Json::array_t&>(), [&](const auto& unit) {
        return unit == network.at("firewall_service") || (network.contains("bootstrap_service") && unit == network.at("bootstrap_service"));
    });
    network["directories"] = NetworkDirectories(network.at("files"));
    variables["network"] = std::move(network);
}

}  // namespace skeleton
