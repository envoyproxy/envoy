#pragma once

namespace Envoy {
namespace Http {

/**
 * Possible HTTP connection/request protocols.
 */
enum class Protocol { Http10, Http11, Http2 };

} // namespace Http
} // namespace Envoy
