#include "workloads.hpp"

namespace skeleton {

Json BuildApplicationNavigation(const InfrastructurePlan& plan, const Json& definition) {
    auto links = definition.value("navigation", Json::array());
    if (!links.is_array()) throw ConfigurationError("navigation must be an array of declared private service references");
    for (auto& link : links) {
        const auto access = link.at("access").get<std::string>();
        if (link.contains("url") || plan.local_access.empty() || !plan.local_access.at("forwards").contains(access) ||
            link.at("label").get<std::string>().empty())
            throw ConfigurationError("navigation requires a label and an existing local access entry: " + access);
        auto url = plan.local_access.at("forwards").at(access).at("remote_url").get<std::string>();
        if (!url.starts_with("https://") && !url.starts_with("http://"))
            throw ConfigurationError("navigation requires an HTTP(S) service: " + access);
        if (link.contains("path")) {
            const auto path = link.at("path").get<std::string>();
            if (!path.starts_with('/') || path.starts_with("//") || path.find_first_of("\\\r\n") != std::string::npos)
                throw ConfigurationError("navigation paths must remain on the declared service origin");
            url = url.substr(0, url.find('/', url.find("://") + 3)) + path;
        }
        link["url"] = std::move(url);
    }
    return links;
}

}  // namespace skeleton
