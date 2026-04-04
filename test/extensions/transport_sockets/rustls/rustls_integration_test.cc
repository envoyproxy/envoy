#include "envoy/extensions/transport_sockets/rustls/v3/rustls.pb.h"

#include "source/common/tls/context_manager_impl.h"

#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Rustls {
namespace {

class RustlsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              public BaseIntegrationTest {
public:
  RustlsIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void initializeWithDownstreamRustls() {
    const std::string cert_path =
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem");
    const std::string key_path =
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem");

    config_helper_.addConfigModifier([cert_path, key_path](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);

      envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext rustls_config;
      rustls_config.set_cert_chain(cert_path);
      rustls_config.set_private_key(key_path);

      auto* ts = filter_chain->mutable_transport_socket();
      ts->set_name("envoy.transport_sockets.rustls");
      ts->mutable_typed_config()->PackFrom(rustls_config);
    });

    BaseIntegrationTest::initialize();

    context_manager_ =
        std::make_unique<Envoy::Extensions::TransportSockets::Tls::ContextManagerImpl>(
            server_factory_context_);
    client_ssl_ctx_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
  }

  std::unique_ptr<Envoy::Extensions::TransportSockets::Tls::ContextManagerImpl> context_manager_;
  Network::UpstreamTransportSocketFactoryPtr client_ssl_ctx_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RustlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that a downstream `rustls` TLS transport socket can accept a TLS connection, complete
// the handshake, and forward data bidirectionally through the TCP proxy to a plaintext upstream.
TEST_P(RustlsIntegrationTest, TlsDownstreamToPlaintextUpstream) {
  initializeWithDownstreamRustls();

  ConnectionStatusCallbacks connect_callbacks;
  auto payload_reader = std::make_shared<WaitForPayloadReader>(*dispatcher_);

  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("listener_0"));
  auto ssl_client = dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_ssl_ctx_->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  ssl_client->addConnectionCallbacks(connect_callbacks);
  ssl_client->addReadFilter(payload_reader);
  ssl_client->connect();

  while (!connect_callbacks.connected() && !connect_callbacks.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ASSERT_TRUE(connect_callbacks.connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  Buffer::OwnedImpl request("hello");
  ssl_client->write(request, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  ASSERT_TRUE(fake_upstream_connection->write("world"));
  payload_reader->setDataToWaitFor("world");
  ssl_client->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(payload_reader->data(), "world");

  ssl_client->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

} // namespace
} // namespace Rustls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
