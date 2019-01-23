#pragma once

#include "quiche/http2/platform/api/http2_string.h"

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {

// Placeholder use of a QUICHE platform type.
// TODO(mpwarres): remove once real uses of QUICHE platform added.
http2::Http2String moreCowbell(const http2::Http2String& s);

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
