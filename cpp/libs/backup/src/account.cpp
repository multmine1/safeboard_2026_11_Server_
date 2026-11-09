#include "host_units.hpp"

#include <charconv>
#include <regex>

namespace skeleton::backup {
namespace {

unsigned ParseMode(const std::string& mode) {
    unsigned value{};
    const auto result = std::from_chars(mode.data(), mode.data() + mode.size(), value, 8);
    if (mode.size() != 4 || result.ec != std::errc{} || result.ptr != mode.data() + mode.size() || value > 07777)
        throw ConfigurationError("backup repository permissions require four octal digits");
    return value;
}

void ValidateAccount(const BackupAccountDefinition& account, const BackupHostContext& context) {
    if (!std::regex_match(account.name, std::regex("[a-z_][a-z0-9_-]{0,31}")) || account.name == "root" ||
        account.name == context.transport_user || account.name == context.lock_user)
        throw ConfigurationError("backup service identity must be separate from root and controller accounts");
    const auto& path = account.shell;
    if (!path.is_absolute() || path == path.root_path() || path != path.lexically_normal() ||
        path.string().find_first_of("\r\n") != std::string::npos)
        throw ConfigurationError("backup service shell must be a normalized absolute location");
    if (account.shell.filename() != "nologin" && account.shell.filename() != "false")
        throw ConfigurationError("backup service account requires a noninteractive shell");
    const auto files = ParseMode(account.file_mode);
    const auto directories = ParseMode(account.directory_mode);
    if ((files & 0440) != 0440 || (files & (07000 | 0111 | 0222 | 0007)) != 0 ||
        (directories & 02770) != 02770 || (directories & 05007) != 0)
        throw ConfigurationError("backup repository requires private group-readable files and setgid group-writable directories");
}

}  // namespace

void PrepareRepositoryAccess(BackupHostPlan& host, const BackupPlan& plan, const BackupHostContext& context) {
    if (!host.settings.account) throw ConfigurationError("native backup requires its dedicated service account");
    const auto& account = *host.settings.account;
    ValidateAccount(account, context);
    host.repository_user = account.name;
    host.repository_access = {{"owner", account.name}, {"group", account.name}, {"transport_user", context.transport_user},
        {"users", std::set<std::string>{account.name, context.transport_user}},
        {"account", {{"name", account.name}, {"home", host.settings.cache_directory.string()}, {"shell", account.shell.string()}}},
        {"file_mode", account.file_mode}, {"directory_mode", account.directory_mode},
        {"permission_argv", Json::array({
            std::vector<std::string>{"find", plan.repository.string(), "-type", "f", "-exec", "chmod", "--changes", account.file_mode, "{}", "+"},
            std::vector<std::string>{"find", plan.repository.string(), "-type", "d", "-exec", "chmod", "--changes", account.directory_mode, "{}", "+"}})}};
}

}  // namespace skeleton::backup
