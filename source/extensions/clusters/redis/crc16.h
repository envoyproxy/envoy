#pragma once

#include <cstdint>

#include "absl/strings/ascii.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class Crc16 {
public:
  /**
   * XMODEM CRC16 implementation according to CITT standards.
   * Based on (https://github.com/antirez/redis/blob/unstable/src/crc16.c).
   * @param key The string to hash.
   * @return The CRC16 hash code.
   */
  static uint16_t crc16(absl::string_view key);
};
} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
