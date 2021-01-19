#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.h"
#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.validate.h"
#include "envoy/network/connection.h"

#include "common/network/transport_socket_options_impl.h"

#include "extensions/transport_sockets/starttls/starttls_socket.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

using testing::_;

class StartTlsTransportSocketMock : public Network::MockTransportSocket {
public:
  MOCK_METHOD(void, Die, ());
  ~StartTlsTransportSocketMock() override { Die(); }
};

TEST(StartTlsTest, BasicSwitch) {
  const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig config;
  Network::TransportSocketOptionsSharedPtr options =
      std::make_shared<Network::TransportSocketOptionsImpl>();
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks;
  NiceMock<StartTlsTransportSocketMock>* raw_socket = new NiceMock<StartTlsTransportSocketMock>;
  Network::MockTransportSocket* ssl_socket = new Network::MockTransportSocket;
  Buffer::OwnedImpl buf;

  std::unique_ptr<StartTlsSocket> socket =
      std::make_unique<StartTlsSocket>(config, Network::TransportSocketPtr(raw_socket),
                                       Network::TransportSocketPtr(ssl_socket), options);
  socket->setTransportSocketCallbacks(transport_callbacks);

  // StartTls socket is initial clear-text state. All calls should be forwarded to raw socket.
  ASSERT_THAT(socket->protocol(), "starttls");
  EXPECT_CALL(*raw_socket, onConnected());
  EXPECT_CALL(*ssl_socket, onConnected()).Times(0);
  socket->onConnected();

  EXPECT_CALL(*raw_socket, failureReason());
  EXPECT_CALL(*ssl_socket, failureReason()).Times(0);
  socket->failureReason();

  EXPECT_CALL(*raw_socket, canFlushClose());
  EXPECT_CALL(*ssl_socket, canFlushClose()).Times(0);
  socket->canFlushClose();

  EXPECT_CALL(*raw_socket, ssl());
  EXPECT_CALL(*ssl_socket, ssl()).Times(0);
  socket->ssl();

  EXPECT_CALL(*raw_socket, closeSocket(Network::ConnectionEvent::RemoteClose));
  EXPECT_CALL(*ssl_socket, closeSocket(Network::ConnectionEvent::RemoteClose)).Times(0);
  socket->closeSocket(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(*raw_socket, doRead(_));
  EXPECT_CALL(*ssl_socket, doRead(_)).Times(0);
  socket->doRead(buf);

  EXPECT_CALL(*raw_socket, doWrite(_, true));
  EXPECT_CALL(*ssl_socket, doWrite(_, true)).Times(0);
  socket->doWrite(buf, true);

  // Now switch to Tls. During the switch, the new socket should register for callbacks
  // and connect.
  EXPECT_CALL(*ssl_socket, setTransportSocketCallbacks(_));
  EXPECT_CALL(*ssl_socket, onConnected);
  // Make sure that raw socket is destructed.
  EXPECT_CALL(*raw_socket, Die);
  socket->startSecureTransport();

  // Calling again should do nothing: No subsequent registration for callbacks
  // and no onConnected.
  socket->startSecureTransport();

  // Now calls to all methods should be forwarded to ssl_socket.
  // raw_socket has been destructed when switch to tls happened.
  ASSERT_THAT(socket->protocol(), "starttls");
  EXPECT_CALL(*ssl_socket, onConnected());
  socket->onConnected();

  EXPECT_CALL(*ssl_socket, failureReason());
  socket->failureReason();

  EXPECT_CALL(*ssl_socket, canFlushClose());
  socket->canFlushClose();

  EXPECT_CALL(*ssl_socket, ssl());
  socket->ssl();

  EXPECT_CALL(*ssl_socket, closeSocket(Network::ConnectionEvent::RemoteClose));
  socket->closeSocket(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(*ssl_socket, doRead(_));
  socket->doRead(buf);

  EXPECT_CALL(*ssl_socket, doWrite(_, true));
  socket->doWrite(buf, true);
}

// Factory test.
TEST(StartTls, BasicFactoryTest) {
  const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig config;
  NiceMock<Network::MockTransportSocketFactory>* raw_buffer_factory =
      new NiceMock<Network::MockTransportSocketFactory>;
  NiceMock<Network::MockTransportSocketFactory>* ssl_factory =
      new NiceMock<Network::MockTransportSocketFactory>;
  std::unique_ptr<ServerStartTlsSocketFactory> factory =
      std::make_unique<ServerStartTlsSocketFactory>(
          config, Network::TransportSocketFactoryPtr(raw_buffer_factory),
          Network::TransportSocketFactoryPtr(ssl_factory));
  ASSERT_FALSE(factory->implementsSecureTransport());
  ASSERT_FALSE(factory->usesProxyProtocolOptions());
}

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
