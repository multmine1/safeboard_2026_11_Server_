#include "skeleton/backup/plan.hpp"
#include "paths.hpp"
#include "host_units.hpp"

#include <regex>

namespace skeleton {
namespace {

std::vector<std::string> BuildRetention(const BackupDefinition& definition, const std::string& restic) {
    if (definition.retention.empty()) throw ConfigurationError("backup retention requires a positive keep policy");
    std::vector<std::string> argv{restic, "forget"};
    for (const auto& [period, count] : definition.retention) {
        if ((period != "daily" && period != "weekly" && period != "monthly") || count == 0)
            throw ConfigurationError("backup retention supports positive daily, weekly and monthly counts");
        argv.insert(argv.end(), {"--keep-" + period, std::to_string(count)});
    }
    return argv;
}

void PrepareSets(BackupPlan& plan, const BackupDefinition& definition, const BackupContext& context) {
    if (definition.sets.empty()) throw ConfigurationError("backup requires at least one named set");
    const std::regex name_format("[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?");
    for (const auto& set : definition.sets) {
        if (!std::regex_match(set.name, name_format) || plan.sets.contains(set.name))
            throw ConfigurationError("backup set names must be distinct DNS labels");
        auto prepared = backup::BuildSet(set, definition, context);
        if (prepared.capture)
            for (const auto& id : prepared.capture->required_vms) {
                if (!context.domain_names.contains(id))
                    throw ConfigurationError("backup capture references an unknown native VM identity");
                plan.required_domains.insert(context.domain_names.at(id));
            }
        if (prepared.nfs)
            for (const auto& id : prepared.nfs->required_vms) {
                if (!context.domain_names.contains(id)) throw ConfigurationError("NFS backup references an unknown native VM");
                plan.required_domains.insert(context.domain_names.at(id));
            }
        plan.sets.emplace(set.name, std::move(prepared));
    }
}

}  // namespace

BackupPlan BuildBackupPlan(const BackupDefinition& definition, const BackupContext& context) {
    BackupPlan plan;
    backup::PreparePaths(plan, definition, context);
    if (definition.password_length < 32 || definition.password_length > 512)
        throw ConfigurationError("backup repository password length must be between 32 and 512 characters");
    plan.password_length = definition.password_length;
    const auto restic = (backup::ProjectPath(context, definition.bin_directory) / "restic").string();
    plan.init_argv = {restic, "init"};
    plan.snapshots_argv = {restic, "snapshots"};
    plan.check_argv = {restic, "check", "--read-data"};
    plan.retention_argv = BuildRetention(definition, restic);
    plan.retention_plan_argv = plan.retention_argv;
    plan.retention_plan_argv.push_back("--dry-run");
    plan.retention_argv.push_back("--prune");
    plan.restore_argv = {restic, "restore", "--target", plan.restore_directory.string(), "--verify", "--"};
    PrepareSets(plan, definition, context);
    backup::PrepareQuiesce(plan, definition, context);
    backup::PrepareHostBackup(plan, definition, context);
    return plan;
}

}  // namespace skeleton
