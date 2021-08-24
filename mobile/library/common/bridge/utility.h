#pragma once

#include <string>

#include "source/common/http/codes.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Bridge {
namespace Utility {

envoy_error_code_t errorCodeFromLocalStatus(Http::Code status);

envoy_map makeEnvoyMap(std::vector<std::pair<std::string, std::string>> pairs);

} // namespace Utility
} // namespace Bridge
} // namespace Envoy
