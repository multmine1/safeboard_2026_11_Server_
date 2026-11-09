#pragma once

#include "skeleton/networks/configuration.hpp"
#include "skeleton/networks/connections.hpp"
#include "skeleton/networks/kubernetes.hpp"

namespace skeleton {

struct PodAccessRule {
    std::string direction;
    std::vector<std::string> peers;
    std::vector<std::string> exclusions;
    std::string protocol;
    std::vector<std::uint16_t> ports;
};

struct NetworkPolicyPlan {
    std::vector<ConnectionPlan> connections;
    std::map<std::string, FirewallRules> firewalls;
    std::map<std::string, std::vector<PodAccessRule>> pod_access;
    std::map<std::string, std::map<std::string, std::set<std::string>>> service_clients;
};

NetworkPolicyPlan BuildNetworkPolicyPlan(const Topology& topology, const AddressPlan& addresses,
                                       const KubernetesPlan& cluster);

}  // namespace skeleton
