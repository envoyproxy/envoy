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

  // Format: {route}@{base64(subject)}@{backend1}:{base64(sid1)},{backend2}:{base64(sid2)}
  static std::string
  buildCompositeSessionId(const std::string& route, const std::string& subject,
                          const absl::flat_hash_map<std::string, std::string>& backend_sessions);

  struct ParsedSession {
    std::string route;
    std::string subject;
    absl::flat_hash_map<std::string, std::string> backend_sessions;
  };
  static absl::StatusOr<ParsedSession> parseCompositeSessionId(const std::string& composite);
};

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
