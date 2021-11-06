#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "utility.h"

namespace Envoy {
namespace {

using testing::HasSubstr;

// This is a minimal litmus test for the v3 xDS APIs.
class XdsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  XdsIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void createEnvoy() override {
    createEnvoyServer({
        "test/config/integration/server_xds.bootstrap.yml",
        "test/config/integration/server_xds.cds.yaml",
        "test/config/integration/server_xds.eds.yaml",
        "test/config/integration/server_xds.lds.yaml",
        "test/config/integration/server_xds.rds.yaml",
    });
  }

  void createEnvoyServer(const ApiFilesystemConfig& api_filesystem_config) {
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createApiTestServer(api_filesystem_config, {"http"}, {false, false, false}, false);
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
        "test/config/integration/server_xds.bootstrap.yml",
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

class UdpaXdsIntegrationTestListCollection : public XdsIntegrationTest {
public:
  UdpaXdsIntegrationTestListCollection() = default;

  void createEnvoy() override {
    // TODO(htuch): Convert CDS/EDS/RDS to UDPA list collections when support is implemented in
    // Envoy.
    createEnvoyServer({
        "test/config/integration/server_xds.bootstrap.udpa.yaml",
        "test/config/integration/server_xds.cds.yaml",
        "test/config/integration/server_xds.eds.yaml",
        "test/config/integration/server_xds.lds.udpa.list_collection.yaml",
        "test/config/integration/server_xds.rds.yaml",
    });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpaXdsIntegrationTestListCollection,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UdpaXdsIntegrationTestListCollection, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

class LdsInplaceUpdateTcpProxyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  LdsInplaceUpdateTcpProxyIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
    - filter_chain_match:
        application_protocols: ["alpn0"]
      filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
    - filter_chain_match:
        application_protocols: ["alpn1"]
      filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_1
  - name: another_listener_to_avoid_worker_thread_routine_exit
    address:
      socket_address:
        address: "127.0.0.1"
        port_value: 0
    filter_chains:
    - filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF") {}

  void initialize() override {
    config_helper_.renameListener("tcp");
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

      bootstrap.mutable_static_resources()->mutable_clusters()->Add()->MergeFrom(
          *bootstrap.mutable_static_resources()->mutable_clusters(0));
      bootstrap.mutable_static_resources()->mutable_clusters(1)->set_name("cluster_1");
    });

    BaseIntegrationTest::initialize();

    context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
        BaseIntegrationTest::timeSystem());
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
  }

  std::unique_ptr<RawConnectionDriver> createConnectionAndWrite(const std::string& alpn,
                                                                const std::string& request,
                                                                std::string& response) {
    Buffer::OwnedImpl buffer(request);
    return std::make_unique<RawConnectionDriver>(
        lookupPort("tcp"), buffer,
        [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
          response.append(data.toString());
        },
        version_, *dispatcher_,
        context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
            absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{alpn})));
  }

  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr context_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
};

