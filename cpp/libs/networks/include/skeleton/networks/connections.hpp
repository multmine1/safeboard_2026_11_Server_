#pragma once

#include "skeleton/networks/address.hpp"

namespace skeleton {

struct NetworkLinkPlan {
    std::string from;
    std::string to;
    std::string source_role;
    std::string target_role;
    std::string network;
};

struct ConnectionPlan {
    std::string from;
    std::string to;
    std::string source_network;
    std::string target_network;
    std::string source_address;
    std::string frontend_address;
    std::string target_address;
    std::string protocol;
    std::vector<std::uint16_t> frontend_ports;
    std::vector<std::uint16_t> target_ports;
    std::vector<std::string> routers;
    std::string nat_network;
    std::string nat_address;
    std::string service_name;
    bool dnat = false;
    bool source_pod = false;
    bool target_pod = false;
};

ConnectionPlan ResolveConnection(const Topology& topology, const AddressPlan& addresses,
                                 const AccessRule& rule);
std::vector<ConnectionPlan> ResolveConnections(const Topology& topology, const AddressPlan& addresses);
NetworkLinkPlan ResolvePhysicalLink(const AddressPlan& addresses, const std::string& from, const std::string& to);
Json GenerateClientConnections(const Topology& topology, const std::vector<ConnectionPlan>& connections,
                                const std::string& vm_id);

}  // namespace skeleton
