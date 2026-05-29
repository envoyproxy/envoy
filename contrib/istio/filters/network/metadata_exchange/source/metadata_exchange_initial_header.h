#pragma once

#include <cstdint>

#include "envoy/common/platform.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetadataExchange {

// Used with MetadataExchangeHeaderProto to be extensible.
PACKED_STRUCT(struct MetadataExchangeInitialHeader {
  uint32_t magic; // Magic number in network byte order. Most significant byte
                  // is placed first.
  static const uint32_t magic_number = 0x3D230467; // decimal 1025705063
  uint32_t data_size; // Size of the data blob in network byte order. Most
                      // significant byte is placed first.
});

} // namespace MetadataExchange
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
