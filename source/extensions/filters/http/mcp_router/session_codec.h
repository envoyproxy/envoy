#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

/**
 * Codec for encoding and decoding composite MCP session IDs.
 * Combines route, subject, and per-backend session IDs into a single encoded string.
 */
class SessionCodec {
public:
  static std::string encode(const std::string& data);
  static std::string decode(const std::string& encoded);

  // Integrity-protected variants keyed by a server-held secret. encodeWithIntegrity appends a
  // Base64 HMAC-SHA256 of the encoded payload as "<payload>.<mac>", so any client-side edit to
  // the session blob (including the bound subject) is detectable. decodeWithIntegrity returns an
  // empty string, matching decode()'s failure contract, when the token is malformed or the MAC
  // does not verify.
  static std::string encodeWithIntegrity(const std::string& data, absl::string_view key);
  static std::string decodeWithIntegrity(absl::string_view encoded, absl::string_view key);

  // Format: {route}@{base64(subject)}@{backend1}:{base64(sid1)},{backend2}:{base64(sid2)}
  static std::string
  buildCompositeSessionId(const std::string& route, const std::string& subject,
                          const absl::flat_hash_map<std::string, std::string>& backend_sessions);

  struct ParsedSession {
    std::string route;
    std::string subject;
    absl::flat_hash_map<std::string, std::string> backend_sessions;
  };
  static absl::StatusOr<ParsedSession> parseCompositeSessionId(absl::string_view composite);
};

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
