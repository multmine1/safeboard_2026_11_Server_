#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace skeleton {
using Json = nlohmann::json;
class ConfigurationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
}  // namespace skeleton
