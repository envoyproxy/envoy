#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Kafka message decoder
 * @tparam MT message type (Kafka request or Kafka response)
 */
template <typename MT> class MessageDecoder {
public:
  virtual ~MessageDecoder() = default;
  virtual void onData(Buffer::Instance& data) PURE;
};

/**
 * Kafka message decoder
 * @tparam MT message type (Kafka request or Kafka response)
 */
template <typename MT> class MessageEncoder {
public:
  virtual ~MessageEncoder() = default;
  virtual void encode(const MT& message) PURE;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
