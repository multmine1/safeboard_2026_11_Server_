#pragma once

#include "hypervisor_fixture.hpp"

namespace skeleton::test {

inline void EnableHostProxy(SourceDefinitions& definitions) {
    auto& host = definitions.site.hypervisor_settings;
    if (host.empty()) host = HypervisorJson();
    host["hypervisor_services"] = {{"admin", {{"port", 443}, {"scheme", "https"}, {"path", "/"},
        {"bind_address", "127.0.0.1"}, {"dns_zone", "site"}}}};
    host["hypervisor_hosts"] = {{"path", "/etc/hosts"}, {"mode", "0644"}};
    host["hypervisor_proxy"] = {{"binary", "/usr/lib/systemd/systemd-socket-proxyd"},
        {"unit_directory", "/etc/systemd/system"}, {"unit_prefix", "stand-proxy-"},
        {"socket", {{"Install", {{"WantedBy", "sockets.target"}}}}},
        {"service", {{"Service", {{"DynamicUser", "yes"}}}}}};
    host["hypervisor_proxies"] = {{"admin", {{"service", "30000000-0000-4000-8000-000000000009"}}}};
    definitions.networks.at(kChild).exports.push_back({
        "30000000-0000-4000-8000-000000000009", "admin-backend", kApi, "tcp", 18445, 8443,
        "uplink", {"hypervisor"}});
}

}  // namespace skeleton::test
