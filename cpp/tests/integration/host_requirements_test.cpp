#include <gtest/gtest.h>

#include "hypervisor_fixture.hpp"
#include "skeleton/pipeline/generator.hpp"

namespace skeleton::test {
namespace {

Json HostRequirementsJson() {
    return {{"hypervisor_platform", {{"distribution", "Debian"}, {"major_version", "13"}}},
        {"hypervisor_package_files", {{"/etc/apt/sources.list.d/stand.sources",
            {{"content", "Types: deb\nURIs: https://example.invalid/debian/\n"}, {"mode", "0644"}}}}},
        {"hypervisor_packages", {"libvirt-daemon-system=11.3"}},
        {"hypervisor_network_packages", {"netplan.io=1.1"}},
        {"cockpit_packages", {"cockpit-machines=330"}},
        {"backup_packages", {"nfs-common=2.8"}},
        {"backup_disabled_services", {"rpcbind.service"}},
        {"image_packages", {"guestfs-tools=1.54"}}};
}

SourceDefinitions HostDefinitions() {
    auto definitions = Definitions();
    definitions.site.hypervisor_settings = HypervisorJson();
    definitions.site.asset_data["host_packages"] = HostRequirementsJson();
    definitions.site.image.erase("hypervisor_packages");
    return definitions;
}

}  // namespace

TEST(HostRequirements, PublishesSelectedPackagesAndOneImageBuilderPlan) {
    auto definitions = HostDefinitions();
    definitions.site.asset_data["host_packages"]["private_metadata"] = "not an exported host setting";
    const auto topology = BuildTopology(definitions);

    const auto files = GenerateConfiguration(topology, AllocateAddressPlan(topology));

    EXPECT_EQ(Json::parse(files.at("ansible/host-requirements.json")), HostRequirementsJson());
    const auto image = Json::parse(files.at("ansible/image.json")).at("image");
    EXPECT_EQ(image.at("hypervisor_packages"), HostRequirementsJson().at("image_packages"));
    const auto report = Json::parse(files.at("reports/software.json"));
    EXPECT_EQ(report.at("image").at("hypervisor_packages"), image.at("hypervisor_packages"));
}

TEST(HostRequirements, RejectsMalformedRequirementsAndDuplicateImagePackageSources) {
    for (const auto fault : {"platform", "packages", "unpinned", "duplicate_image", "package_path", "package_content"}) {
        SCOPED_TRACE(fault);
        auto definitions = HostDefinitions();
        auto& requirements = definitions.site.asset_data["host_packages"];
        if (std::string(fault) == "platform") requirements["hypervisor_platform"].erase("major_version");
        if (std::string(fault) == "packages") requirements["hypervisor_packages"] = "libvirt=11.3";
        if (std::string(fault) == "unpinned") requirements["hypervisor_packages"] = {"libvirt"};
        if (std::string(fault) == "duplicate_image") definitions.site.image["hypervisor_packages"] = {"other=1"};
        if (std::string(fault) == "package_path") requirements["hypervisor_package_files"] = {{"../repository", {{"content", ""}, {"mode", "0644"}}}};
        if (std::string(fault) == "package_content") requirements["hypervisor_package_files"].begin().value()["content"] = 13;
        const auto topology = BuildTopology(definitions);
        EXPECT_THROW(GenerateConfiguration(topology, AllocateAddressPlan(topology)), ConfigurationError);
    }
}

}  // namespace skeleton::test
