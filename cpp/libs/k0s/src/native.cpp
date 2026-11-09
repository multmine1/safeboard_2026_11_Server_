#include "native.hpp"

namespace skeleton::k0s {

std::string FormatNodeLabels(const Json& labels) {
    std::string result;
    for (const auto& [key, value] : labels.items())
        result += (result.empty() ? "" : ",") + key + "=" + value.get<std::string>();
    return result;
}

std::string FormatNodeTaints(const Json& taints) {
    std::string result;
    for (const auto& taint : taints)
        result += (result.empty() ? "" : ",") + taint.at("key").get<std::string>() + "=" +
            taint.at("value").get<std::string>() + ":" + taint.at("effect").get<std::string>();
    return result;
}

}  // namespace skeleton::k0s
