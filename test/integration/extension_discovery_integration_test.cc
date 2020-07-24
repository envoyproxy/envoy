#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

std::string denyPrivateConfig() {
  return R"EOF(
      rules:
        action: DENY
        policies:
          "test":
            permissions:
              - url_path: { path: { prefix: "/private" } }
            principals:
              - any: true
)EOF";
}

std::string allowAllConfig() {
  return R"EOF(
      rules:
        action: ALLOW
        policies:
          "test":
            permissions:
              - any: true
            principals:
              - any: true
)EOF";
}

std::string invalidConfig() {
  return R"EOF(
      rules:
        action: DENY
        policies:
          "test": {}
)EOF";
}

class ExtensionDiscoveryIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                          public HttpIntegrationTest {
public:
  ExtensionDiscoveryIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false) {
    config_helper_.addConfigModifier(
        [this, name, apply_without_warming, set_default_config, rate_limit](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                http_connection_manager) {
          auto* filter = http_connection_manager.mutable_http_filters()->Add();
          filter->set_name(name);
          auto* discovery = filter->mutable_config_discovery();
          discovery->add_type_urls(
              "type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC");
          if (set_default_config) {
            const auto rbac_configuration =
                TestUtility::parseYaml<envoy::extensions::filters::http::rbac::v3::RBAC>(R"EOF(
                rules:
                  action: DENY
                  policies:
                    "test":
                      permissions:
                        - any: true
                      principals:
                        - any: true
              )EOF");
            discovery->mutable_default_config()->PackFrom(rbac_configuration);
          }
          discovery->set_apply_default_config_without_warming(apply_without_warming);
          auto* api_config_source = discovery->mutable_config_source()->mutable_api_config_source();
          api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
          api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
          if (rate_limit) {
            api_config_source->mutable_rate_limit_settings()->mutable_max_tokens()->set_value(10);
          }
          auto* grpc_service = api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "ecds_cluster", getEcdsFakeUpstream().localAddress());
          // keep router the last
          auto size = http_connection_manager.http_filters_size();
          http_connection_manager.mutable_http_filters()->SwapElements(size - 2, size - 1);
        });
  }

  void initialize() override {
    defer_listener_finalization_ = true;
    setUpstreamCount(1);
    // Add an xDS cluster for extension config discovery.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ecds_cluster->set_name("ecds_cluster");
      ecds_cluster->mutable_http2_protocol_options();
    });
    // Make HCM do a direct response to avoid timing issues with the upstream.
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               http_connection_manager) {
          http_connection_manager.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_direct_response()
              ->set_status(200);
        });
    HttpIntegrationTest::initialize();
  }

  ~ExtensionDiscoveryIntegrationTest() override {
    if (ecds_connection_ != nullptr) {
      AssertionResult result = ecds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = ecds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      ecds_connection_.reset();
    }
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the extension config discovery upstream (fake_upstreams_[1]).
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_,
                                                  timeSystem(), enable_half_close_));
    for (auto& upstream : fake_upstreams_) {
      upstream->set_allow_unexpected_disconnects(true);
    }
  }

  void waitXdsStream() {
    auto& upstream = getEcdsFakeUpstream();
    AssertionResult result = upstream.waitForHttpConnection(*dispatcher_, ecds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = ecds_connection_->waitForNewStream(*dispatcher_, ecds_stream_);
    RELEASE_ASSERT(result, result.message());
    ecds_stream_->startGrpcStream();
  }

  void sendXdsResponse(const std::string& name, const std::string& version,
                       const std::string& rbac_config) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    const auto rbac_configuration =
        TestUtility::parseYaml<envoy::extensions::filters::http::rbac::v3::RBAC>(rbac_config);
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    typed_config.mutable_typed_config()->PackFrom(rbac_configuration);
    response.add_resources()->PackFrom(typed_config);
    ecds_stream_->sendGrpcMessage(response);
  }

  FakeUpstream& getEcdsFakeUpstream() const { return *fake_upstreams_[1]; }

  FakeHttpConnectionPtr ecds_connection_{nullptr};
  FakeStreamPtr ecds_stream_{nullptr};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ExtensionDiscoveryIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendXdsResponse("foo", "1", denyPrivateConfig());
  test_server_->waitForCounterGe("http.config_test.extension_config_discovery.foo.config_reload",
                                 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    response->waitForEndStream();
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
  Http::TestRequestHeaderMapImpl banned_request_headers{
      {":method", "GET"}, {":path", "/private/key"}, {":scheme", "http"}, {":authority", "host"}};
  {
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    response->waitForEndStream();
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }
  // Update again but keep the connection.
  {
    sendXdsResponse("foo", "2", allowAllConfig());
    test_server_->waitForCounterGe("http.config_test.extension_config_discovery.foo.config_reload",
                                   2);
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    response->waitForEndStream();
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicFailWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendXdsResponse("foo", "1", invalidConfig());
  test_server_->waitForCounterGe("http.config_test.extension_config_discovery.foo.config_fail", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicFailWithoutDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false, false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendXdsResponse("foo", "1", invalidConfig());
  test_server_->waitForCounterGe("http.config_test.extension_config_discovery.foo.config_fail", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicWithoutWarming) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("bar", true);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  registerTestServerPorts({"http"});
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  // Initial request uses the default config.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  {
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    response->waitForEndStream();
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }

  // Update should cause a different response.
  sendXdsResponse("bar", "1", denyPrivateConfig());
  test_server_->waitForCounterGe("http.config_test.extension_config_discovery.bar.config_reload",
                                 1);
  {
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    response->waitForEndStream();
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicWithoutWarmingFail) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("bar", true);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  registerTestServerPorts({"http"});
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  // Update should not cause a different response.
  sendXdsResponse("bar", "1", invalidConfig());
  test_server_->waitForCounterGe("http.config_test.extension_config_discovery.bar.config_fail", 1);
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicTwoSubscriptionsSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("baz", true);
  addDynamicFilter("baz", false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendXdsResponse("baz", "1", denyPrivateConfig());
  test_server_->waitForCounterGe("http.config_test.extension_config_discovery.baz.config_reload",
                                 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(ExtensionDiscoveryIntegrationTest, DestroyDuringInit) {
  // If rate limiting is enabled on the config source, gRPC mux drainage updates the requests
  // queue size on destruction. The update calls out to stats scope nested under the extension
  // config subscription stats scope. This test verifies that the stats scope outlasts the gRPC
  // subscription.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false, true);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  test_server_.reset();
  auto result = ecds_connection_->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
  ecds_connection_.reset();
}

} // namespace
} // namespace Envoy
