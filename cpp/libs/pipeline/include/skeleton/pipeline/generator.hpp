#pragma once

#include "skeleton/planning/infrastructure.hpp"
#include "skeleton/output/publication.hpp"
#include "skeleton/input/source.hpp"

namespace skeleton {

GeneratedFiles GenerateConfiguration(const Topology& topology, const AddressPlan& addresses);
void RunGenerationPipeline(const std::filesystem::path& source, const std::filesystem::path& output);
Json RunResetInspection(const std::filesystem::path& configuration);
Json RunResetValidation(const std::filesystem::path& configuration, const std::filesystem::path& scope,
                        const std::filesystem::path& snapshot, const std::filesystem::path& filesystem);

}  // namespace skeleton