// Verify that tcp connection 1 is closed while client 0 survives when deleting filter chain 1.
TEST_P(LdsInplaceUpdateTcpProxyIntegrationTest, ReloadConfigDeletingFilterChain) {
  setUpstreamCount(2);
  initialize();
  std::string response_0;
  auto client_conn_0 = createConnectionAndWrite("alpn0", "hello", response_0);
  client_conn_0->waitForConnection();
  FakeRawConnectionPtr fake_upstream_connection_0;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_0));

  std::string response_1;
  auto client_conn_1 = createConnectionAndWrite("alpn1", "dummy", response_1);
  client_conn_1->waitForConnection();
  FakeRawConnectionPtr fake_upstream_connection_1;
  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_upstream_connection_1));

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);

  while (!client_conn_1->closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ASSERT_EQ(response_1, "");

  std::string observed_data_0;
  ASSERT_TRUE(fake_upstream_connection_0->waitForData(5, &observed_data_0));
  EXPECT_EQ("hello", observed_data_0);

  ASSERT_TRUE(fake_upstream_connection_0->write("world"));
  while (response_0.find("world") == std::string::npos) {
    client_conn_0->run(Event::Dispatcher::RunType::NonBlock);
  }
  client_conn_0->close();
  while (!client_conn_0->closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

// Verify that tcp connection of filter chain 0 survives if new listener config adds new filter
// chain 2.
TEST_P(LdsInplaceUpdateTcpProxyIntegrationTest, ReloadConfigAddingFilterChain) {
  setUpstreamCount(2);
  initialize();
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  std::string response_0;
  auto client_conn_0 = createConnectionAndWrite("alpn0", "hello", response_0);
  client_conn_0->waitForConnection();
  FakeRawConnectionPtr fake_upstream_connection_0;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_0));

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->Add()->MergeFrom(*listener->mutable_filter_chains(1));
        *listener->mutable_filter_chains(2)
             ->mutable_filter_chain_match()
             ->mutable_application_protocols(0) = "alpn2";
      });
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  std::string response_2;
  auto client_conn_2 = createConnectionAndWrite("alpn2", "hello2", response_2);
  client_conn_2->waitForConnection();
  FakeRawConnectionPtr fake_upstream_connection_2;
  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_upstream_connection_2));
  std::string observed_data_2;
  ASSERT_TRUE(fake_upstream_connection_2->waitForData(6, &observed_data_2));
  EXPECT_EQ("hello2", observed_data_2);

  ASSERT_TRUE(fake_upstream_connection_2->write("world2"));
  while (response_2.find("world2") == std::string::npos) {
    client_conn_2->run(Event::Dispatcher::RunType::NonBlock);
  }
  client_conn_2->close();
  while (!client_conn_2->closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  std::string observed_data_0;
  ASSERT_TRUE(fake_upstream_connection_0->waitForData(5, &observed_data_0));
  EXPECT_EQ("hello", observed_data_0);

  ASSERT_TRUE(fake_upstream_connection_0->write("world"));
  while (response_0.find("world") == std::string::npos) {
    client_conn_0->run(Event::Dispatcher::RunType::NonBlock);
  }
  client_conn_0->close();
  while (!client_conn_0->closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

class LdsInplaceUpdateHttpIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  LdsInplaceUpdateHttpIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void inplaceInitialize(bool add_default_filter_chain = false) {
    autonomous_upstream_ = true;
    setUpstreamCount(2);

    config_helper_.renameListener("http");
    std::string tls_inspector_config = ConfigHelper::tlsInspectorFilter();
    config_helper_.addListenerFilter(tls_inspector_config);
    config_helper_.addSslConfig();
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) { hcm.mutable_stat_prefix()->assign("hcm0"); });
    config_helper_.addConfigModifier([this, add_default_filter_chain](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      if (!use_default_balancer_) {
        bootstrap.mutable_static_resources()
            ->mutable_listeners(0)
            ->mutable_connection_balance_config()
            ->mutable_exact_balance();
      }
      auto* filter_chain_0 =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      *filter_chain_0->mutable_filter_chain_match()->mutable_application_protocols()->Add() =
          "alpn0";
      auto* filter_chain_1 = bootstrap.mutable_static_resources()
                                 ->mutable_listeners(0)
                                 ->mutable_filter_chains()
                                 ->Add();
      filter_chain_1->MergeFrom(*filter_chain_0);

      // filter chain 1
      // alpn1, route to cluster_1
      *filter_chain_1->mutable_filter_chain_match()->mutable_application_protocols(0) = "alpn1";

      auto* config_blob = filter_chain_1->mutable_filters(0)->mutable_typed_config();

      ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::http_connection_manager::v3::
                                      HttpConnectionManager>());
      auto hcm_config = MessageUtil::anyConvert<
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
          *config_blob);
      hcm_config.mutable_route_config()
          ->mutable_virtual_hosts(0)
          ->mutable_routes(0)
          ->mutable_route()
          ->set_cluster("cluster_1");
      hcm_config.mutable_stat_prefix()->assign("hcm1");
      config_blob->PackFrom(hcm_config);
      bootstrap.mutable_static_resources()->mutable_clusters()->Add()->MergeFrom(
          *bootstrap.mutable_static_resources()->mutable_clusters(0));
      bootstrap.mutable_static_resources()->mutable_clusters(1)->set_name("cluster_1");

      if (add_default_filter_chain) {
        auto default_filter_chain = bootstrap.mutable_static_resources()
                                        ->mutable_listeners(0)
                                        ->mutable_default_filter_chain();
        default_filter_chain->MergeFrom(*filter_chain_0);
      }
    });

    BaseIntegrationTest::initialize();

    context_manager_ =
        std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
    address_ = Ssl::getSslAddress(version_, lookupPort("http"));
  }

  IntegrationCodecClientPtr createHttpCodec(const std::string& alpn) {
    auto ssl_conn = dispatcher_->createClientConnection(
        address_, Network::Address::InstanceConstSharedPtr(),
        context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
            absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{alpn})),
        nullptr);
    return makeHttpConnection(std::move(ssl_conn));
  }

  void expectResponseHeaderConnectionClose(IntegrationCodecClient& codec_client,
                                           bool expect_close) {
    IntegrationStreamDecoderPtr response =
        codec_client.makeHeaderOnlyRequest(default_request_headers_);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    if (expect_close) {
      EXPECT_EQ("close", response->headers().getConnectionValue());

    } else {
      EXPECT_EQ(nullptr, response->headers().Connection());
    }
  }

  void expectConnectionServed(std::string alpn = "alpn0") {
    auto codec_client_after_config_update = createHttpCodec(alpn);
    expectResponseHeaderConnectionClose(*codec_client_after_config_update, false);
    codec_client_after_config_update->close();
  }

  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr context_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  Network::Address::InstanceConstSharedPtr address_;
  bool use_default_balancer_{false};
};

