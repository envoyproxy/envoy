#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/filters/test_listener_filter.h"
#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
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
    skip_tag_extraction_rule_check_ = false;
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
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public BaseIntegrationTest {
public:
  LdsInplaceUpdateTcpProxyIntegrationTest()
      : BaseIntegrationTest(std::get<0>(GetParam()), ConfigHelper::baseConfig() +
                                                         (std::get<1>(GetParam()) ? R"EOF(
    filter_chain_matcher:
      matcher_tree:
        input:
          name: alpn
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.ApplicationProtocolInput
        exact_match_map:
          map:
            "'alpn0'":
              action:
                name: foo
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: foo
            "'alpn1'":
              action:
                name: bar
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: bar
)EOF"
                                                                                  : "") +
                                                         R"EOF(
    filter_chains:
    - filter_chain_match:
        application_protocols: ["alpn0"]
      name: foo
      filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
    - filter_chain_match:
        application_protocols: ["alpn1"]
      name: bar
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
)EOF"),
        matcher_(std::get<1>(GetParam())) {}

  void initialize() override {
    config_helper_.renameListener("tcp");
    config_helper_.addListenerFilter(ConfigHelper::testInspectorFilter());

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
        server_factory_context_);
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
  }

  std::unique_ptr<RawConnectionDriver> createConnectionAndWrite(const std::string& alpn,
                                                                const std::string& request,
                                                                std::string& response) {
    Buffer::OwnedImpl buffer(request);
    TestListenerFilter::setAlpn(alpn);
    return std::make_unique<RawConnectionDriver>(
        lookupPort("tcp"), buffer,
        [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
          response.append(data.toString());
        },
        version_, *dispatcher_,
        context_->createTransportSocket(
            std::make_shared<Network::TransportSocketOptionsImpl>(
                absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{alpn}),
            nullptr));
  }

  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::UpstreamTransportSocketFactoryPtr context_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  bool matcher_;
};

// Verify that tcp connection 1 is closed while client 0 survives when deleting filter chain 1.
TEST_P(LdsInplaceUpdateTcpProxyIntegrationTest, ReloadConfigDeletingFilterChain) {
  setUpstreamCount(2);
  initialize();
  std::string response_0;
  auto client_conn_0 = createConnectionAndWrite("alpn0", "hello", response_0);
  ASSERT_TRUE(client_conn_0->waitForConnection());
  FakeRawConnectionPtr fake_upstream_connection_0;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_0));

  std::string response_1;
  auto client_conn_1 = createConnectionAndWrite("alpn1", "dummy", response_1);
  ASSERT_TRUE(client_conn_1->waitForConnection());
  FakeRawConnectionPtr fake_upstream_connection_1;
  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_upstream_connection_1));

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
        if (matcher_) {
          TestUtility::loadFromYaml(R"EOF(
      matcher_tree:
        input:
          name: alpn
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.ApplicationProtocolInput
        exact_match_map:
          map:
            "'alpn0'":
              action:
                name: foo
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: foo
          )EOF",
                                    *listener->mutable_filter_chain_matcher());
        }
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
    ASSERT_TRUE(client_conn_0->run(Event::Dispatcher::RunType::NonBlock));
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
  ASSERT_TRUE(client_conn_0->waitForConnection());
  FakeRawConnectionPtr fake_upstream_connection_0;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_0));

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->Add()->MergeFrom(*listener->mutable_filter_chains(1));
        *listener->mutable_filter_chains(2)
             ->mutable_filter_chain_match()
             ->mutable_application_protocols(0) = "alpn2";
        listener->mutable_filter_chains(2)->set_name("baz");
        if (matcher_) {
          TestUtility::loadFromYaml(R"EOF(
      matcher_tree:
        input:
          name: alpn
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.ApplicationProtocolInput
        exact_match_map:
          map:
            "'alpn2'":
              action:
                name: baz
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: baz
          )EOF",
                                    *listener->mutable_filter_chain_matcher());
        }
      });
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  std::string response_2;
  auto client_conn_2 = createConnectionAndWrite("alpn2", "hello2", response_2);
  ASSERT_TRUE(client_conn_2->waitForConnection());
  FakeRawConnectionPtr fake_upstream_connection_2;
  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_upstream_connection_2));
  std::string observed_data_2;
  ASSERT_TRUE(fake_upstream_connection_2->waitForData(6, &observed_data_2));
  EXPECT_EQ("hello2", observed_data_2);

  ASSERT_TRUE(fake_upstream_connection_2->write("world2"));
  while (response_2.find("world2") == std::string::npos) {
    ASSERT_TRUE(client_conn_2->run(Event::Dispatcher::RunType::NonBlock));
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
    ASSERT_TRUE(client_conn_0->run(Event::Dispatcher::RunType::NonBlock));
  }
  client_conn_0->close();
  while (!client_conn_0->closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

class LdsInplaceUpdateHttpIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public HttpIntegrationTest {
public:
  LdsInplaceUpdateHttpIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())),
        matcher_(std::get<1>(GetParam())) {}

  void inplaceInitialize(bool add_default_filter_chain = false) {
    autonomous_upstream_ = true;
    setUpstreamCount(2);

    config_helper_.renameListener("http");
    config_helper_.addListenerFilter(ConfigHelper::testInspectorFilter());
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
      filter_chain_0->set_name("alpn0");
      auto* filter_chain_1 = bootstrap.mutable_static_resources()
                                 ->mutable_listeners(0)
                                 ->mutable_filter_chains()
                                 ->Add();
      filter_chain_1->MergeFrom(*filter_chain_0);

      // filter chain 1
      // alpn1, route to cluster_1
      *filter_chain_1->mutable_filter_chain_match()->mutable_application_protocols(0) = "alpn1";
      filter_chain_1->set_name("alpn1");

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
        default_filter_chain->set_name("default");
      }
      if (matcher_) {
        TestUtility::loadFromYaml(R"EOF(
      matcher_tree:
        input:
          name: alpn
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.ApplicationProtocolInput
        exact_match_map:
          map:
            "'alpn0'":
              action:
                name: alpn0
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: alpn0
            "'alpn1'":
              action:
                name: alpn1
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: alpn1
            "'alpn2'":
              action:
                name: alpn2
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: alpn2
          )EOF",
                                  *bootstrap.mutable_static_resources()
                                       ->mutable_listeners(0)
                                       ->mutable_filter_chain_matcher());
      }
    });

    BaseIntegrationTest::initialize();

    context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
        server_factory_context_);
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
    address_ = Ssl::getSslAddress(version_, lookupPort("http"));
  }

  IntegrationCodecClientPtr createHttpCodec(const std::string& alpn) {
    TestListenerFilter::setAlpn(alpn);
    auto ssl_conn = dispatcher_->createClientConnection(
        address_, Network::Address::InstanceConstSharedPtr(),
        context_->createTransportSocket(
            std::make_shared<Network::TransportSocketOptionsImpl>(
                absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{alpn}),
            nullptr),
        nullptr, nullptr);
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
  Network::UpstreamTransportSocketFactoryPtr context_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  Network::Address::InstanceConstSharedPtr address_;
  bool use_default_balancer_{false};
  bool matcher_;
};

