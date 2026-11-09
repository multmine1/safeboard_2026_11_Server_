#pragma once

#include "skeleton/contracts/types.hpp"
#include <limits>

namespace skeleton::placement {

inline void AddChecked(std::uint64_t& total, std::uint64_t amount) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - total)
        throw ConfigurationError("resource total exceeds the supported integer range");
    total += amount;
}

inline std::uint64_t MultiplyChecked(std::uint64_t value, std::uint64_t count) {
    if (count && value > std::numeric_limits<std::uint64_t>::max() / count)
        throw ConfigurationError("replicated resource total exceeds the supported integer range");
    return value * count;
}

}  // namespace skeleton::placement