// Verify that http response on filter chain 1 and default filter chain have "Connection: close"
// header when these 2 filter chains are deleted during the listener update.
TEST_P(LdsInplaceUpdateHttpIntegrationTest, ReloadConfigDeletingFilterChain) {
  inplaceInitialize(/*add_default_filter_chain=*/true);

  auto codec_client_1 = createHttpCodec("alpn1");
  auto codec_client_0 = createHttpCodec("alpn0");
  auto codec_client_default = createHttpCodec("alpndefault");

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
        listener->clear_default_filter_chain();
      });

  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);

  test_server_->waitForGaugeGe("http.hcm0.downstream_cx_active", 1);
  test_server_->waitForGaugeGe("http.hcm1.downstream_cx_active", 1);

  expectResponseHeaderConnectionClose(*codec_client_1, true);
  expectResponseHeaderConnectionClose(*codec_client_default, true);

  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 0);
  expectResponseHeaderConnectionClose(*codec_client_0, false);
  expectConnectionServed();

  codec_client_1->close();
  test_server_->waitForGaugeDestroyed("http.hcm1.downstream_cx_active");
  codec_client_0->close();
  codec_client_default->close();
}

// Verify that http clients of filter chain 0 survives if new listener config adds new filter
// chain 2 and default filter chain.
TEST_P(LdsInplaceUpdateHttpIntegrationTest, ReloadConfigAddingFilterChain) {
  inplaceInitialize();
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  auto codec_client_0 = createHttpCodec("alpn0");
  Cleanup cleanup0([c0 = codec_client_0.get()]() { c0->close(); });
  test_server_->waitForGaugeGe("http.hcm0.downstream_cx_active", 1);

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap)
                                          -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    // Note that HCM2 copies the stats prefix from HCM0
    listener->mutable_filter_chains()->Add()->MergeFrom(*listener->mutable_filter_chains(0));
    *listener->mutable_filter_chains(2)
         ->mutable_filter_chain_match()
         ->mutable_application_protocols(0) = "alpn2";

    auto default_filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_default_filter_chain();
    default_filter_chain->MergeFrom(*listener->mutable_filter_chains(1));
  });
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  auto codec_client_2 = createHttpCodec("alpn2");
  auto codec_client_default = createHttpCodec("alpndefault");

  // 1 connection from filter chain 0 and 1 connection from filter chain 2.
  test_server_->waitForGaugeGe("http.hcm0.downstream_cx_active", 2);

  Cleanup cleanup2([c2 = codec_client_2.get(), c_default = codec_client_default.get()]() {
    c2->close();
    c_default->close();
  });
  expectResponseHeaderConnectionClose(*codec_client_2, false);
  expectResponseHeaderConnectionClose(*codec_client_default, false);
  expectResponseHeaderConnectionClose(*codec_client_0, false);
  expectConnectionServed();
}

