#include <gtest/gtest.h>

#include "skeleton/installation/preseed.hpp"

namespace skeleton::test {

TEST(InstallerPreseed, CombinesNativeDefaultsAndGeneratedValuesWithoutChangingTheSource) {
    const Json settings{{"d-i", {{"passwd/user-password-crypted", {{"type", "password"}, {"value", "!"}}}}}};
    const Json generated{{"d-i", {{"netcfg/get_ipaddress", {{"type", "string"}, {"value", "192.0.2.2"}}}}}};
    const auto result = BuildInstallerPreseed(settings, generated);
    EXPECT_EQ(result.at("d-i").size(), 2);
    EXPECT_EQ(result.at("d-i").at("netcfg/get_ipaddress").at("value"), "192.0.2.2");
    EXPECT_EQ(settings.at("d-i").size(), 1);
}

TEST(InstallerPreseed, RejectsSecretsEscapesAndOverridesOfComputedPartitioning) {
    for (const auto fault : {"secret", "hash", "escaped", "multiline", "override", "partitioning", "wrong_type"}) {
        SCOPED_TRACE(fault);
        Json settings{{"d-i", {{"time/zone", {{"type", "string"}, {"value", "Etc/UTC"}}}}}};
        Json generated{{"d-i", {{"netcfg/get_ipaddress", {{"type", "string"}, {"value", "192.0.2.2"}}}}}};
        if (std::string(fault) == "secret") settings["d-i"]["passwd/user-password"] = {{"type", "password"}, {"value", "plaintext"}};
        if (std::string(fault) == "hash") settings["d-i"]["passwd/user-password-crypted"] = {{"type", "string"}, {"value", "$6$hash"}};
        if (std::string(fault) == "escaped") settings["d-i"]["time/zone"]["value"] = "value\\nnew-line";
        if (std::string(fault) == "multiline") settings["d-i"]["time/zone"]["value"] = "value\nnew-line";
        if (std::string(fault) == "override") settings["d-i"]["netcfg/get_ipaddress"] = generated.at("d-i").at("netcfg/get_ipaddress");
        if (std::string(fault) == "partitioning") settings["d-i"]["partman-auto/disk"] = {{"type", "string"}, {"value", "/dev/sda"}};
        if (std::string(fault) == "wrong_type") settings["d-i"]["time/zone"]["value"] = 1;
        EXPECT_THROW(BuildInstallerPreseed(settings, generated), ConfigurationError);
    }
}

}  // namespace skeleton::test
