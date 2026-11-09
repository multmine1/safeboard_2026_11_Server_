#include "host_requirements.hpp"

namespace skeleton {
namespace {

void ValidateHostPackageList(const Json& values, const std::string& name) {
    if (!values.is_array()) throw ConfigurationError("host requirements/" + name + ": expected an array");
    for (const auto& value : values) {
        if (!value.is_string() || value.get_ref<const std::string&>().empty())
            throw ConfigurationError("host requirements/" + name + ": expected nonempty strings");
        if (name == "backup_disabled_services") continue;
        const auto& package = value.get_ref<const std::string&>();
        const auto separator = package.find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 == package.size())
            throw ConfigurationError("host requirements/" + name + ": pin each package as name=version");
    }
}

Json ReadHostPlatform(const Json& requirements) {
    if (!requirements.contains("hypervisor_platform") || !requirements.at("hypervisor_platform").is_object())
        throw ConfigurationError("host requirements: provide hypervisor_platform");
    const auto& platform = requirements.at("hypervisor_platform");
    Json result = Json::object();
    for (const auto key : {"distribution", "major_version"}) {
        if (!platform.contains(key) || !platform.at(key).is_string() || platform.at(key).get_ref<const std::string&>().empty())
            throw ConfigurationError("host requirements/hypervisor_platform: provide " + std::string(key));
        result[key] = platform.at(key);
    }
    return result;
}

Json ReadHostPackageFiles(const Json& source) {
    if (!source.is_object()) throw ConfigurationError("host requirements/hypervisor_package_files: expected a path map");
    Json result = Json::object();
    for (const auto& [name, file] : source.items()) {
        const auto path = std::filesystem::path(name);
        if (!path.is_absolute() || path == path.root_path() || path != path.lexically_normal())
            throw ConfigurationError("host package files require normalized absolute file paths");
        if (!file.is_object() || !file.contains("content") || !file.at("content").is_string() ||
            !file.contains("mode") || !file.at("mode").is_string() || file.at("mode").get_ref<const std::string&>().empty())
            throw ConfigurationError("host package files require explicit content and mode strings");
        result[name] = {{"content", file.at("content")}, {"mode", file.at("mode")}};
    }
    return result;
}

}  // namespace

Json BuildHostRequirements(const SiteDefinition& site) {
    if (!site.asset_data.contains("host_packages")) return Json::object();
    const auto& source = site.asset_data.at("host_packages");
    Json result{{"hypervisor_platform", ReadHostPlatform(source)}};
    for (const auto key : {"hypervisor_packages", "hypervisor_network_packages", "cockpit_packages",
                           "backup_packages", "backup_disabled_services", "image_packages"}) {
        if (!source.contains(key)) continue;
        ValidateHostPackageList(source.at(key), key);
        result[key] = source.at(key);
    }
    if (source.contains("hypervisor_package_files"))
        result["hypervisor_package_files"] = ReadHostPackageFiles(source.at("hypervisor_package_files"));
    return result;
}

Json BuildHostImagePlan(const SiteDefinition& site, const Json& requirements) {
    auto image = site.image;
    if (!requirements.contains("image_packages")) return image;
    if (image.contains("hypervisor_packages"))
        throw ConfigurationError("image builder packages declared twice; use host requirements/image_packages");
    image["hypervisor_packages"] = requirements.at("image_packages");
    return image;
}

}  // namespace skeleton
