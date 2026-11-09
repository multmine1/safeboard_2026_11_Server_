#pragma once

#include "skeleton/backup/plan.hpp"

namespace skeleton::test {

inline BackupDefinition HostDefinition() {
    BackupDefinition definition{"hd", "backups/project", "environment/.tools/env/bin",
        "environment/.tools/backup/cache", "environment/.tools/backup/restore", 64,
        {{"daily", 7}}, {{"database", BackupSetKind::Postgresql, {}, {}, "postgres", 16},
                       {"project", BackupSetKind::Files, {"."}, {}}}};
    definition.capture = BackupCaptureDefinition{"/var/tmp", "environment/.tools/backup/capture", 32};
    definition.host = BackupHostDefinition{"project-backup", "/usr/local/bin/restic", "/etc/systemd/system",
        "/var/cache/project/capture", "/var/cache/project/restic", "/etc/project/credentials",
        "/etc/project/known_hosts", "/etc/ssh/ssh_host_ed25519_key.pub", "/etc/tmpfiles.d/project-backup.conf",
        "/var/cache/project/installation",
        "host", {{"TimeoutStartSec", 3600}, {"UMask", "0077"}},
        {{"BatchMode", "yes"}, {"StrictHostKeyChecking", "yes"}, {"IdentitiesOnly", "yes"}, {"IdentityAgent", "none"}},
        {{"Timer", {{"OnCalendar", "daily"}}}, {"Install", {{"WantedBy", "timers.target"}}}}};
    definition.host->account = BackupAccountDefinition{"svc-backup", "/usr/sbin/nologin", "0440", "2770"};
    return definition;
}

inline BackupContext HostContext() {
    BackupContext context{"/work/project", {}, {{"hd", {"/data/hd", "UUID=archive"}}}, {"/data/hd/volumes"}};
    context.repository_ssh_alias = "hypervisor";
    context.domain_names = {{"controller", "vm-controller"}};
    context.remote_sources["database"] = {BackupSetKind::Postgresql, "controller", {"k0s", "dump", "$x%y"},
        {"controller"}, {"/var/lib/k0s"}};
    context.host = BackupHostContext{"vmctl", "operator", "/run/lock/project-maintenance.lock",
        {{"controller", {"10.1.2.3", "admin", 2222}}}};
    context.host->installation = {{"url", "https://example.org/restic.bz2"}, {"checksum", "sha256:" + std::string(64, 'a')},
        {"archive_name", "restic.bz2"}};
    return context;
}

}  // namespace skeleton::test
