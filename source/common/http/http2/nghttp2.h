#pragma once

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * Setup nghttp2 trace-level logging for when debugging.
 */
void initializeNghttp2Logging();

} // namespace Http2
} // namespace Http
} // namespace Envoy
