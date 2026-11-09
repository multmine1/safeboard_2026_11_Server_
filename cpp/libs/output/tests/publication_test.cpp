#include <gtest/gtest.h>

#include <fstream>

#include "temporary_directory.hpp"
#include "skeleton/contracts/types.hpp"
#include "skeleton/output/publication.hpp"

namespace skeleton::test {

TEST(Publication, RefusesANewManagedFileThatShadowsAnUnmanagedSymlink) {
    TemporaryDirectory directory;
    const auto output = directory.path / "generated";
    const auto external = directory.path / "outside.json";
    std::ofstream(external) << "original";
    WriteGeneratedFiles({{"old.json", "previous result"}}, output);
    std::filesystem::create_symlink(external, output / "new.json");

    EXPECT_THROW(WriteGeneratedFiles({{"new.json", "replacement"}}, output), ConfigurationError);

    std::ifstream stream(external);
    std::string value;
    std::getline(stream, value);
    EXPECT_EQ(value, "original");
    EXPECT_TRUE(std::filesystem::exists(output / "old.json"));
}

}  // namespace skeleton::test
