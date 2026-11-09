#include "browser_proxy.hpp"

#include <cctype>

namespace skeleton {

Json BuildBrowserProxyPlan(const SiteDefinition& site, const Json& access) {
    const auto& settings = site.services.at("local_access").at("browser_proxy");
    const auto port = settings.at("port").get<unsigned>();
    const auto label = settings.at("label").get<std::string>();
    if (!access.contains("socks_address"))
        throw ConfigurationError("browser proxy requires the SSH SOCKS endpoint");
    if (port == 0 || port > 65535 || port == site.services.at("local_access").at("socks_port").get<unsigned>())
        throw ConfigurationError("PAC HTTP port must be within 1..65535 and separate from SOCKS");
    if (label.empty() || label.front() == '.' || !std::all_of(label.begin(), label.end(), [](unsigned char character) {
            return std::isalnum(character) || character == '.' || character == '-' || character == '_';
        })) throw ConfigurationError("browser proxy launch agent label must be a safe filename");
    for (const auto& entry : access.at("forwards"))
        if (entry.contains("local_port") && entry.at("local_port").get<unsigned>() == port)
            throw ConfigurationError("PAC HTTP port conflicts with a local service forward");

    const auto endpoint = access.at("socks_address").get<std::string>();
    // Safari's CFNetwork accepts SOCKS as version 5; Chromium/Firefox use SOCKS5.
    const auto proxy = Json("SOCKS5 " + endpoint + "; SOCKS " + endpoint).dump();
    const std::string pac = "function FindProxyForURL(url, host) {\n"
        "    host = host.toLowerCase().replace(/\\.$/, \"\");\n"
        "    var domain = " + Json(site.domain).dump() + ";\n"
        "    if (host === domain || host.slice(-(domain.length + 1)) === \".\" + domain)\n"
        "        return " + proxy + ";\n"
        "    return \"DIRECT\";\n"
        "}\n";
    return {{"label", label}, {"port", port}, {"pac", pac},
        {"url", "http://" + access.at("bind_address").get<std::string>() + ":" + std::to_string(port) + "/proxy.pac"}};
}

}  // namespace skeleton
