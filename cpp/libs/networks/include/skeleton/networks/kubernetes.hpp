#pragma once

#include "skeleton/networks/address.hpp"

namespace skeleton {

struct KubernetesNodePlan {
    std::string id;
    std::string role;
    std::string network;
    std::string address;
    Ipv4Cidr pods;
    std::uint32_t asn;
    Json labels;
    Json taints;
    bool initially_running = false;
};

struct BgpPeerPlan {
    std::string from;
    std::string to;
    std::string network;
    std::string source_role;
    std::string target_role;
    std::string source_address;
    std::string target_address;
    std::uint32_t local_asn;
    std::uint32_t remote_asn;
};

struct KubernetesPlan {
    Json settings;
    Json configuration;
    Json api_environment;
    std::string controller;
    std::map<std::string, KubernetesNodePlan> nodes;
    std::vector<BgpPeerPlan> peers;
    std::map<std::string, std::string> namespaces;
    std::map<std::string, std::string> dns_servers;
};

KubernetesPlan BuildKubernetesPlan(const Topology& topology, const AddressPlan& addresses);

}  // namespace skeleton
