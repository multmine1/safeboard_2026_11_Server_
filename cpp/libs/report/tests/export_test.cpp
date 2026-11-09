#include <gtest/gtest.h>

#include "fixture.hpp"
#include "kubernetes_fixture.hpp"
#include "skeleton/report/export.hpp"

namespace skeleton::test {

TEST(Reports, LeavesUndeclaredCapacityAndStartupAssessmentsUnknown) {
    const auto topology = BuildTopology(Definitions());
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddResourceReport(files, plan);

    const auto report = Json::parse(files.at("reports/resources.json"));
    EXPECT_TRUE(report.at("memory").is_null());
    EXPECT_TRUE(report.at("vms").at(kDb).at("startup_memory").is_null());
    EXPECT_EQ(report.at("totals").at("configured").at("ram_mib"), 8192);
    EXPECT_EQ(report.at("vms").at(kDb).at("fqdn"), topology.vm_domains.at(kDb));
}

TEST(Reports, ExposesThePreparedWorkloadAllocationsAndBudgetsInExplicitUnits) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.workload_capacity = WorkloadCapacityPolicy{{500, 512}, true, {{kDb, {500, 1024}}}};
    SetWorkloads(definitions, Json::array({ComputeWorkloadJson("worker", kDb)}));
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddWorkloadResourceReport(files, plan);

    const auto report = Json::parse(files.at("reports/workloads.json"));
    EXPECT_EQ(report.at("totals").at("enabled").at("requests").at("cpu_millicores"), 400);
    EXPECT_EQ(report.at("vms").at(kDb).at("budget").at("ram_mib"), 1024);
    EXPECT_EQ(report.at("policy").at("vm_reserves").at(kDb).at("ram_mib"), 1024);
    EXPECT_EQ(report.at("applications").at("worker").at("pods").at("main").at("replicas"), 2);
    EXPECT_EQ(report.at("applications").at("worker").at("requests").at("ram_mib"), 256);
    EXPECT_EQ(report.at("applications").at("worker").at("storage"), plan.applications.at("worker").storage);
}

TEST(Reports, EmitsExplicitVmBudgetsEvenWithoutApplicationDefinitions) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.workload_capacity = WorkloadCapacityPolicy{{500, 512}, true};
    const auto topology = BuildTopology(definitions);
    const auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    GeneratedFiles files;

    AddWorkloadResourceReport(files, plan);

    ASSERT_TRUE(files.contains("reports/workloads.json"));
    const auto report = Json::parse(files.at("reports/workloads.json"));
    EXPECT_TRUE(report.at("applications").empty());
    EXPECT_EQ(report.at("vms").at(kApi).at("budget").at("ram_mib"), 1536);
}

TEST(Reports, CataloguesPreparedSoftwareAndVolumesWithoutCopyingPrivateConfigurationBodies) {
    auto definitions = Definitions();
    EnableKubernetes(definitions);
    definitions.site.services["kubernetes"]["binary"] = {{"url", "https://example.invalid/k0s"}};
    definitions.site.image["hypervisor_packages"] = {"image-builder=1"};
    definitions.site.asset_data["host_packages"] = {
        {"hypervisor_platform", {{"distribution", "Debian"}, {"major_version", "13"}}},
        {"hypervisor_packages", {"libvirt=1"}}, {"cockpit_packages", {"cockpit=2"}},
        {"hypervisor_network_packages", {"netplan=3"}},
        {"unpublished", "do-not-export"}};
    const auto topology = BuildTopology(definitions);
    auto plan = BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
    plan.hypervisor["bootstrap"] = {{"hypervisor_monitoring", {
        {"unit", "metrics.service"}, {"binary", "/usr/lib/metrics"},
        {"configuration", {{"private", "do-not-export"}}}}}, {"private_key", "do-not-export"}};
    plan.vms.at(kDb).variables["files"]["/etc/private"] = "do-not-export";
    GeneratedFiles files;

    AddSoftwareReport(files, plan);

    ASSERT_TRUE(files.contains("reports/software.json"));
    const auto report = Json::parse(files.at("reports/software.json"));
    EXPECT_EQ(report.at("scope"), "generated_configuration");
    EXPECT_EQ(report.at("image").at("hypervisor_packages"), Json::array({"image-builder=1"}));
    EXPECT_EQ(report.at("hypervisor").at("packages").at("hypervisor_packages"), Json::array({"libvirt=1"}));
    EXPECT_EQ(report.at("hypervisor").at("packages").at("hypervisor_network_packages"), Json::array({"netplan=3"}));
    EXPECT_EQ(report.at("guests").at(kDb).at("packages"), plan.vms.at(kDb).variables.at("packages"));
    EXPECT_EQ(report.at("guests").at(kApi).at("kubernetes").at("role"), "controller");
    EXPECT_EQ(report.at("guests").at(kDb).at("virtual_disks").at("os"), plan.vms.at(kDb).storage.files.at("os").string());
    EXPECT_EQ(report.at("kubernetes").at("artifact"), plan.kubernetes.settings.at("binary"));
    EXPECT_TRUE(report.at("application_report").is_null());
    EXPECT_EQ(report.dump().find("do-not-export"), std::string::npos);

    plan.kubernetes.settings.erase("binary");
    AddSoftwareReport(files, plan);
    EXPECT_TRUE(Json::parse(files.at("reports/software.json")).at("kubernetes").at("artifact").is_null());
}

}  // namespace skeleton::test
