#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "extensions/filters/network/kafka/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Kafka message decoder
 */
class MessageDecoder {
public:
  virtual ~MessageDecoder() = default;

  /**
   * Processes given buffer attempting to decode messages contained within
   * @param data buffer instance
   */
  virtual void onData(Buffer::Instance& data) PURE;
};

/**
 * Kafka message encoder
 */
class MessageEncoder {
public:
  virtual ~MessageEncoder() = default;

  /**
   * Encodes given message
   * @param message message to be encoded
   */
  virtual void encode(const Message& message) PURE;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
