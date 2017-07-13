#include "test/integration/tcp_proxy_integration_test.h"

#include "common/network/utility.h"
#include "common/ssl/context_manager_impl.h"

#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/mocks/runtime/mocks.h"

#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::Invoke;
using testing::_;

namespace Envoy {
namespace {

INSTANTIATE_TEST_CASE_P(IpVersions, TcpProxyIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test proxying data in both directions, and that all data is flushed properly
// when there is an upstream disconnect.
TEST_P(TcpProxyIntegrationTest, TcpProxyUpstreamDisconnect) {
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  FakeRawConnectionPtr fake_rest_connection;
  executeActions({
      [&]() -> void { tcp_client = makeTcpConnection(lookupPort("tcp_proxy")); },
      [&]() -> void { tcp_client->write("hello"); },
      [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
      [&]() -> void { fake_upstream_connection->waitForData(5); },
      [&]() -> void { fake_upstream_connection->write("world"); },
      [&]() -> void { fake_upstream_connection->close(); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
      [&]() -> void { tcp_client->waitForDisconnect(); },

      // Clean up unused client_ssl_auth
      [&]() -> void { fake_rest_connection = fake_upstreams_[1]->waitForRawConnection(); },
      [&]() -> void { fake_rest_connection->close(); },
      [&]() -> void { fake_rest_connection->waitForDisconnect(true); },
  });

  EXPECT_EQ("world", tcp_client->data());
}

// Test proxying data in both directions, and that all data is flushed properly
// when the client disconnects.
TEST_P(TcpProxyIntegrationTest, TcpProxyDownstreamDisconnect) {
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  FakeRawConnectionPtr fake_rest_connection;
  executeActions({
      [&]() -> void { tcp_client = makeTcpConnection(lookupPort("tcp_proxy")); },
      [&]() -> void { tcp_client->write("hello"); },
      [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
      [&]() -> void { fake_upstream_connection->waitForData(5); },
      [&]() -> void { fake_upstream_connection->write("world"); },
      [&]() -> void { tcp_client->waitForData("world"); },
      [&]() -> void { tcp_client->write("hello"); },
      [&]() -> void { tcp_client->close(); },
      [&]() -> void { fake_upstream_connection->waitForData(10); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); },

      // Clean up unused client_ssl_auth
      [&]() -> void { fake_rest_connection = fake_upstreams_[1]->waitForRawConnection(); },
      [&]() -> void { fake_rest_connection->close(); },
      [&]() -> void { fake_rest_connection->waitForDisconnect(true); },
  });
}

TEST_P(TcpProxyIntegrationTest, TcpProxyLargeWrite) {
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  FakeRawConnectionPtr fake_rest_connection;
  std::string data(1024 * 16, 'a');
  executeActions({
      [&]() -> void { tcp_client = makeTcpConnection(lookupPort("tcp_proxy_with_write_limits")); },
      [&]() -> void { tcp_client->write(data); },
      [&]() -> void { fake_upstream_connection = fake_upstreams_[2]->waitForRawConnection(); },
      [&]() -> void { fake_upstream_connection->waitForData(data.size()); },
      [&]() -> void { fake_upstream_connection->write(data); },
      [&]() -> void { tcp_client->waitForData(data); },
      [&]() -> void { tcp_client->close(); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); },

      // Clean up unused client_ssl_auth
      [&]() -> void { fake_rest_connection = fake_upstreams_[1]->waitForRawConnection(); },
      [&]() -> void { fake_rest_connection->close(); },
      [&]() -> void { fake_rest_connection->waitForDisconnect(true); },
  });

  uint32_t upstream_pauses =
      test_server_->store()
          .counter("cluster.cluster_with_buffer_limits.upstream_flow_control_paused_reading_total")
          .value();
  uint32_t upstream_resumes =
      test_server_->store()
          .counter("cluster.cluster_with_buffer_limits.upstream_flow_control_resumed_reading_total")
          .value();
  EXPECT_EQ(upstream_pauses, upstream_resumes);

  uint32_t downstream_pauses =
      test_server_->store()
          .counter("tcp.tcp_with_write_limits.downstream_flow_control_paused_reading_total")
          .value();
  uint32_t downstream_resumes =
      test_server_->store()
          .counter("tcp.tcp_with_write_limits.downstream_flow_control_resumed_reading_total")
          .value();
  EXPECT_EQ(downstream_pauses, downstream_resumes);
}

// Test proxying data in both directions with envoy doing TCP and TLS
// termination.
void TcpProxyIntegrationTest::sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                                                    const std::string& data_to_send_downstream) {
  Network::ClientConnectionPtr ssl_client;
  FakeRawConnectionPtr fake_upstream_connection;
  FakeHttpConnectionPtr fake_rest_connection;
  testing::NiceMock<Runtime::MockLoader> runtime;
  std::unique_ptr<Ssl::ContextManager> context_manager(new Ssl::ContextManagerImpl(runtime));
  FakeStreamPtr request;
  Ssl::ClientContextPtr context;
  ConnectionStatusCallbacks connect_callbacks;
  MockBuffer* client_write_buffer;
  executeActions({
      // Set up the mock buffer factory so the newly created SSL client will have a mock write
      // buffer.  This allows us to track the bytes actually written to the socket.
      [&]() -> void {
        EXPECT_CALL(*mock_buffer_factory_, create_())
            .Times(2)
            .WillOnce(Invoke([&]() -> Buffer::Instance* {
              return new Buffer::OwnedImpl; // client read buffer.
            }))
            .WillOnce(Invoke([&]() -> Buffer::Instance* {
              client_write_buffer = new MockBuffer;
              ON_CALL(*client_write_buffer, move(_))
                  .WillByDefault(Invoke(client_write_buffer, &MockBuffer::baseMove));
              ON_CALL(*client_write_buffer, drain(_))
                  .WillByDefault(Invoke(client_write_buffer, &MockBuffer::trackDrains));
              return client_write_buffer;
            }));

        // Set up the SSl client.
        Network::Address::InstanceConstSharedPtr address =
            Ssl::getSslAddress(version_, lookupPort("tcp_proxy_with_tls_termination"));
        context = Ssl::createClientSslContext(false, false, *context_manager);
        ssl_client = dispatcher_->createSslClientConnection(*context, address);
      },
      // Set up the initial REST response for the ssl_auth filter to avoid the async client doing
      // reconnects.  Loopback is whitelisted by default so no response payload is necessary.
      [&]() -> void {
        fake_rest_connection = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
        request = fake_rest_connection->waitForNewStream();
        request->waitForEndStream(*dispatcher_);
        request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
        request->encodeData(0, true);
      },
      // Perform the SSL handshake.  Loopback is whitelisted in tcp_proxy.json for the ssl_auth
      // filter so there will be no pause waiting on auth data.
      [&]() -> void {
        ssl_client->connect();
        ssl_client->addConnectionCallbacks(connect_callbacks);
        while (!connect_callbacks.connected()) {
          dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
        }
      },
      // Ship some data upstream.
      [&]() -> void {
        Buffer::OwnedImpl buffer(data_to_send_upstream);
        ssl_client->write(buffer);
        while (client_write_buffer->bytes_drained() != data_to_send_upstream.size()) {
          dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
        }
      },
      // Make sure the data makes it upstream.
      [&]() -> void { fake_upstream_connection = fake_upstreams_[2]->waitForRawConnection(); },
      [&]() -> void { fake_upstream_connection->waitForData(data_to_send_upstream.size()); },
      // Now send data downstream and make sure it arrives.
      [&]() -> void {
        std::shared_ptr<WaitForPayloadReader> payload_reader(
            new WaitForPayloadReader(*dispatcher_));
        ssl_client->addReadFilter(payload_reader);
        fake_upstream_connection->write(data_to_send_downstream);
        payload_reader->set_data_to_wait_for(data_to_send_downstream);
        ssl_client->dispatcher().run(Event::Dispatcher::RunType::Block);
      },
      // Clean up.
      [&]() -> void { ssl_client->close(Network::ConnectionCloseType::NoFlush); },
      [&]() -> void { fake_upstream_connection->close(); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
      [&]() -> void { fake_rest_connection->close(); },
      [&]() -> void { fake_rest_connection->waitForDisconnect(); },
  });
}

TEST_P(TcpProxyIntegrationTest, SendTlsToTlsListener) { sendAndReceiveTlsData("hello", "world"); }

TEST_P(TcpProxyIntegrationTest, LargeBidirectionalTlsWrites) {
  std::string large_data(1024 * 8, 'a');
  sendAndReceiveTlsData(large_data, large_data);
}

} // namespace
} // namespace Envoy