// Verify that http response on filter chain 1 and default filter chain have "Connection: close"
// header when these 2 filter chains are deleted during the listener update.
TEST_P(LdsInplaceUpdateHttpIntegrationTest, ReloadConfigDeletingFilterChain) {
  inplaceInitialize(/*add_default_filter_chain=*/true);

  auto codec_client_1 = createHttpCodec("alpn1");
  auto codec_client_0 = createHttpCodec("alpn0");
  auto codec_client_default = createHttpCodec("alpndefault");

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
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

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap)
                                          -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    // Note that HCM2 copies the stats prefix from HCM0
    listener->mutable_filter_chains()->Add()->MergeFrom(*listener->mutable_filter_chains(0));
    *listener->mutable_filter_chains(2)
         ->mutable_filter_chain_match()
         ->mutable_application_protocols(0) = "alpn2";
    listener->mutable_filter_chains(2)->set_name("alpn2");

    auto default_filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_default_filter_chain();
    default_filter_chain->MergeFrom(*listener->mutable_filter_chains(1));
    default_filter_chain->set_name("default");
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
  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
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
  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
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
INSTANTIATE_TEST_SUITE_P(
    IpVersionsAndMatcher, LdsInplaceUpdateHttpIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(false, true)));

INSTANTIATE_TEST_SUITE_P(
    IpVersionsAndMatcher, LdsInplaceUpdateTcpProxyIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(false, true)));

using LdsIntegrationTest = HttpProtocolIntegrationTest;

INSTANTIATE_TEST_SUITE_P(Protocols, LdsIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Sample test making sure our config framework correctly reloads listeners.
TEST_P(LdsIntegrationTest, ReloadConfig) {
  config_helper_.disableDelayClose();
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
  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
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
  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* new_listener = bootstrap.mutable_static_resources()->add_listeners();
        new_listener->MergeFrom(bootstrap.static_resources().listeners(0));
        new_listener->set_name("new_listener");
        new_listener->mutable_address()->mutable_socket_address()->set_port_value(
            addr_socket.second->connectionInfoProvider().localAddress()->ip()->port());
        auto socket_option = new_listener->add_socket_options();
        socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_LISTENING);
        socket_option->set_level(10000);     // Invalid level.
        socket_option->set_int_value(10000); // Invalid value.
      });
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_create_failure", 1);
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

