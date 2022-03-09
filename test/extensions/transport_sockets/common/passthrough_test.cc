#include "source/extensions/transport_sockets/common/passthrough.h"

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
  EXPECT_CALL(*inner_socket_, setTransportSocketCallbacks(Ref(*transport_callbacks)));
  passthrough_socket_->setTransportSocketCallbacks(*transport_callbacks);
}

// Test protocol method defers to inner socket
TEST_F(PassthroughTest, ProtocolDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, protocol());
  passthrough_socket_->protocol();
}

// Test failureReason method defers to inner socket
TEST_F(PassthroughTest, FailureReasonDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, failureReason());
  passthrough_socket_->failureReason();
}

// Test canFlushClose method defers to inner socket
TEST_F(PassthroughTest, CanFlushCloseDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, canFlushClose());
  passthrough_socket_->canFlushClose();
}

// Test closeSocket method defers to inner socket
TEST_F(PassthroughTest, CloseSocketDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, closeSocket(testing::Eq(Network::ConnectionEvent::LocalClose)));
  passthrough_socket_->closeSocket(Network::ConnectionEvent::LocalClose);
}

// Test doRead method defers to inner socket
TEST_F(PassthroughTest, DoReadDefersToInnerSocket) {
  auto buff = Buffer::OwnedImpl("data");
  EXPECT_CALL(*inner_socket_, doRead(BufferEqual(&buff)));
  passthrough_socket_->doRead(buff);
}

// Test doWrite method defers to inner socket
TEST_F(PassthroughTest, DoWriteDefersToInnerSocket) {
  auto buff = Buffer::OwnedImpl("data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&buff), false));
  passthrough_socket_->doWrite(buff, false);
}

// Test onConnected method defers to inner socket
TEST_F(PassthroughTest, OnConnectedDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, onConnected());
  passthrough_socket_->onConnected();
}

// Test ssl method defers to inner socket
TEST_F(PassthroughTest, SslDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_, ssl());
  passthrough_socket_->ssl();
}

// Test invoking startSecureTransport.
TEST_F(PassthroughTest, FailOnStartSecureTransport) {
  EXPECT_FALSE(passthrough_socket_->startSecureTransport());
}

// Test configureInitialCongestionWindow method defers to inner socket
TEST_F(PassthroughTest, ConfigureInitialCongestionWindowDefersToInnerSocket) {
  EXPECT_CALL(*inner_socket_,
              configureInitialCongestionWindow(100, std::chrono::microseconds(123)));
  passthrough_socket_->configureInitialCongestionWindow(100, std::chrono::microseconds(123));
}

TEST(PassthroughFactoryTest, TestDelegation) {
  auto inner_factory_ptr = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
  Network::MockTransportSocketFactory* inner_factory = inner_factory_ptr.get();
  Network::TransportSocketFactoryPtr factory{std::move(inner_factory_ptr)};

  {
    EXPECT_CALL(*inner_factory, implementsSecureTransport());
    factory->implementsSecureTransport();
  }

  {
    EXPECT_CALL(*inner_factory, supportsAlpn());
    factory->supportsAlpn();
  }
  {
    std::vector<uint8_t> key;
    EXPECT_CALL(*inner_factory, hashKey(_, _));
    factory->hashKey(key, nullptr);
  }
}

} // namespace
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
