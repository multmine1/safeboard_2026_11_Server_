#pragma once

#include "skeleton/contracts/files.hpp"
#include "skeleton/networks/configuration.hpp"
#include "skeleton/networks/connections.hpp"
#include "skeleton/networks/kubernetes.hpp"
#include "skeleton/networks/policy.hpp"
#include "skeleton/vm/initialization.hpp"
#include "skeleton/vm/storage.hpp"
#include "skeleton/placement/resources.hpp"
#include "skeleton/installation/disks.hpp"
#include "skeleton/installation/bootstrap.hpp"

namespace skeleton {

struct VmPlan {
    DnsConfiguration dns;
    Json netplan;
    Json bootstrap_netplan;
    Json variables;
    Json bootstrap_files;
    VmInitializationPlan initialization;
    Json connection;
    VmStoragePlan storage;
    std::vector<PciAssignment> pci_devices;
};

struct RouterRuntimePlan {
    std::string id;
    std::string network;
    std::string fingerprint;
    std::string corefile;
    Json settings;
    Json host_configuration;
    Json payload;
    Json dns_data;
    Json environment;
};

struct AllowedVmPath {
    std::vector<std::string> nodes;
    std::vector<NetworkLinkPlan> links;
    std::vector<ConnectionPlan> flows;
};

struct ApplicationPlan {
    std::string name;
    std::optional<std::string> component;
    std::string vm;
    std::string network_namespace;
    std::string fingerprint;
    bool initially_running = false;
    bool enabled = false;
    std::map<std::string, PodResourcePlan> compute;
    Json resources;
    Json chart;
    Json chart_archive;
    Json guest;
    Json readiness;
    Json storage = Json::object();
    std::vector<std::string> dependencies;
    std::vector<std::string> deployment_order;
    std::set<std::string> required_vms;
    std::vector<std::string> required_secrets;
    Json managed_secrets = Json::object();
    std::map<std::string, std::string> files;
};

struct NfsExportPlan {
    std::string application;
    std::string server;
    std::string service;
    std::string name;
    std::string path;
    std::set<std::string> clients;
    Json host_export;
    std::optional<std::string> hypervisor_service;
};

struct SystemWorkloadReadiness {
    std::string network_namespace;
    std::string resource;
    std::optional<std::string> pod_selector;
    std::vector<std::string> nodes;
};

struct InfrastructurePlan {
    Topology topology;
    AddressPlan addresses;
    std::map<std::string, VmPlan> vms;
    Json hypervisor;
    std::map<std::pair<std::string, std::string>, AllowedVmPath> paths;
    KubernetesPlan kubernetes;
    NetworkPolicyPlan policy;
    std::map<std::string, RouterRuntimePlan> routers;
    std::map<std::string, ApplicationPlan> applications;
    std::map<std::pair<std::string, std::string>, NfsExportPlan> nfs_exports;
    std::vector<std::string> application_order;
    Json secret_configuration = Json::object();
    Json local_access = Json::object();
    ResourcePlan resources;
    std::optional<WorkloadResourcePlan> workload_resources;
    std::optional<BackupPlan> backup;
    std::optional<std::filesystem::path> controller_project_directory;
    std::vector<SystemWorkloadReadiness> system_readiness;
    Json host_requirements = Json::object();
    Json image = Json::object();
    std::optional<HostDiskPlan> host_installation;
    std::optional<InstallerBootstrapPlan> installer_bootstrap;
};

InfrastructurePlan BuildInfrastructurePlan(Topology topology, AddressPlan addresses);
Json BuildHypervisorBootstrap(const Topology& topology, const Json& network);
Json BuildLocalAccessPlan(const InfrastructurePlan& plan);
Json BuildHypervisorSshPlan(const InfrastructurePlan& plan);
void PrepareRegistryClients(InfrastructurePlan& plan);

}  // namespace skeleton
