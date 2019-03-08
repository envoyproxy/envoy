#pragma once

#include <cstdint>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Common {
namespace Crypto {

class Utility {
public:
  /**
   * Computes the SHA-256 digest of a buffer.
   * @param buffer the buffer.
   * @return a vector of bytes for the computed digest.
   */
  static std::vector<uint8_t> getSha256Digest(const Buffer::Instance& buffer);

  /**
   * Computes the SHA-256 HMAC for a given key and message.
   * @param key the HMAC function key.
   * @param message message data for the HMAC function.
   * @return a vector of bytes for the computed HMAC.
   */
  static std::vector<uint8_t> getSha256Hmac(const std::vector<uint8_t>& key,
                                            absl::string_view message);
};

} // namespace Crypto
} // namespace Common
} // namespace Envoy
