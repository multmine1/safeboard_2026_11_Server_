#include <gtest/gtest.h>

#include "skeleton/placement/resources.hpp"

namespace skeleton::test {

TEST(Placement, SummarizesAllInitialAndKubernetesResourcesIncludingStoppedVmDisks) {
    const std::map<std::string, VmAllocation> allocations{
        {"router", {1, 1024, {{"nvme", 8}}, true, false}},
        {"worker", {2, 2048, {{"nvme", 16}, {"ssd", 32}}, true, true}},
        {"database", {4, 4096, {{"nvme", 32}, {"nvme", 64}, {"hd", 128}}, false, true}}};

    const auto plan = BuildResourcePlan(allocations, MemoryBudget(16 * 1024 * 1024, 8192));

    EXPECT_EQ(plan.configured.vm_count, 3);
    EXPECT_EQ(plan.configured.vcpu, 7);
    EXPECT_EQ(plan.configured.ram_mib, 7168);
    EXPECT_EQ(plan.configured.disk_gib_by_storage,
              (std::map<std::string, std::uint64_t>{{"nvme", 120}, {"ssd", 32}, {"hd", 128}}));
    EXPECT_EQ(plan.initially_running.vm_count, 2);
    EXPECT_EQ(plan.initially_running.vcpu, 3);
    EXPECT_EQ(plan.initially_running.ram_mib, 3072);
    EXPECT_EQ(plan.kubernetes_configured.ram_mib, 6144);
    EXPECT_EQ(plan.kubernetes_initially_running.ram_mib, 2048);
    EXPECT_EQ(plan.vms.at("database").resources.disk_gib_by_storage.at("nvme"), 96);
}

TEST(Placement, AssessesEachStoppedVmAgainstTheSameBudgetWithoutDoubleCountingRunningVms) {
    const std::map<std::string, VmAllocation> allocations{
        {"on", {2, 4096, {}, true, true}},
        {"fits", {2, 4096, {}, false, true}},
        {"too-large", {4, 6144, {}, false, true}}};

    const auto plan = BuildResourcePlan(allocations, MemoryBudget(12 * 1024 * 1024 + 512, 8192));

    ASSERT_TRUE(plan.memory);
    EXPECT_EQ(plan.memory->budget.HypervisorReserveKiB(), 4 * 1024 * 1024 + 512);
    EXPECT_EQ(plan.memory->vm_headroom_mib, 4096);
    const auto on = plan.vms.at("on").startup_memory.value();
    EXPECT_TRUE(on.within_budget);
    EXPECT_EQ(on.additional_mib, 0);
    EXPECT_EQ(on.remaining_mib, 4096);
    const auto fits = plan.vms.at("fits").startup_memory.value();
    EXPECT_TRUE(fits.within_budget);
    EXPECT_EQ(fits.remaining_mib, 0);
    EXPECT_EQ(fits.shortfall_mib, 0);
    const auto large = plan.vms.at("too-large").startup_memory.value();
    EXPECT_FALSE(large.within_budget);
    EXPECT_EQ(large.additional_mib, 6144);
    EXPECT_EQ(large.shortfall_mib, 2048);
}

TEST(Placement, DoesNotInventCapacityWhenNoMemoryBudgetIsSupplied) {
    const auto plan = BuildResourcePlan({{"worker", {2, 4096, {}, false, true}}}, std::nullopt);

    EXPECT_FALSE(plan.memory);
    EXPECT_FALSE(plan.vms.at("worker").startup_memory);
    EXPECT_EQ(plan.configured.ram_mib, 4096);
    EXPECT_EQ(plan.initially_running.ram_mib, 0);
}

TEST(Placement, RejectsImpossibleBudgetsAndInitialAllocationsAboveTheLimit) {
    for (const auto [physical, limit] : std::vector<std::pair<std::uint64_t, std::uint64_t>>{
             {0, 1}, {1024, 0}, {1024, 2}, {UINT64_MAX, UINT64_MAX}})
        EXPECT_THROW(MemoryBudget(physical, limit), ConfigurationError);
    const std::map<std::string, VmAllocation> allocations{
        {"first", {2, 4096, {}, true, true}}, {"second", {2, 4097, {}, true, true}}};

    EXPECT_THROW(BuildResourcePlan(allocations, MemoryBudget(16 * 1024 * 1024, 8192)), ConfigurationError);
}

TEST(Placement, RejectsOverflowRatherThanReportingWrappedResourceTotals) {
    for (const bool disks : {false, true}) {
        auto first = VmAllocation{1, UINT64_MAX, {}, false, false};
        auto second = VmAllocation{1, 1, {}, false, false};
        if (disks) {
            first.ram_mib = 1;
            first.disks = {{"ssd", UINT64_MAX}};
            second.disks = {{"ssd", 1}};
        }
        EXPECT_THROW(BuildResourcePlan({{"first", first}, {"second", second}}, std::nullopt), ConfigurationError);
    }
}

}  // namespace skeleton::test
