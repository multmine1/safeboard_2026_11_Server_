#include "readers.hpp"

#include <limits>

namespace skeleton::input {
namespace {

BackupSetDefinition ReadSet(const Json& value, const std::string& context) {
    BackupSetDefinition result;
    result.name = ReadName(value, "name", context);
    const auto kind = ReadString(value, "kind", context);
    if (kind == "nfs") {
        CheckFields(value, {"name", "kind", "workload", "export"}, context);
        result.kind = BackupSetKind::Nfs;
        result.workload = ReadName(value, "workload", context);
        result.export_name = ReadName(value, "export", context);
        return result;
    }
    if (kind == "k0s" || kind == "postgresql") {
        if (kind == "k0s") CheckFields(value, {"name", "kind", "max_size_mib"}, context);
        else CheckFields(value, {"name", "kind", "workload", "max_size_mib"}, context);
        result.kind = kind == "k0s" ? BackupSetKind::K0s : BackupSetKind::Postgresql;
        result.max_size_mib = ReadPositive(value, "max_size_mib", context, std::numeric_limits<std::uint64_t>::max());
        if (kind == "postgresql") result.workload = ReadName(value, "workload", context);
        return result;
    }
    CheckFields(value, {"name", "kind", "paths", "excludes"}, context);
    if (kind != "files" && kind != "tofu-state") Fail(context + "/kind", "expected files or tofu-state");
    result.kind = kind == "files" ? BackupSetKind::Files : BackupSetKind::TofuState;
    if (value.contains("paths"))
        for (const auto& path : ReadStringArray(value, "paths", context)) result.paths.emplace_back(path);
    if (value.contains("excludes")) result.excludes = ReadStringArray(value, "excludes", context);
    return result;
}

}  // namespace

BackupDefinition ReadBackupDefinition(const Json& value, const std::string& context) {
    CheckFields(value, {"storage", "repository_directory", "bin_directory", "cache_directory",
        "restore_directory", "password_length", "retention", "sets", "capture", "host", "quiesce", "filesystem_watchdog"}, context);
    BackupDefinition result;
    result.storage = ReadName(value, "storage", context);
    result.repository_directory = ReadString(value, "repository_directory", context);
    result.bin_directory = ReadString(value, "bin_directory", context);
    result.cache_directory = ReadString(value, "cache_directory", context);
    result.restore_directory = ReadString(value, "restore_directory", context);
    result.password_length = ReadPositive(value, "password_length", context, 512);
    const auto& retention = Require(value, "retention", context);
    CheckFields(retention, {"daily", "weekly", "monthly"}, context + "/retention");
    for (const auto& [period, count] : retention.items())
        result.retention[period] = ReadPositive(retention, period, context + "/retention", std::numeric_limits<int>::max());
    const auto& sets = Require(value, "sets", context);
    if (!sets.is_array()) Fail(context + "/sets", "expected an array");
    for (std::size_t i = 0; i < sets.size(); ++i)
        result.sets.push_back(ReadSet(sets.at(i), context + "/sets/" + std::to_string(i)));
    if (value.contains("capture")) {
        const auto& capture = value.at("capture");
        const auto location = context + "/capture";
        CheckFields(capture, {"guest_directory", "local_directory", "reserve_mib"}, location);
        result.capture = BackupCaptureDefinition{ReadString(capture, "guest_directory", location),
            ReadString(capture, "local_directory", location),
            ReadPositive(capture, "reserve_mib", location, std::numeric_limits<std::uint64_t>::max())};
    }
    if (value.contains("host")) result.host = ReadBackupHost(value.at("host"), context + "/host");
    if (value.contains("quiesce")) result.quiesce = ReadStringArray(value, "quiesce", context);
    if (value.contains("filesystem_watchdog")) {
        const auto& timer = value.at("filesystem_watchdog");
        const auto location = context + "/filesystem_watchdog";
        CheckFields(timer, {"OnActiveSec", "AccuracySec"}, location);
        result.filesystem_watchdog = {{"OnActiveSec", ReadPositive(timer, "OnActiveSec", location, std::numeric_limits<std::int64_t>::max())},
            {"AccuracySec", ReadPositive(timer, "AccuracySec", location, std::numeric_limits<std::int64_t>::max())}};
    }
    return result;
}

}  // namespace skeleton::input
