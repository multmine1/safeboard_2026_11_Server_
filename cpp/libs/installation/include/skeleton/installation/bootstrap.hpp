#pragma once

#include "skeleton/contracts/files.hpp"
#include "skeleton/installation/disks.hpp"

namespace skeleton {

struct InstallerTargetFile {
    std::string path;
    std::string mode;
    std::string content;
};

struct InstallerHostKey {
    std::string path;
    std::string mode;
};

struct InstallerBootstrapContext {
    std::string user;
    std::string user_home;
    std::vector<std::string> authorized_keys;
    std::vector<InstallerHostKey> host_keys;
    std::vector<InstallerTargetFile> files;
    std::string retain_script;
    std::string finish_script;
};

struct InstallerBootstrapPlan {
    std::string payload_directory;
    std::string previous_root;
    std::string early_command;
    std::string late_command;
    GeneratedFiles overlay;
    Json preseed = Json::object();
    Json firmware_artifacts = Json::array();
};

InstallerBootstrapPlan BuildInstallerBootstrapPlan(const HostDiskPlan& disks,
    const Json& settings, const std::string& previous_root, const InstallerBootstrapContext& context);

}  // namespace skeleton
