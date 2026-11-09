#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace skeleton {

using GeneratedFiles = std::map<std::filesystem::path, std::string>;

}  // namespace skeleton
