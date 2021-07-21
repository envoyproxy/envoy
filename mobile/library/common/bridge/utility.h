#pragma once

#include <string>

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Bridge {

envoy_map makeEnvoyMap(std::vector<std::pair<std::string, std::string>> pairs);

} // namespace Bridge
} // namespace Envoy
