#include "source/extensions/filters/http/mcp_router/session_codec.h"

#include "source/common/common/base64.h"
#include "source/common/common/empty_string.h"
#include "source/common/crypto/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "openssl/crypto.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

namespace {

// Base64-encoded HMAC-SHA256 of `message` under `key`.
std::string hmacSha256Base64(absl::string_view key, absl::string_view message) {
  const std::vector<uint8_t> mac = Common::Crypto::UtilitySingleton::get().getSha256Hmac(
      {reinterpret_cast<const uint8_t*>(key.data()), key.size()}, message);
  return Base64::encode(reinterpret_cast<const char*>(mac.data()), mac.size());
}

} // namespace

std::string SessionCodec::encode(const std::string& data) {
  return Base64::encode(data.data(), data.size());
}

std::string SessionCodec::decode(const std::string& encoded) { return Base64::decode(encoded); }

std::string SessionCodec::encodeWithIntegrity(const std::string& data, absl::string_view key) {
  const std::string payload = encode(data);
  return absl::StrCat(payload, ".", hmacSha256Base64(key, payload));
}

std::string SessionCodec::decodeWithIntegrity(absl::string_view encoded, absl::string_view key) {
  const size_t sep = encoded.rfind('.');
  if (sep == absl::string_view::npos) {
    return EMPTY_STRING;
  }
  const absl::string_view payload = encoded.substr(0, sep);
  const absl::string_view mac = encoded.substr(sep + 1);
  const std::string expected_mac = hmacSha256Base64(key, payload);
  if (mac.size() != expected_mac.size() ||
      CRYPTO_memcmp(mac.data(), expected_mac.data(), mac.size()) != 0) {
    return EMPTY_STRING;
  }
  return Base64::decode(payload);
}

std::string SessionCodec::buildCompositeSessionId(
    const std::string& route, const std::string& subject,
    const absl::flat_hash_map<std::string, std::string>& backend_sessions) {
  std::vector<std::string> backend_parts;
  backend_parts.reserve(backend_sessions.size());

  for (const auto& [backend, session] : backend_sessions) {
    std::string encoded = Base64::encode(session.data(), session.size());
    backend_parts.push_back(absl::StrCat(backend, ":", encoded));
  }

  std::string encoded_subject = Base64::encode(subject.data(), subject.size());
  return absl::StrCat(route, "@", encoded_subject, "@", absl::StrJoin(backend_parts, ","));
}

absl::StatusOr<SessionCodec::ParsedSession>
SessionCodec::parseCompositeSessionId(absl::string_view composite) {
  std::vector<absl::string_view> parts = absl::StrSplit(composite, '@');
  if (parts.size() != 3) {
    return absl::InvalidArgumentError("Invalid session format");
  }

  ParsedSession result;
  result.route = parts[0];
  result.subject = Base64::decode(parts[1]);

  if (parts[2].empty()) {
    // No backend sessions (all backends are session-less).
    return result;
  }

  std::vector<absl::string_view> backend_parts = absl::StrSplit(parts[2], ',');
  for (const auto& bp : backend_parts) {
    size_t colon = bp.find(':');
    if (colon == std::string::npos || colon == 0) {
      return absl::InvalidArgumentError("Invalid backend session format");
    }
    absl::string_view backend = bp.substr(0, colon);
    absl::string_view encoded_session = bp.substr(colon + 1);
    result.backend_sessions[backend] = Base64::decode(encoded_session);
  }

  return result;
}

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
