#include "skeleton/tofu/export.hpp"
#include "skeleton/ansible/export.hpp"
#include "skeleton/graphviz/export.hpp"
#include "skeleton/k0s/export.hpp"
#include "skeleton/report/export.hpp"
#include "skeleton/pipeline/generator.hpp"
#include "skeleton/input/source.hpp"

namespace skeleton {
namespace {

bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& parent) {
    auto child_part = path.begin();
    for (auto parent_part = parent.begin(); parent_part != parent.end(); ++parent_part, ++child_part)
        if (child_part == path.end() || *child_part != *parent_part) return false;
    return true;
}

void ValidateOutputLocation(const std::filesystem::path& source, const std::filesystem::path& output) {
    const auto input = std::filesystem::weakly_canonical(source);
    const auto destination = std::filesystem::weakly_canonical(output);
    if (IsWithin(input, destination) || IsWithin(destination, input))
        throw ConfigurationError("source and output directories must not contain each other");
}

}  // namespace

GeneratedFiles GenerateConfiguration(const Topology& topology, const AddressPlan& plan) {
    try {
        GeneratedFiles files;
        const auto infrastructure = BuildInfrastructurePlan(topology, plan);
        AddAnsibleConfigurations(files, infrastructure);
        AddTofuConfigurations(files, infrastructure);
        AddK0sConfigurations(files, infrastructure);
        AddGraphvizConfiguration(files, infrastructure);
        AddResourceReport(files, infrastructure);
        AddWorkloadResourceReport(files, infrastructure);
        AddSoftwareReport(files, infrastructure);
        return files;
    } catch (const Json::exception& error) {
        throw ConfigurationError(topology.definitions.site.source.string() + ": " + error.what());
    }
}

void RunGenerationPipeline(const std::filesystem::path& source, const std::filesystem::path& output) {
    ValidateOutputLocation(source, output);
    const auto topology = BuildTopology(ReadSourceDefinitions(source));
    const auto addresses = AllocateAddressPlan(topology);
    const auto files = GenerateConfiguration(topology, addresses);
    WriteGeneratedFiles(files, output);
}

}  // namespace skeleton