// Verify that http clients of default filter chain is drained and recreated if the default filter
// chain updates.
TEST_P(LdsInplaceUpdateHttpIntegrationTest, ReloadConfigUpdatingDefaultFilterChain) {
  inplaceInitialize(true);
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  auto codec_client_default = createHttpCodec("alpndefault");
  Cleanup cleanup0([c_default = codec_client_default.get()]() { c_default->close(); });
  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap)
                                          -> void {
    auto default_filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_default_filter_chain();
    default_filter_chain->set_name("default_filter_chain_v3");
  });
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  auto codec_client_default_v3 = createHttpCodec("alpndefaultv3");

  Cleanup cleanup2([c_default_v3 = codec_client_default_v3.get()]() { c_default_v3->close(); });
  expectResponseHeaderConnectionClose(*codec_client_default, true);
  expectResponseHeaderConnectionClose(*codec_client_default_v3, false);
  expectConnectionServed();
}

// Verify that balancer is inherited. Test only default balancer because ExactConnectionBalancer
// is verified in filter chain add and delete test case.
TEST_P(LdsInplaceUpdateHttpIntegrationTest, OverlappingFilterChainServesNewConnection) {
  use_default_balancer_ = true;
  inplaceInitialize();

  auto codec_client_0 = createHttpCodec("alpn0");
  Cleanup cleanup([c0 = codec_client_0.get()]() { c0->close(); });
  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });

  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  expectResponseHeaderConnectionClose(*codec_client_0, false);
  expectConnectionServed();
}

// Verify default filter chain update is filter chain only update.
TEST_P(LdsInplaceUpdateHttpIntegrationTest, DefaultFilterChainUpdate) {}
INSTANTIATE_TEST_SUITE_P(IpVersions, LdsInplaceUpdateHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, LdsInplaceUpdateTcpProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

using LdsIntegrationTest = HttpProtocolIntegrationTest;

INSTANTIATE_TEST_SUITE_P(Protocols, LdsIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Sample test making sure our config framework correctly reloads listeners.
TEST_P(LdsIntegrationTest, ReloadConfig) {
  autonomous_upstream_ = true;
  initialize();
  // Given we're using LDS in this test, initialize() will not complete until
  // the initial LDS file has loaded.
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  // HTTP 1.0 is disabled by default.
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);

  // Create a new config with HTTP/1.0 proxying.
  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_http_protocol_options()->set_accept_http_10(true);
        hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
      });

  // Create an LDS response with the new config, and reload config.
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);

  // HTTP 1.0 should now be enabled.
  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response2, false);
  EXPECT_THAT(response2, HasSubstr("HTTP/1.0 200 OK\r\n"));
}

// Verify that a listener that goes through the individual warming path (not server init) is
// failed and removed correctly if there are issues with final pre-worker init.
TEST_P(LdsIntegrationTest, NewListenerWithBadPostListenSocketOption) {
  autonomous_upstream_ = true;
  initialize();
  // Given we're using LDS in this test, initialize() will not complete until
  // the initial LDS file has loaded.
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  // Reserve a port that we can then use on the integration listener with reuse_port.
  auto addr_socket =
      Network::Test::bindFreeLoopbackPort(version_, Network::Socket::Type::Stream, true);
  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_address()->mutable_socket_address()->set_port_value(
            addr_socket.second->connectionInfoProvider().localAddress()->ip()->port());
        auto socket_option = listener->add_socket_options();
        socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_LISTENING);
        socket_option->set_level(10000);     // Invalid level.
        socket_option->set_int_value(10000); // Invalid value.
      });
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_create_failure", 1);
}

