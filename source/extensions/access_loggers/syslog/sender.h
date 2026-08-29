#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

class Sender {
public:
  virtual ~Sender() = default;

  /**
   * Submits one complete syslog record for best-effort delivery. Implementations must copy the
   * record before returning if they retain it for asynchronous delivery.
   */
  virtual void send(absl::string_view record) PURE;
};

using SenderPtr = std::unique_ptr<Sender>;

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
