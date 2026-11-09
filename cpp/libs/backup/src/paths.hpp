#pragma once

#include "skeleton/backup/plan.hpp"

namespace skeleton::backup {

std::filesystem::path NormalizePath(const std::filesystem::path& path);
bool ContainsPath(const std::filesystem::path& parent, const std::filesystem::path& child);
bool PathsOverlap(const std::filesystem::path& left, const std::filesystem::path& right);
std::filesystem::path ProjectPath(const BackupContext& context, const std::filesystem::path& relative);
void PreparePaths(BackupPlan& plan, const BackupDefinition& definition, const BackupContext& context);
BackupSetPlan BuildSet(const BackupSetDefinition& definition, const BackupDefinition& policy,
                       const BackupContext& context);
void PrepareCapture(BackupSetPlan& plan, const BackupSetDefinition& definition,
                    const BackupDefinition& policy, const BackupContext& context);
void PrepareHostBackup(BackupPlan& plan, const BackupDefinition& definition, const BackupContext& context);
BackupNfsPlan BuildNfsSet(const BackupSetDefinition& definition, const BackupDefinition& policy,
                         const BackupContext& context);

}  // namespace skeleton::backup