// Verify the grpc cached logger is available after the initial logger filter is destroyed.
// Regression test for https://github.com/envoyproxy/envoy/issues/18066
TEST_P(LdsIntegrationTest, GrpcLoggerSurvivesAfterReloadConfig) {
  autonomous_upstream_ = true;
  // The grpc access logger connection never closes. It's ok to see an incomplete logging stream.
  autonomous_allow_incomplete_streams_ = true;

  const std::string grpc_logger_string = R"EOF(
    name: grpc_accesslog
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig
      common_config:
        log_name: bar
        transport_api_version: V3
        grpc_service:
          envoy_grpc:
            cluster_name: cluster_0
  )EOF";

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->set_stat_prefix("listener_0");
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) { TestUtility::loadFromYaml(grpc_logger_string, *hcm.add_access_log()); });
  initialize();
  // Given we're using LDS in this test, initialize() will not complete until
  // the initial LDS file has loaded.
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  // HTTP 1.1 is allowed and the connection is kept open until the listener update.
  std::string response;
  auto connection =
      createConnectionDriver(lookupPort("http"), "GET / HTTP/1.1\r\nHost: host\r\n\r\n",
                             [&response, &dispatcher = *dispatcher_](
                                 Network::ClientConnection&, const Buffer::Instance& data) -> void {
                               response.append(data.toString());
                               if (response.find("\r\n\r\n") != std::string::npos) {
                                 dispatcher.exit();
                               }
                             });
  connection->run();
  EXPECT_TRUE(response.find("HTTP/1.1 200") == 0);

  test_server_->waitForCounterEq("access_logs.grpc_access_log.logs_written", 1);

  // Create a new config with HTTP/1.0 proxying. The goal is to trigger a listener update.
  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_http_protocol_options()->set_accept_http_10(true);
        hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
      });

  // Create an LDS response with the new config, and reload config.
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 2);

  // Wait until the http 1.1 connection is destroyed due to the listener update. It indicates the
  // listener starts draining.
  test_server_->waitForGaugeEq("listener.listener_0.downstream_cx_active", 0);
  // Wait until all the draining filter chain is gone. It indicates the old listener and filter
  // chains are destroyed.
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);

  // Verify that the new listener config is applied.
  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response2, true);
  EXPECT_THAT(response2, HasSubstr("HTTP/1.0 200 OK\r\n"));

  // Verify that the grpc access logger is available after the listener update.
  test_server_->waitForCounterEq("access_logs.grpc_access_log.logs_written", 2);
}

// Sample test making sure our config framework informs on listener failure.
TEST_P(LdsIntegrationTest, FailConfigLoad) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    filter_chain->mutable_filters(0)->clear_typed_config();
    filter_chain->mutable_filters(0)->set_name("grewgragra");
  });
  EXPECT_DEATH(initialize(), "Didn't find a registered implementation for name: 'grewgragra'");
}

// This test case uses `SimulatedTimeSystem` to stack two listener update in the same time point.
class LdsStsIntegrationTest : public Event::SimulatedTimeSystem,
                              public LdsInplaceUpdateTcpProxyIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(IpVersions, LdsStsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that the listener in place update will accomplish anyway if the listener is removed.
TEST_P(LdsStsIntegrationTest, TcpListenerRemoveFilterChainCalledAfterListenerIsRemoved) {
  // The in place listener update takes 2 seconds. We will remove the listener.
  drain_time_ = std::chrono::seconds(2);
  // 1. Start the first in place listener update.
  setUpstreamCount(2);
  initialize();
  std::string response_0;
  auto client_conn_0 = createConnectionAndWrite("alpn0", "hello", response_0);
  client_conn_0->waitForConnection();
  FakeRawConnectionPtr fake_upstream_connection_0;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_0));

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });
  new_config_helper.setLds("1");

  // 2. Remove the tcp listener immediately. This listener update should stack in the same poller
  // cycle so that this listener update has the same time stamp as the first update.
  ConfigHelper new_config_helper1(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper1.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        bootstrap.mutable_static_resources()->mutable_listeners(0)->Swap(
            bootstrap.mutable_static_resources()->mutable_listeners(1));
        bootstrap.mutable_static_resources()->mutable_listeners()->RemoveLast();
      });
  new_config_helper1.setLds("2");

  std::string observed_data_0;
  ASSERT_TRUE(fake_upstream_connection_0->waitForData(5, &observed_data_0));
  EXPECT_EQ("hello", observed_data_0);

  ASSERT_TRUE(fake_upstream_connection_0->write("world"));
  while (response_0.find("world") == std::string::npos) {
    client_conn_0->run(Event::Dispatcher::RunType::NonBlock);
  }
  client_conn_0->close();
  while (!client_conn_0->closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Wait for the filter chain removal start. Ideally we have `drain_time_` to detect the
  // value 1. Increase the drain_time_ at the beginning of the test if the test is flaky.
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 1);
  // Wait for the filter chain removal at worker thread. When the value drops from 1, all pending
  // removal at the worker is completed. This is the end of the in place update.
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);
}
} // namespace
} // namespace Envoy
