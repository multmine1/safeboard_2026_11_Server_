#include <gtest/gtest.h>

#include "skeleton/placement/workloads.hpp"

namespace skeleton::test {
namespace {

PodResourcePlan Pod(std::uint64_t replicas, ComputeAmount requests, ComputeAmount limits) {
    return BuildPodResourcePlan({replicas, {{"main", ContainerResources(requests, limits)}}, {}});
}

}  // namespace

TEST(PodResources, UsesConcurrentApplicationSumsAndSequentialInitPeaksBeforeMultiplyingReplicas) {
    const auto plan = BuildPodResourcePlan({2,
        {{"app", ContainerResources({100, 64}, {500, 256})},
         {"sidecar", ContainerResources({50, 32}, {100, 128})}},
        {{"setup", ContainerResources({400, 16}, {800, 64})},
         {"migrate", ContainerResources({200, 128}, {400, 256})}}});

    EXPECT_EQ(plan.per_pod.requests, (ComputeAmount{400, 128}));
    EXPECT_EQ(plan.per_pod.limits, (ComputeAmount{800, 384}));
    EXPECT_EQ(plan.total.requests, (ComputeAmount{800, 256}));
    EXPECT_EQ(plan.total.limits, (ComputeAmount{1600, 768}));
    EXPECT_EQ(plan.total.pods, 2);
}

TEST(PodResources, RejectsInvalidBoundsMissingApplicationsAndArithmeticOverflow) {
    EXPECT_THROW(ContainerResources({0, 1}, {1, 1}), ConfigurationError);
    EXPECT_THROW(ContainerResources({2, 1}, {1, 1}), ConfigurationError);
    EXPECT_THROW(ContainerResources({1, 2}, {1, 1}), ConfigurationError);
    EXPECT_THROW(BuildPodResourcePlan({1, {}, {}}), ConfigurationError);
    EXPECT_THROW(Pod(0, {1, 1}, {1, 1}), ConfigurationError);
    EXPECT_THROW(Pod(2, {UINT64_MAX, 1}, {UINT64_MAX, 1}), ConfigurationError);
}

TEST(WorkloadPlacement, AccountsForEnabledPodsOnStoppedVmsAndReportsLimitOvercommitSeparately) {
    const auto vms = BuildResourcePlan({{"on", {2, 2048, {}, true, true}},
                                       {"off", {1, 1024, {}, false, true}}}, std::nullopt);
    const std::map<std::string, WorkloadAllocation> apps{
        {"first", {"on", true, {{"web", Pod(2, {250, 128}, {1000, 768})}}}},
        {"future", {"off", true, {{"job", Pod(1, {500, 512}, {500, 512})}}}},
        {"disabled", {"on", false, {{"unused", Pod(1, {1000, 1024}, {2000, 2048})}}}}};

    const auto plan = BuildWorkloadResourcePlan(vms, apps, {{500, 512}, true});

    EXPECT_EQ(plan.enabled.requests, (ComputeAmount{1000, 768}));
    EXPECT_EQ(plan.configured.pods, 4);
    EXPECT_EQ(plan.enabled.pods, 3);
    EXPECT_EQ(plan.source_initially_running.pods, 2);
    EXPECT_EQ(plan.source_initially_running.requests, (ComputeAmount{500, 256}));
    EXPECT_EQ(plan.vms.at("on").budget, (ComputeAmount{1500, 1536}));
    EXPECT_EQ(plan.vms.at("on").request_headroom, (ComputeAmount{1000, 1280}));
    EXPECT_TRUE(plan.vms.at("on").cpu_limits_overcommitted);
    EXPECT_FALSE(plan.vms.at("on").memory_limits_overcommitted);
    EXPECT_EQ(plan.vms.at("off").request_headroom, (ComputeAmount{0, 0}));
}

TEST(WorkloadPlacement, RejectsOversizedRequestsUnknownNodesAndExplicitlyForbiddenLimitOvercommit) {
    const auto vms = BuildResourcePlan({{"node", {1, 1024, {}, true, true}},
                                       {"host", {1, 1024, {}, true, false}}}, std::nullopt);
    for (const auto target : {"node", "missing", "host"}) {
        const std::map<std::string, WorkloadAllocation> apps{
            {"service", {target, true, {{"main", Pod(1, {501, 512}, {1000, 1024})}}}}};
        EXPECT_THROW(BuildWorkloadResourcePlan(vms, apps, {{500, 512}, true}), ConfigurationError);
    }
    const std::map<std::string, WorkloadAllocation> overcommitted{
        {"service", {"node", true, {{"main", Pod(1, {500, 512}, {1000, 1024})}}}}};
    EXPECT_THROW(BuildWorkloadResourcePlan(vms, overcommitted, {{500, 512}, false}), ConfigurationError);
    EXPECT_THROW(BuildWorkloadResourcePlan(vms, {}, {{1001, 512}, true}), ConfigurationError);
}

TEST(WorkloadPlacement, UsesVmSpecificReservesForStoppedNodesAndRetainsTheDeclaredGlobalFallback) {
    const auto vms = BuildResourcePlan({{"on", {2, 2048, {}, true, true}},
                                       {"off", {1, 1024, {}, false, true}}}, std::nullopt);
    const WorkloadCapacityPolicy policy{{100, 256}, false, {{"off", {250, 768}}}};
    const std::map<std::string, WorkloadAllocation> apps{
        {"service", {"off", true, {{"main", Pod(1, {750, 256}, {750, 256})}}}}};

    const auto plan = BuildWorkloadResourcePlan(vms, apps, policy);

    EXPECT_EQ(plan.vms.at("on").reserved, (ComputeAmount{100, 256}));
    EXPECT_EQ(plan.vms.at("on").budget, (ComputeAmount{1900, 1792}));
    EXPECT_EQ(plan.vms.at("off").reserved, (ComputeAmount{250, 768}));
    EXPECT_EQ(plan.vms.at("off").budget, (ComputeAmount{750, 256}));
    EXPECT_EQ(plan.vms.at("off").request_headroom, (ComputeAmount{0, 0}));
    EXPECT_EQ(plan.source_initially_running.pods, 0);
}

TEST(WorkloadPlacement, RejectsUnresolvedVmReservesAndOverridesBeyondEitherCapacity) {
    const auto vms = BuildResourcePlan({{"node", {1, 1024, {}, true, true}},
                                       {"host", {1, 1024, {}, true, false}}}, std::nullopt);
    for (const auto target : {"missing", "host"}) {
        const WorkloadCapacityPolicy policy{{100, 128}, true, {{target, {200, 256}}}};
        EXPECT_THROW(BuildWorkloadResourcePlan(vms, {}, policy), ConfigurationError);
    }
    for (const auto reserve : {ComputeAmount{1001, 256}, ComputeAmount{200, 1025}}) {
        const WorkloadCapacityPolicy policy{{100, 128}, true, {{"node", reserve}}};
        EXPECT_THROW(BuildWorkloadResourcePlan(vms, {}, policy), ConfigurationError);
    }
}

}  // namespace skeleton::test
