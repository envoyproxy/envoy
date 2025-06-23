#include "contrib/postgres_proxy/filters/network/source/postgres_encoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

Envoy::Buffer::OwnedImpl Encoder::buildErrorResponse(absl::string_view severity,
                                                     absl::string_view message,
                                                     absl::string_view code) {
  Buffer::OwnedImpl response;
  response.add("E");
  // Length of message contents in bytes, including self.
  const uint32_t length =
      sizeof(uint32_t) + 3 + severity.length() + message.length() + code.length() + 3;
  response.writeBEInt<uint32_t>(length);

  // Severity
  response.add("S");
  response.add(severity);
  response.add("\0", 1);

  // Message
  response.add("M");
  response.add(message);
  response.add("\0", 1);

  // Code
  response.add("C");
  response.add(code);
  response.add("\0", 1);

  // Final null terminator
  response.add("\0", 1);

  return response;
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
