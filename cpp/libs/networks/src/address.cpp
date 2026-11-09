#include "skeleton/networks/address.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <charconv>
#include <iomanip>
#include <sstream>

#include <openssl/evp.h>

namespace skeleton {

std::uint64_t StableHash(const std::string& value) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (EVP_Digest(value.data(), value.size(), digest, &length, EVP_sha256(), nullptr) != 1)
        throw ConfigurationError("cannot calculate SHA-256");
    std::uint64_t result = 0;
    for (unsigned index = 0; index < 8; ++index) result = (result << 8) | digest[index];
    return result;
}

std::uint32_t ParseIpv4(const std::string& value) {
    in_addr address{};
    if (inet_pton(AF_INET, value.c_str(), &address) != 1)
        throw ConfigurationError("invalid IPv4 address: " + value);
    return ntohl(address.s_addr);
}

std::string FormatIpv4(std::uint32_t address) {
    in_addr native{htonl(address)};
    char buffer[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &native, buffer, sizeof(buffer)))
        throw ConfigurationError("cannot format IPv4 address");
    return buffer;
}

std::uint32_t Ipv4Cidr::Network() const {
    return address & (prefix == 0 ? 0 : UINT32_MAX << (32 - prefix));
}

std::uint64_t Ipv4Cidr::Size() const { return std::uint64_t{1} << (32 - prefix); }

bool Ipv4Cidr::Contains(std::uint32_t other) const {
    return other >= Network() && std::uint64_t{other} < std::uint64_t{Network()} + Size();
}

bool Ipv4Cidr::Overlaps(const Ipv4Cidr& other) const {
    return Contains(other.Network()) || other.Contains(Network());
}

std::string Ipv4Cidr::ToString() const {
    return FormatIpv4(address) + "/" + std::to_string(prefix);
}

Ipv4Cidr ParseCidr(const std::string& value) {
    const auto slash = value.find('/');
    if (slash == std::string::npos) throw ConfigurationError("CIDR prefix is required: " + value);
    unsigned prefix = 0;
    const auto begin = value.data() + slash + 1;
    const auto end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, prefix);
    if (result.ec != std::errc{} || result.ptr != end || prefix > 32)
        throw ConfigurationError("invalid IPv4 prefix: " + value);
    return {ParseIpv4(value.substr(0, slash)), prefix};
}

SlotAllocator::SlotAllocator(std::uint64_t capacity, std::string description)
    : capacity_(capacity), description_(std::move(description)) {}

void SlotAllocator::Reserve(std::uint64_t slot) {
    if (slot >= capacity_) throw ConfigurationError(description_ + ": reserved slot is outside the pool");
    occupied_.insert(slot);
}

std::uint64_t SlotAllocator::Allocate(const std::string& key) {
    if (occupied_.size() >= capacity_)
        throw ConfigurationError(description_ + ": address capacity exhausted for " + key);
    auto candidate = StableHash(key) % capacity_;
    while (!occupied_.insert(candidate).second) candidate = (candidate + 1) % capacity_;
    return candidate;
}

