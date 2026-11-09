#include "skeleton/installation/preseed.hpp"

namespace skeleton {
namespace {

void ValidatePreseedRecords(const Json& values) {
    if (!values.is_object()) throw ConfigurationError("installer preseed must be an owner/field map");
    for (const auto& [owner, fields] : values.items()) {
        if (!fields.is_object()) throw ConfigurationError("installer preseed owner requires a field map");
        for (const auto& [key, entry] : fields.items()) {
            const auto type = entry.at("type").get<std::string>();
            const auto value = entry.at("value").get<std::string>();
            for (const auto& token : {owner, key, type})
                if (token.empty() || token.find_first_of(" \t\r\n") != std::string::npos)
                    throw ConfigurationError("preseed owner, field and type must be single tokens");
            if (value.find_first_of("\r\n\\") != std::string::npos)
                throw ConfigurationError("preseed values must be single lines without backslash escapes");
            const bool password = type == "password" || (key.starts_with("passwd/") && key.find("password") != std::string::npos);
            if ((password && value != "!") || value.find("PRIVATE KEY-----") != std::string::npos)
                throw ConfigurationError("secrets must remain outside installer preseed");
        }
    }
}

Json BuildPreseed(const Json& settings, const Json& generated) {
    ValidatePreseedRecords(settings);
    ValidatePreseedRecords(generated);
    for (const auto& [owner, fields] : settings.items())
        for (const auto& [key, entry] : fields.items())
            if (key.starts_with("partman") || (generated.contains(owner) && generated.at(owner).contains(key)))
                throw ConfigurationError("source preseed must not override generated installation fields: " + key);
    auto result = settings;
    result.merge_patch(generated);
    return result;
}

}  // namespace

Json BuildInstallerPreseed(const Json& settings, const Json& generated) {
    try {
        return BuildPreseed(settings, generated);
    } catch (const Json::exception& error) {
        throw ConfigurationError(std::string("invalid installer preseed: ") + error.what());
    }
}

}  // namespace skeleton
