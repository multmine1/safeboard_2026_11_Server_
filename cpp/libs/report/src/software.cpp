#include "skeleton/report/export.hpp"

namespace skeleton {
namespace {

Json SelectFields(const Json& source, std::initializer_list<std::string_view> names) {
    Json result = Json::object();
    for (const auto name : names)
        if (source.contains(name)) result[std::string(name)] = source.at(name);
    return result;
}

Json HypervisorSoftware(const InfrastructurePlan& plan) {
    if (!plan.hypervisor.contains("bootstrap")) return nullptr;
    const auto& host = plan.hypervisor.at("bootstrap");
    Json result = Json::object();
    if (!plan.host_requirements.empty())
        result["packages"] = SelectFields(plan.host_requirements,
            {"hypervisor_packages", "hypervisor_network_packages", "cockpit_packages"});
    if (host.contains("hypervisor_cockpit"))
        result["cockpit"] = SelectFields(host.at("hypervisor_cockpit"), {"unit", "socket"});
    if (host.contains("hypervisor_monitoring"))
        result["monitoring"] = SelectFields(host.at("hypervisor_monitoring"),
            {"artifact", "unit", "binary", "config_path", "read_only_socket"});
    return result;
}

Json GuestSoftware(const InfrastructurePlan& plan, const std::string& id, const VmPlan& guest) {
    Json result = SelectFields(guest.variables, {"packages", "services", "binaries", "mounts"});
    result["type"] = plan.topology.definitions.vms.at(id).type;
    result["fqdn"] = plan.topology.vm_domains.at(id);
    result["virtual_disks"] = Json::object();
    for (const auto& [name, path] : guest.storage.files) result["virtual_disks"][name] = path.string();
    if (guest.variables.contains("network"))
        result["bootstrap_network"] = SelectFields(guest.variables.at("network"), {"packages", "services", "binaries"});
    if (plan.kubernetes.nodes.contains(id))
        result["kubernetes"] = {{"role", plan.kubernetes.nodes.at(id).role},
                                 {"artifact", plan.kubernetes.settings.value("binary", Json())}};
    return result;
}

Json KubernetesSoftware(const KubernetesPlan& cluster, const std::vector<SystemWorkloadReadiness>& workloads) {
    if (cluster.nodes.empty()) return nullptr;
    Json result{{"artifact", cluster.settings.value("binary", Json())}, {"system_workloads", Json::array()}};
    const auto& spec = cluster.configuration.at("spec");
    if (spec.contains("images")) result["images"] = spec.at("images");
    if (cluster.settings.contains("router_runtime"))
        result["router_runtime"] = SelectFields(cluster.settings.at("router_runtime"),
            {"namespace", "dns_image", "init_image", "resources", "init_resources"});
    for (const auto& workload : workloads)
        result["system_workloads"].push_back({{"namespace", workload.network_namespace},
            {"resource", workload.resource}, {"nodes", workload.nodes}});
    return result;
}

}  // namespace

void AddSoftwareReport(GeneratedFiles& files, const InfrastructurePlan& plan) {
    Json guests = Json::object();
    for (const auto& [id, guest] : plan.vms) guests[id] = GuestSoftware(plan, id, guest);
    const auto& site = plan.topology.definitions.site;
    const auto image = SelectFields(plan.image,
        {"url", "checksum", "path", "packages", "apt_sources", "hypervisor_packages", "prepare_command", "prepare_ram_mib"});
    const bool has_application_report = site.workloads.has_value() || site.workload_capacity.has_value();
    files["reports/software.json"] = Json{{"scope", "generated_configuration"},
        {"image", image}, {"hypervisor", HypervisorSoftware(plan)}, {"guests", guests},
        {"kubernetes", KubernetesSoftware(plan.kubernetes, plan.system_readiness)},
        {"application_report", has_application_report ? Json("reports/workloads.json") : Json()}}.dump(2) + "\n";
}

}  // namespace skeleton
