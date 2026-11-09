#pragma once

#include "skeleton/contracts/types.hpp"

namespace skeleton {

struct AccessRule {
    std::string from;
    std::string to;
    std::string protocol;
    std::vector<std::uint16_t> ports;
};

struct GatewayService {
    std::string id;
    std::string name;
    std::string vm;
    std::string protocol;
    std::uint16_t port;
    std::uint16_t target_port;
    std::string scope;
    std::vector<std::string> sources;
    std::vector<std::string> aliases;

    std::vector<std::string> Names() const {
        auto names = aliases;
        names.insert(names.begin(), name);
        return names;
    }
};

struct NetworkDefinition {
    std::string id;
    std::string name;
    std::string router;
    std::vector<std::string> members;
    std::vector<AccessRule> allow;
    std::filesystem::path source;
    std::vector<GatewayService> exports;
    std::vector<std::string> dns_upstreams;
};

}  // namespace skeleton
