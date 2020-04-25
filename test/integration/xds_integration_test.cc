#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using testing::HasSubstr;

// This is a minimal litmus test for the v2 xDS APIs.
class XdsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  XdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

  void createEnvoy() override {
    createEnvoyServer({
        "test/config/integration/server_xds.bootstrap.yaml",
        "test/config/integration/server_xds.cds.yaml",
        "test/config/integration/server_xds.eds.yaml",
        "test/config/integration/server_xds.lds.yaml",
        "test/config/integration/server_xds.rds.yaml",
    });
  }

  void createEnvoyServer(const ApiFilesystemConfig& api_filesystem_config) {
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createApiTestServer(api_filesystem_config, {"http"}, false, false, false);
    EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("http.router.rds.route_config_0.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("cluster_manager.cds.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.update_success")->value());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, XdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(XdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

class XdsIntegrationTestTypedStruct : public XdsIntegrationTest {
public:
  XdsIntegrationTestTypedStruct() = default;

  void createEnvoy() override {
    createEnvoyServer({
        "test/config/integration/server_xds.bootstrap.yaml",
        "test/config/integration/server_xds.cds.yaml",
        "test/config/integration/server_xds.eds.yaml",
        "test/config/integration/server_xds.lds.typed_struct.yaml",
        "test/config/integration/server_xds.rds.yaml",
    });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, XdsIntegrationTestTypedStruct,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(XdsIntegrationTestTypedStruct, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

class LdsInplaceUpdateIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public BaseIntegrationTest {
public:
  LdsInplaceUpdateIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
    - filter_chain_match:
        application_protocols: ["alpn0"]
      filters:
       -  name: envoy.filters.network.echo
    - filter_chain_match:
        application_protocols: ["alpn1"]
      filters:
       -  name: envoy.filters.network.echo
)EOF") {}

  ~LdsInplaceUpdateIntegrationTest() override = default;

  void initialize() override {
    config_helper_.renameListener("echo");
    std::string tls_inspector_config = ConfigHelper::tlsInspectorFilter();
    config_helper_.addListenerFilter(tls_inspector_config);

    config_helper_.addSslConfig();
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain_0 =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      auto* filter_chain_1 =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(1);
      filter_chain_1->mutable_transport_socket()->MergeFrom(
          *filter_chain_0->mutable_transport_socket());
    });
    BaseIntegrationTest::initialize();

    context_manager_ =
        std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
  }

  class SslClient {
  public:
    SslClient(Network::ClientConnectionPtr ssl_conn, Event::Dispatcher& dispatcher)
        : ssl_conn_(std::move(ssl_conn)),
          payload_reader_(std::make_shared<WaitForPayloadReader>(dispatcher)) {
      ssl_conn_->addConnectionCallbacks(connect_callbacks_);
      ssl_conn_->addReadFilter(payload_reader_);
      ssl_conn_->connect();
      while (!connect_callbacks_.connected()) {
        dispatcher.run(Event::Dispatcher::RunType::NonBlock);
      }
    }
    Network::ClientConnectionPtr ssl_conn_;
    MockWatermarkBuffer* client_write_buffer;
    std::shared_ptr<WaitForPayloadReader> payload_reader_;
    ConnectionStatusCallbacks connect_callbacks_;
  };

  std::unique_ptr<SslClient> connect(const std::string& alpn) {
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("echo"));
    auto ssl_conn = dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
            absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{alpn})),
        nullptr);
    return std::make_unique<SslClient>(std::move(ssl_conn), *dispatcher_);
  }
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr context_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
};

// Verify that client 1 is closed while client 0 survives when deleting filter chain 1.
TEST_P(LdsInplaceUpdateIntegrationTest, ReloadConfigDeletingFilterChain) {
  initialize();

  auto client_0 = connect("alpn0");
  auto client_1 = connect("alpn1");

  ConfigHelper new_config_helper(version_, *api_,
                                 MessageUtil::getJsonStringFromMessage(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });
  new_config_helper.setLds("1");

  while (!client_1->connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  Buffer::OwnedImpl buffer("hello");
  client_0->ssl_conn_->write(buffer, false);
  client_0->payload_reader_->set_data_to_wait_for("hello");
  client_0->ssl_conn_->dispatcher().run(Event::Dispatcher::RunType::Block);
  client_0->ssl_conn_->close(Network::ConnectionCloseType::NoFlush);

  while (!client_0->connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

// Verify that client 0 survives while adding new filter chain 2.
TEST_P(LdsInplaceUpdateIntegrationTest, ReloadConfigAddingFilterChain) {
  initialize();
  auto client_0 = connect("alpn0");

  ConfigHelper new_config_helper(version_, *api_,
                                 MessageUtil::getJsonStringFromMessage(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->Add()->MergeFrom(*listener->mutable_filter_chains(0));
        *listener->mutable_filter_chains(2)
             ->mutable_filter_chain_match()
             ->mutable_application_protocols(0) = "alpn2";
      });
  new_config_helper.setLds("1");

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  auto client_2 = connect("alpn2");

  Buffer::OwnedImpl buffer2("hello");
  client_2->ssl_conn_->write(buffer2, false);
  client_2->payload_reader_->set_data_to_wait_for("hello");
  client_2->ssl_conn_->dispatcher().run(Event::Dispatcher::RunType::Block);
  client_2->ssl_conn_->close(Network::ConnectionCloseType::NoFlush);

  while (!client_2->connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  Buffer::OwnedImpl buffer("hello");
  client_0->ssl_conn_->write(buffer, false);
  client_0->payload_reader_->set_data_to_wait_for("hello");
  client_0->ssl_conn_->dispatcher().run(Event::Dispatcher::RunType::Block);
  client_0->ssl_conn_->close(Network::ConnectionCloseType::NoFlush);

  while (!client_0->connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

INSTANTIATE_TEST_SUITE_P(IpVersions, LdsInplaceUpdateIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

using LdsIntegrationTest = HttpProtocolIntegrationTest;

INSTANTIATE_TEST_SUITE_P(Protocols, LdsIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1}, {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Sample test making sure our config framework correctly reloads listeners.
TEST_P(LdsIntegrationTest, ReloadConfig) {
  autonomous_upstream_ = true;
  initialize();
  // Given we're using LDS in this test, initialize() will not complete until
  // the initial LDS file has loaded.
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // HTTP 1.0 is disabled by default.
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);

  // Create a new config with HTTP/1.0 proxying.
  ConfigHelper new_config_helper(version_, *api_,
                                 MessageUtil::getJsonStringFromMessage(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_http_protocol_options()->set_accept_http_10(true);
        hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
      });

  // Create an LDS response with the new config, and reload config.
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);

  // HTTP 1.0 should now be enabled.
  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response2, false);
  EXPECT_THAT(response2, HasSubstr("HTTP/1.0 200 OK\r\n"));
}

// Sample test making sure our config framework informs on listener failure.
TEST_P(LdsIntegrationTest, FailConfigLoad) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    filter_chain->mutable_filters(0)->clear_typed_config();
    filter_chain->mutable_filters(0)->set_name("grewgragra");
  });
  EXPECT_DEATH_LOG_TO_STDERR(initialize(),
                             "Didn't find a registered implementation for name: 'grewgragra'");
}

} // namespace
} // namespace Envoy
