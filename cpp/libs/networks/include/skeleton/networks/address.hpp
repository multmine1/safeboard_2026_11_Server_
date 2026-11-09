#pragma once

#include <set>

#include "skeleton/topology/model.hpp"

namespace skeleton {

std::uint64_t StableHash(const std::string& value);
std::uint32_t ParseIpv4(const std::string& value);
std::string FormatIpv4(std::uint32_t address);

struct Ipv4Cidr {
    std::uint32_t address;
    unsigned prefix;

    std::uint32_t Network() const;
    std::uint64_t Size() const;
    bool Contains(std::uint32_t other) const;
    bool Overlaps(const Ipv4Cidr& other) const;
    std::string ToString() const;
};

Ipv4Cidr ParseCidr(const std::string& value);

class SlotAllocator {
public:
    SlotAllocator(std::uint64_t capacity, std::string description);
    void Reserve(std::uint64_t slot);
    std::uint64_t Allocate(const std::string& key);

private:
    std::uint64_t capacity_;
    std::string description_;
    std::set<std::uint64_t> occupied_;
};

struct NicPlan {
    std::string key;
    std::string role;
    std::string network;
    std::string mac;
    Ipv4Cidr address;
    std::optional<std::uint32_t> gateway;
    bool isolated;
};

struct AddressPlan {
    std::map<std::string, Ipv4Cidr> subnets;
    std::map<std::string, std::string> bridges;
    std::map<std::string, std::map<std::string, NicPlan>> vms;
};

AddressPlan AllocateAddressPlan(const Topology& topology);

}  // namespace skeleton
