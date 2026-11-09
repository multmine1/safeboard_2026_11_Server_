#pragma once

#include "skeleton/contracts/types.hpp"
#include "skeleton/backup/plan.hpp"
#include "skeleton/networks/types.hpp"
#include "skeleton/topology/workloads.hpp"
#include "skeleton/vm/pci.hpp"

namespace skeleton {

struct SiteDefinition {
    std::string domain;
    std::string ipv4_pool;
    unsigned network_prefix{};
    std::uint64_t gateway_offset{};
    std::string mac_prefix;
    std::string uplink_bridge;
    std::string uplink_address;
    std::string uplink_gateway;
    std::string hypervisor_address;
    std::string hypervisor_alias;
    std::vector<std::string> management_sources;
    std::vector<std::string> dns_upstreams;
    std::string image_url;
    std::string image_format;
    std::string image_pool;
    std::string image_path;
    Json image;
    std::string bootstrap_public_key;
    std::string storage_root;
    std::vector<std::string> storage_classes;
    std::string vmctl_home;
    Json provider;
    std::string provider_lock;
    Json defaults;
    Json services;
    std::optional<std::vector<WorkloadDefinition>> workloads;
    std::optional<WorkloadCapacityPolicy> workload_capacity;
    std::optional<BackupDefinition> backup;
    Json profiles;
    Json asset_data = Json::object();
    Json hypervisor_settings = Json::object();
    std::vector<AccessRule> common_allow;
    std::filesystem::path source;
    std::map<std::string, PciAddress> pci_devices;
};

}  // namespace skeleton
