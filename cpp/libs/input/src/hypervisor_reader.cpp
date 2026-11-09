#include "readers.hpp"

namespace skeleton::input {

void ReadHypervisor(const Json& value, SiteDefinition& site, const std::filesystem::path& root,
    const Json& instance) {
    const auto context = site.source.string() + "/hypervisor";
    if (value.contains("file")) {
        CheckFields(value, {"file"}, context);
        site.hypervisor_settings = ReadJson(root / ReadString(value, "file", context));
        site.hypervisor_settings.merge_patch(instance);
        for (const auto& connection : site.hypervisor_settings.at("hypervisor_connections"))
            if (connection.contains("match_exec"))
                Fail(context + "/hypervisor_connections", "replace match_exec TCP probes with a native proxy_command fallback");
        site.hypervisor_address = ReadString(site.hypervisor_settings, "hypervisor_address", context);
        site.hypervisor_alias = ReadName(site.hypervisor_settings, "hypervisor_alias", context);
    } else {
        if (!instance.empty()) Fail(context, "local host overrides require a hypervisor policy file");
        CheckFields(value, {"address", "ssh_alias"}, context);
        site.hypervisor_address = ReadString(value, "address", context);
        site.hypervisor_alias = ReadName(value, "ssh_alias", context);
    }
}

void ReadSiteDirectories(const Json& value, SiteDefinition& site) {
    const auto context = site.source.string();
    const auto& settings = site.hypervisor_settings.empty() ? value : site.hypervisor_settings.at("generator");
    if (!site.hypervisor_settings.empty())
        for (const auto& key : {"storage_root", "storage_classes", "vmctl_home"})
            if (value.contains(key)) Fail(context + "/" + key, "already provided by the hypervisor file");
    site.storage_root = ReadString(settings, "storage_root", context);
    site.vmctl_home = ReadString(settings, "vmctl_home", context);
    if (site.hypervisor_settings.empty()) site.storage_classes = ReadStringArray(settings, "storage_classes", context);
    else {
        const auto& classes = Require(settings, "storage_classes", context);
        if (!classes.is_object()) Fail(context + "/storage_classes", "expected an object");
        for (const auto& [name, storage] : classes.items()) {
            ReadName(Json{{"name", name}}, "name", context + "/storage_classes");
            site.storage_classes.push_back(name);
        }
    }
    for (const auto& directory : {site.storage_root, site.vmctl_home})
        if (!std::filesystem::path(directory).is_absolute() || directory.find("..") != std::string::npos)
            Fail(context, "storage_root and vmctl_home must be absolute paths without '..'");
}

void ResolveHypervisorSettings(SiteDefinition& site) {
    if (site.hypervisor_settings.empty()) return;
    if (site.services.contains("hypervisor"))
        Fail(site.source.string() + "/services/hypervisor", "already provided by the hypervisor file");
    const auto& host = site.hypervisor_settings;
    auto controller = host.at("generator").at("controller");
    controller["interface"] = host.at("hypervisor_network").at("bridge");
    controller["python"] = host.at("ansible_python_interpreter");
    controller["become_exe"] = host.at("ansible_become_exe");
    controller["admin_alias"] = site.services.at("ssh").at("gateway") == "hypervisor" ?
        host.at("hypervisor_bootstrap_alias") : host.at("hypervisor_admin_alias");
    controller["storage_user"] = host.at("hypervisor_user");
    site.services["hypervisor"] = std::move(controller);
}

}  // namespace skeleton::input
