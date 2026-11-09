#include "skeleton/tofu/export.hpp"
#include "skeleton/vm/resources.hpp"
#include "reset_scope.hpp"

#include <iomanip>
#include <sstream>

namespace skeleton {
namespace {

std::string Expression(const std::string& expression) { return "${" + expression + "}"; }

std::string EscapeTemplate(std::string value) {
    for (const auto& token : {std::string("${"), std::string("%{")}) {
        std::size_t offset = 0;
        while ((offset = value.find(token, offset)) != std::string::npos) {
            value.insert(offset, 1, token.front());
            offset += token.size() + 1;
        }
    }
    return value;
}

Json Literal(Json value) {
    if (value.is_string()) return EscapeTemplate(value.get<std::string>());
    if (value.is_object() || value.is_array())
        for (auto& child : value) child = Literal(std::move(child));
    return value;
}

std::string ResourceField(const std::string& resource, const std::string& key,
                           const std::string& field) {
    return Expression(resource + "[" + Json(key).dump() + "]." + field);
}

void AddResource(Json& module, const std::string& type, const std::string& name,
                 const Json& values) {
    if (values.empty()) return;
    module["locals"][name] = values;
    Json resource{{"for_each", Expression("local." + name)}};
    std::set<std::string> fields;
    for (const auto& [id, object] : values.items())
        for (const auto& [field, value] : object.items()) fields.insert(field);
    for (const auto& field : fields)
        resource[field] = Expression("try(each.value[" + Json(field).dump() + "], null)");
    module["resource"][type][name] = resource;
}

Json RequiredProvider(const SiteDefinition& site) {
    return {{"required_providers", {{"libvirt", {{"source", site.provider.at("source")},
                                                {"version", site.provider.at("version")}}}}}};
}

Json GenerateNetworks(const Topology& topology, const AddressPlan& plan) {
    Json networks = Json::object();
    for (const auto& [id, network] : topology.definitions.networks) {
        auto configuration = Literal(topology.definitions.site.defaults.at("network"));
        configuration["name"] = "net-" + id;
        configuration["bridge"]["name"] = plan.bridges.at(id);
        networks[id] = configuration;
    }
    return networks;
}

Json GeneratePools(const Topology& topology, const std::map<std::string, VmPlan>& guests) {
    const auto& site = topology.definitions.site;
    Json pools = Json::object();
    for (const auto& [id, vm] : topology.definitions.vms) {
        for (const auto& disk : vm.disks) {
            auto configuration = site.defaults.at("pool");
            configuration["name"] = "vm-" + id + "-" + disk.storage;
            configuration["target"]["path"] =
                guests.at(id).storage.directories.at(disk.storage).string();
            pools[id + "/" + disk.storage] = Literal(configuration);
        }
    }
    return pools;
}

Json GenerateCloudInit(const std::map<std::string, VmPlan>& vms) {
    Json seeds = Json::object();
    for (const auto& [id, vm] : vms) {
        const auto& data = vm.initialization;
        seeds[id] = Literal(Json{{"name", "seed-" + id}, {"meta_data", data.metadata.dump()},
            {"user_data", "#cloud-config\n" + data.user_data.dump(2) + "\n"},
            {"network_config", data.network.dump(2)}});
    }
    return seeds;
}

Json GenerateBaseImage(const SiteDefinition& site) {
    std::ostringstream name;
    name << "image-" << std::hex << StableHash(site.image_url) << '.' << site.image_format;
    return Literal(Json{{"name", name.str()}, {"pool", site.image_pool},
                         {"target", {{"format", {{"type", site.image_format}}}}},
                         {"create", {{"content", {{"url", site.image_url}}}}}});
}

Json GenerateDiskVolume(const SiteDefinition& site, const VmDefinition& vm,
                        const DiskDefinition& disk, const VmStoragePlan& storage) {
    auto volume = Literal(site.defaults.at("volume"));
    volume["name"] = storage.files.at(disk.name).filename().string();
    volume["pool"] = ResourceField("libvirt_pool.storage", vm.id + "/" + disk.storage, "name");
    volume["capacity"] = ConvertBinaryUnits(disk.size_gib, 30, volume.at("capacity_unit"));
    if (disk.name == "os")
        volume["backing_store"] = {{"path", site.image_path.empty() ? Expression("libvirt_volume.image.path") : EscapeTemplate(site.image_path)},
                                    {"format", {{"type", site.image_format}}}};
    return volume;
}

Json GenerateVolumes(const Topology& topology, const std::map<std::string, VmPlan>& guests) {
    const auto& site = topology.definitions.site;
    Json volumes = Json::object();
    for (const auto& [id, vm] : topology.definitions.vms) {
        for (const auto& disk : vm.disks)
            volumes[id + "/" + disk.name] = GenerateDiskVolume(site, vm, disk, guests.at(id).storage);
        const auto& os = vm.disks.front();
        volumes[id + "/seed.iso"] = {{"name", guests.at(id).storage.seed.filename().string()},
                                   {"pool", ResourceField("libvirt_pool.storage", id + "/" + os.storage, "name")},
                                   {"create", {{"content", {{"url", ResourceField("libvirt_cloudinit_disk.seeds", id, "path")}}}}}};
    }
    return volumes;
}

Json GenerateDomainDisks(const SiteDefinition& site, const VmDefinition& vm) {
    Json disks = Json::array();
    for (std::size_t index = 0; index < vm.disks.size(); ++index) {
        const auto& definition = vm.disks[index];
        auto disk = Literal(site.defaults.at("disk"));
        disk["target"]["dev"] = FormatDiskDevice(index, disk.at("target").at("bus"));
        disk["serial"] = definition.name;
        disk["driver"]["type"] = site.defaults.at("volume").at("target").at("format").at("type");
        disk["source"] = {{"file", {{"file", ResourceField("libvirt_volume.volumes", vm.id + "/" + definition.name, "path")}}}};
        disks.push_back(disk);
    }
    auto seed = Literal(site.defaults.at("seed_disk"));
    seed["source"] = {{"file", {{"file", ResourceField("libvirt_volume.volumes", vm.id + "/seed.iso", "path")}}}};
    disks.push_back(seed);
    return disks;
}

Json GenerateDomainNics(const SiteDefinition& site, const std::map<std::string, NicPlan>& nics) {
    Json interfaces = Json::array();
    for (const auto& [role, nic] : nics) {
        auto interface = Literal(site.defaults.at("nic"));
        interface["mac"] = {{"address", nic.mac}};
        if (interface.contains("filter_ref")) {
            auto parameters = interface.at("filter_ref").value("parameters", Json::array());
            parameters.push_back({{"name", "IP"}, {"value", FormatIpv4(nic.address.address)}});
            interface["filter_ref"]["parameters"] = parameters;
        }
        interface["port_options"] = {{"isolated", nic.isolated ? "yes" : "no"}};
        if (nic.network.empty()) interface["source"] = {{"bridge", {{"bridge", site.uplink_bridge}}}};
        else interface["source"] = {{"network", {{"network", Expression("var.networks[" + Json(nic.network).dump() + "]")}}}};
        interfaces.push_back(interface);
    }
    return interfaces;
}

Json GenerateDomainHostDevices(const std::vector<PciAssignment>& devices) {
    Json result = Json::array();
    for (const auto& device : devices) {
        const auto& address = device.address;
        result.push_back({{"managed", device.managed}, {"subsys_pci", {{"source", {{"address",
            {{"domain", address.domain}, {"bus", address.bus}, {"slot", address.slot}, {"function", address.function}}}}}}}});
    }
    return result;
}

Json GenerateDomains(const Topology& topology, const AddressPlan& plan, const std::map<std::string, VmPlan>& guests) {
    const auto& site = topology.definitions.site;
    Json domains = Json::object();
    for (const auto& [id, vm] : topology.definitions.vms) {
        auto domain = Literal(site.defaults.at("domain"));
        domain.merge_patch(Literal(vm.domain));
        domain["name"] = "vm-" + id;
        domain["vcpu"] = vm.vcpu;
        domain["memory"] = ConvertBinaryUnits(vm.ram_mib, 20, domain.at("memory_unit"));
        domain["devices"]["interfaces"] = GenerateDomainNics(site, plan.vms.at(id));
        domain["devices"]["disks"] = GenerateDomainDisks(site, vm);
        if (!guests.at(id).pci_devices.empty())
            domain["devices"]["hostdevs"] = GenerateDomainHostDevices(guests.at(id).pci_devices);
        domains[id] = domain;
    }
    return domains;
}

Json AddNetworkModule(GeneratedFiles& files, const Topology& topology, const AddressPlan& plan) {
    Json module{{"terraform", RequiredProvider(topology.definitions.site)}};
    AddResource(module, "libvirt_network", "networks", GenerateNetworks(topology, plan));
    module["output"]["names"]["value"] = Expression("{ for id, network in libvirt_network.networks : id => network.name }");
    module["output"]["networks"]["value"] = Expression(
        "{ for id, network in libvirt_network.networks : id => { name = network.name, uuid = network.uuid, bridge = network.bridge.name } }");
    files["tofu/modules/network/main.tf.json"] = module.dump(2) + "\n";
    return module.at("locals").at("networks");
}

Json AddVmModule(GeneratedFiles& files, const Topology& topology, const AddressPlan& plan,
                  const std::map<std::string, VmPlan>& guests) {
    const auto& site = topology.definitions.site;
    Json module{{"terraform", RequiredProvider(site)},
                {"variable", {{"networks", {{"type", "map(string)"}}}}}};
    AddResource(module, "libvirt_pool", "storage", GeneratePools(topology, guests));
    AddResource(module, "libvirt_cloudinit_disk", "seeds", GenerateCloudInit(guests));
    if (site.defaults.contains("cloud_init_lifecycle"))
        module["resource"]["libvirt_cloudinit_disk"]["seeds"]["lifecycle"] = Literal(site.defaults.at("cloud_init_lifecycle"));
    if (site.image_path.empty()) module["resource"]["libvirt_volume"]["image"] = GenerateBaseImage(site);
    AddResource(module, "libvirt_volume", "volumes", GenerateVolumes(topology, guests));
    if (site.defaults.contains("volume_lifecycle"))
        module["resource"]["libvirt_volume"]["volumes"]["lifecycle"] = Literal(site.defaults.at("volume_lifecycle"));
    AddResource(module, "libvirt_domain", "domains", GenerateDomains(topology, plan, guests));
    if (site.defaults.contains("domain_lifecycle"))
        module["resource"]["libvirt_domain"]["domains"]["lifecycle"] = Literal(site.defaults.at("domain_lifecycle"));
    module["output"]["vms"]["value"] = Expression(
        "{ for id, vm in libvirt_domain.domains : id => { id = vm.id, uuid = vm.uuid, name = vm.name, running = vm.running } }");
    module["output"]["volumes"]["value"] = Expression(
        "{ for id, volume in libvirt_volume.volumes : id => { id = volume.id, path = volume.path, pool = volume.pool } }");
    const auto data = Json{{"locals", module.at("locals")}};
    module.erase("locals");
    files["tofu/modules/vm/main.tf.json"] = module.dump(2) + "\n";
    files["tofu/modules/vm/data.tf.json"] = data.dump(2) + "\n";
    auto values = data.at("locals");
    values["image"] = module.at("resource").at("libvirt_volume").value("image", Json::object());
    return values;
}

}  // namespace

void AddTofuConfigurations(GeneratedFiles& files, const InfrastructurePlan& infrastructure) {
    const auto& topology = infrastructure.topology;
    const auto& plan = infrastructure.addresses;
    const auto& guests = infrastructure.vms;
    const auto& site = topology.definitions.site;
    Json root{{"terraform", RequiredProvider(site)},
              {"provider", {{"libvirt", {{"uri", Literal(site.provider.at("uri"))}}}}},
              {"module", {{"network", {{"source", "./modules/network"}}},
                           {"vm", {{"source", "./modules/vm"},
                                    {"networks", Expression("module.network.names")}}}}},
              {"output", {{"infrastructure", {{"value", {{"networks", Expression("module.network.networks")},
                                                         {"vms", Expression("module.vm.vms")},
                                                         {"volumes", Expression("module.vm.volumes")}}}}}}}};
    files["tofu/main.tf.json"] = root.dump(2) + "\n";
    files["tofu/.terraform.lock.hcl"] = site.provider_lock;
    const auto networks = AddNetworkModule(files, topology, plan);
    const auto vm_data = AddVmModule(files, topology, plan, guests);
    files["tofu/reset-scope.json"] = tofu::BuildResetScope(networks, vm_data, guests, site).dump(2) + "\n";
}

}  // namespace skeleton
