#pragma once

#include <string>
#include <vector>

#include "headers.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

envoy_headers raw_header_map_as_envoy_headers(const RawHeaderMap& headers);
RawHeaderMap envoy_headers_as_raw_header_map(envoy_headers raw_headers);

} // namespace Platform
} // namespace Envoy
