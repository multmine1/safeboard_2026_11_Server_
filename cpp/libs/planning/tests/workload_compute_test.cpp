#include <gtest/gtest.h>

#include "kubernetes_fixture.hpp"
#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::test {
TEST(WorkloadCompute, ValidatesConfiguredVmReservesBeforeAnyApplicationsExist) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.workload_capacity = WorkloadCapacityPolicy{{500, 512}, true, {{kApi, {500, 1024}}}};
    const auto topology = BuildTopology(definitions);

    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    ASSERT_TRUE(plan.workload_resources);
    EXPECT_EQ(plan.workload_resources->vms.at(kApi).reserved, (ComputeAmount{500, 1024}));
    EXPECT_TRUE(plan.workload_resources->applications.empty());
    definitions.site.workload_capacity->vm_reserves["missing"] = {500, 512};
    const auto invalid = BuildTopology(definitions);
    EXPECT_THROW(BuildInfrastructurePlan(invalid, AllocateAddressPlan(invalid)), ConfigurationError);
}

TEST(WorkloadCompute, SharesOneTypedAllocationBetweenNativeResourcesAndTheBudgetOfAStoppedVm) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.workload_capacity = WorkloadCapacityPolicy{{500, 512}, true};
    definitions.vms.at(kDb).domain["running"] = false;
    SetWorkloads(definitions, Json::array({ComputeWorkloadJson("worker", kDb)}));
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    const auto& application = plan.applications.at("worker");
    const auto& native = application.resources.at(0).at("spec");
    EXPECT_EQ(native.at("replicas"), 2);
    EXPECT_EQ(native.at("template").at("spec").at("containers").at(0).at("resources").at("requests"),
              Json({{"cpu", "200m"}, {"memory", "128Mi"}}));
    EXPECT_EQ(application.compute.at("main").total.requests, (ComputeAmount{400, 256}));
    ASSERT_TRUE(plan.workload_resources);
    EXPECT_EQ(plan.workload_resources->vms.at(kDb).enabled.requests, (ComputeAmount{400, 256}));
}

TEST(WorkloadCompute, RefusesMissingCapacityUnboundAllocationsAndRequestsAboveTheVmBudget) {
    for (const auto invalid : {"capacity", "unbound", "over_budget", "duplicate_native_resources"}) {
        auto definitions = Definitions();
        EnableKubernetes(definitions);
        auto worker = ComputeWorkloadJson("worker", kDb);
        if (std::string(invalid) != "capacity") definitions.site.workload_capacity = WorkloadCapacityPolicy{{500, 512}, true};
        if (std::string(invalid) == "unbound") worker["patches"] = Json::array();
        if (std::string(invalid) == "over_budget") worker["compute"]["main"]["replicas"] = 20;
        if (std::string(invalid) == "duplicate_native_resources")
            worker["resources"][0]["spec"]["template"]["spec"]["containers"][0]["resources"] = {{"requests", {{"cpu", "1"}}}};
        SetWorkloads(definitions, Json::array({worker}));
        const auto topology = BuildTopology(definitions);

        EXPECT_THROW(BuildInfrastructurePlan(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

TEST(WorkloadCompute, DoesNotInventAnUnusedReplicaSettingForAFixedNativeController) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.workload_capacity = WorkloadCapacityPolicy{{500, 512}, true};
    auto workload = ComputeWorkloadJson("worker", kDb);
    workload["compute"]["main"].erase("replicas");
    workload["compute"]["main"]["fixed_replicas"] = 1;
    workload["resources"][0]["spec"]["replicas"] = 1;
    workload["patches"].erase(0);
    SetWorkloads(definitions, Json::array({workload}));
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(plan.applications.at("worker").compute.at("main").total.pods, 1);
    EXPECT_EQ(plan.applications.at("worker").resources.at(0).at("spec").at("replicas"), 1);
}

}  // namespace skeleton::test
