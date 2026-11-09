#include "skeleton/ansible/export.hpp"
#include "backup.hpp"
#include "installation.hpp"

namespace skeleton {
namespace {

Json GenerateHostGroups(const SiteDefinition& site) {
    const auto& host = site.hypervisor_settings;
    if (!host.contains("hypervisor_bootstrap_alias")) return Json::object();
    const auto& bootstrap_alias = host.at("hypervisor_bootstrap_alias").get<std::string>();
    const Json bootstrap{{"ansible_connection", "ssh"}, {"ansible_user", host.at("controller_user")},
        {"ansible_python_interpreter", host.at("ansible_python_interpreter")},
        {"ansible_become_exe", host.at("ansible_become_exe")}};
    const Json libvirt{{"ansible_connection", "ssh"},
        {"ansible_user", host.at("hypervisor_user")},
        {"ansible_python_interpreter", host.at("ansible_python_interpreter")}};
    return {{"hypervisors", {{"hosts", {{bootstrap_alias, bootstrap}}}}},
        {"libvirt", {{"hosts", {{site.hypervisor_alias, libvirt}}}}}};
}

Json GenerateInventory(const InfrastructurePlan& plan) {
    Json hosts = Json::object();
    for (const auto& [id, vm] : plan.vms) {
        hosts[id] = {{"ansible_host", vm.connection.at("host")},
                     {"ansible_port", vm.connection.at("port")},
                     {"ansible_user", vm.connection.at("user")},
                     {"ansible_connection", vm.connection.at("type")},
                     {"ansible_python_interpreter", vm.connection.at("python")}};
        if (vm.connection.contains("ssh_args")) hosts[id]["ansible_ssh_common_args"] = vm.connection.at("ssh_args");
    }
    return {{"all", {{"hosts", hosts}, {"children", GenerateHostGroups(plan.topology.definitions.site)}}}};
}

Json GenerateDeploymentOrder(const InfrastructurePlan& plan) {
    const auto& topology = plan.topology;
    Json stages = Json::array();
    const auto& root = topology.definitions.networks.at(topology.root).router;
    for (const auto& id : topology.configuration_order) {
        stages.push_back({{"configure_vm", id}});
        if (id == root) stages.push_back({{"configure_hypervisor", topology.definitions.site.hypervisor_alias}});
    }
    Json result{{"stages", stages}, {"vm_dependencies", topology.host_access_paths}};
    if (plan.backup) result["backup_password_length"] = plan.backup->password_length;
    return result;
}

}  // namespace

void AddAnsibleConfigurations(GeneratedFiles& files, const InfrastructurePlan& plan) {
    files["ansible/inventory.json"] = GenerateInventory(plan).dump(2) + "\n";
    files["ansible/hypervisor.json"] = plan.hypervisor.dump(2) + "\n";
    if (plan.hypervisor.contains("reset"))
        files["ansible/reset.json"] = Json{{"reset", plan.hypervisor.at("reset")}}.dump(2) + "\n";
    if (plan.hypervisor.contains("sshd"))
        files["ansible/sshd.json"] = Json{{"sshd", plan.hypervisor.at("sshd")}}.dump(2) + "\n";
    if (!plan.local_access.empty()) files["ansible/local-access.json"] = plan.local_access.dump(2) + "\n";
    if (plan.local_access.contains("browser_proxy"))
        files["ansible/browser-proxy/proxy.pac"] = plan.local_access.at("browser_proxy").at("pac").get<std::string>();
    if (plan.hypervisor.contains("bootstrap"))
        files["ansible/hypervisor-bootstrap.json"] = plan.hypervisor.at("bootstrap").dump(2) + "\n";
    files["ansible/deployment.json"] = GenerateDeploymentOrder(plan).dump(2) + "\n";
    files["ansible/image.json"] = Json{{"image", plan.image}}.dump(2) + "\n";
    if (!plan.host_requirements.empty())
        files["ansible/host-requirements.json"] = plan.host_requirements.dump(2) + "\n";
    if (plan.host_installation) AddHostInstallationConfiguration(files, *plan.host_installation, plan.installer_bootstrap);
    if (plan.installer_bootstrap) AddInstallerBootstrapConfiguration(files, *plan.installer_bootstrap, *plan.host_installation);
    for (const auto& [id, vm] : plan.vms)
        files["ansible/host_vars/" + id + ".json"] = vm.variables.dump(2) + "\n";
    AddBackupConfiguration(files, plan);
}

}  // namespace skeleton
