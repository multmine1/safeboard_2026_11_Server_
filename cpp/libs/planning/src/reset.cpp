#include "reset.hpp"
#include "skeleton/cleanup/boundary.hpp"

namespace skeleton {
namespace {

std::filesystem::path ProjectPath(const std::filesystem::path& root, const std::string& relative) {
    const auto path = std::filesystem::path(relative);
    if (path.empty() || path.is_absolute() || std::find(path.begin(), path.end(), "..") != path.end())
        throw ConfigurationError("reset workspace paths must remain below the declared project directory");
    return (root / path).lexically_normal();
}

Json BuildResetPaths(const Json& policy, const std::filesystem::path& root) {
    const auto directory = ProjectPath(root, policy.at("directory"));
    const auto tofu = ProjectPath(root, policy.at("tofu_directory"));
    const auto [directory_part, tofu_part] = std::mismatch(directory.begin(), directory.end(), tofu.begin(), tofu.end());
    if (directory_part == directory.end() || tofu_part == tofu.end())
        throw ConfigurationError("reset diagnostic files must remain outside the native OpenTofu directory");
    Json paths{{"directory", directory.string()}, {"tofu", tofu.string()},
        {"scope", (tofu / "reset-scope.json").string()},
        {"state", ProjectPath(tofu, policy.at("state_file")).string()},
        {"validator", ProjectPath(root, policy.at("validator")).string()}};
    std::set<std::filesystem::path> occupied{paths.at("state").get<std::string>(), paths.at("scope").get<std::string>(),
                                           paths.at("validator").get<std::string>()};
    for (const auto& [name, filename] : policy.at("files").items()) {
        const auto path = ProjectPath(directory, filename);
        if (std::filesystem::path(filename.get<std::string>()).has_parent_path() || !occupied.insert(path).second)
            throw ConfigurationError("reset snapshot filenames must be distinct and cannot overwrite state or the validator");
        paths[name] = path.string();
    }
    return paths;
}

void CollectVmRemovals(Json& reset, std::vector<RemovalTarget>& targets, const InfrastructurePlan& plan) {
    reset["volume_directories"] = Json::array();
    reset["remove_links"] = Json::array();
    reset["known_host_aliases"] = Json::array();
    for (const auto& [id, vm] : plan.vms) {
        reset["known_host_aliases"].push_back(id);
        const auto port = plan.topology.definitions.site.services.at("ssh").at("port").get<unsigned>();
        if (port != 22) reset["known_host_aliases"].push_back("[" + id + "]:" + std::to_string(port));
        for (const auto& [storage, path] : vm.storage.directories) {
            reset["volume_directories"].push_back(path.string());
            targets.push_back({path, RemovalKind::Directory});
            const auto& link = vm.storage.links.at(storage);
            reset["remove_links"].push_back(link.string());
            targets.push_back({link, RemovalKind::Link});
        }
    }
}

void CollectHostRemovals(Json& reset, std::vector<RemovalTarget>& targets, const Json& bootstrap,
                         const Json& controller, const Json& policy) {
    std::set<std::string> files{bootstrap.at("hypervisor_network").at("runtime_marker")};
    if (controller.contains("nwfilter")) {
        reset["nwfilter"] = controller.at("nwfilter");
        files.insert(reset.at("nwfilter").at("path"));
    }
    reset["stop_services"] = Json::array();
    if (policy.contains("retired")) {
        for (const auto& path : policy.at("retired").at("units")) {
            files.insert(path);
            reset["stop_services"].push_back(std::filesystem::path(path.get<std::string>()).filename().string());
        }
        for (const auto& path : policy.at("retired").at("files")) files.insert(path);
    }
    for (const auto& proxy : bootstrap.value("hypervisor_socket_proxies", Json::array())) {
        for (const auto key : {"socket_unit", "service_unit"}) reset["stop_services"].push_back(proxy.at(key));
        for (const auto key : {"socket_path", "service_path"}) files.insert(proxy.at(key));
    }
    if (bootstrap.contains("hypervisor_cockpit")) {
        const auto& cockpit = bootstrap.at("hypervisor_cockpit");
        reset["stop_services"].push_back(cockpit.at("socket"));
        reset["stop_services"].push_back(cockpit.at("unit"));
        files.insert(cockpit.at("dropin"));
        for (const auto key : {"certificate_file", "private_key_file"}) files.insert(cockpit.at("tls").at(key));
    }
    if (bootstrap.contains("hypervisor_monitoring")) {
        const auto& monitoring = bootstrap.at("hypervisor_monitoring");
        reset["stop_services"].push_back(monitoring.at("unit"));
        for (const auto key : {"binary", "config_path", "unit_path"}) files.insert(monitoring.at(key));
    }
    reset["remove_files"] = files;
    for (const auto& path : files) targets.push_back({path, RemovalKind::File});
}

void ValidateRetainedUnits(const Json& reset, const Json& bootstrap) {
    const auto& units = bootstrap.at("hypervisor_units");
    if (!units.is_object() || units.empty())
        throw ConfigurationError("reset requires the declared foundation units and retained companions");
    std::set<std::string> retained;
    for (const auto& [name, companions] : units.items()) {
        if (!name.ends_with(".service") || name.find_first_of("/ \t\r\n") != std::string::npos)
            throw ConfigurationError("foundation services require full service unit names");
        retained.insert(name);
        if (!companions.is_array()) throw ConfigurationError("retained companion units must be an array");
        for (const auto& companion : companions) {
            if (!companion.is_string()) throw ConfigurationError("retained companion units must be strings");
            retained.insert(companion.get<std::string>());
        }
    }
    for (const auto& value : reset.at("stop_services")) {
        auto name = value.get<std::string>();
        if (name.find('.') == std::string::npos) name += ".service";
        if (retained.contains(name)) throw ConfigurationError("reset cannot stop retained host unit: " + name);
    }
}

Json BuildPreservedBoundary(const InfrastructurePlan& plan, const Json& bootstrap) {
    const auto& site = plan.topology.definitions.site;
    std::set<std::string> paths{site.storage_root, bootstrap.at("hypervisor_network").at("path"),
                               bootstrap.at("hypervisor_sysctl_file"), site.services.at("reset").at("operation_lock_file")};
    for (const auto& pool : bootstrap.at("storage_pools")) paths.insert(pool.at("path"));
    for (const auto& mount : bootstrap.at("storage_mounts")) paths.insert(mount.at("path"));
    if (!site.image_path.empty()) paths.insert(site.image_path);
    for (const auto& [path, content] : bootstrap.at("hypervisor_files").items()) paths.insert(path);
    if (bootstrap.contains("hypervisor_boot")) {
        paths.insert(bootstrap.at("hypervisor_boot").at("link"));
        paths.insert(bootstrap.at("hypervisor_boot").at("target"));
    }
    const auto& host = site.hypervisor_settings;
    std::set<std::string> trees{host.at("controller_project_directory"),
        std::filesystem::path(bootstrap.at("controller_access").at("ssh_config").get<std::string>()).parent_path().string(),
        std::filesystem::path(host.at("hypervisor_sshd").at("path").get<std::string>()).parent_path().string()};
    for (const auto& path : host.at("hypervisor_sshd").at("host_keys"))
        trees.insert(std::filesystem::path(path.get<std::string>()).parent_path().string());
    return {{"paths", paths}, {"trees", trees}};
}

Json BuildTransitionNetwork(const SiteDefinition& site, const Json& bootstrap) {
    auto transition = bootstrap.at("hypervisor_runtime_netplan");
    transition["network"]["bridges"][site.uplink_bridge]["nameservers"] =
        bootstrap.at("hypervisor_netplan").at("network").at("bridges").at(site.uplink_bridge).at("nameservers");
    return transition;
}

std::string ReadControllerTrustFile(const SiteDefinition& site) {
    const auto& controller = site.services.at("controller");
    if (!controller.contains("known_hosts") || !controller.at("known_hosts").is_string())
        throw ConfigurationError("reset requires the explicit controller.known_hosts file");
    const auto path = std::filesystem::path(controller.at("known_hosts").get<std::string>());
    if (!path.is_absolute() || !path.lexically_normal().has_filename() ||
        std::find(path.begin(), path.end(), "..") != path.end())
        throw ConfigurationError("controller.known_hosts must be an absolute file path without parent traversal");
    return path.lexically_normal().string();
}

void ValidateResetHost(const SiteDefinition& site, const Json& bootstrap) {
    const auto account = bootstrap.at("hypervisor_user").get<std::string>();
    if (account == "root" || account == bootstrap.at("controller_user").get<std::string>())
        throw ConfigurationError("reset cannot remove the root or controller account");
    for (const auto& user : site.services.at("ssh").at("users"))
        if (user.at("name") == account) throw ConfigurationError("reset cannot remove a declared human account");
    if (site.provider.at("uri") != "qemu+sshcmd://" + site.hypervisor_alias + "/system")
        throw ConfigurationError("reset requires the declared hypervisor SSH alias in the provider URI");
    if (bootstrap.at("hypervisor_network").at("renderer") != "NetworkManager")
        throw ConfigurationError("reset currently supports the declared NetworkManager bridge");
}

Json BuildHostReset(const InfrastructurePlan& plan) {
    const auto& site = plan.topology.definitions.site;
    const auto& bootstrap = plan.hypervisor.at("bootstrap");
    const auto& policy = site.services.at("reset");
    const auto project = plan.controller_project_directory.value().string();
    ValidateResetHost(site, bootstrap);
    return {{"project_directory", project}, {"controller_user", bootstrap.at("controller_user")},
        {"controller_access", bootstrap.at("controller_access")},
        {"controller_known_hosts", ReadControllerTrustFile(site)},
        {"platform_pools", bootstrap.at("storage_pools")},
        {"account", {{"name", bootstrap.at("hypervisor_user")}, {"home", site.vmctl_home}}},
        {"directory_mode", policy.at("directory_mode")}, {"file_mode", policy.at("file_mode")},
        {"workspace", policy.at("workspace")}, {"session_retries", policy.at("session_retries")},
        {"session_delay_seconds", policy.at("session_delay_seconds")},
        {"paths", BuildResetPaths(policy, project)}, {"boundary", BuildPreservedBoundary(plan, bootstrap)},
        {"operation_lock_file", policy.at("operation_lock_file")},
        {"operation_lock_alias", plan.hypervisor.at("controller").at("admin_alias")},
        {"remove_directories", Json::array()},
        {"bootstrap_netplan", bootstrap.at("hypervisor_netplan")}, {"network", bootstrap.at("hypervisor_network")},
        {"transition_netplan", BuildTransitionNetwork(site, bootstrap)},
        {"libvirt_uri", plan.hypervisor.at("controller").at("local_libvirt_uri")},
        {"ssh_alias", site.hypervisor_alias}, {"hypervisor_address", site.hypervisor_address},
        {"vm_routes", plan.hypervisor.at("routes")}};
}

}  // namespace

void PrepareResetPlan(InfrastructurePlan& plan) {
    if (!plan.topology.definitions.site.services.contains("reset")) return;
    if (!plan.hypervisor.contains("bootstrap")) throw ConfigurationError("reset requires the complete hypervisor policy");
    auto reset = BuildHostReset(plan);
    std::vector<RemovalTarget> targets{{plan.topology.definitions.site.vmctl_home, RemovalKind::Directory}};
    CollectVmRemovals(reset, targets, plan);
    CollectHostRemovals(reset, targets, plan.hypervisor.at("bootstrap"), plan.hypervisor.at("controller"),
                        plan.topology.definitions.site.services.at("reset"));
    ValidateRetainedUnits(reset, plan.hypervisor.at("bootstrap"));
    if (plan.hypervisor.at("bootstrap").contains("hypervisor_hosts"))
        reset["hosts_block"] = plan.hypervisor.at("bootstrap").at("hypervisor_hosts");
    const CleanupBoundary boundary(reset.at("boundary").at("paths").get<std::vector<std::filesystem::path>>(),
                                    reset.at("boundary").at("trees").get<std::vector<std::filesystem::path>>());
    for (const auto& target : targets) boundary.ValidateTarget(target);
    plan.hypervisor["reset"] = std::move(reset);
}

}  // namespace skeleton
