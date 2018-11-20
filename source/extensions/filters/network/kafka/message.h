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

  friend std::ostream& operator<<(std::ostream& out, const Message& arg) { return arg.print(out); }

protected:
  virtual std::ostream& print(std::ostream& os) const PURE;
};

typedef std::shared_ptr<Message> MessageSharedPtr;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
