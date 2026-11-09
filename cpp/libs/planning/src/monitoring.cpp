#include "monitoring.hpp"

#include <regex>

namespace skeleton {
namespace {

std::filesystem::path RelativeMonitoringPath(const Json& value) {
    const auto path = std::filesystem::path(value.get<std::string>());
    if (path.empty() || path.is_absolute() || std::find(path.begin(), path.end(), "..") != path.end())
        throw ConfigurationError("monitoring artifact paths must remain below their declared cache");
    return path;
}

void PrepareMonitoringInstallation(Json& monitoring, const SiteDefinition& site) {
    auto installation = site.asset_data.at("host_monitoring").at(monitoring.at("artifact").get<std::string>());
    if (!std::regex_match(installation.at("checksum").get<std::string>(), std::regex("sha256:[a-f0-9]{64}")))
        throw ConfigurationError("host monitoring requires an artifact pinned by SHA-256");
    const auto cache = RelativeMonitoringPath(monitoring.at("cache_directory"));
    const auto root = std::filesystem::path(site.hypervisor_settings.at("controller_project_directory").get<std::string>());
    if (!root.is_absolute() || !cache.generic_string().starts_with("environment/.tools/"))
        throw ConfigurationError("host monitoring cache must belong to the declared controller environment/.tools");
    const auto directory = root / cache;
    installation["directory"] = directory.string();
    installation["archive"] = (directory / RelativeMonitoringPath(installation.at("archive_name"))).string();
    const auto archived_binary = RelativeMonitoringPath(installation.at("archive_binary"));
    if (archived_binary.filename() != std::filesystem::path(monitoring.at("binary").get<std::string>()).filename())
        throw ConfigurationError("monitoring installation must preserve the native binary name");
    monitoring["installation"] = std::move(installation);
}

void RequireDerivedField(Json& object, const std::string& key, Json value) {
    if (object.contains(key)) throw ConfigurationError("host monitoring field is calculated and must not be duplicated: " + key);
    object[key] = std::move(value);
}

Json& SingleMonitoringPlugin(Json& configuration, const std::string& direction, const std::string& name) {
    auto& plugins = configuration.at(direction).at(name);
    if (!plugins.is_array() || plugins.size() != 1 || !plugins.front().is_object())
        throw ConfigurationError("host monitoring requires exactly one " + name + " plugin");
    return plugins.front();
}

void PrepareMonitoringMetrics(Json& monitoring, const InfrastructurePlan& plan) {
    const auto& topology = plan.topology;
    const auto name = monitoring.at("service").get<std::string>();
    if (!topology.host_services.contains(name)) throw ConfigurationError("unknown host monitoring service: " + name);
    const auto& endpoint = topology.host_services.at(name);
    auto& configuration = monitoring.at("configuration");
    auto& libvirt = SingleMonitoringPlugin(configuration, "inputs", "libvirt");
    Json domains = Json::array();
    for (const auto& [id, vm] : plan.vms) domains.push_back(vm.variables.at("libvirt_name"));
    RequireDerivedField(libvirt, "domains", domains);
    RequireDerivedField(libvirt, "libvirt_uri", "qemu+unix:///system?socket=" + monitoring.at("read_only_socket").get<std::string>());
    auto& output = SingleMonitoringPlugin(configuration, "outputs", "prometheus_client");
    RequireDerivedField(output, "listen", endpoint.socket);
    RequireDerivedField(output, "path", topology.definitions.site.hypervisor_settings.at("hypervisor_services").at(name).at("path"));
    const auto& root = topology.definitions.networks.at(topology.root).router;
    RequireDerivedField(output, "ip_range", Json::array({endpoint.address + "/32",
        FormatIpv4(plan.addresses.vms.at(root).at("wan").address.address) + "/32"}));
}

void PrepareMonitoringUnit(Json& monitoring) {
    std::set<std::filesystem::path> paths;
    for (const auto key : {"binary", "config_path", "unit_path", "read_only_socket"}) {
        const auto path = std::filesystem::path(monitoring.at(key).get<std::string>());
        if (!path.is_absolute() || path.filename().empty() ||
            std::find(path.begin(), path.end(), "..") != path.end() || !paths.insert(path).second)
            throw ConfigurationError("host monitoring files require distinct absolute paths");
    }
    const auto socket = std::filesystem::path(monitoring.at("read_only_socket").get<std::string>()).filename().string();
    if (!socket.ends_with("-sock-ro")) throw ConfigurationError("host monitoring requires the read-only libvirt socket");
    const auto unit_path = std::filesystem::path(monitoring.at("unit_path").get<std::string>());
    if (unit_path.filename() != monitoring.at("unit").get<std::string>())
        throw ConfigurationError("host monitoring unit name must match its declared file");
    RequireDerivedField(monitoring["systemd"]["Service"], "ExecStart",
        monitoring.at("binary").dump() + " --config " + monitoring.at("config_path").dump());
}

}  // namespace

void PrepareHypervisorMonitoring(InfrastructurePlan& plan) {
    const auto& site = plan.topology.definitions.site;
    if (!site.hypervisor_settings.contains("hypervisor_monitoring")) return;
    auto monitoring = site.hypervisor_settings.at("hypervisor_monitoring");
    PrepareMonitoringInstallation(monitoring, site);
    PrepareMonitoringUnit(monitoring);
    PrepareMonitoringMetrics(monitoring, plan);
    plan.hypervisor["bootstrap"]["hypervisor_monitoring"] = std::move(monitoring);
}

}  // namespace skeleton
