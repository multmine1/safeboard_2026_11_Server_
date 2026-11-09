#include "native.hpp"

namespace skeleton::k0s {

Json SerializeNodeInstallation(const KubernetesPlan& plan, const KubernetesNodePlan& node) {
    const auto& settings = plan.settings;
    const auto& paths = settings.at("paths");
    const std::string role = node.role == "controller" ? "controller" : "worker";
    Json argv = Json::array({paths.at("binary"), "install", role});
    if (paths.contains("data_directory")) {
        argv.push_back("--data-dir");
        argv.push_back(paths.at("data_directory"));
    }
    if (role == "controller") {
        argv.push_back("--config");
        argv.push_back(paths.at("config"));
    } else {
        argv.push_back("--token-file");
        argv.push_back(paths.at("join_token"));
    }
    for (const auto& argument : settings.at(role + "_args")) argv.push_back(argument);
    for (const auto& argument : Json::array({"--profile", plan.namespaces.at(node.network),
            "--labels", FormatNodeLabels(node.labels), "--kubelet-extra-args",
            "--node-ip=" + node.address + " --hostname-override=" + node.id +
            " --cluster-dns=" + plan.dns_servers.at(node.network)})) argv.push_back(argument);
    if (!node.taints.empty()) {
        argv.push_back("--taints");
        argv.push_back(FormatNodeTaints(node.taints));
    }
    Json preload = Json::array();
    if (node.role == "router")
        for (const auto& image : {"init_image", "dns_image"})
            preload.push_back(Json::array({paths.at("binary"), "ctr", "--namespace", "k8s.io", "images", "pull",
                settings.at("router_runtime").at(image)}));
    std::set<std::string> directories;
    for (const auto& path : {"binary", "config", "join_token", "install_record"})
        directories.insert(std::filesystem::path(paths.at(path).get<std::string>()).parent_path().string());
    return {{"id", node.id}, {"role", role}, {"initially_running", node.initially_running}, {"address", node.address},
        {"dns_address", plan.dns_servers.at(node.network)},
        {"directories", directories},
        {"labels", node.labels}, {"taints", node.taints}, {"pod_cidr", node.pods.ToString()},
        {"unit_path", (std::filesystem::path(paths.at("unit_directory").get<std::string>()) /
            (settings.at("services").at(role).get<std::string>() + ".service")).string()},
        {"record_path", paths.at("install_record")},
        {"install_argv", argv}, {"preload_argv", preload}, {"service", settings.at("services").at(role)}};
}

}  // namespace skeleton::k0s