INSTANTIATE_TEST_SUITE_P(
    IpVersionsAndMatcher, LdsStsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(false, true)));

// Verify that the listener in place update will accomplish anyway if the listener is removed.
// https://github.com/envoyproxy/envoy/issues/22489
TEST_P(LdsStsIntegrationTest, DISABLED_TcpListenerRemoveFilterChainCalledAfterListenerIsRemoved) {
  // The in place listener update takes 2 seconds. We will remove the listener.
  drain_time_ = std::chrono::seconds(2);
  // 1. Start the first in place listener update.
  setUpstreamCount(2);
  initialize();
  std::string response_0;
  auto client_conn_0 = createConnectionAndWrite("alpn0", "hello", response_0);
  ASSERT_TRUE(client_conn_0->waitForConnection());
  FakeRawConnectionPtr fake_upstream_connection_0;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_0));

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });
  new_config_helper.setLds("1");

  // 2. Remove the tcp listener immediately. This listener update should stack in the same poller
  // cycle so that this listener update has the same time stamp as the first update.
  ConfigHelper new_config_helper1(version_, config_helper_.bootstrap());
  while (!client_conn_0->closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Wait for the filter chain removal at worker thread. When the value drops from 1, all pending
  // removal at the worker is completed. This is the end of the in place update.
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);
}

constexpr char XDS_CLUSTER_NAME_1[] = "xds_cluster_1.lyft.com";
constexpr char XDS_CLUSTER_NAME_2[] = "xds_cluster_2.lyft.com";
constexpr char CLIENT_CERT_NAME[] = "client_cert";

class XdsSotwMultipleAuthoritiesTest : public HttpIntegrationTest,
                                       public Grpc::GrpcClientIntegrationParamTest {
public:
  XdsSotwMultipleAuthoritiesTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::baseConfigNoListeners()) {
    use_lds_ = false;
    sotw_or_delta_ = Grpc::SotwOrDelta::Sotw;
    skip_tag_extraction_rule_check_ = true;

    // Make the default cluster HTTP2.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ConfigHelper::setHttp2(*bootstrap.mutable_static_resources()->mutable_clusters(0));
    });

    // Add a second cluster.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      *cluster = bootstrap.mutable_static_resources()->clusters(0);
      cluster->set_name("cluster_1");
      cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
    });

    // Add two xDS clusters.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      addXdsCluster(bootstrap, std::string(XDS_CLUSTER_NAME_1));
      addXdsCluster(bootstrap, std::string(XDS_CLUSTER_NAME_2));
    });

    // Set up the two static clusters with SSL using SDS.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      setUpClusterSsl(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                      std::string(XDS_CLUSTER_NAME_1), getXdsUpstream1());
      setUpClusterSsl(*bootstrap.mutable_static_resources()->mutable_clusters(1),
                      std::string(XDS_CLUSTER_NAME_2), getXdsUpstream2());
    });
  }

  void initialize() override {
    HttpIntegrationTest::initialize();
    registerTestServerPorts({});
  }

  void TearDown() override {
    closeConnection(xds_connection_1_);
    closeConnection(xds_connection_2_);
    cleanupUpstreamAndDownstream();
    codec_client_.reset();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    // Static cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
    // Static cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
    // XDS Cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
    // XDS Cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
  }

