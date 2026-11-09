#include <gtest/gtest.h>

#include "hypervisor_fixture.hpp"
#include "skeleton/planning/infrastructure.hpp"

namespace skeleton::test {
namespace {

SourceDefinitions PciDefinitions() {
    auto definitions = Definitions();
    definitions.site.pci_devices = {{"accelerator", ParsePciAddress("0000:0a:00.0")},
        {"audio", ParsePciAddress("0000:0a:00.1")}};
    definitions.site.hypervisor_settings = HypervisorJson();
    definitions.site.hypervisor_settings["hypervisor_pci"] = PciPolicyJson();
    definitions.site.defaults["hostdev"] = {{"managed", true}};
    return definitions;
}

InfrastructurePlan CompilePciHost(SourceDefinitions definitions) {
    const auto topology = BuildTopology(std::move(definitions));
    return BuildInfrastructurePlan(topology, AllocateAddressPlan(topology));
}

}  // namespace

TEST(PciHost, PreparesOnlyReadonlyObservationsAndSameVmIsolationBoundaries) {
    auto definitions = PciDefinitions();
    definitions.vms.at(kDb).pci_devices = {"accelerator", "audio"};
    const auto plan = CompilePciHost(definitions);
    const auto& pci = plan.hypervisor.at("bootstrap").at("hypervisor_pci");
    EXPECT_FALSE(pci.contains("files"));
    EXPECT_FALSE(pci.contains("rollback"));
    EXPECT_EQ(pci.at("devices")[0].at("class_path"), "/sys/bus/pci/devices/0000:0a:00.0/class");
    EXPECT_EQ(pci.at("devices")[1].at("iommu_link"), "/sys/bus/pci/devices/0000:0a:00.1/iommu_group");
    EXPECT_EQ(pci.at("devices")[0].at("allowed_group_members"), Json::array({"0000:0a:00.0", "0000:0a:00.1"}));
    EXPECT_EQ(pci.dump().find("reboot"), std::string::npos);

    auto disabled = PciDefinitions();
    disabled.site.pci_devices.clear();
    EXPECT_FALSE(CompilePciHost(disabled).hypervisor.at("bootstrap").contains("hypervisor_pci"));
}

TEST(PciHost, KeepsDifferentVmOwnersOutOfEachOthersIsolationBoundary) {
    auto definitions = PciDefinitions();
    definitions.vms.at(kDb).pci_devices = {"accelerator"};
    definitions.vms.at(kApi).pci_devices = {"audio"};
    const auto plan = CompilePciHost(definitions);
    const auto& devices = plan.hypervisor.at("bootstrap").at("hypervisor_pci").at("devices");
    EXPECT_EQ(devices[0].at("allowed_group_members"), Json::array({"0000:0a:00.0"}));
    EXPECT_EQ(devices[1].at("allowed_group_members"), Json::array({"0000:0a:00.1"}));
}

TEST(PciHost, RejectsMissingHardwarePolicyMalformedPathsAndClasses) {
    for (const auto fault : {"missing-policy", "path", "class"}) {
        SCOPED_TRACE(fault);
        auto definitions = PciDefinitions();
        auto& policy = definitions.site.hypervisor_settings["hypervisor_pci"];
        const std::string name = fault;
        if (name == "missing-policy") definitions.site.hypervisor_settings.erase("hypervisor_pci");
        if (name == "path") policy["sysfs_devices"] = "/sys/bus/../devices";
        if (name == "class") policy["allowed_classes"] = {"invalid"};
        EXPECT_THROW(CompilePciHost(definitions), ConfigurationError);
    }
}

}  // namespace skeleton::test
