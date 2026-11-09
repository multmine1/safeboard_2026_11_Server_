#include <gtest/gtest.h>

#include "skeleton/tofu/export.hpp"

namespace skeleton::test {
namespace {

const std::string kVmAddress = "module.vm.libvirt_domain.domains[\"id\"]";

Json Scope() {
    return {{"format", 1}, {"resources", {{kVmAddress, {{"/name", "vm-id"}}}}}};
}

Json DestroyPlan() {
    return {{"format_version", "1.2"}, {"planned_values", Json::object()},
        {"resource_changes", {{{"address", kVmAddress}, {"mode", "managed"}, {"type", "libvirt_domain"},
            {"change", {{"actions", {"delete"}}, {"before", {{"name", "vm-id"}, {"private_data", "must-not-be-reported"}}}}}}}}};
}

}  // namespace

TEST(ResetPlan, AcceptsOnlyApprovedIdentitiesAndReportsNoPrivateNativeState) {
    const auto preview = ValidateDestroyPlan(Scope(), DestroyPlan());

    EXPECT_EQ(preview.at("delete_count"), 1);
    EXPECT_EQ(preview.at("vm_names"), Json::array({"vm-id"}));
    EXPECT_EQ(preview.at("resources").at(0).at("address"), kVmAddress);
    EXPECT_EQ(preview.dump().find("must-not-be-reported"), std::string::npos);
    EXPECT_EQ(ValidateDestroyPlan(Scope(), {{"format_version", "1.2"}, {"planned_values", Json::object()}})
        .at("delete_count"), 0);
}

TEST(ResetPlan, RejectsForeignResourcesChangedIdentitiesMutationsAndIncompleteDestruction) {
    for (const auto invalid : {"foreign", "renamed", "replace", "update", "remaining", "missing_plan"}) {
        SCOPED_TRACE(invalid);
        auto plan = DestroyPlan();
        if (std::string(invalid) == "foreign") plan["resource_changes"][0]["address"] = "module.other.vm";
        if (std::string(invalid) == "renamed") plan["resource_changes"][0]["change"]["before"]["name"] = "unmanaged";
        if (std::string(invalid) == "replace") plan["resource_changes"][0]["change"]["actions"] = {"delete", "create"};
        if (std::string(invalid) == "update") plan["resource_changes"][0]["change"]["actions"] = {"update"};
        if (std::string(invalid) == "remaining")
            plan["planned_values"]["root_module"]["child_modules"] = {{{"resources", {{{"mode", "managed"}}}}}};
        if (std::string(invalid) == "missing_plan") plan.erase("planned_values");

        EXPECT_THROW(ValidateDestroyPlan(Scope(), plan), ConfigurationError);
    }
}

TEST(ResetPlan, DistinguishesAnEmptyStateFromAMissingPlanForExistingResources) {
    const Json snapshot{{"state_addresses", Json::array()}, {"plan", nullptr}};

    EXPECT_EQ(ValidateResetSnapshot(Scope(), snapshot).at("basis"), "empty_state");
    auto missing_plan = snapshot;
    missing_plan["state_addresses"] = {kVmAddress};
    EXPECT_THROW(ValidateResetSnapshot(Scope(), missing_plan), ConfigurationError);
    auto unsupported_scope = Scope();
    unsupported_scope["format"] = 2;
    EXPECT_THROW(ValidateResetSnapshot(unsupported_scope, snapshot), ConfigurationError);
}

}  // namespace skeleton::test
