#include "extensions/transport_sockets/common/passthrough.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace {

class PassthroughTest : public testing::Test {
protected:
  void SetUp() override {
    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_ = inner_socket.get();
    passthrough_socket_ = std::make_unique<PassthroughSocket>(std::move(inner_socket));
  }

  NiceMock<Network::MockTransportSocket>* inner_socket_;
  std::unique_ptr<PassthroughSocket> passthrough_socket_;
};

// Test setTransportSocketCallbacks method defers to inner socket
TEST_F(PassthroughTest, SetTransportSocketCallbacksDefersToInnerSocket) {
  auto transport_callbacks = std::make_unique<NiceMock<Network::MockTransportSocketCallbacks>>();
  EXPECT_CALL(*inner_socket_, setTransportSocketCallbacks(Ref(*transport_callbacks))).Times(1);
  passthrough_socket_->setTransportSocketCallbacks(*transport_callbacks);
}

// Test protocol method defers to inner socket
TEST_F(PassthroughTest, ProtocolDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, protocol()).Times(1);
  passthrough_socket_->protocol();
}

// Test failureReason method defers to inner socket
TEST_F(PassthroughTest, FailureReasonDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, failureReason()).Times(1);
  passthrough_socket_->failureReason();
}

// Test canFlushClose method defers to inner socket
TEST_F(PassthroughTest, CanFlushCloseDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, canFlushClose()).Times(1);
  passthrough_socket_->canFlushClose();
}

// Test closeSocket method defers to inner socket
TEST_F(PassthroughTest, CloseSocketDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, closeSocket(testing::Eq(Network::ConnectionEvent::LocalClose)))
      .Times(1);
  passthrough_socket_->closeSocket(Network::ConnectionEvent::LocalClose);
}

// Test doRead method defers to inner socket
TEST_F(PassthroughTest, DoReadDefersToInnerSocket) {
  auto buff = Buffer::OwnedImpl("data");
  EXPECT_CALL(*inner_socket_, doRead(BufferEqual(&buff))).Times(1);
  passthrough_socket_->doRead(buff);
}

// Test doWrite method defers to inner socket
TEST_F(PassthroughTest, DoWriteDefersToInnerSocket) {
  auto buff = Buffer::OwnedImpl("data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&buff), false)).Times(1);
  passthrough_socket_->doWrite(buff, false);
}

// Test onConnected method defers to inner socket
TEST_F(PassthroughTest, OnConnectedDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, onConnected()).Times(1);
  passthrough_socket_->onConnected();
}

// Test ssl method defers to inner socket
TEST_F(PassthroughTest, SslDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, ssl()).Times(1);
  passthrough_socket_->ssl();
}

} // namespace
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy