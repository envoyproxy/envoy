#include "transport_socket.h"

#include <cstdint>

#include "envoy/buffer/buffer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Network {

MockTransportSocket::MockTransportSocket() {
  ON_CALL(*this, setTransportSocketCallbacks(_))
      .WillByDefault(Invoke([&](TransportSocketCallbacks& callbacks) { callbacks_ = &callbacks; }));
}
MockTransportSocket::~MockTransportSocket() = default;

MockTransportSocketFactory::MockTransportSocketFactory() = default;
MockTransportSocketFactory::~MockTransportSocketFactory() = default;

} // namespace Network
} // namespace Envoy