namespace {

void RequireHost(const Ipv4Cidr& lan, std::uint32_t address, const std::string& field) {
    if (!lan.Contains(address) || address == lan.Network() ||
        std::uint64_t{address} == std::uint64_t{lan.Network()} + lan.Size() - 1)
        throw ConfigurationError(field + ": expected a usable address in the physical LAN");
}

void ValidateSiteAddresses(const SiteDefinition& site) {
    const auto pool = ParseCidr(site.ipv4_pool);
    const auto uplink = ParseCidr(site.uplink_address);
    const auto gateway = ParseIpv4(site.uplink_gateway);
    const auto hypervisor = ParseIpv4(site.hypervisor_address);
    if (pool.address != pool.Network()) throw ConfigurationError("ipv4_pool must be a network address");
    if (site.network_prefix < pool.prefix || site.network_prefix > 30)
        throw ConfigurationError("network_prefix must fit ipv4_pool and leave a gateway and members");
    const auto subnet_size = std::uint64_t{1} << (32 - site.network_prefix);
    if (site.gateway_offset == 0 || site.gateway_offset >= subnet_size - 1)
        throw ConfigurationError("gateway_offset must be a usable host offset in each subnet");
    unsigned mac_prefix = 0;
    const auto mac_result = std::from_chars(site.mac_prefix.data(), site.mac_prefix.data() + site.mac_prefix.size(), mac_prefix, 16);
    if (site.mac_prefix.size() != 2 || mac_result.ec != std::errc{} ||
        mac_result.ptr != site.mac_prefix.data() + 2 || (mac_prefix & 3) != 2)
        throw ConfigurationError("mac_prefix must be one locally administered unicast byte");
    if (pool.Overlaps(uplink)) throw ConfigurationError("ipv4_pool overlaps the physical uplink LAN");
    RequireHost(uplink, uplink.address, "uplink/address");
    RequireHost(uplink, gateway, "uplink/gateway");
    RequireHost(uplink, hypervisor, "hypervisor/address");
    if (uplink.address == hypervisor || uplink.address == gateway || hypervisor == gateway)
        throw ConfigurationError("uplink, gateway and hypervisor must have distinct IP addresses");
    for (const auto& source : site.management_sources) {
        const auto cidr = ParseCidr(source.find('/') == std::string::npos ? source + "/32" : source);
        if (pool.Overlaps(cidr)) throw ConfigurationError("management_sources overlaps the VM pool");
    }
    for (const auto& upstream : site.dns_upstreams) {
        const auto address = ParseIpv4(upstream);
        if (pool.Contains(address) || address == uplink.address || address == hypervisor ||
            address >> 24 == 127 || address >> 24 == 0 || address >> 28 >= 14)
            throw ConfigurationError("dns_upstreams contains a local, managed or unusable DNS address");
    }
}

std::string FormatBridge(std::uint64_t slot) {
    std::ostringstream name;
    name << "br-" << std::hex << std::setfill('0') << std::setw(12) << slot;
    return name.str();
}

void AllocateSubnets(const Topology& topology, AddressPlan& plan) {
    const auto& site = topology.definitions.site;
    const auto pool = ParseCidr(site.ipv4_pool);
    const auto subnet_size = std::uint64_t{1} << (32 - site.network_prefix);
    SlotAllocator subnets(pool.Size() / subnet_size, "IPv4 subnet pool " + site.ipv4_pool);
    SlotAllocator bridges(std::uint64_t{1} << 48, "bridge names");
    for (const auto& [id, network] : topology.definitions.networks) {
        const auto slot = subnets.Allocate("subnet:" + id);
        plan.subnets.emplace(id, Ipv4Cidr{static_cast<std::uint32_t>(pool.address + slot * subnet_size),
                                          site.network_prefix});
        auto bridge = FormatBridge(bridges.Allocate("bridge:" + id));
        if (bridge == site.uplink_bridge) bridge = FormatBridge(bridges.Allocate("bridge:" + id));
        plan.bridges.emplace(id, bridge);
    }
}

NicPlan MakeNic(const std::string& vm_id, const std::string& role, const std::string& network,
                 Ipv4Cidr address, std::optional<std::uint32_t> gateway, bool isolated) {
    return {vm_id + "/" + role, role, network, {}, address, gateway, isolated};
}

void AllocateMemberAddresses(const Topology& topology, AddressPlan& plan) {
    for (const auto& [id, network] : topology.definitions.networks) {
        const auto subnet = plan.subnets.at(id);
        const auto gateway = static_cast<std::uint32_t>(subnet.address + topology.definitions.site.gateway_offset);
        plan.vms[network.router]["lan"] = MakeNic(network.router, "lan", id,
                                                  {gateway, subnet.prefix}, {}, false);
        SlotAllocator hosts(subnet.Size() - 2, network.source.string() + " [" + id + "]");
        hosts.Reserve(topology.definitions.site.gateway_offset - 1);
        auto members = network.members;
        std::sort(members.begin(), members.end());
        for (const auto& member : members) {
            const std::string role = topology.IsRouter(member) ? "wan" : "lan";
            const auto address = static_cast<std::uint32_t>(subnet.address + 1 +
                                                           hosts.Allocate("nic:" + member + "/" + role));
            plan.vms[member][role] = MakeNic(member, role, id, {address, 32}, gateway, true);
        }
    }
    const auto& site = topology.definitions.site;
    const auto& root = topology.definitions.networks.at(topology.root).router;
    plan.vms[root]["wan"] = MakeNic(root, "wan", {}, ParseCidr(site.uplink_address),
                                     ParseIpv4(site.uplink_gateway), false);
}

std::string FormatMac(std::uint64_t slot, const std::string& prefix) {
    std::ostringstream mac;
    mac << prefix << std::hex << std::setfill('0');
    for (int shift = 32; shift >= 0; shift -= 8)
        mac << ':' << std::setw(2) << ((slot >> shift) & 255);
    return mac.str();
}

void AllocateMacAddresses(AddressPlan& plan, const std::string& prefix) {
    SlotAllocator macs(std::uint64_t{1} << 40, "MAC addresses");
    std::set<std::uint32_t> assigned_ips;
    for (auto& [vm_id, nics] : plan.vms) {
        for (auto& [role, nic] : nics) {
            nic.mac = FormatMac(macs.Allocate("nic:" + nic.key), prefix);
            if (!assigned_ips.insert(nic.address.address).second)
                throw ConfigurationError("duplicate assigned IPv4 address for " + nic.key);
        }
    }
}

}  // namespace

AddressPlan AllocateAddressPlan(const Topology& topology) {
    try {
        ValidateSiteAddresses(topology.definitions.site);
    } catch (const ConfigurationError& error) {
        throw ConfigurationError(topology.definitions.site.source.string() + ": " + error.what());
    }
    AddressPlan plan;
    AllocateSubnets(topology, plan);
    AllocateMemberAddresses(topology, plan);
    AllocateMacAddresses(plan, topology.definitions.site.mac_prefix);
    return plan;
}

}  // namespace skeleton
