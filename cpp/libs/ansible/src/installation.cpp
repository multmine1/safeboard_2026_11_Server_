#include "installation.hpp"

namespace skeleton {
namespace {

Json SerializePartition(const InstallationPartition& partition) {
    Json result{{"number", partition.number},
        {"part_start", std::to_string(partition.start_mib) + "MiB"},
        {"part_end", std::to_string(partition.end_mib) + "MiB"},
        {"fstype", partition.filesystem}, {"mountpoint", partition.mountpoint},
        {"flags", partition.esp ? Json::array({"esp"}) : Json::array()}};
    if (partition.filesystem_uuid) result["uuid"] = *partition.filesystem_uuid;
    return result;
}

std::string SerializeDebianPartitioning(const HostDiskPlan& plan) {
    const auto efi = std::to_string(plan.debian_recipe.efi_megabytes);
    const auto root = std::to_string(plan.debian_recipe.root_minimum_megabytes);
    return "d-i partman-auto/method string regular\n"
        "d-i partman-partitioning/choose_label select " + plan.partition_table + "\n"
        "d-i partman-partitioning/default_label string " + plan.partition_table + "\n"
        "d-i partman-auto/expert_recipe string installation :: " + efi + " " + efi + " " + efi +
        " free $iflabel{ gpt } method{ efi } format{ } . " + root + " " + root +
        " -1 ext4 method{ format } format{ } use_filesystem{ } filesystem{ ext4 } mountpoint{ / } .\n"
        "d-i partman-auto/choose_recipe select installation\n"
        "d-i partman-basicfilesystems/no_swap boolean false\n"
        "d-i partman-partitioning/confirm_write_new_label boolean true\n"
        "d-i partman/choose_partition select finish\n"
        "d-i partman/confirm boolean true\n"
        "d-i partman/confirm_nooverwrite boolean true\n";
}

std::string SerializePreseed(const Json& values) {
    std::string content;
    for (const auto& [owner, fields] : values.items())
        for (const auto& [key, entry] : fields.items()) {
            const auto type = entry.at("type").get<std::string>();
            const auto value = entry.at("value").get<std::string>();
            content += owner + " " + key + " " + type + " " + value + "\n";
        }
    return content;
}

}  // namespace

void AddHostInstallationConfiguration(GeneratedFiles& files, const HostDiskPlan& plan,
    const std::optional<InstallerBootstrapPlan>& bootstrap) {
    Json disks = Json::array();
    for (const auto& disk : plan.disks) {
        Json partitions = Json::array();
        for (const auto& partition : disk.partitions) partitions.push_back(SerializePartition(partition));
        disks.push_back({{"storage_class", disk.storage_class}, {"device", disk.device},
            {"serial", disk.serial}, {"size_bytes", disk.size_bytes}, {"partitions", std::move(partitions)}});
    }
    Json installation{{"partition_table", plan.partition_table}, {"disks", std::move(disks)}};
    if (bootstrap) installation["previous_root"] = bootstrap->previous_root;
    files["ansible/hypervisor-installation.json"] = Json{{"installation", std::move(installation)}}.dump(2) + "\n";
    files["ansible/installer/partitioning.preseed"] = SerializeDebianPartitioning(plan);
}

void AddInstallerBootstrapConfiguration(GeneratedFiles& files, const InstallerBootstrapPlan& plan, const HostDiskPlan& disks) {
    files["ansible/installer/bootstrap.json"] = Json{{"installer_bootstrap", {
        {"payload_directory", plan.payload_directory},
        {"firmware_artifacts", plan.firmware_artifacts},
        {"early_command", plan.early_command}, {"late_command", plan.late_command}}}}.dump(2) + "\n";
    const auto directory = std::filesystem::path("ansible/installer/overlay") /
        std::filesystem::path(plan.payload_directory).relative_path();
    for (const auto& [path, content] : plan.overlay) files[directory / path] = content;
    files["ansible/installer/overlay/preseed.cfg"] = SerializePreseed(plan.preseed) + SerializeDebianPartitioning(disks);
}

}  // namespace skeleton
