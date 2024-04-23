#pragma once

#include "library/cc/headers.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

RawHeaderMap envoyHeadersAsRawHeaderMap(envoy_headers raw_headers);

} // namespace Platform
} // namespace Envoy
