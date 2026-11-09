#include "skeleton/tofu/export.hpp"

namespace skeleton {
namespace {

bool HasManagedResources(const Json& module) {
    for (const auto& resource : module.value("resources", Json::array()))
        if (resource.at("mode") == "managed") return true;
    for (const auto& child : module.value("child_modules", Json::array()))
        if (HasManagedResources(child)) return true;
    return false;
}

void ValidateIdentity(const std::string& address, const Json& expected, const Json& before) {
    if (!expected.is_object() || !expected.contains("/name"))
        throw ConfigurationError("reset scope has no identity for " + address);
    for (const auto& [pointer, value] : expected.items())
        if (before.at(Json::json_pointer(pointer)) != value)
            throw ConfigurationError("reset identity differs from the generated scope at " + address + pointer);
}

void ValidateResetScope(const Json& scope) {
    if (scope.at("format") != 1 || !scope.at("resources").is_object())
        throw ConfigurationError("unsupported reset scope format");
}

Json InspectDestroyPlan(const Json& scope, const Json& native_plan) {
    if (!native_plan.at("format_version").get<std::string>().starts_with("1.") ||
        !native_plan.at("planned_values").is_object() || native_plan.value("errored", false))
        throw ConfigurationError("reset requires a valid native OpenTofu plan");
    if (HasManagedResources(native_plan.at("planned_values").value("root_module", Json::object())))
        throw ConfigurationError("reset plan leaves managed infrastructure behind");
    Json resources = Json::array(), vms = Json::array(), networks = Json::array(), pools = Json::array();
    for (const auto& resource : native_plan.value("resource_changes", Json::array())) {
        if (resource.at("mode") == "data" || resource.at("change").at("actions") == Json::array({"no-op"})) continue;
        const auto address = resource.at("address").get<std::string>();
        if (resource.at("change").at("actions") != Json::array({"delete"}))
            throw ConfigurationError("reset permits deletion only: " + address);
        if (!scope.at("resources").contains(address)) throw ConfigurationError("unapproved reset resource " + address);
        const auto& expected = scope.at("resources").at(address);
        ValidateIdentity(address, expected, resource.at("change").at("before"));
        resources.push_back({{"address", address}, {"identity", expected}});
        if (resource.at("type") == "libvirt_domain") vms.push_back(expected.at("/name"));
        if (resource.at("type") == "libvirt_network") networks.push_back(expected.at("/name"));
        if (resource.at("type") == "libvirt_pool") pools.push_back(expected.at("/name"));
    }
    return {{"basis", "native_destroy_plan"}, {"delete_count", resources.size()}, {"resources", resources},
            {"vm_names", vms}, {"network_names", networks}, {"pool_names", pools}};
}

}  // namespace

Json ValidateDestroyPlan(const Json& scope, const Json& native_plan) {
    try {
        ValidateResetScope(scope);
        return InspectDestroyPlan(scope, native_plan);
    } catch (const Json::exception& error) {
        throw ConfigurationError(std::string("invalid reset plan: ") + error.what());
    }
}

Json ValidateResetSnapshot(const Json& scope, const Json& snapshot) {
    try {
        ValidateResetScope(scope);
        if (!snapshot.at("state_addresses").is_array()) throw ConfigurationError("reset requires native state addresses");
        if (!snapshot.at("plan").is_null()) return InspectDestroyPlan(scope, snapshot.at("plan"));
        if (!snapshot.at("state_addresses").empty()) throw ConfigurationError("reset requires a saved destroy plan for nonempty state");
        return {{"basis", "empty_state"}, {"delete_count", 0}, {"resources", Json::array()},
                {"vm_names", Json::array()}, {"network_names", Json::array()}, {"pool_names", Json::array()}};
    } catch (const Json::exception& error) {
        throw ConfigurationError(std::string("invalid reset snapshot: ") + error.what());
    }
}

}  // namespace skeleton
