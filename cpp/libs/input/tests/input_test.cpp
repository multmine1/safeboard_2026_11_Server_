#include <gtest/gtest.h>

#include "fixture.hpp"
#include "hypervisor_fixture.hpp"

namespace skeleton::test {

TEST(Source, RejectsBareLanProbesThatAccumulateSshAuthenticationPenalties) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto host = HypervisorJson();
    host["hypervisor_connections"][0]["match_exec"] = "nc -z 192.0.2.2 22";
    UseHypervisorFile(directory.path, host);
    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
}

TEST(Source, ReadsServiceAliasesAsNamesWithoutAnotherGatewayPort) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    const auto network_file = directory.path / "root.network.json";
    auto network = Json::parse(std::ifstream(network_file));
    network["exports"] = {{{"id", "30000000-0000-4000-8000-000000000009"},
        {"name", "https"}, {"vm", kDb}, {"protocol", "tcp"}, {"port", 443},
        {"scope", "uplink"}, {"sources", {"hypervisor"}}, {"aliases", {"cloud", "portfolio"}}}};
    WriteJson(network_file, network);

    const auto definitions = ReadSourceDefinitions(directory.path);

    ASSERT_EQ(definitions.networks.at(kRoot).exports.size(), 1);
    EXPECT_EQ(definitions.networks.at(kRoot).exports.front().aliases,
              (std::vector<std::string>{"cloud", "portfolio"}));
    network["exports"][0]["aliases"] = {"invalid name"};
    WriteJson(network_file, network);
    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
}

TEST(Source, AppliesOneLocalInstanceToTheSiteAndPhysicalHostBeforeValidation) {
    TemporaryDirectory directory;
    const auto root = directory.path / "source";
    WriteSources(root);
    UseHypervisorFile(root, HypervisorJson());
    auto site = Json::parse(std::ifstream(root / "site.json"));
    site.erase("domain");
    site["instance_file"] = "../instance.json";
    WriteJson(root / "site.json", site);
    WriteJson(directory.path / "instance.json", {
        {"site", {{"domain", "example.test"}}},
        {"hypervisor", {{"hypervisor_alias", "test-host"},
            {"generator", {{"storage_root", "/data/virtual-machines"}}}}}});

    const auto definitions = ReadSourceDefinitions(root);

    EXPECT_EQ(definitions.site.domain, "example.test");
    EXPECT_EQ(definitions.site.hypervisor_alias, "test-host");
    EXPECT_EQ(definitions.site.storage_root, "/data/virtual-machines");
    EXPECT_EQ(definitions.site.hypervisor_settings.at("hypervisor_network").at("bridge"), "br-wan");
    EXPECT_EQ(definitions.site.provider.at("version"), SiteJson().at("provider").at("version"));
}

TEST(Source, RejectsAMissingOrMalformedLocalInstanceInsteadOfUsingPublicFallbacks) {
    for (const auto invalid : {"missing", "array", "unknown_field", "unfilled_template"}) {
        SCOPED_TRACE(invalid);
        TemporaryDirectory directory;
        const auto root = directory.path / "source";
        WriteSources(root);
        auto site = SiteJson();
        site["instance_file"] = "../instance.json";
        WriteJson(root / "site.json", site);
        if (std::string(invalid) == "array") WriteJson(directory.path / "instance.json", Json::array());
        if (std::string(invalid) == "unknown_field")
            WriteJson(directory.path / "instance.json", {{"site", Json::object()}, {"password", "example"}});
        if (std::string(invalid) == "unfilled_template")
            WriteJson(directory.path / "instance.json", {
                {"site", {{"bootstrap_public_key", "ssh-ed25519 REPLACE_WITH_PUBLIC_KEY"}}}});

        try {
            ReadSourceDefinitions(root);
            FAIL() << "Invalid local instance was accepted";
        } catch (const ConfigurationError& error) {
            EXPECT_NE(std::string(error.what()).find("instance.json"), std::string::npos);
        }
    }
}

TEST(Source, RejectsManualIpParametersThatWouldOverrideNicIdentity) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto site = SiteJson();
    site["defaults"]["nic"]["filter_ref"]["parameters"] = {{{"name", "IP"}, {"value", "192.0.2.99"}}};
    WriteJson(directory.path / "site.json", site);

    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
}

TEST(Source, RejectsNonPositiveOrNonIntegralCapacityInsteadOfWrappingIt) {
    for (const Json invalid : {Json(-1), Json(0), Json(1.5), Json("8192")}) {
        SCOPED_TRACE(invalid.dump());
        TemporaryDirectory directory;
        WriteSources(directory.path);
        auto site = SiteJson();
        site["services"]["capacity"] = {
            {"hypervisor_memory_kib", 16 * 1024 * 1024}, {"active_vm_limit_mib", invalid}};
        WriteJson(directory.path / "site.json", site);

        EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
    }
}

TEST(Source, ReadsDefinitionsRecursivelyWithoutChoosingVmResources) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto db = VmJson(kDb, "db", "postgres");
    db["vcpu"] = 7;
    db["ram_mib"] = 12345;
    WriteJson(directory.path / "machines/db.vm.json", db);

    const auto definitions = ReadSourceDefinitions(directory.path);

    ASSERT_EQ(definitions.vms.size(), 4);
    ASSERT_EQ(definitions.networks.size(), 2);
    EXPECT_EQ(definitions.vms.at(kDb).vcpu, 7);
    EXPECT_EQ(definitions.vms.at(kDb).ram_mib, 12345);
}

