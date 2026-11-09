#include "readers.hpp"

#include <fstream>
#include <limits>
#include <regex>
#include <sstream>

namespace skeleton::input {

void ReadUplink(const Json& value, SiteDefinition& site) {
    const auto context = site.source.string() + "/uplink";
    CheckFields(value, {"bridge", "address", "gateway"}, context);
    site.uplink_bridge = value.contains("bridge") ? ReadString(value, "bridge", context) :
        ReadString(site.hypervisor_settings.at("hypervisor_network"), "bridge", context);
    site.uplink_address = ReadString(value, "address", context);
    site.uplink_gateway = value.contains("gateway") ? ReadString(value, "gateway", context) :
        ReadString(site.hypervisor_settings.at("hypervisor_network"), "gateway", context);
    if (!std::regex_match(site.uplink_bridge, std::regex("[a-zA-Z0-9_.-]{1,15}")))
        Fail(context + "/bridge", "expected a Linux interface name");
}

void ReadImage(const Json& value, SiteDefinition& site) {
    const auto context = site.source.string() + "/image";
    CheckFields(value, {"url", "format", "pool", "path", "checksum", "packages", "apt_sources", "apt_config", "hypervisor_packages", "prepare_command", "prepare_ram_mib", "prepare_retries", "prepare_retry_delay_seconds", "sysprep_operations"}, context);
    for (const auto key : {"prepare_ram_mib", "prepare_retries", "prepare_retry_delay_seconds"})
        if (value.contains("packages") || value.contains(key))
            ReadPositive(value, key, context, std::numeric_limits<int>::max());
    if ((value.contains("packages") || value.contains("sysprep_operations")) &&
        ReadStringArray(value, "sysprep_operations", context).empty())
        Fail(context + "/sysprep_operations", "expected at least one identity reset operation");
    if (value.contains("packages") || value.contains("apt_config")) {
        const auto& config = Require(value, "apt_config", context);
        if (!std::filesystem::path(ReadString(config, "path", context + "/apt_config")).is_absolute())
            Fail(context + "/apt_config/path", "expected an absolute guest path");
        ReadString(config, "content", context + "/apt_config");
    }
    site.image_url = ReadString(value, "url", context);
    site.image = value;
    site.image_format = ReadString(value, "format", context);
    site.image_pool = ReadString(value, "pool", context);
    if (value.contains("path")) {
        site.image_path = ReadString(value, "path", context);
        if (!std::filesystem::path(site.image_path).is_absolute()) Fail(context + "/path", "expected an absolute hypervisor path");
    }
    if (site.image_format != "qcow2" && site.image_format != "raw")
        Fail(context + "/format", "expected qcow2 or raw");
    if (!site.image_url.starts_with("https://") && !site.image_url.starts_with("file:///"))
        Fail(context + "/url", "expected an immutable HTTPS URL or absolute file URI");
}

std::string ReadPublicKey(const Json& value, const std::filesystem::path& path,
                          const std::filesystem::path& source_root) {
    const auto context = path.string();
    if (value.contains("bootstrap_public_key") == value.contains("bootstrap_public_key_file"))
        Fail(context, "provide exactly one bootstrap_public_key or bootstrap_public_key_file");
    auto key = value.value("bootstrap_public_key", std::string{});
    if (value.contains("bootstrap_public_key_file")) {
        const auto file = source_root / ReadString(value, "bootstrap_public_key_file", context);
        std::ifstream stream(file);
        if (!stream) Fail(file.string(), "cannot read bootstrap public key");
        key.assign(std::istreambuf_iterator<char>(stream), {});
    }
    while (!key.empty() && (key.back() == '\n' || key.back() == '\r')) key.pop_back();
    if ((!key.starts_with("ssh-") && !key.starts_with("ecdsa-")) ||
        key.find_first_of("\r\n") != std::string::npos)
        Fail(context + "/bootstrap_public_key", "expected one OpenSSH public key");
    return key;
}

void ReadSiteAddressing(const Json& value, SiteDefinition& site) {
    const auto context = site.source.string();
    site.domain = Lowercase(ReadString(value, "domain", context));
    if (site.domain.size() > 253) Fail(context + "/domain", "DNS name exceeds 253 characters");
    std::istringstream labels(site.domain);
    for (std::string label; std::getline(labels, label, '.');)
        ReadName(Json{{"label", label}}, "label", context + "/domain");
    if (site.domain.back() == '.') Fail(context + "/domain", "omit the trailing dot");
    site.ipv4_pool = ReadString(value, "ipv4_pool", context);
    site.network_prefix = ReadPositive(value, "network_prefix", context, 30);
    site.gateway_offset = ReadPositive(value, "gateway_offset", context, UINT32_MAX);
    site.mac_prefix = Lowercase(ReadString(value, "mac_prefix", context));
}

void ValidateServiceSettings(const SiteDefinition& site) {
    const auto context = site.source.string() + "/services";
    const auto& ssh = Require(site.services, "ssh", context);
    ReadPositive(ssh, "port", context + "/ssh", 65535);
    if (ReadString(ssh, "transport", context + "/ssh") != "ssh")
        Fail(context + "/ssh/transport", "guest configuration requires SSH");
    const auto& dns = Require(site.services, "dns", context);
    if (ReadPositive(dns, "port", context + "/dns", 65535) != 53)
        Fail(context + "/dns/port", "system resolvers require DNS on port 53");
    const auto& network = Require(site.services, "network", context);
    const auto& interfaces = Require(network, "interfaces", context + "/network");
    std::set<std::string> names;
    for (const auto& role : {"lan", "wan"}) {
        const auto name = ReadString(interfaces, role, context + "/network/interfaces");
        if (!std::regex_match(name, std::regex("[a-zA-Z][a-zA-Z0-9_-]{0,14}")) || !names.insert(name).second)
            Fail(context + "/network/interfaces", "expected distinct Linux interface names");
    }
    const auto& firewall = Require(site.services, "firewall", context);
    const auto table = ReadString(firewall, "table", context + "/firewall");
    if (!std::regex_match(table, std::regex("[a-zA-Z_][a-zA-Z0-9_]*")))
        Fail(context + "/firewall/table", "expected an nftables identifier");
    for (const auto& chain : {"input", "forward", "output"})
        if (ReadString(firewall.at("policy"), chain, context + "/firewall/policy") != "drop")
            Fail(context + "/firewall/policy", "unlisted interactions must be denied");
}

void ValidateManagedNicParameters(const SiteDefinition& site) {
    const auto& domain = site.defaults.at("domain");
    if (domain.contains("devices") && domain.at("devices").contains("hostdevs"))
        Fail(site.source.string() + "/defaults/domain/devices/hostdevs", "PCI devices must come from named VM assignments");
    const auto& nic = site.defaults.at("nic");
    if (!nic.contains("filter_ref")) return;
    const auto parameters = nic.at("filter_ref").value("parameters", Json::array());
    for (const auto& parameter : parameters)
        if (parameter.at("name") == "IP")
            Fail(site.source.string() + "/defaults/nic/filter_ref/parameters", "IP is generated from NIC identity");
}

void ValidateCapacitySettings(const SiteDefinition& site) {
    if (!site.services.contains("capacity")) return;
    const auto& capacity = site.services.at("capacity");
    const auto context = site.source.string() + "/services/capacity";
    CheckFields(capacity, {"hypervisor_memory_kib", "active_vm_limit_mib"}, context);
    for (const auto key : {"hypervisor_memory_kib", "active_vm_limit_mib"})
        ReadPositive(capacity, key, context, std::numeric_limits<std::uint64_t>::max());
}

void ReadAssets(const Json& value, SiteDefinition& site, const std::filesystem::path& root) {
    for (const auto& [name, reference] : value.items()) {
        const auto path = root / ReadString(value, name, site.source.string() + "/assets");
        if (path.extension() == ".json") site.asset_data[name] = ReadJson(path);
        else {
            std::ifstream stream(path);
            if (!stream) Fail(path.string(), "cannot read asset");
            site.asset_data[name] = std::string(std::istreambuf_iterator<char>(stream), {});
        }
    }
}

void ReadSiteTemplates(const Json& value, SiteDefinition& site,
                        const std::filesystem::path& source_root) {
    const auto context = site.source.string();
    for (const auto& key : {"provider", "defaults", "services", "profiles"})
        if (!Require(value, key, context).is_object()) Fail(context + "/" + key, "expected an object");
    site.provider = value.at("provider");
    site.defaults = value.at("defaults");
    site.services = value.at("services");
    site.profiles = value.at("profiles");
    ResolveHypervisorSettings(site);
    const auto common = site.services.at("firewall").value("common_allow", Json::array());
    if (!common.is_array()) Fail(context + "/services/firewall/common_allow", "expected an array");
    for (std::size_t index = 0; index < common.size(); ++index)
        site.common_allow.push_back(ReadAccessRule(common[index], context + "/services/firewall/common_allow/" + std::to_string(index), false));
    for (const auto& key : {"network", "domain", "nic", "disk", "volume", "pool", "seed_disk", "cloud_init", "cloud_init_user"})
        if (!Require(site.defaults, key, context + "/defaults").is_object())
            Fail(context + "/defaults/" + key, "expected an object");
    for (const auto& key : {"source", "version", "uri"}) ReadString(site.provider, key, context + "/provider");
    const auto lock_path = source_root / ReadString(site.provider, "lock_file", context + "/provider");
    std::ifstream lock(lock_path);
    if (!lock) Fail(lock_path.string(), "cannot read provider lock file");
    site.provider_lock.assign(std::istreambuf_iterator<char>(lock), {});
    ValidateServiceSettings(site);
    ValidateManagedNicParameters(site);
    ValidateCapacitySettings(site);
    if (value.contains("assets")) ReadAssets(value.at("assets"), site, source_root);
    if (site.services.contains("workloads")) {
        site.workloads = ReadWorkloadDefinitions(ReadWorkloads(site.services.at("workloads"), source_root),
                                                site.asset_data.value("components", Json::object()));
        site.services.erase("workloads");
    }
    if (site.services.contains("workload_capacity")) {
        site.workload_capacity = ReadWorkloadCapacity(site.services.at("workload_capacity"), context + "/services/workload_capacity");
        site.services.erase("workload_capacity");
    }
    if (site.services.contains("backup")) {
        site.backup = ReadBackupDefinition(site.services.at("backup"), context + "/services/backup");
        site.services.erase("backup");
    }
}

SiteDefinition ReadSite(const Json& source, const std::filesystem::path& path,
                        const std::filesystem::path& source_root) {
    const auto input = ReadInstanceConfiguration(source, path, source_root);
    const auto& value = input.definition;
    const auto context = path.string();
    CheckFields(value, {"domain", "ipv4_pool", "network_prefix", "gateway_offset", "mac_prefix", "uplink", "hypervisor",
                        "management_sources", "dns_upstreams", "image", "bootstrap_public_key",
                        "bootstrap_public_key_file", "storage_root", "storage_classes", "vmctl_home", "provider",
                        "defaults", "services", "profiles", "assets", "pci_devices"}, context);
    SiteDefinition site;
    site.source = path;
    ReadSiteAddressing(value, site);
    ReadHypervisor(Require(value, "hypervisor", context), site, source_root, input.hypervisor);
    ReadUplink(Require(value, "uplink", context), site);
    ReadImage(Require(value, "image", context), site);
    ReadSiteDirectories(value, site);
    ReadSiteTemplates(value, site, source_root);
    ReadPciDevices(value, site);
    site.dns_upstreams = ReadStringArray(value, "dns_upstreams", context);
    if (site.dns_upstreams.empty()) Fail(context + "/dns_upstreams", "at least one upstream is required");
    site.management_sources = ReadStringArray(value, "management_sources", context);
    site.bootstrap_public_key = ReadPublicKey(value, path, source_root);
    return site;
}

}  // namespace skeleton::input
