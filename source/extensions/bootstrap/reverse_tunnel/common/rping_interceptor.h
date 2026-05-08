#pragma once

#include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class RpingInterceptor : public virtual Network::IoSocketHandleImpl {
public:
  // Intercept reads to handle reverse connection keep-alive pings.
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length) override;

  virtual void onPingMessage() PURE;

protected:
  // Whether to actively echo RPING messages while the connection is idle.
  // Disabled permanently after the first non-RPING application byte is observed.
  bool ping_echo_active_{true};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