TEST(Source, RejectsDuplicateIdsInsteadOfOverwritingAnObject) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    WriteJson(directory.path / "duplicate.vm.json", VmJson(kDb, "other"));

    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
}

TEST(Source, ReportsTheSourceFileAndInvalidField) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto db = VmJson(kDb, "db");
    db["ram_mib"] = 0;
    WriteJson(directory.path / "machines/db.vm.json", db);

    try {
        ReadSourceDefinitions(directory.path);
        FAIL() << "Zero RAM was accepted";
    } catch (const ConfigurationError& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("db.vm.json"), std::string::npos);
        EXPECT_NE(message.find("ram_mib"), std::string::npos);
    }
}

TEST(Source, RequiresExplicitSiteConfigurationInsteadOfUsingCompiledDefaults) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto site = SiteJson();
    site.erase("ipv4_pool");
    WriteJson(directory.path / "site.json", site);

    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
}

TEST(Source, RequiresPositiveExplicitRamForImagePackagePreparation) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto site = SiteJson();
    site["image"]["packages"] = {"nftables"};
    WriteJson(directory.path / "site.json", site);
    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);

    for (const auto& invalid : Json::array({0, -1, 1.5, "2048", nullptr, 2147483648ULL})) {
        SCOPED_TRACE(invalid.dump());
        site["image"]["prepare_ram_mib"] = invalid;
        WriteJson(directory.path / "site.json", site);
        EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
    }
}

TEST(Source, RequiresExplicitImagePreparationSettings) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto site = SiteJson();
    site["image"]["packages"] = {"nftables"};
    site["image"]["prepare_ram_mib"] = 2048;
    WriteJson(directory.path / "site.json", site);
    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);

    site["image"]["prepare_retries"] = 3;
    site["image"]["prepare_retry_delay_seconds"] = 30;
    site["image"]["sysprep_operations"] = {"machine-id", "ssh-hostkeys"};
    site["image"]["apt_config"] = {{"path", "/etc/apt/apt.conf.d/80-acquire"},
                                   {"content", "Acquire::Retries \"0\";\n"}};
    WriteJson(directory.path / "site.json", site);
    EXPECT_EQ(ReadSourceDefinitions(directory.path).site.image, site.at("image"));

    for (const auto& operations : Json::array({nullptr, Json::array()})) {
        auto invalid = site;
        if (operations.is_null()) invalid["image"].erase("sysprep_operations");
        else invalid["image"]["sysprep_operations"] = operations;
        WriteJson(directory.path / "site.json", invalid);
        EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
    }

    for (const auto key : {"prepare_retries", "prepare_retry_delay_seconds"}) {
        auto invalid = site;
        invalid["image"][key] = 0;
        WriteJson(directory.path / "site.json", invalid);
        EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
    }
}

TEST(Source, RejectsDiskCapacityThatWouldOverflowBytes) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto db = VmJson(kDb, "db");
    db["disks"][0]["size_gib"] = std::uint64_t{1} << 34;
    WriteJson(directory.path / "machines/db.vm.json", db);

    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
}

TEST(Source, RejectsLocalTransportForGuestConfiguration) {
    TemporaryDirectory directory;
    WriteSources(directory.path);
    auto site = SiteJson();
    site["services"]["ssh"]["transport"] = "local";
    WriteJson(directory.path / "site.json", site);
    EXPECT_THROW(ReadSourceDefinitions(directory.path), ConfigurationError);
}

TEST(Source, ReadsSharedHypervisorSettingsFromAReferencedJson) {
    TemporaryDirectory directory;
    const auto root = directory.path / "source";
    WriteSources(root);
    auto host = HypervisorJson();
    host["hypervisor_address"] = "192.0.2.3";
    host["generator"]["storage_root"] = "/data/libvirt";
    UseHypervisorFile(root, host);

    const auto definitions = ReadSourceDefinitions(root);

    EXPECT_EQ(definitions.site.hypervisor_address, "192.0.2.3");
    EXPECT_EQ(definitions.site.storage_root, "/data/libvirt");
    EXPECT_EQ(definitions.site.hypervisor_alias, host.at("hypervisor_alias").get<std::string>());
    EXPECT_EQ(definitions.site.services.at("hypervisor").at("admin_alias"), "host-bootstrap");
}

TEST(Source, RejectsStorageClassesThatEscapeTheHypervisorRoot) {
    TemporaryDirectory directory;
    const auto root = directory.path / "source";
    WriteSources(root);
    auto host = HypervisorJson();
    host["generator"]["storage_classes"]["../outside"] = Json::object();
    UseHypervisorFile(root, host);

    EXPECT_THROW(ReadSourceDefinitions(root), ConfigurationError);
}

TEST(Source, RejectsUnexpectedJsonInsteadOfSilentlyRetiringAllWorkloads) {
    TemporaryDirectory directory;
    const auto source = directory.path / "source";
    WriteSources(source);
    auto site = SiteJson();
    site["services"]["workloads"] = {{"directory", "../applications"}};
    WriteJson(source / "site.json", site);
    WriteJson(directory.path / "applications/misspelled.worklaod.json", {{"name", "existing"}});

    EXPECT_THROW(ReadSourceDefinitions(source), ConfigurationError);
}

}  // namespace skeleton::test
