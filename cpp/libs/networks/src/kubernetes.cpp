#include "skeleton/networks/kubernetes.hpp"

namespace skeleton {
namespace {

std::string NodeRole(const Topology& topology, const VmDefinition& vm) {
    if (topology.IsRouter(vm.id)) return "router";
    const auto& role = topology.definitions.site.profiles.at(vm.type).at("kubernetes").at("role");
    if (role == "standalone") return {};
    if (role != "controller" && role != "worker")
        throw ConfigurationError(vm.source.string() + "/kubernetes/role: expected controller, worker or standalone");
    return role;
}

void ValidateClusterPools(const SiteDefinition& site, const Json& settings) {
    const auto& network = settings.at("config").at("spec").at("network");
    auto uplink = ParseCidr(site.uplink_address);
    uplink.address = uplink.Network();
    const std::vector<Ipv4Cidr> pools{ParseCidr(network.at("podCIDR")), ParseCidr(network.at("serviceCIDR")),
                                    ParseCidr(site.ipv4_pool), uplink};
    for (std::size_t i = 0; i < pools.size(); ++i) {
        if (pools[i].address != pools[i].Network()) throw ConfigurationError("Kubernetes pools must be network addresses");
        for (std::size_t j = i + 1; j < pools.size(); ++j)
            if (pools[i].Overlaps(pools[j])) throw ConfigurationError("Kubernetes Pod/Service/VM/LAN pools overlap");
    }
    const auto prefix = settings.at("pod_prefix").get<unsigned>();
    if (prefix < pools.front().prefix || prefix < 20 || prefix > 30)
        throw ConfigurationError("Kubernetes pod_prefix must fit podCIDR and Calico block sizes");
    const auto& calico = network.at("calico");
    if (network.at("provider") != "calico" || calico.at("mode") != "bird" || calico.at("overlay") != "Never")
        throw ConfigurationError("the routed tree requires Calico BGP without an overlay");
}

void ValidateServicePort(const Json& port) {
    if (!port.is_number_integer() || port.get<std::int64_t>() < 1 || port.get<std::int64_t>() > 65535)
        throw ConfigurationError("Kubernetes service ports must be integers in 1..65535");
}

void ValidateRoutedSettings(const Json& settings) {
    if (!settings.at("ipam").at("strictAffinity").get<bool>())
        throw ConfigurationError("Pod pools bound to VM identity must disable address borrowing");
    const auto& bgp = settings.at("bgp");
    if (bgp.at("sourceAddress") != "None" || bgp.at("nextHopMode") != "Self")
        throw ConfigurationError("the routed NIC tree requires adjacent BGP source addresses and next-hop self");
    ValidateServicePort(bgp.at("port"));
    ValidateServicePort(settings.at("config").at("spec").at("api").at("port"));
    ValidateServicePort(settings.at("ports").at("kubelet"));
}

void ValidateDataDirectory(const Json& settings) {
    const auto& paths = settings.at("paths");
    if (!paths.contains("data_directory")) return;
    if (!paths.at("data_directory").is_string())
        throw ConfigurationError("k0s data_directory must be an absolute path");
    const auto path = std::filesystem::path(paths.at("data_directory").get<std::string>());
    if (!path.is_absolute() || path == path.root_path() || std::find(path.begin(), path.end(), "..") != path.end())
        throw ConfigurationError("k0s data_directory must remain below the filesystem root");
    for (const auto role : {"controller_args", "worker_args"})
        for (const auto& argument : settings.at(role)) {
            const auto value = argument.get<std::string>();
            if (value == "--data-dir" || value.starts_with("--data-dir="))
                throw ConfigurationError("k0s data directory must be declared only in paths.data_directory");
        }
}

void ValidateAsnPool(std::uint64_t first, std::uint64_t last) {
    const bool private16 = first >= 64512 && last <= 65534;
    const bool private32 = first >= 4200000000ULL && last <= 4294967294ULL;
    if (first > last || (!private16 && !private32))
        throw ConfigurationError("Kubernetes ASN pool must fit one private ASN range");
}

void AllocateClusterNodes(const Topology& topology, const AddressPlan& addresses, KubernetesPlan& cluster) {
    const auto& settings = cluster.settings;
    const auto pod_pool = ParseCidr(settings.at("config").at("spec").at("network").at("podCIDR"));
    const auto prefix = settings.at("pod_prefix").get<unsigned>();
    const auto subnet_size = std::uint64_t{1} << (32 - prefix);
    SlotAllocator pods(pod_pool.Size() / subnet_size, "Kubernetes node Pod pools");
    const auto first_asn = settings.at("asn_pool").at("first").get<std::uint64_t>();
    const auto last_asn = settings.at("asn_pool").at("last").get<std::uint64_t>();
    ValidateAsnPool(first_asn, last_asn);
    SlotAllocator asns(last_asn - first_asn + 1, "Kubernetes node ASNs");
    for (const auto& [id, vm] : topology.definitions.vms) {
        const auto role = NodeRole(topology, vm);
        if (role.empty()) continue;
        if (role == "controller") {
            if (!cluster.controller.empty()) throw ConfigurationError("exactly one k0s control-plane VM is supported");
            cluster.controller = id;
        }
        const auto& network = topology.vm_networks.at(id);
        const auto pod_slot = pods.Allocate("pods:" + id);
        const auto asn = static_cast<std::uint32_t>(first_asn + asns.Allocate("asn:" + id));
        Json taints = Json::array();
        if (role == "router") taints.push_back(settings.at("router_taint"));
        cluster.nodes.emplace(id, KubernetesNodePlan{id, role, network,
            FormatIpv4(addresses.vms.at(id).at("lan").address.address),
            {static_cast<std::uint32_t>(pod_pool.address + pod_slot * subnet_size), prefix}, asn,
            {{"skeleton.vm", id}, {"skeleton.network", network}, {"skeleton.role", vm.type}}, taints,
            vm.domain.value("running", topology.definitions.site.defaults.at("domain").at("running").get<bool>())});
    }
    if (cluster.controller.empty()) throw ConfigurationError("one k0s control-plane VM is required");
}

BgpPeerPlan MakePeer(const KubernetesPlan& cluster, const AddressPlan& addresses,
                     const std::string& from, const std::string& source_role,
                     const std::string& to, const std::string& target_role) {
    const auto& source = addresses.vms.at(from).at(source_role);
    const auto& target = addresses.vms.at(to).at(target_role);
    return {from, to, source.network, source_role, target_role,
        FormatIpv4(source.address.address), FormatIpv4(target.address.address),
        cluster.nodes.at(from).asn, cluster.nodes.at(to).asn};
}

void BuildPhysicalPeers(const Topology& topology, const AddressPlan& addresses, KubernetesPlan& cluster) {
    for (const auto& [id, network] : topology.definitions.networks) {
        for (const auto& member : network.members) {
            if (!cluster.nodes.contains(member)) continue;
            const auto role = topology.IsRouter(member) ? "wan" : "lan";
            cluster.peers.push_back(MakePeer(cluster, addresses, network.router, "lan", member, role));
            cluster.peers.push_back(MakePeer(cluster, addresses, member, role, network.router, "lan"));
        }
    }
}

void PrepareCalicoApi(Json& calico, const Json& environment) {
    Json variables = Json::array();
    for (const auto& [name, value] : environment.items()) {
        calico["envVars"][name] = value;
        variables.push_back({{"name", name}, {"value", value}});
    }
    const Json init{{"name", "install-cni"}, {"env", variables}};
    const Json content{{"spec", {{"template", {{"spec", {{"initContainers", Json::array({init})}}}}}}}};
    if (!calico.contains("patches")) calico["patches"] = Json::array();
    calico["patches"].push_back({
        {"target", {{"kind", "DaemonSet"}, {"name", "calico-node"}, {"namespace", "kube-system"}}},
        {"patch", {{"type", "StrategicMergePatch"}, {"content", content.dump()}}}});
}

Json PrepareK0sConfiguration(const Topology& topology, const KubernetesPlan& cluster) {
    auto configuration = cluster.settings.at("config");
    auto& spec = configuration["spec"];
    const auto& controller = cluster.nodes.at(cluster.controller);
    spec["api"]["address"] = controller.address;
    spec["api"]["sans"] = Json::array({controller.address, topology.vm_domains.at(controller.id)});
    PrepareCalicoApi(spec["network"]["calico"], cluster.api_environment);
    if (!spec.contains("workerProfiles")) spec["workerProfiles"] = Json::array();
    for (const auto& [id, name] : cluster.namespaces)
        spec["workerProfiles"].push_back({{"name", name}, {"values", cluster.settings.at("kubelet")}});
    return configuration;
}

}  // namespace

KubernetesPlan BuildKubernetesPlan(const Topology& topology, const AddressPlan& addresses) {
    KubernetesPlan cluster;
    const auto& services = topology.definitions.site.services;
    if (!services.contains("kubernetes")) return cluster;
    cluster.settings = services.at("kubernetes");
    for (const auto& [id, network] : topology.definitions.networks) {
        cluster.namespaces.emplace(id, "net-" + id);
        cluster.dns_servers.emplace(id, FormatIpv4(addresses.vms.at(network.router).at("lan").address.address));
    }
    ValidateClusterPools(topology.definitions.site, cluster.settings);
    ValidateRoutedSettings(cluster.settings);
    ValidateDataDirectory(cluster.settings);
    AllocateClusterNodes(topology, addresses, cluster);
    BuildPhysicalPeers(topology, addresses, cluster);
    cluster.api_environment = {{"KUBERNETES_SERVICE_HOST", cluster.nodes.at(cluster.controller).address},
        {"KUBERNETES_SERVICE_PORT", std::to_string(cluster.settings.at("config").at("spec").at("api").at("port").get<unsigned>())}};
    cluster.configuration = PrepareK0sConfiguration(topology, cluster);
    return cluster;
}

}  // namespace skeleton
