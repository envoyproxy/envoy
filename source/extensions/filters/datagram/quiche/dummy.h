#pragma once

#include "quiche/http2/platform/api/http2_string.h"

namespace Envoy {
namespace Extensions {
namespace DatagramFilters {
namespace Quiche {

http2::Http2String moreCowbell(const http2::Http2String& s);

} // namespace Quiche
} // namespace DatagramFilters
} // namespace Extensions
} // namespace Envoy
