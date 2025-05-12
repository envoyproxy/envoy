#include "transport_socket.h"

#include <cstdint>

#include "envoy/buffer/buffer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Network {

MockTransportSocket::MockTransportSocket() {
  ON_CALL(*this, setTransportSocketCallbacks(_))
      .WillByDefault(Invoke([&](TransportSocketCallbacks& callbacks) { callbacks_ = &callbacks; }));
  ON_CALL(*this, connect(_)).WillByDefault(Invoke([&](Network::ConnectionSocket& socket) {
    return TransportSocket::connect(socket);
  }));
  ON_CALL(*this, doRead(_))
      .WillByDefault(Return(IoResult{PostIoAction::KeepOpen, 0, false, absl::nullopt}));
  ON_CALL(*this, doWrite(_, _))
      .WillByDefault(Return(IoResult{PostIoAction::KeepOpen, 0, false, absl::nullopt}));
}

MockTransportSocket::~MockTransportSocket() = default;

MockTransportSocketFactory::MockTransportSocketFactory() = default;
MockTransportSocketFactory::~MockTransportSocketFactory() = default;

MockDownstreamTransportSocketFactory::MockDownstreamTransportSocketFactory() = default;
MockDownstreamTransportSocketFactory::~MockDownstreamTransportSocketFactory() = default;

} // namespace Network
} // namespace Envoy
