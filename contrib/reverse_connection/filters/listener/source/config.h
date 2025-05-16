#pragma once

#include "contrib/envoy/extensions/filters/listener/reverse_connection/v3alpha/reverse_connection.pb.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ReverseConnection {

class Config {
public:
  Config(const envoy::extensions::filters::listener::reverse_connection::v3alpha::ReverseConnection&
             config);

  std::chrono::seconds pingWaitTimeout() const { return ping_wait_timeout_; }

private:
  const std::chrono::seconds ping_wait_timeout_;
};

} // namespace ReverseConnection
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
