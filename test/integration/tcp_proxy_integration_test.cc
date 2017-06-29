#include "test/integration/tcp_proxy_integration_test.h"

#include "common/network/utility.h"
#include "common/ssl/context_manager_impl.h"

#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/mocks/runtime/mocks.h"

#include "gtest/gtest.h"

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
      [&]() -> void { fake_rest_connection->waitForDisconnect(); },
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
      [&]() -> void { tcp_client->write("hello"); }, [&]() -> void { tcp_client->close(); },
      [&]() -> void { fake_upstream_connection->waitForData(10); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); },

      // Clean up unused client_ssl_auth
      [&]() -> void { fake_rest_connection = fake_upstreams_[1]->waitForRawConnection(); },
      [&]() -> void { fake_rest_connection->close(); },
      [&]() -> void { fake_rest_connection->waitForDisconnect(); },
  });
}

// Test proxying data in both directions with envoy doing TCP and TLS
// termination.
TEST_P(TcpProxyIntegrationTest, SendTlsToTlsListener) {
  Network::ClientConnectionPtr ssl_client;
  FakeRawConnectionPtr fake_upstream_connection;
  FakeHttpConnectionPtr fake_rest_connection;
  testing::NiceMock<Runtime::MockLoader> runtime;
  std::unique_ptr<Ssl::ContextManager> context_manager(new Ssl::ContextManagerImpl(runtime));
  FakeStreamPtr request;
  Ssl::ClientContextPtr context;
  ConnectionStatusCallbacks connect_callbacks;
  executeActions({
      // Set up the SSl client.
      [&]() -> void {
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
        ssl_client->connect();
        while (!connect_callbacks.connected()) {
          dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
        }
      },
      // Ship some data upstream.
      [&]() -> void {
        Buffer::OwnedImpl buffer("hello");
        ssl_client->write(buffer);
        dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
      },
      // Make sure the data makes it upstream.
      [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
      [&]() -> void { fake_upstream_connection->waitForData(5); },
      // Now send data downstream and make sure it arrives.
      [&]() -> void {
        std::shared_ptr<WaitForPayloadReader> payload_reader(
            new WaitForPayloadReader(*dispatcher_));
        ssl_client->addReadFilter(payload_reader);
        fake_upstream_connection->write("world");
        payload_reader->set_data_to_wait_for("world");
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

} // namespace
} // Envoy
