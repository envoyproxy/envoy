#include "source/common/websocket/handshake.h"

#include <openssl/sha.h>

#include <array>
#include <cstdint>
#include <string>

#include "source/common/common/base64.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace WebSocket {
namespace {

// https://datatracker.ietf.org/doc/html/rfc6455#section-1.3
constexpr absl::string_view WebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

} // namespace

std::string generateKey(Random::RandomGenerator& random) {
  const std::array<uint64_t, 2> nonce = {random.random(), random.random()};
  return Base64::encode(reinterpret_cast<const char*>(nonce.data()), sizeof(nonce));
}

std::string computeAccept(absl::string_view key) {
  const std::string input = absl::StrCat(key, WebSocketGuid);
  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const uint8_t*>(input.data()), input.size(), digest);
  return Base64::encode(reinterpret_cast<const char*>(digest), SHA_DIGEST_LENGTH);
}

} // namespace WebSocket
} // namespace Envoy
