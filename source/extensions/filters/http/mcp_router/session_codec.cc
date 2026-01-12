#include "source/extensions/filters/http/mcp_router/session_codec.h"

#include "source/common/common/base64.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

// TODO(botengyao): Add encryption for session IDs to prevent tampering.
// Currently using Base64 encoding only, which provides no security.
std::string SessionCodec::encode(const std::string& data) {
  return Base64::encode(data.data(), data.size());
}

std::string SessionCodec::decode(const std::string& encoded) { return Base64::decode(encoded); }

std::string SessionCodec::buildCompositeSessionId(
    const std::string& route, const std::string& subject,
    const absl::flat_hash_map<std::string, std::string>& backend_sessions) {
  std::vector<std::string> backend_parts;
  backend_parts.reserve(backend_sessions.size());

  for (const auto& [backend, session] : backend_sessions) {
    std::string encoded = Base64::encode(session.data(), session.size());
    backend_parts.push_back(absl::StrCat(backend, ":", encoded));
  }

  return absl::StrCat(route, "@", subject, "@", absl::StrJoin(backend_parts, ","));
}

absl::StatusOr<SessionCodec::ParsedSession>
SessionCodec::parseCompositeSessionId(const std::string& composite) {
  std::vector<std::string> parts = absl::StrSplit(composite, '@');
  if (parts.size() != 3) {
    return absl::InvalidArgumentError("Invalid session format");
  }

  ParsedSession result;
  result.route = parts[0];
  result.subject = parts[1];

  if (parts[2].empty()) {
    return absl::InvalidArgumentError("Empty backend sessions");
  }

  std::vector<std::string> backend_parts = absl::StrSplit(parts[2], ',');
  for (const auto& bp : backend_parts) {
    size_t colon = bp.find(':');
    if (colon == std::string::npos || colon == 0) {
      return absl::InvalidArgumentError("Invalid backend session format");
    }
    std::string backend = bp.substr(0, colon);
    std::string encoded_session = bp.substr(colon + 1);
    result.backend_sessions[backend] = Base64::decode(encoded_session);
  }

  return result;
}

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
