#pragma once

#include "hypervisor_fixture.hpp"

namespace skeleton::test {

inline void EnableHostMonitoring(SourceDefinitions& definitions) {
    auto& host = definitions.site.hypervisor_settings;
    host["controller_project_directory"] = "/home/operator/project";
    host["hypervisor_hosts"] = {{"path", "/etc/hosts"}, {"mode", "0644"}};
    host["hypervisor_services"]["host-metrics"] = {{"port", 19273}, {"scheme", "http"}, {"path", "/metrics"}};
    host["hypervisor_monitoring"] = {
        {"service", "host-metrics"}, {"artifact", "telegraf"},
        {"cache_directory", "environment/.tools/artifacts/telegraf"},
        {"binary", "/usr/local/bin/telegraf"}, {"config_path", "/etc/skeleton/host-metrics.toml"},
        {"unit", "skeleton-host-metrics.service"}, {"unit_path", "/etc/systemd/system/skeleton-host-metrics.service"},
        {"read_only_socket", "/run/libvirt/libvirt-sock-ro"},
        {"configuration", {{"agent", {{"interval", "30s"}}},
            {"inputs", {{"libvirt", Json::array({Json::object()})}}},
            {"outputs", {{"prometheus_client", Json::array({Json::object()})}}}}},
        {"systemd", {{"Service", {{"DynamicUser", "yes"}}}}}};
    definitions.site.asset_data["host_monitoring"]["telegraf"] = {
        {"url", "https://example.test/telegraf.tar.gz"}, {"checksum", "sha256:" + std::string(64, 'a')},
        {"archive_name", "telegraf.tar.gz"}, {"archive_binary", "release/usr/bin/telegraf"}};
}

}  // namespace skeleton::test
