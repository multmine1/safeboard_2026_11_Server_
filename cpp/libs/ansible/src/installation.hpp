#pragma once

#include "skeleton/ansible/export.hpp"

namespace skeleton {

void AddHostInstallationConfiguration(GeneratedFiles& files, const HostDiskPlan& plan,
    const std::optional<InstallerBootstrapPlan>& bootstrap);
void AddInstallerBootstrapConfiguration(GeneratedFiles& files, const InstallerBootstrapPlan& plan, const HostDiskPlan& disks);

}  // namespace skeleton
