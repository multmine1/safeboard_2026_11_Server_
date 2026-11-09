#include "skeleton/topology/model.hpp"
#include "skeleton/vm/resources.hpp"

#include <algorithm>
#include <set>

namespace skeleton {

void IndexHypervisorServices(Topology& topology);
void ValidatePciAssignments(const SourceDefinitions& definitions);

namespace {

[[noreturn]] void Fail(const NetworkDefinition& network, const std::string& message) {
    throw ConfigurationError(network.source.string() + " [" + network.id + "]: " + message);
}

void IndexRouters(Topology& topology) {
    for (const auto& [id, network] : topology.definitions.networks) {
        const auto found = topology.definitions.vms.find(network.router);
        if (found == topology.definitions.vms.end()) Fail(network, "router references an unknown VM");
        if (!topology.router_networks.emplace(network.router, id).second)
            Fail(network, "a router cannot own two networks");
        topology.vm_networks.emplace(network.router, id);
    }
}

void IndexMembers(Topology& topology) {
    for (const auto& [id, network] : topology.definitions.networks) {
        for (const auto& member : network.members) {
            if (!topology.definitions.vms.contains(member)) Fail(network, "unknown member " + member);
            if (member == network.router) Fail(network, "own router must not be repeated in members");
            if (topology.IsRouter(member)) {
                const auto& child = topology.router_networks.at(member);
                if (!topology.parents.emplace(child, id).second)
                    Fail(network, "a child router cannot have two upstream networks");
            } else if (!topology.vm_networks.emplace(member, id).second)
                Fail(network, "a VM cannot belong to two working networks");
        }
    }
    for (const auto& [id, vm] : topology.definitions.vms) {
        if (!topology.vm_networks.contains(id))
            throw ConfigurationError(vm.source.string() + " [" + id + "]: VM has no working network");
    }
}

void ValidateTree(Topology& topology) {
    std::vector<std::string> roots;
    for (const auto& [id, network] : topology.definitions.networks) {
        std::set<std::string> seen;
        auto current = id;
        while (topology.parents.contains(current)) {
            if (!seen.insert(current).second) Fail(network, "cycle in the network tree");
            current = topology.parents.at(current);
        }
        if (!topology.parents.contains(id)) roots.push_back(id);
    }
    if (roots.size() != 1) throw ConfigurationError("network tree must have exactly one root");
    topology.root = roots.front();
}

void RegisterName(std::set<std::string>& names, const std::string& name,
                  const NetworkDefinition& network) {
    if (name.size() > 253) Fail(network, "generated DNS name exceeds 253 characters: " + name);
    if (!names.insert(name).second) Fail(network, "duplicate generated DNS name: " + name);
}

void AssignVmName(Topology& topology, std::set<std::string>& names,
                   const std::string& vm_id, const NetworkDefinition& network) {
    const auto name = topology.definitions.vms.at(vm_id).name + "." +
                      topology.network_domains.at(network.id);
    RegisterName(names, name, network);
    topology.vm_domains.emplace(vm_id, name);
    topology.configuration_order.push_back(vm_id);
}

void BuildNamesAndOrder(Topology& topology) {
    std::set<std::string> names;
    std::vector<std::string> queue{topology.root};
    for (std::size_t index = 0; index < queue.size(); ++index) {
        const auto id = queue[index];
        const auto& network = topology.definitions.networks.at(id);
        const auto suffix = topology.parents.contains(id)
            ? topology.network_domains.at(topology.parents.at(id)) : topology.definitions.site.domain;
        const auto domain = network.name + "." + suffix;
        RegisterName(names, domain, network);
        topology.network_domains.emplace(id, domain);
        AssignVmName(topology, names, network.router, network);
        auto members = network.members;
        std::sort(members.begin(), members.end());
        for (const auto& member : members) {
            if (topology.IsRouter(member)) queue.push_back(topology.router_networks.at(member));
            else AssignVmName(topology, names, member, network);
        }
    }
}

void ValidateAccessRules(const Topology& topology) {
    for (const auto& [id, network] : topology.definitions.networks) {
        for (const auto& rule : network.allow) {
            if (!topology.vm_networks.contains(rule.from)) Fail(network, "unknown allow/from VM " + rule.from);
            if (topology.vm_networks.at(rule.from) != id) Fail(network, "allow/from must belong to the owning network");
            if (rule.to != "internet" && rule.to != "hypervisor" && !rule.to.starts_with("hypervisor:") &&
                !topology.definitions.vms.contains(rule.to) && !topology.service_networks.contains(rule.to))
                Fail(network, "unknown allow/to VM " + rule.to);
            if (rule.protocol.empty()) Fail(network, "direct allow requires protocol; service allow derives it from the export");
            if (rule.from == rule.to) Fail(network, "local self-connections do not traverse a router");
        }
    }
}

void ResolveHypervisorSources(Topology& topology) {
    for (auto& [id, network] : topology.definitions.networks)
        for (auto& service : network.exports)
            for (auto& source : service.sources)
                if (source == "hypervisor") source = topology.definitions.site.hypervisor_address + "/32";
}

void IndexServices(Topology& topology) {
    std::set<std::string> bindings;
    std::set<std::string> domains;
    for (const auto& [id, domain] : topology.network_domains) domains.insert(domain);
    for (const auto& [id, domain] : topology.vm_domains) domains.insert(domain);
    for (const auto& [id, network] : topology.definitions.networks) {
        for (const auto& service : network.exports) {
            if (!topology.vm_networks.contains(service.vm) || topology.vm_networks.at(service.vm) != id || topology.IsRouter(service.vm))
                Fail(network, "export target must be an ordinary VM of its network");
            if (topology.definitions.vms.contains(service.id) || topology.definitions.networks.contains(service.id) ||
                !topology.service_networks.emplace(service.id, id).second) Fail(network, "duplicate export UUID " + service.id);
            for (const auto& name : service.Names()) {
                if (!topology.service_ids.emplace(name, service.id).second) Fail(network, "duplicate service name " + name);
                const auto domain = name + "." + topology.network_domains.at(topology.root);
                RegisterName(domains, domain, network);
                topology.service_domains.emplace(name, domain);
            }
            if (!bindings.insert(service.scope + "/" + service.protocol + "/" + std::to_string(service.port)).second)
                Fail(network, "conflicting gateway port " + std::to_string(service.port));
        }
    }
}

void ResolveServiceRules(Topology& topology) {
    for (auto& [id, network] : topology.definitions.networks) {
        for (auto& rule : network.allow) {
            if (rule.to.starts_with("hypervisor:")) {
                const auto name = rule.to.substr(std::string("hypervisor:").size());
                if (!topology.host_services.contains(name)) Fail(network, "unknown hypervisor service " + name);
                if (!rule.protocol.empty() || !rule.ports.empty()) Fail(network, "host service allow derives protocol and ports");
                rule.protocol = "tcp";
                rule.ports = {topology.host_services.at(name).port};
                continue;
            }
            if (!topology.service_networks.contains(rule.to)) continue;
            const auto& service = topology.Service(rule.to);
            if (!rule.protocol.empty() || !rule.ports.empty()) Fail(network, "service allow derives protocol and ports from the export");
            rule.protocol = service.protocol;
            rule.ports = {service.port};
        }
    }
}

void BuildHostAccessPaths(Topology& topology) {
    for (const auto& [id, network] : topology.vm_networks) {
        auto& path = topology.host_access_paths[id];
        for (const auto& hop : topology.RouterPath(topology.root, network))
            path.push_back(topology.definitions.networks.at(hop).router);
        if (path.back() != id) path.push_back(id);
    }
}

}  // namespace

Topology BuildTopology(SourceDefinitions definitions) {
    ValidateVmProfiles(definitions.vms, definitions.site.profiles, definitions.site.storage_classes);
    ValidatePciAssignments(definitions);
    Topology topology{std::move(definitions)};
    IndexRouters(topology);
    IndexMembers(topology);
    ValidateTree(topology);
    BuildNamesAndOrder(topology);
    BuildHostAccessPaths(topology);
    ResolveHypervisorSources(topology);
    IndexServices(topology);
    IndexHypervisorServices(topology);
    ResolveServiceRules(topology);
    ValidateAccessRules(topology);
    return topology;
}

bool Topology::IsRouter(const std::string& vm_id) const {
    return router_networks.contains(vm_id);
}

bool Topology::IsDescendant(const std::string& network_id, const std::string& ancestor_id) const {
    auto current = network_id;
    while (current != ancestor_id && parents.contains(current)) current = parents.at(current);
    return current == ancestor_id;
}

std::string Topology::CommonAncestor(const std::string& from_network, const std::string& to_network) const {
    auto current = from_network;
    while (!IsDescendant(to_network, current)) current = parents.at(current);
    return current;
}

const GatewayService& Topology::Service(const std::string& id) const {
    const auto& services = definitions.networks.at(service_networks.at(id)).exports;
    return *std::find_if(services.begin(), services.end(), [&](const auto& service) { return service.id == id; });
}

const GatewayService& Topology::ServiceNamed(const std::string& name) const {
    return Service(service_ids.at(name));
}

std::vector<std::string> Topology::RouterPath(const std::string& from_network,
                                             const std::string& to_network) const {
    std::vector<std::string> ancestors{from_network};
    while (parents.contains(ancestors.back())) ancestors.push_back(parents.at(ancestors.back()));
    std::vector<std::string> tail;
    auto common = to_network;
    while (std::find(ancestors.begin(), ancestors.end(), common) == ancestors.end()) {
        tail.push_back(common);
        common = parents.at(common);
    }
    ancestors.erase(std::find(ancestors.begin(), ancestors.end(), common) + 1, ancestors.end());
    ancestors.insert(ancestors.end(), tail.rbegin(), tail.rend());
    return ancestors;
}

}  // namespace skeleton
