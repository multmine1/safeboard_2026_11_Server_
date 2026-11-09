#include "host_units.hpp"

namespace skeleton::backup {
namespace {

bool IsNamedAbsolutePath(const std::filesystem::path& path) {
    return path.is_absolute() && path != path.root_path() && !path.filename().empty() &&
        path == path.lexically_normal() && path.string().find_first_of("\r\n") == std::string::npos;
}

Json ValidateWatchdog(const BackupDefinition& definition) {
    if (!definition.filesystem_watchdog) throw ConfigurationError("filesystem quiesce requires an explicit watchdog policy");
    const auto& timer = *definition.filesystem_watchdog;
    if (!timer.is_object() || timer.size() != 2 || !timer.contains("OnActiveSec") || !timer.contains("AccuracySec"))
        throw ConfigurationError("filesystem watchdog requires only OnActiveSec and AccuracySec");
    for (const auto& value : timer)
        if (!value.is_number_integer() || value.get<std::int64_t>() <= 0)
            throw ConfigurationError("filesystem watchdog intervals must be positive seconds");
    if (timer.at("AccuracySec").get<std::uint64_t>() > timer.at("OnActiveSec").get<std::uint64_t>())
        throw ConfigurationError("filesystem watchdog accuracy exceeds its hold interval");
    return timer;
}

}  // namespace

void PrepareFilesystemQuiesce(BackupPlan& plan, BackupQuiesceSource& source, const std::string& name,
                             const BackupDefinition& definition) {
    const auto& filesystem = *source.filesystem;
    if (!IsNamedAbsolutePath(filesystem.mount) || !IsNamedAbsolutePath(filesystem.unit_directory))
        throw ConfigurationError("filesystem quiesce requires a dedicated absolute mount and unit directory");
    const auto prefix = definition.host->name + "-" + name;
    const auto freeze = prefix + "-freeze.service";
    const auto release = prefix + "-release.service";
    const auto watchdog = prefix + "-watchdog.timer";
    auto timer = ValidateWatchdog(definition);
    timer["Unit"] = release;
    Json units{{freeze, {{"Service", {{"Type", "oneshot"}, {"RemainAfterExit", true},
        {"ExecStart", UnitCommand({"fsfreeze", "--freeze", filesystem.mount.string()})},
        {"ExecStartPost", UnitCommand({"systemctl", "restart", watchdog})},
        {"ExecStop", UnitCommand({"fsfreeze", "--unfreeze", filesystem.mount.string()})},
        {"ExecStopPost", UnitCommand({"systemctl", "stop", watchdog})}}}}},
        {release, {{"Service", {{"Type", "oneshot"}, {"ExecStart", UnitCommand({"systemctl", "stop", freeze})}}}}},
        {watchdog, {{"Timer", timer}}}};
    auto& guest = plan.guest_quiesce_units[source.vm];
    if (guest.is_null()) guest = {{"unit_directory", filesystem.unit_directory.string()}, {"units", Json::object()}};
    if (guest.at("unit_directory") != filesystem.unit_directory.string())
        throw ConfigurationError("guest filesystem units have conflicting installation directories");
    guest["units"].update(units);
    source.status = {"systemctl", "show", "--property=ActiveState", "--value", freeze};
    source.enter = {"systemctl", "start", freeze};
    source.leave = {"systemctl", "stop", freeze};
}

}  // namespace skeleton::backup
