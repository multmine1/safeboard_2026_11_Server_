#include "backup.hpp"

namespace skeleton {
namespace {

const Json& ResolvePostgresCluster(const ApplicationPlan& application) {
    const Json* cluster = nullptr;
    for (const auto& resource : application.resources)
        if (resource.value("apiVersion", "") == "postgresql.cnpg.io/v1" && resource.value("kind", "") == "Cluster") {
            if (cluster) throw ConfigurationError("backup workload must contain exactly one CloudNativePG cluster");
            cluster = &resource;
        }
    if (!cluster) throw ConfigurationError("PostgreSQL backup requires a CloudNativePG cluster workload");
    return *cluster;
}

std::string ResolvePrimaryService(const Json& cluster) {
    if (!cluster.contains(Json::json_pointer("/spec/managed/services/additional")))
        throw ConfigurationError("PostgreSQL backup requires an explicitly named writable service");
    std::string name;
    for (const auto& service : cluster.at("spec").at("managed").at("services").at("additional"))
        if (service.value("selectorType", "") == "rw") {
            if (!name.empty()) throw ConfigurationError("PostgreSQL backup has ambiguous writable services");
            name = service.at("serviceTemplate").at("metadata").at("name").get<std::string>();
        }
    if (name.empty()) throw ConfigurationError("PostgreSQL backup requires an explicitly named writable service");
    return name;
}

BackupRemoteSource BuildProducer(const BackupSetDefinition& definition, const InfrastructurePlan& plan) {
    const auto& kubernetes = plan.kubernetes;
    if (kubernetes.controller.empty() || !kubernetes.settings.at("paths").contains("data_directory"))
        throw ConfigurationError("remote backup requires a Kubernetes controller and its declared data directory");
    const auto& paths = kubernetes.settings.at("paths");
    const auto binary = paths.at("binary").get<std::string>();
    const auto data = std::filesystem::path(paths.at("data_directory").get<std::string>());
    const auto& control_path = plan.topology.host_access_paths.at(kubernetes.controller);
    BackupRemoteSource source{definition.kind, kubernetes.controller, {},
        {control_path.begin(), control_path.end()}, {data, std::filesystem::path(paths.at("config").get<std::string>()).parent_path()}};
    if (definition.kind == BackupSetKind::K0s) {
        source.argv = {binary, "backup", "--data-dir", data.string(), "--save-path", "-"};
        return source;
    }
    if (!definition.workload || !plan.applications.contains(*definition.workload))
        throw ConfigurationError("PostgreSQL backup references an unknown workload");
    const auto& application = plan.applications.at(*definition.workload);
    const auto service = ResolvePrimaryService(ResolvePostgresCluster(application));
    source.argv = {binary, "kubectl", "exec", "--namespace", application.network_namespace,
        "--container", "postgres", "service/" + service, "--", "pg_dumpall", "--username=postgres", "--no-password"};
    for (const auto& vm : application.required_vms) {
        const auto& route = plan.topology.host_access_paths.at(vm);
        source.required_vms.insert(route.begin(), route.end());
    }
    return source;
}

}  // namespace

std::map<std::string, BackupRemoteSource> BuildBackupProducers(const InfrastructurePlan& plan) {
    std::map<std::string, BackupRemoteSource> sources;
    for (const auto& definition : plan.topology.definitions.site.backup->sets)
        if (definition.kind == BackupSetKind::K0s || definition.kind == BackupSetKind::Postgresql)
            sources.emplace(definition.name, BuildProducer(definition, plan));
    return sources;
}

}  // namespace skeleton
