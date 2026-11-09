#include <gtest/gtest.h>

#include "fixture.hpp"
#include "skeleton/pipeline/generator.hpp"

namespace skeleton::test {
namespace {

void WritePciSources(const std::filesystem::path& root) {
    WriteSources(root);
    auto site = SiteJson();
    site["pci_devices"] = {{"accelerator", "0000:0A:1f.0"}, {"audio", "0000:0a:1f.1"}};
    site["defaults"]["hostdev"] = {{"managed", true}};
    WriteJson(root / "site.json", site);
    auto vm = VmJson(kDb, "lab", "postgres");
    vm["pci_devices"] = {"audio", "accelerator"};
    WriteJson(root / "machines/db.vm.json", vm);
}

GeneratedFiles CompilePciSources(const std::filesystem::path& root) {
    const auto topology = BuildTopology(ReadSourceDefinitions(root));
    return GenerateConfiguration(topology, AllocateAddressPlan(topology));
}

}  // namespace

TEST(PciExport, ResolvesLocalHardwareNamesAndSerializesPreparedNumericAddresses) {
    TemporaryDirectory directory;
    const auto root = directory.path / "source";
    WritePciSources(root);
    auto site = Json::parse(std::ifstream(root / "site.json"));
    const auto hardware = site.at("pci_devices");
    site.erase("pci_devices");
    site["instance_file"] = "../instance.json";
    WriteJson(root / "site.json", site);
    WriteJson(directory.path / "instance.json", {{"site", {{"pci_devices", hardware}}}});

    const auto files = CompilePciSources(root);
    const auto domains = Json::parse(files.at("tofu/modules/vm/data.tf.json")).at("locals").at("domains");
    const auto& devices = domains.at(kDb).at("devices").at("hostdevs");
    ASSERT_EQ(devices.size(), 2);
    EXPECT_EQ(devices[0], (Json{{"managed", true}, {"subsys_pci", {{"source", {{"address",
        {{"domain", 0}, {"bus", 10}, {"slot", 31}, {"function", 0}}}}}}}}));
    EXPECT_EQ(devices[1].at("subsys_pci").at("source").at("address").at("function"), 1);
    EXPECT_FALSE(domains.at(kApi).at("devices").contains("hostdevs"));
}

TEST(PciExport, RejectsUnknownOrSharedHardwareAndUnmanagedOverridePaths) {
    for (const auto fault : {"unknown", "repeated", "shared", "alias", "raw-vm", "raw-default", "policy"}) {
        SCOPED_TRACE(fault);
        TemporaryDirectory directory;
        WritePciSources(directory.path);
        auto site = Json::parse(std::ifstream(directory.path / "site.json"));
        auto vm = Json::parse(std::ifstream(directory.path / "machines/db.vm.json"));
        const auto name = std::string(fault);
        if (name == "unknown") vm["pci_devices"] = {"missing"};
        if (name == "repeated") vm["pci_devices"] = {"accelerator", "accelerator"};
        if (name == "shared") {
            auto other = VmJson(kApi, "api");
            other["pci_devices"] = {"accelerator"};
            WriteJson(directory.path / "machines/api.vm.json", other);
        }
        if (name == "alias") site["pci_devices"]["audio"] = "0000:0a:1f.0";
        if (name == "raw-vm") vm["domain"]["devices"]["hostdevs"] = Json::array();
        if (name == "raw-default") site["defaults"]["domain"]["devices"]["hostdevs"] = Json::array();
        if (name == "policy") site["defaults"].erase("hostdev");
        WriteJson(directory.path / "site.json", site);
        WriteJson(directory.path / "machines/db.vm.json", vm);
        EXPECT_THROW(CompilePciSources(directory.path), ConfigurationError);
    }
}

TEST(PciExport, RejectsMalformedAndOutOfRangePciAddresses) {
    for (const auto address : {"0a:1f.0", "0000:100:00.0", "0000:00:20.0", "0000:00:1f.8", "0000:00:gg.0"}) {
        SCOPED_TRACE(address);
        TemporaryDirectory directory;
        WritePciSources(directory.path);
        auto site = Json::parse(std::ifstream(directory.path / "site.json"));
        site["pci_devices"]["accelerator"] = address;
        WriteJson(directory.path / "site.json", site);
        EXPECT_THROW(CompilePciSources(directory.path), ConfigurationError);
    }
}

}  // namespace skeleton::test
