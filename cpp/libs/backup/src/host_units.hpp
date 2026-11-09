#pragma once

#include "skeleton/backup/plan.hpp"

namespace skeleton::backup {

void PrepareRepositoryAccess(BackupHostPlan& host, const BackupPlan& plan, const BackupHostContext& context);

std::string UnitCommand(const std::vector<std::string>& arguments);
std::string ShellCommand(const std::vector<std::string>& arguments);
std::string ShellArgument(const std::string& argument);
std::string Credential(const BackupHostDefinition& settings, const std::string& name);
Json BoundToGate(const BackupHostPlan& host, const std::string& previous);
std::string GuestShellCommand(BackupHostPlan& host, const std::string& vm, const std::vector<std::string>& arguments,
                              const BackupHostContext& context);
void PrepareQuiesce(BackupPlan& plan, const BackupDefinition& definition, const BackupContext& context);
void PrepareFilesystemQuiesce(BackupPlan& plan, BackupQuiesceSource& source, const std::string& name,
                             const BackupDefinition& definition);
std::string QuiesceStatusCommand(BackupHostPlan& host, const BackupQuiesceSource& source,
                                 bool active, const BackupHostContext& context);
std::string BuildQuiesceUnit(BackupHostPlan& host, const BackupPlan& plan, const BackupHostContext& context);
void BuildHostUnits(BackupHostPlan& host, const BackupPlan& plan, const BackupHostContext& context);

}  // namespace skeleton::backup