protected:
  FakeUpstream& getXdsUpstream1() { return *fake_upstreams_[2]; }
  FakeUpstream& getXdsUpstream2() { return *fake_upstreams_[3]; }

  void addXdsCluster(envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                     const std::string& cluster_name) {
    auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
    xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    xds_cluster->set_name(cluster_name);
    xds_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
    ConfigHelper::setHttp2(*xds_cluster);
  }

  void setUpClusterSsl(envoy::config::cluster::v3::Cluster& cluster,
                       const std::string& cluster_name, FakeUpstream& cluster_upstream) {
    auto* transport_socket = cluster.mutable_transport_socket();
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
    tls_context.set_sni("lyft.com");
    auto* secret_config =
        tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
    setUpSdsConfig(secret_config, CLIENT_CERT_NAME, cluster_name, cluster_upstream);
    transport_socket->set_name("envoy.transport_sockets.tls");
    transport_socket->mutable_typed_config()->PackFrom(tls_context);
  }

  void initXdsStream(FakeUpstream& upstream, FakeHttpConnectionPtr& connection,
                     FakeStreamPtr& stream) {
    AssertionResult result = upstream.waitForHttpConnection(*dispatcher_, connection);
    RELEASE_ASSERT(result, result.message());
    result = connection->waitForNewStream(*dispatcher_, stream);
    RELEASE_ASSERT(result, result.message());
    stream->startGrpcStream();
  }

  void closeConnection(FakeHttpConnectionPtr& connection) {
    AssertionResult result = connection->close();
    RELEASE_ASSERT(result, result.message());
    result = connection->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
    connection.reset();
  }

  void setUpSdsConfig(envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig* secret_config,
                      const std::string& secret_name, const std::string& cluster_name,
                      FakeUpstream& cluster_upstream) {
    secret_config->set_name(secret_name);
    auto* config_source = secret_config->mutable_sds_config();
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, cluster_name, cluster_upstream.localAddress());
  }

  envoy::extensions::transport_sockets::tls::v3::Secret
  getClientSecret(const std::string& secret_name) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(secret_name);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    return secret;
  }

  envoy::admin::v3::ConfigDump getSecretsFromConfigDump() {
    auto response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", "/config_dump?resource=dynamic_active_secrets", "",
        downstreamProtocol(), version_);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(response->body());
    envoy::admin::v3::ConfigDump config_dump;
    TestUtility::loadFromJson(loader->asJsonString(), config_dump);
    return config_dump;
  }

  FakeHttpConnectionPtr xds_connection_1_;
  FakeStreamPtr xds_stream_1_;
  FakeHttpConnectionPtr xds_connection_2_;
  FakeStreamPtr xds_stream_2_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, XdsSotwMultipleAuthoritiesTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Verifies that if two different xDS servers send resources of the same name and same type, Envoy
// still treats them as two separate resources.
TEST_P(XdsSotwMultipleAuthoritiesTest, SameResourceNameAndTypeFromMultipleAuthorities) {
  const std::string cert_name{CLIENT_CERT_NAME};

  on_server_init_function_ = [this, &cert_name]() {
    {
      // SDS for the first cluster.
      initXdsStream(getXdsUpstream1(), xds_connection_1_, xds_stream_1_);
      EXPECT_TRUE(compareSotwDiscoveryRequest(
          /*expected_type_url=*/Config::TypeUrl::get().Secret,
          /*expected_version=*/"",
          /*expected_resource_names=*/{cert_name}, /*expect_node=*/true,
          Grpc::Status::WellKnownGrpcStatus::Ok,
          /*expected_error_message=*/"", xds_stream_1_.get()));
      auto sds_resource = getClientSecret(cert_name);
      sendSotwDiscoveryResponse<envoy::extensions::transport_sockets::tls::v3::Secret>(
          Config::TypeUrl::get().Secret, {sds_resource}, "1", xds_stream_1_.get());
    }
    {
      // SDS for the second cluster.
      initXdsStream(getXdsUpstream2(), xds_connection_2_, xds_stream_2_);
      EXPECT_TRUE(compareSotwDiscoveryRequest(
          /*expected_type_url=*/Config::TypeUrl::get().Secret,
          /*expected_version=*/"",
          /*expected_resource_names=*/{cert_name}, /*expect_node=*/true,
          Grpc::Status::WellKnownGrpcStatus::Ok,
          /*expected_error_message=*/"", xds_stream_2_.get()));
      auto sds_resource = getClientSecret(cert_name);
      sendSotwDiscoveryResponse<envoy::extensions::transport_sockets::tls::v3::Secret>(
          Config::TypeUrl::get().Secret, {sds_resource}, "1", xds_stream_2_.get());
    }
  };

  initialize();

  // Wait until the discovery responses have been processed.
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);
  test_server_->waitForCounterGe(
      "cluster.cluster_1.client_ssl_socket_factory.ssl_context_update_by_sds", 1);

  auto config_dump = getSecretsFromConfigDump();
  // Two xDS resources with the same name and same type.
  ASSERT_EQ(config_dump.configs_size(), 2);
  envoy::admin::v3::SecretsConfigDump::DynamicSecret dynamic_secret;
  ASSERT_OK(MessageUtil::unpackTo(config_dump.configs(0), dynamic_secret));
  EXPECT_EQ(cert_name, dynamic_secret.name());
  EXPECT_EQ("1", dynamic_secret.version_info());
  ASSERT_OK(MessageUtil::unpackTo(config_dump.configs(1), dynamic_secret));
  EXPECT_EQ(cert_name, dynamic_secret.name());
  EXPECT_EQ("1", dynamic_secret.version_info());
}

} // namespace
} // namespace Envoy
