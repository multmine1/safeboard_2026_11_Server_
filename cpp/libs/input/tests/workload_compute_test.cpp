#include <gtest/gtest.h>

#include "fixture.hpp"

namespace skeleton::test {
namespace {

Json Unit() {
    return {{"replicas", 2}, {"containers", {{"main", {
        {"requests", {{"cpu_millicores", 100}, {"ram_mib", 64}}},
        {"limits", {{"cpu_millicores", 500}, {"ram_mib", 128}}}}}}}};
}

Json Workload() {
    return {{"name", "service"}, {"vm", kApi}, {"enabled", true}, {"component", "server"},
        {"compute", {{"web", {{"containers", {{"main", {{"requests", {{"ram_mib", 96}}}}}}}}}}}};
}

}  // namespace

TEST(WorkloadInput, ResolvesComponentComputeAndOverridesOnceWithoutDuplicatingNativeResources) {
    const Json components{{"server", {{"compute", {{"web", Unit()}}},
        {"patches", {{{"op", "copy"}, {"from", "/context/compute/web/replicas"}, {"path", "/resources/0/spec/replicas"}}}}}}};
    const auto definitions = ReadWorkloadDefinitions(Json::array({Workload()}), components);

    ASSERT_EQ(definitions.size(), 1);
    const auto& workload = definitions.front();
    EXPECT_EQ(workload.name, "service");
    EXPECT_EQ(workload.vm, kApi);
    EXPECT_EQ(workload.compute.at("web").replicas, 2);
    EXPECT_EQ(workload.compute.at("web").containers.at("main").Requests(), (ComputeAmount{100, 96}));
    EXPECT_EQ(workload.patches.size(), 1);
}

TEST(WorkloadInput, RejectsNegativeFractionalUnknownAndIncompleteComputeFields) {
    for (const Json invalid : {Json(-1), Json(0), Json(1.5), Json("100")}) {
        auto unit = Unit();
        unit["containers"]["main"]["requests"]["cpu_millicores"] = invalid;
        auto workload = Workload();
        workload.erase("component");
        workload["compute"]["web"] = unit;
        EXPECT_THROW(ReadWorkloadDefinitions(Json::array({workload}), Json::object()), ConfigurationError);
    }
    for (const auto invalid : {"unknown", "missing", "bounds"}) {
        auto unit = Unit();
        if (std::string(invalid) == "unknown") unit["sidecar_scheduler"] = true;
        if (std::string(invalid) == "missing") unit.erase("replicas");
        if (std::string(invalid) == "bounds") unit["containers"]["main"]["limits"]["ram_mib"] = 1;
        auto workload = Workload();
        workload.erase("component");
        workload["compute"]["web"] = unit;
        EXPECT_THROW(ReadWorkloadDefinitions(Json::array({workload}), Json::object()), ConfigurationError);
    }
}

TEST(WorkloadInput, PreservesFixedReplicaFactsFromThePinnedComponentAndRejectsOverrides) {
    auto unit = Unit();
    unit.erase("replicas");
    unit["fixed_replicas"] = 1;
    const Json components{{"server", {{"compute", {{"web", unit}}}}}};
    auto workload = Workload();
    const auto parsed = ReadWorkloadDefinitions(Json::array({workload}), components);

    EXPECT_EQ(parsed.front().compute.at("web").replicas, 1);
    EXPECT_TRUE(parsed.front().compute.at("web").fixed_replicas);
    workload["compute"]["web"]["fixed_replicas"] = 2;
    EXPECT_THROW(ReadWorkloadDefinitions(Json::array({workload}), components), ConfigurationError);
    workload["compute"]["web"].erase("fixed_replicas");
    workload["compute"]["web"]["replicas"] = 2;
    EXPECT_THROW(ReadWorkloadDefinitions(Json::array({workload}), components), ConfigurationError);
}

TEST(CapacityInput, ResolvesPartialVmReservesAgainstTheDeclaredGlobalPolicy) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto site = SiteJson();
    site["services"]["workload_capacity"] = {
        {"non_application_reserve", {{"cpu_millicores", 500}, {"ram_mib", 512}}},
        {"permit_limit_overcommit", true},
        {"vm_reserves", {{kApi, {{"ram_mib", 1536}}}, {kDb, {{"cpu_millicores", 100}}}}}};
    WriteJson(directory.path / "site.json", site);

    const auto definitions = ReadSourceDefinitions(directory.path);

    ASSERT_TRUE(definitions.site.workload_capacity);
    const auto& policy = *definitions.site.workload_capacity;
    EXPECT_EQ(policy.non_application_reserve, (ComputeAmount{500, 512}));
    EXPECT_EQ(policy.vm_reserves.at(kApi), (ComputeAmount{500, 1536}));
    EXPECT_EQ(policy.vm_reserves.at(kDb), (ComputeAmount{100, 512}));
}

TEST(CapacityInput, RejectsMalformedReserveMapsAmountsAndNormalizedDuplicateUuids) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto site = SiteJson();
    site["services"]["workload_capacity"] = {
        {"non_application_reserve", {{"cpu_millicores", 500}, {"ram_mib", 512}}},
        {"permit_limit_overcommit", true}};
    const std::vector<Json> invalid{
        Json::array(), {{"not-a-uuid", {{"ram_mib", 128}}}},
        {{kApi, {{"ram_mib", 0}}}}, {{kApi, {{"ram_mib", nullptr}}}},
        {{kApi, {{"unknown", 1}}}},
        {{"20000000-0000-4000-8000-00000000000A", {{"ram_mib", 128}}},
         {"20000000-0000-4000-8000-00000000000a", {{"ram_mib", 256}}}}};
    for (const auto& value : invalid) {
        site["services"]["workload_capacity"]["vm_reserves"] = value;
        WriteJson(directory.path / "site.json", site);
        EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
    }
}

}  // namespace skeleton::test
