#pragma once

#include <string>
#include <vector>

#include "headers.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

envoy_headers rawHeaderMapAsEnvoyHeaders(const RawHeaderMap& headers);
RawHeaderMap envoyHeadersAsRawHeaderMap(envoy_headers raw_headers);

} // namespace Platform
} // namespace Envoy
