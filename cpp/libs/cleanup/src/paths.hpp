#pragma once

#include "skeleton/cleanup/boundary.hpp"

namespace skeleton::cleanup {

bool IsWithin(const std::filesystem::path& child, const std::filesystem::path& parent);
std::filesystem::path NormalizePath(const std::filesystem::path& path);

}  // namespace skeleton::cleanup
