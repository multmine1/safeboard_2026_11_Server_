#include "paths.hpp"

namespace skeleton::backup {

BackupSetPlan BuildSet(const BackupSetDefinition& definition, const BackupDefinition& policy,
                       const BackupContext& context) {
    BackupSetPlan plan{ProjectPath(context, "."),
        {(ProjectPath(context, policy.bin_directory) / "restic").string(), "backup", "--tag", definition.name}};
    if (definition.kind == BackupSetKind::Nfs) {
        plan.nfs = BuildNfsSet(definition, policy, context);
        plan.argv.clear();
        return plan;
    }
    if (definition.export_name) throw ConfigurationError("only an NFS backup can reference an export");
    if (definition.kind == BackupSetKind::K0s || definition.kind == BackupSetKind::Postgresql) {
        PrepareCapture(plan, definition, policy, context);
        return plan;
    }
    if (definition.workload || definition.max_size_mib)
        throw ConfigurationError("local backup sets cannot declare remote workload or size fields");
    if (definition.kind == BackupSetKind::Files) {
        if (definition.paths.empty()) throw ConfigurationError("backup file sets require at least one source path");
        for (const auto& pattern : definition.excludes) {
            if (pattern.empty()) throw ConfigurationError("backup exclude patterns cannot be empty");
            plan.argv.insert(plan.argv.end(), {"--exclude", ProjectPath(context, pattern).string()});
        }
        plan.argv.push_back("--");
        for (const auto& path : definition.paths) plan.argv.push_back(ProjectPath(context, path).string());
        return plan;
    }
    if (definition.kind != BackupSetKind::TofuState || !context.tofu_directory || !context.tofu_state_file ||
        !definition.paths.empty() || !definition.excludes.empty())
        throw ConfigurationError("native OpenTofu backup requires its prepared directory and no file paths or excludes");
    plan.working_directory = NormalizePath(*context.tofu_directory);
    if (plan.working_directory == NormalizePath(context.project_directory) ||
        !ContainsPath(NormalizePath(context.project_directory), plan.working_directory))
        throw ConfigurationError("native OpenTofu backup directory must be below the declared project");
    const auto state_file = NormalizePath(*context.tofu_state_file);
    if (!state_file.is_absolute() || state_file == plan.working_directory || !ContainsPath(plan.working_directory, state_file))
        throw ConfigurationError("native OpenTofu backup requires its prepared local state path");
    plan.required_files.push_back(state_file);
    plan.argv.insert(plan.argv.end(), {"--stdin-from-command", "--stdin-filename", definition.name + ".json",
        "--", (ProjectPath(context, policy.bin_directory) / "tofu").string(), "state", "pull"});
    return plan;
}

}  // namespace skeleton::backup
