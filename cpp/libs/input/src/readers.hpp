#pragma once

#include "skeleton/input/source.hpp"
#include <initializer_list>
#include <string_view>

namespace skeleton::input {
[[noreturn]] void Fail(const std::string& context, const std::string& message);
void CheckFields(const Json& object, std::initializer_list<std::string_view> fields, const std::string& context);
const Json& Require(const Json& object, const std::string& key, const std::string& context);
std::string ReadString(const Json& object, const std::string& key, const std::string& context);
std::uint64_t ReadPositive(const Json& object, const std::string& key, const std::string& context, std::uint64_t maximum);
std::string Lowercase(std::string value);
std::string ReadUuid(const Json& object, const std::string& key, const std::string& context);
std::string ReadName(const Json& object, const std::string& key, const std::string& context);
std::vector<std::string> ReadStringArray(const Json& object, const std::string& key, const std::string& context);
Json ReadJson(const std::filesystem::path& path);
VmDefinition ReadVm(const Json& value, const std::filesystem::path& path);
NetworkDefinition ReadNetwork(const Json& value, const std::filesystem::path& path);
AccessRule ReadAccessRule(const Json& value, const std::string& context, bool require_source);
SiteDefinition ReadSite(const Json& value, const std::filesystem::path& path, const std::filesystem::path& source_root);
struct SiteInput {
    Json definition;
    Json hypervisor;
};
SiteInput ReadInstanceConfiguration(const Json& source, const std::filesystem::path& path,
    const std::filesystem::path& root);
void ReadHypervisor(const Json& value, SiteDefinition& site, const std::filesystem::path& root,
    const Json& instance);
void ReadSiteDirectories(const Json& value, SiteDefinition& site);
void ReadPciDevices(const Json& value, SiteDefinition& site);
void ResolveHypervisorSettings(SiteDefinition& site);
Json ReadWorkloads(const Json& value, const std::filesystem::path& root);
std::map<std::string, PodDefinition> ReadPodDefinitions(const Json& value, const std::string& context);
WorkloadCapacityPolicy ReadWorkloadCapacity(const Json& value, const std::string& context);
BackupDefinition ReadBackupDefinition(const Json& value, const std::string& context);
BackupHostDefinition ReadBackupHost(const Json& value, const std::string& context);
}  // namespace skeleton::input
