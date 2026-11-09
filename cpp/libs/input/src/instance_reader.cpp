#include "readers.hpp"

namespace skeleton::input {
namespace {

void ValidateInstancePlaceholders(const Json& instance, const std::filesystem::path& file) {
    const auto values = instance.flatten();
    for (const auto& [pointer, value] : values.items())
        if (value.is_string() && value.get_ref<const std::string&>().find("REPLACE_WITH_") != std::string::npos)
            Fail(file.string() + pointer, "replace the template placeholder in the local configuration");
}

}  // namespace

SiteInput ReadInstanceConfiguration(const Json& source, const std::filesystem::path& path,
    const std::filesystem::path& root) {
    SiteInput result{source, Json::object()};
    if (!source.contains("instance_file")) return result;
    const auto file = root / ReadString(source, "instance_file", path.string());
    const auto instance = ReadJson(file);
    CheckFields(instance, {"site", "hypervisor"}, file.string());
    ValidateInstancePlaceholders(instance, file);
    const auto& settings = Require(instance, "site", file.string());
    if (!settings.is_object()) Fail(file.string() + "/site", "expected an object");
    result.definition.erase("instance_file");
    result.definition.merge_patch(settings);
    if (instance.contains("hypervisor")) {
        result.hypervisor = instance.at("hypervisor");
        if (!result.hypervisor.is_object()) Fail(file.string() + "/hypervisor", "expected an object");
    }
    return result;
}

}  // namespace skeleton::input
