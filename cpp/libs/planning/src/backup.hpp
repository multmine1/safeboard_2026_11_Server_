#pragma once

#include "skeleton/planning/infrastructure.hpp"

namespace skeleton {

void PrepareBackupPlan(InfrastructurePlan& plan);
std::map<std::string, BackupRemoteSource> BuildBackupProducers(const InfrastructurePlan& plan);
std::map<std::string, BackupNfsSource> BuildBackupNfsSources(const InfrastructurePlan& plan);
std::map<std::string, BackupQuiesceSource> BuildBackupQuiesceSources(const InfrastructurePlan& plan);
void ValidateBackupDatabaseCopies(const InfrastructurePlan& plan, const ApplicationPlan& application);
BackupQuiesceSource BuildFilesystemBackupSource(const InfrastructurePlan& plan, const ApplicationPlan& application);

}  // namespace skeleton
