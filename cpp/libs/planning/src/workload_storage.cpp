#include "workloads.hpp"

#include <sstream>

namespace skeleton {
namespace {

std::string ResolveStorageDirectory(const InfrastructurePlan& plan, const std::string& vm, const Json& storage) {
    const auto directory = std::filesystem::path(storage.at("directory").get<std::string>());
    if (directory.empty() || directory.is_absolute() ||
        std::find(directory.begin(), directory.end(), "..") != directory.end())
        throw ConfigurationError("application storage directory must stay below its declared mount");
    const auto disk = storage.at("mount").get<std::string>();
    for (const auto& mount : plan.vms.at(vm).variables.at("mounts")) {
        const auto root = std::filesystem::path(mount.at("path").get<std::string>());
        if (root.filename() == disk) return (root / directory).lexically_normal().string();
    }
    throw ConfigurationError("application storage requires a mounted data disk: " + vm + "/" + disk);
}

Json ResolveLocalPaths(const InfrastructurePlan& plan, const Json& definitions) {
    std::map<std::string, std::set<std::string>> paths;
    for (const auto& definition : definitions) {
        const auto vm = definition.at("vm").get<std::string>();
        if (!plan.kubernetes.nodes.contains(vm) || plan.topology.IsRouter(vm))
            throw ConfigurationError("local volumes require an ordinary Kubernetes node: " + vm);
        paths[vm].insert(ResolveStorageDirectory(plan, vm, definition));
    }
    Json nodes = Json::array();
    for (const auto& [vm, directories] : paths)
        nodes.push_back({{"node", vm}, {"paths", directories}});
    return nodes;
}

Json ResolveNfsClients(const InfrastructurePlan& plan, const std::string& server, const Json& definition) {
    std::set<std::string> addresses;
    const auto service = definition.at("service").get<std::string>();
    for (const auto& client : definition.at("clients")) {
        const auto client_id = client.get<std::string>();
        bool permitted = false;
        for (const auto& connection : plan.policy.connections) {
            if (connection.from != client_id || connection.to != server || connection.protocol != "tcp" ||
                connection.service_name != service) continue;
            addresses.insert(connection.nat_network.empty() ? connection.source_address : connection.nat_address);
            permitted = true;
        }
        if (!permitted) throw ConfigurationError("NFS client has no permitted service connection: " + client_id);
    }
    if (addresses.empty() && !definition.contains("hypervisor_access"))
        throw ConfigurationError("an NFS export requires explicitly permitted VM or hypervisor clients");
    return addresses;
}

void BindHypervisorNfsClient(NfsExportPlan& exported, const InfrastructurePlan& plan, const Json& definition) {
    if (!definition.contains("hypervisor_access")) return;
    const auto& access = definition.at("hypervisor_access");
    const auto name = access.at("service").get<std::string>();
    const auto& service = plan.topology.ServiceNamed(name);
    const auto& backend = plan.topology.ServiceNamed(exported.service);
    const auto& address = plan.topology.definitions.site.hypervisor_address;
    const auto granted = std::any_of(service.sources.begin(), service.sources.end(), [&](const auto& source) {
        return ParseCidr(source).ToString() == address + "/32";
    });
    const auto options = access.at("options").get<std::string>();
    std::set<std::string> tokens;
    std::istringstream stream(options);
    for (std::string token; std::getline(stream, token, ',');) tokens.insert(token);
    if (service.scope != "uplink" || !granted || service.vm != exported.server ||
        service.protocol != "tcp" || service.target_port != backend.target_port)
        throw ConfigurationError("hypervisor NFS access requires its exact published host grant and the same backend");
    if (!tokens.contains("ro") || tokens.contains("rw") || tokens.contains("no_root_squash"))
        throw ConfigurationError("hypervisor NFS access must be read-only without disabling root squash");
    for (const auto& client : exported.host_export.at("clients"))
        if (client.at("address") == address) throw ConfigurationError("hypervisor NFS permissions are ambiguous");
    exported.hypervisor_service = name;
    exported.host_export["clients"].push_back({{"address", address}, {"options", options}});
}

NfsExportPlan BuildNfsExport(const InfrastructurePlan& plan, const Json& application, const Json& definition) {
    NfsExportPlan result;
    result.application = application.at("name");
    result.server = application.at("vm");
    result.service = definition.at("service");
    result.name = definition.at("name");
    if (!plan.vms.contains(result.server)) throw ConfigurationError("unknown NFS server VM: " + result.server);
    if (result.name.empty()) throw ConfigurationError("an NFS export requires a name");
    result.path = ResolveStorageDirectory(plan, result.server, definition);
    result.clients = definition.at("clients").get<std::set<std::string>>();
    result.host_export = {{"path", result.path}, {"clients", Json::array()}, {"owner", definition.at("owner")},
        {"group", definition.at("group")}, {"mode", definition.at("mode")}};
    for (const auto& address : ResolveNfsClients(plan, result.server, definition))
        result.host_export["clients"].push_back({{"address", address}, {"options", definition.at("options")}});
    BindHypervisorNfsClient(result, plan, definition);
    return result;
}

}  // namespace

Json BuildApplicationStorage(const InfrastructurePlan& plan, const Json& definition) {
    const auto storage = definition.value("storage", Json::object());
    Json result{{"local_paths", ResolveLocalPaths(plan, storage.value("local_paths", Json::array()))},
        {"nfs_exports", Json::array()}, {"nfs", Json::object()}};
    const auto vm = definition.at("vm").get<std::string>();
    const auto application = definition.at("name").get<std::string>();
    result["volumes"] = Json::object();
    const auto volumes = storage.value("volumes", Json::object());
    for (const auto& [name, volume] : volumes.items())
        result["volumes"][name] = ResolveStorageDirectory(plan, vm, volume);
    for (const auto& [key, exported] : plan.nfs_exports) {
        if (exported.application == application) result["nfs_exports"].push_back(exported.host_export);
        if (!exported.clients.contains(vm)) continue;
        const auto& endpoint = plan.vms.at(vm).variables.at("connections").at(exported.service);
        result["nfs"][exported.service][exported.name] = {
            {"volume", {{"server", endpoint.at("fqdn")}, {"path", exported.path}}},
            {"port_option", "port=" + std::to_string(endpoint.at("port").get<unsigned>())}};
    }
    return result;
}

std::map<std::pair<std::string, std::string>, NfsExportPlan> BuildNfsExportPlans(const InfrastructurePlan& plan) {
    const auto& workloads = plan.topology.definitions.site.workloads;
    if (!workloads) return {};
    std::map<std::pair<std::string, std::string>, NfsExportPlan> exports;
    for (const auto& workload : *workloads) {
        const auto& application = workload.specification;
        try {
            const auto storage = application.value("storage", Json::object());
            for (const auto& definition : storage.value("nfs_exports", Json::array())) {
                auto exported = BuildNfsExport(plan, application, definition);
                const auto key = std::make_pair(exported.service, exported.name);
                if (!exports.emplace(key, std::move(exported)).second)
                    throw ConfigurationError("duplicate NFS export: " + key.first + "/" + key.second);
            }
        } catch (const Json::exception& error) {
            throw ConfigurationError("workload NFS storage: " + std::string(error.what()));
        }
    }
    return exports;
}

}  // namespace skeleton
