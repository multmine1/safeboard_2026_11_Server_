#include "guest.hpp"
#include "skeleton/networks/connections.hpp"

namespace skeleton {
namespace {

std::string GenerateSshConfiguration(const SiteDefinition& site, const std::string& stage) {
    const auto& ssh = site.services.at("ssh");
    std::string result = "Port " + std::to_string(ssh.at("port").get<unsigned>()) + "\n";
    for (const auto& [key, value] : ssh.at(stage + "_options").items())
        result += key + " " + value.get<std::string>() + "\n";
    return result;
}

Json GenerateStorageLinks(const VmStoragePlan& storage) {
    Json links = Json::array();
    for (const auto& [name, path] : storage.directories)
        links.push_back({{"src", path.string()}, {"dest", storage.links.at(name).string()}, {"state", "link"}});
    return links;
}

Json GenerateSysctl(const SiteDefinition& site, bool router, const Json& overrides) {
    auto result = site.services.at("guest_sysctl");
    result.merge_patch(overrides);
    if (!router) return result;
    result.merge_patch(site.services.at("router_sysctl"));
    for (const auto& role : {"lan", "wan"})
        for (const auto& [key, value] : site.services.at("router_interface_sysctl").items())
            result["net.ipv4.conf." + InterfaceName(site, role) + "." + key] = value;
    return result;
}

Json GenerateGuestMounts(const SiteDefinition& site, const VmDefinition& vm) {
    Json mounts = Json::array();
    if (!site.services.contains("storage")) return mounts;
    if (vm.disks.size() > 1 && site.defaults.at("disk").at("target").at("bus") != "virtio")
        throw ConfigurationError(vm.source.string() + "/disks: automatic guest mounts require virtio disks");
    const auto& storage = site.services.at("storage");
    for (const auto& disk : vm.disks) {
        if (disk.name == "os") continue;
        mounts.push_back({{"src", "/dev/disk/by-id/virtio-" + disk.name},
            {"path", (std::filesystem::path(storage.at("mount_root").get<std::string>()) / disk.name).string()},
            {"fstype", storage.at("fstype")}, {"opts", storage.at("opts")}});
    }
    return mounts;
}

void AddRouterFiles(VmPlan& guest, const Topology& topology,
                     const AddressPlan& plan, const NetworkPolicyPlan& policy, const std::string& id) {
    const auto& services = topology.definitions.site.services;
    const auto dns = GenerateCoreDnsConfiguration(topology, plan, policy.connections, id);
    guest.dns = dns;
    auto& files = guest.variables["files"];
    files[services.at("dns").at("corefile_path").get<std::string>()] = dns.corefile;
    for (const auto& [path, data] : dns.zones) files[path] = data;
    const auto resolver = services.at("dns_resolver");
    files[resolver.at("path").get<std::string>()] =
        "nameserver " + resolver.at("local_address").get<std::string>() + "\n";
    guest.variables["services"].push_back(services.at("dns").at("unit"));
    if (services.at("dns").contains("files"))
        files.merge_patch(services.at("dns").at("files"));
    if (services.at("dns").contains("binary")) guest.variables["binaries"].push_back(services.at("dns").at("binary"));
    if (services.at("dns").contains("account")) guest.variables["system_accounts"].push_back(services.at("dns").at("account"));
}

Json GenerateBootstrapNetplan(const Topology& topology, const AddressPlan& plan,
                               const std::string& vm_id, Json configuration) {
    if (!topology.IsRouter(vm_id)) return configuration;
    const auto& site = topology.definitions.site;
    const auto& network = topology.router_networks.at(vm_id);
    auto servers = site.dns_upstreams;
    const auto& upstreams = topology.definitions.networks.at(network).dns_upstreams;
    if (!upstreams.empty()) servers = upstreams;
    else if (network != topology.root) {
        const auto& parent = topology.definitions.networks.at(topology.parents.at(network)).router;
        servers = {FormatIpv4(plan.vms.at(parent).at("lan").address.address)};
    }
    auto& interfaces = configuration["network"]["ethernets"];
    interfaces[InterfaceName(site, "lan")].erase("nameservers");
    interfaces[InterfaceName(site, "wan")]["nameservers"]["addresses"] = servers;
    return configuration;
}

VmPlan GenerateGuest(const Topology& topology, const AddressPlan& plan,
                     const NetworkPolicyPlan& policy, const VmDefinition& vm) {
    const auto& site = topology.definitions.site;
    const auto& ssh = site.services.at("ssh");
    VmPlan guest;
    guest.pci_devices = BuildVmPciPlan(vm, site.pci_devices, site.defaults.value("hostdev", Json::object()));
    guest.storage = BuildVmStoragePlan(vm, {site.storage_root, site.vmctl_home,
        site.defaults.at("volume").at("target").at("format").at("type").get<std::string>()});
    guest.netplan = GenerateNetplan(topology, plan, vm.id);
    guest.bootstrap_netplan = GenerateBootstrapNetplan(topology, plan, vm.id, guest.netplan);
    if (!site.profiles.contains(vm.type))
        throw ConfigurationError(vm.source.string() + "/type: unknown profile " + vm.type);
    guest.variables = site.services.value("guest", Json::object());
    guest.variables.merge_patch(site.profiles.at(vm.type));
    if (topology.IsRouter(vm.id)) guest.variables["bootstrap_netplan"] = guest.bootstrap_netplan.dump(2) + "\n";
    if (site.image.contains("apt_config")) {
        const auto& config = site.image.at("apt_config");
        guest.variables["package_files"][config.at("path").get<std::string>()] = config.at("content");
    }
    guest.variables["id"] = vm.id;
    guest.variables["fqdn"] = topology.vm_domains.at(vm.id);
    guest.variables["libvirt_name"] = "vm-" + vm.id;
    guest.variables["network_path"] = site.services.at("network").at("path");
    if (site.services.at("network").contains("mode"))
        guest.variables["file_modes"][site.services.at("network").at("path").get<std::string>()] = site.services.at("network").at("mode");
    guest.variables["firewall_path"] = site.services.at("firewall").at("path");
    guest.variables["ssh_service"] = ssh.at("unit");
    guest.variables["connections"] = GenerateClientConnections(topology, policy.connections, vm.id);
    guest.variables["users"] = ssh.at("users");
    guest.variables["sysctl"] = GenerateSysctl(site, topology.IsRouter(vm.id),
                                               guest.variables.value("sysctl", Json::object()));
    if (!guest.variables.contains("services")) guest.variables["services"] = Json::array();
    guest.variables["services"].push_back(ssh.at("unit"));
    guest.variables["services"].push_back(site.services.at("firewall").at("unit"));
    for (const auto& package : site.services.at("firewall").at("packages"))
        guest.variables["packages"].push_back(package);
    if (topology.IsRouter(vm.id))
        for (const auto& package : site.services.at("router_packages"))
            guest.variables["packages"].push_back(package);
    guest.variables["files"][site.services.at("network").at("path").get<std::string>()] = guest.netplan.dump(2) + "\n";
    guest.variables["storage_links"] = GenerateStorageLinks(guest.storage);
    guest.variables["mounts"] = GenerateGuestMounts(site, vm);
    guest.variables["access"] = {{"bootstrap_user", ssh.at("bootstrap_user")},
                                 {"admin_user", ssh.at("admin_user")},
                                 {"private_key_ref", vm.id + "/admin/key"},
                                 {"sudo_password_ref", vm.id + "/admin/sudo_password"}};
    if (ssh.contains("admin_settings")) guest.variables["access"]["admin"] = ssh.at("admin_settings");
    guest.variables["address"] = FormatIpv4(plan.vms.at(vm.id).at("lan").address.address);
    const auto ssh_path = ssh.at("config_path").get<std::string>();
    guest.bootstrap_files[ssh_path] = GenerateSshConfiguration(site, "bootstrap");
    guest.variables["after_admin_verified_files"][ssh_path] = GenerateSshConfiguration(site, "admin");
    return guest;
}

Json GenerateUfwRules(const FirewallRules& firewall) {
    std::vector<Json> rules;
    for (const auto& access : firewall.guest_access) {
        if (access.protocol != "tcp" && access.protocol != "udp" && access.protocol != "any")
            throw ConfigurationError("the UFW module backend supports TCP/UDP or unrestricted local protocol rules");
        const bool routed = access.direction.starts_with("forward");
        Json rule{{"rule", "allow"}, {"direction", routed ? "in" : access.direction}, {"proto", access.protocol},
                   {"from_ip", access.source}, {"to_ip", access.destination}};
        if (routed) rule["route"] = true;
        if (access.ports.empty()) rules.push_back(rule);
        for (const auto port : access.ports) {
            rule["port"] = std::to_string(port);
            rules.push_back(rule);
        }
    }
    std::sort(rules.begin(), rules.end());
    rules.erase(std::unique(rules.begin(), rules.end()), rules.end());
    return rules;
}

Json RequiredDirectories(const Json& files) {
    std::set<std::string> directories;
    for (const auto& [path, data] : files.items())
        directories.insert(std::filesystem::path(path).parent_path().string());
    return directories;
}

}  // namespace

std::map<std::string, VmPlan> BuildVmPlans(
    const Topology& topology, const AddressPlan& plan, const NetworkPolicyPlan& policy) {
    const auto& firewall = policy.firewalls;
    const auto gateway = ResolveGuestSshGateway(topology.definitions.site);
    std::map<std::string, VmPlan> guests;
    for (const auto& [id, vm] : topology.definitions.vms) {
        auto guest = GenerateGuest(topology, plan, policy, vm);
        if (topology.IsRouter(id)) AddRouterFiles(guest, topology, plan, policy, id);
        guest.variables["files"][topology.definitions.site.services.at("firewall").at("path").get<std::string>()] =
            GenerateNftablesConfiguration(topology, plan, id, firewall.at(id));
        if (guest.variables.contains("ufw")) guest.variables["ufw"]["rules"] = GenerateUfwRules(firewall.at(id));
        SplitGuestNetwork(guest, topology, id);
        guest.variables["directories"] = RequiredDirectories(guest.variables.at("files"));
        const auto& site = topology.definitions.site;
        const VmInitializationSettings settings{topology.vm_domains.at(id),
            site.services.at("ssh").at("bootstrap_user"), site.bootstrap_public_key,
            site.defaults.at("cloud_init_user"), site.defaults.at("cloud_init")};
        guest.initialization = BuildVmInitialization(vm, settings, guest.bootstrap_netplan.at("network"), guest.bootstrap_files);
        const auto role = id == topology.definitions.networks.at(topology.root).router ? "wan" : "lan";
        guest.connection = BuildGuestSshConnection(site, id, FormatIpv4(plan.vms.at(id).at(role).address.address),
                                                   guest.variables.at("python"), gateway);
        guests.emplace(id, std::move(guest));
    }
    return guests;
}

}  // namespace skeleton
