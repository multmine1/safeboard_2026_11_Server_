#include "installation.hpp"
#include "skeleton/installation/preseed.hpp"

namespace skeleton {
namespace {

Json PrepareInstallerFirmware(const SiteDefinition& site) {
    const auto selected = site.hypervisor_settings.at("hypervisor_installation").value("firmware", Json::array());
    if (!selected.is_array()) throw ConfigurationError("installer firmware must be an array of catalog names");
    Json artifacts = Json::array();
    std::set<std::string> names;
    const auto catalog = site.asset_data.at("host_installer").value("firmware_catalog", Json::object());
    for (const auto& value : selected) {
        if (!value.is_string() || !catalog.contains(value.get<std::string>()) || !names.insert(value.get<std::string>()).second)
            throw ConfigurationError("installer firmware references must be known and unique");
        const auto name = value.get<std::string>();
        const auto& definition = catalog.at(name);
        Json artifact{{"name", name}};
        for (const auto key : {"package", "version", "url", "filename", "checksum", "source_directory", "destination_directory"})
            artifact[key] = definition.at(key).get<std::string>();
        const auto filename = std::filesystem::path(artifact.at("filename").get<std::string>());
        if (filename != filename.filename() || filename.extension() != ".deb" ||
            !artifact.at("url").get_ref<const std::string&>().starts_with("https://") ||
            !artifact.at("checksum").get_ref<const std::string&>().starts_with("sha256:"))
            throw ConfigurationError("installer firmware requires an HTTPS .deb artifact with a SHA256 checksum");
        for (const auto key : {"source_directory", "destination_directory"}) {
            const auto path = std::filesystem::path(artifact.at(key).get<std::string>());
            if (path.empty() || path.is_absolute() || path != path.lexically_normal() || std::find(path.begin(), path.end(), "..") != path.end())
                throw ConfigurationError("installer firmware directories must stay inside the extracted package and initrd");
        }
        artifacts.push_back(std::move(artifact));
    }
    return artifacts;
}

void AddGeneratedPreseed(Json& values, const std::string& key, const std::string& type, const std::string& value) {
    if (values["d-i"].contains(key)) throw ConfigurationError("installer preseed overrides a generated field: " + key);
    values["d-i"][key] = {{"type", type}, {"value", value}};
}

void PrepareInstallerPreseed(InfrastructurePlan& plan) {
    const auto& site = plan.topology.definitions.site;
    const auto& host = site.hypervisor_settings;
    auto& bootstrap = *plan.installer_bootstrap;
    const auto& network = host.at("hypervisor_network");
    const auto prefix = network.at("prefix").get<unsigned>();
    const auto mask = FormatIpv4(Ipv4Cidr{UINT32_MAX, prefix}.Network());
    auto& values = bootstrap.preseed;
    AddGeneratedPreseed(values, "netcfg/get_ipaddress", "string", site.hypervisor_address);
    AddGeneratedPreseed(values, "netcfg/get_netmask", "string", mask);
    AddGeneratedPreseed(values, "netcfg/get_gateway", "string", network.at("gateway"));
    std::string dns;
    for (const auto& address : network.at("bridge_settings").at("nameservers").at("addresses"))
        dns += (dns.empty() ? "" : " ") + address.get<std::string>();
    AddGeneratedPreseed(values, "netcfg/get_nameservers", "string", dns);
    AddGeneratedPreseed(values, "netcfg/get_hostname", "string", site.hypervisor_alias);
    AddGeneratedPreseed(values, "passwd/user-fullname", "string", host.at("controller_user"));
    AddGeneratedPreseed(values, "passwd/username", "string", host.at("controller_user"));
    AddGeneratedPreseed(values, "partman/early_command", "string", bootstrap.early_command);
    AddGeneratedPreseed(values, "preseed/late_command", "string", bootstrap.late_command);
    auto settings = site.asset_data.at("host_installer").at("preseed");
    for (const auto& artifact : bootstrap.firmware_artifacts)
        settings["d-i"]["pkgsel/include"]["value"] = settings.at("d-i").at("pkgsel/include").at("value").get<std::string>() +
            " " + artifact.at("package").get<std::string>() + "=" + artifact.at("version").get<std::string>();
    bootstrap.preseed = BuildInstallerPreseed(settings, values);
}

InstallerBootstrapContext BuildInstallerContext(const InfrastructurePlan& plan) {
    const auto& site = plan.topology.definitions.site;
    const auto& host = site.hypervisor_settings;
    const auto& bootstrap = plan.hypervisor.at("bootstrap");
    const auto& network = bootstrap.at("hypervisor_network");
    InstallerBootstrapContext context;
    context.user = host.at("controller_user");
    context.user_home = host.at("controller_home");
    for (const auto& account : plan.hypervisor.at("sshd").at("authorized_keys"))
        if (account.at("user") == context.user)
            context.authorized_keys = account.at("keys").get<std::vector<std::string>>();
    for (const auto& file : plan.hypervisor.at("sshd").at("backup").at("files"))
        context.host_keys.push_back({file.at("path"), file.at("mode")});
    context.files.push_back({network.at("path"), network.at("mode"), bootstrap.at("hypervisor_netplan").dump(2) + "\n"});
    context.retain_script = site.asset_data.at("installer_retain").get<std::string>();
    context.finish_script = site.asset_data.at("installer_finish").get<std::string>();
    return context;
}

}  // namespace

void PrepareInstallerBootstrap(InfrastructurePlan& plan) {
    const auto& site = plan.topology.definitions.site;
    if (!plan.host_installation || !site.hypervisor_settings.at("hypervisor_installation").contains("previous_root")) return;
    if (!site.asset_data.at("host_installation").contains("bootstrap") ||
        !site.asset_data.contains("installer_retain") || !site.asset_data.contains("installer_finish") ||
        !site.asset_data.contains("host_installer") ||
        !plan.hypervisor.contains("sshd"))
        throw ConfigurationError("installer bootstrap requires source settings, native helper assets and the prepared SSH policy");
    plan.installer_bootstrap = BuildInstallerBootstrapPlan(*plan.host_installation,
        site.asset_data.at("host_installation").at("bootstrap"),
        site.hypervisor_settings.at("hypervisor_installation").at("previous_root"), BuildInstallerContext(plan));
    plan.installer_bootstrap->firmware_artifacts = PrepareInstallerFirmware(site);
    PrepareInstallerPreseed(plan);
}

}  // namespace skeleton
