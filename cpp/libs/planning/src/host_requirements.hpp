#pragma once

#include "skeleton/topology/settings.hpp"

namespace skeleton {

Json BuildHostRequirements(const SiteDefinition& site);
Json BuildHostImagePlan(const SiteDefinition& site, const Json& requirements);

}  // namespace skeleton
