#pragma once

#include <memory>
#include <sstream>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Abstract message (that can be either request or response).
 */
class Message {
public:
  virtual ~Message() = default;

  /**
   * Encode the contents of this message into a given buffer.
   * @param dst buffer instance to keep serialized message
   */
  virtual size_t encode(Buffer::Instance& dst) const PURE;
};

typedef std::shared_ptr<Message> MessageSharedPtr;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
