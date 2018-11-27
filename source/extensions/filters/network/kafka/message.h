#pragma once

#include <memory>
#include <sstream>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Abstract message (that can be either request or response)
 */
class Message {
public:
  virtual ~Message() = default;
};

typedef std::shared_ptr<Message> MessageSharedPtr;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
