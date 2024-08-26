#include "envoy/extensions/common/matching/v3/extension_matcher.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/filters/set_is_terminal_filter_config.pb.h"
#include "test/integration/filters/set_response_code_filter_config.pb.h"
#include "test/integration/filters/test_listener_filter.pb.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

std::string denyPrivateConfig() {
  return R"EOF(
    prefix: "/private"
    code: 403
)EOF";
}

std::string denyPrivateConfigWithMatcher() {
  return R"EOF(
    "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
    extension_config:
      name: response-filter-config
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
        prefix: "/private"
        code: 403
    xds_matcher:
      matcher_tree:
        input:
          name: request-headers
          typed_config:
            "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
            header_name: some-header
        exact_match_map:
          map:
            match:
              action:
                name: skip
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
  )EOF";
}

std::string allowAllConfig() { return "code: 200"; }

std::string invalidConfig() { return "code: 90"; }

std::string terminalFilterConfig() { return "is_terminal_filter: true"; }

class ExtensionDiscoveryIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                          public HttpIntegrationTest {
public:
  ExtensionDiscoveryIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat
    // 'listener_manager.lds.grpc.lds_cluster.streams_closed_10' and stat_prefix 'lds_cluster'.
    skip_tag_extraction_rule_check_ = true;
  }

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        bool use_default_matcher = false) {
    config_helper_.addConfigModifier(
        [this, name, apply_without_warming, set_default_config, rate_limit, use_default_matcher](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                http_connection_manager) {
          auto* filter = http_connection_manager.mutable_http_filters()->Add();
          filter->set_name(name);
          auto* discovery = filter->mutable_config_discovery();
          discovery->add_type_urls(
              "type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig");
          discovery->add_type_urls(
              "type.googleapis.com/test.integration.filters.SetIsTerminalFilterConfig");
          discovery->add_type_urls(
              "type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher");
          if (set_default_config) {
            if (use_default_matcher) {
              const auto default_configuration = TestUtility::parseYaml<
                  envoy::extensions::common::matching::v3::ExtensionWithMatcher>(
                  R"EOF(
                    extension_config:
                      name: set-response-code
                      typed_config:
                        "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
                        code: 403
                    xds_matcher:
                      matcher_tree:
                        input:
                          name: request-headers
                          typed_config:
                            "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                            header_name: default-matcher-header
                        exact_match_map:
                          map:
                            match:
                              action:
                                name: skip
                                typed_config:
                                  "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
                  )EOF");

              discovery->mutable_default_config()->PackFrom(default_configuration);
            } else {
              const auto default_configuration =
                  TestUtility::parseYaml<test::integration::filters::SetResponseCodeFilterConfig>(
                      "code: 403");
              discovery->mutable_default_config()->PackFrom(default_configuration);
            }
          }
          discovery->set_apply_default_config_without_warming(apply_without_warming);
          discovery->mutable_config_source()->set_resource_api_version(
              envoy::config::core::v3::ApiVersion::V3);
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

  void addEcdsCluster(const std::string& cluster_name) {
    // Add an xDS cluster for extension config discovery.
    config_helper_.addConfigModifier(
        [cluster_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ecds_cluster->set_name(cluster_name);
          ConfigHelper::setHttp2(*ecds_cluster);
        });
  }

  void addDynamicListenerFilter(const std::string& name) {
    config_helper_.addConfigModifier(
        [name, this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* listener_filter = listener->add_listener_filters();
          listener_filter->set_name(name);
          auto* discovery = listener_filter->mutable_config_discovery();
          discovery->add_type_urls(
              "type.googleapis.com/test.integration.filters.TestTcpListenerFilterConfig");
          discovery->mutable_config_source()->set_resource_api_version(
              envoy::config::core::v3::ApiVersion::V3);
          auto* api_config_source = discovery->mutable_config_source()->mutable_api_config_source();
          api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
          api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
          auto* grpc_service = api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "ecds2_cluster", getEcds2FakeUpstream().localAddress());
        });
  }

  void initialize() override {
    defer_listener_finalization_ = true;
    setUpstreamCount(1);

    addEcdsCluster("ecds_cluster");
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
    // Use gRPC LDS instead of default file LDS.
    use_lds_ = false;
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      lds_cluster->set_name("lds_cluster");
      ConfigHelper::setHttp2(*lds_cluster);
    });

    // In case to configure both HTTP and Listener ECDS filters, adding the 2nd ECDS cluster.
    if (two_ecds_filters_) {
      addEcdsCluster("ecds2_cluster");
    }

    // Must be the last since it nukes static listeners.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
      listener_config_.set_name(listener_name_);
      ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());
      bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
      auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
      lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
      lds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      lds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
      envoy::config::core::v3::GrpcService* grpc_service =
          lds_api_config_source->add_grpc_services();
      setGrpcService(*grpc_service, "lds_cluster", getLdsFakeUpstream().localAddress());
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
    if (ecds2_connection_ != nullptr) {
      AssertionResult result = ecds2_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = ecds2_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      ecds2_connection_.reset();
    }
    if (lds_connection_ != nullptr) {
      AssertionResult result = lds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = lds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      lds_connection_.reset();
    }
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the extension config discovery upstream (fake_upstreams_[1]).
    addFakeUpstream(Http::CodecType::HTTP2);
    // Create the listener config discovery upstream (fake_upstreams_[2]).
    addFakeUpstream(Http::CodecType::HTTP2);
    if (two_ecds_filters_) {
      addFakeUpstream(Http::CodecType::HTTP2);
    }
  }

  // Wait for ECDS stream.
  void waitForEcdsStream(FakeUpstream& upstream, FakeHttpConnectionPtr& connection,
                         FakeStreamPtr& stream) {
    AssertionResult result = upstream.waitForHttpConnection(*dispatcher_, connection);
    ASSERT_TRUE(result);
    result = connection->waitForNewStream(*dispatcher_, stream);
    ASSERT_TRUE(result);
    stream->startGrpcStream();
  }

  void waitXdsStream() {
    // Wait for LDS stream.
    auto& lds_upstream = getLdsFakeUpstream();
    AssertionResult result = lds_upstream.waitForHttpConnection(*dispatcher_, lds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = lds_connection_->waitForNewStream(*dispatcher_, lds_stream_);
    RELEASE_ASSERT(result, result.message());
    lds_stream_->startGrpcStream();

    // Response with initial LDS.
    sendLdsResponse("initial");

    waitForEcdsStream(getEcdsFakeUpstream(), ecds_connection_, ecds_stream_);
    if (two_ecds_filters_) {
      // Wait for 2nd ECDS stream.
      waitForEcdsStream(getEcds2FakeUpstream(), ecds2_connection_, ecds2_stream_);
    }
  }

  void sendLdsResponse(const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().Listener);
    response.add_resources()->PackFrom(listener_config_);
    lds_stream_->sendGrpcMessage(response);
  }

  void sendEcdsResponse(const envoy::config::core::v3::TypedExtensionConfig& typed_config,
                        const std::string& name, const std::string& version, const bool ttl,
                        FakeStreamPtr& ecds_stream) {
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(name);
    if (ttl) {
      resource.mutable_ttl()->set_seconds(1);
    }
    resource.mutable_resource()->PackFrom(typed_config);

    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    response.add_resources()->PackFrom(resource);
    ecds_stream->sendGrpcMessage(response);
  }

  void sendHttpFilterEcdsResponse(const std::string& name, const std::string& version,
                                  const std::string& yaml_config, bool ttl = false,
                                  bool is_set_resp_code_config = true) {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    if (is_set_resp_code_config) {
      const auto configuration =
          TestUtility::parseYaml<test::integration::filters::SetResponseCodeFilterConfig>(
              yaml_config);
      typed_config.mutable_typed_config()->PackFrom(configuration);
    } else {
      const auto configuration =
          TestUtility::parseYaml<test::integration::filters::SetIsTerminalFilterConfig>(
              yaml_config);
      typed_config.mutable_typed_config()->PackFrom(configuration);
    }
    sendEcdsResponse(typed_config, name, version, ttl, ecds_stream_);
  }

  void sendListenerFilterEcdsResponse(const std::string& name, const std::string& version,
                                      const uint32_t drain_bytes) {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    auto configuration = test::integration::filters::TestTcpListenerFilterConfig();
    configuration.set_drain_bytes(drain_bytes);
    typed_config.mutable_typed_config()->PackFrom(configuration);
    sendEcdsResponse(typed_config, name, version, false, ecds2_stream_);
  }

  void sendHttpFilterEcdsResponseWithFullYaml(const std::string& name, const std::string& version,
                                              const std::string& full_yaml) {
    const auto configuration = TestUtility::parseYaml<ProtobufWkt::Any>(full_yaml);
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    typed_config.mutable_typed_config()->MergeFrom(configuration);
    sendEcdsResponse(typed_config, name, version, false, ecds_stream_);
  }

  absl::string_view request(const std::string port_key, const std::string method,
                            const std::string endpoint, BufferingStreamDecoderPtr& response) {
    response = IntegrationUtil::makeSingleRequest(lookupPort(port_key), method, endpoint, "",
                                                  Http::CodecType::HTTP1, version_);
    EXPECT_TRUE(response->complete());
    return response->headers().getStatusValue();
  }

  absl::string_view contentType(const BufferingStreamDecoderPtr& response) {
    const Http::HeaderEntry* entry = response->headers().ContentType();
    if (entry == nullptr) {
      return "(null)";
    }
    return entry->value().getStringView();
  }

  bool two_ecds_filters_{false};
  FakeUpstream& getEcdsFakeUpstream() const { return *fake_upstreams_[1]; }
  FakeUpstream& getLdsFakeUpstream() const { return *fake_upstreams_[2]; }
  FakeUpstream& getEcds2FakeUpstream() const { return *fake_upstreams_[3]; }

  // gRPC LDS set-up
  envoy::config::listener::v3::Listener listener_config_;
  std::string listener_name_{"testing-listener-0"};
  FakeHttpConnectionPtr lds_connection_{nullptr};
  FakeStreamPtr lds_stream_{nullptr};

  // gRPC ECDS set-up
  FakeHttpConnectionPtr ecds_connection_{nullptr};
  FakeStreamPtr ecds_stream_{nullptr};
  FakeHttpConnectionPtr ecds2_connection_{nullptr};
  FakeStreamPtr ecds2_stream_{nullptr};
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
  sendHttpFilterEcdsResponse("foo", "1", denyPrivateConfig());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
  Http::TestRequestHeaderMapImpl banned_request_headers{
      {":method", "GET"}, {":path", "/private/key"}, {":scheme", "http"}, {":authority", "host"}};
  {
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }
  // Update again but keep the connection.
  {
    sendHttpFilterEcdsResponse("foo", "2", allowAllConfig());
    test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 2);
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicSuccessWithTtl) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false, false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendHttpFilterEcdsResponse("foo", "1", denyPrivateConfig(), true);
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
  Http::TestRequestHeaderMapImpl banned_request_headers{
      {":method", "GET"}, {":path", "/private/key"}, {":scheme", "http"}, {":authority", "host"}};
  {
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }

  {
    // Wait until the the TTL for the resource expires, which will trigger a config load to remove
    // the resource.
    test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 2);
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("500", response->headers().getStatusValue());
  }

  {
    // Reinstate the previous configuration.
    sendHttpFilterEcdsResponse("foo", "1", denyPrivateConfig(), true);
    // Wait until the new configuration has been applied.
    test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 3);
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicSuccessWithTtlWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false, true);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendHttpFilterEcdsResponse("foo", "1", allowAllConfig(), true);
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl banned_request_headers{
      {":method", "GET"}, {":path", "/private/key"}, {":scheme", "http"}, {":authority", "host"}};
  {
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  {
    // Wait until the the TTL for the resource expires, which will trigger a config load to remove
    // the resource.
    test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 2);
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicSuccessWithMatcher) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendHttpFilterEcdsResponseWithFullYaml("foo", "1", denyPrivateConfigWithMatcher());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
  Http::TestRequestHeaderMapImpl banned_request_headers{
      {":method", "GET"}, {":path", "/private/key"}, {":scheme", "http"}, {":authority", "host"}};
  {
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }
  Http::TestRequestHeaderMapImpl banned_request_headers_skipped{{":method", "GET"},
                                                                {":path", "/private/key"},
                                                                {"some-header", "match"},
                                                                {":scheme", "http"},
                                                                {":authority", "host"}};
  {
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers_skipped);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicDefaultMatcher) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false, true, false, true);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendHttpFilterEcdsResponse("foo", "1", invalidConfig());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_fail", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {"default-matcher-header", "match"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Validate that a listener update reuses the extension configuration. Prior
// to https://github.com/envoyproxy/envoy/pull/15371, the updated listener
// would not use the subscribed extension configuration and would not trigger a
// fresh xDS request. See issue
// https://github.com/envoyproxy/envoy/issues/14934.
TEST_P(ExtensionDiscoveryIntegrationTest, ReuseExtensionConfig) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendHttpFilterEcdsResponse("foo", "1", allowAllConfig());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);

  // Update listener and expect it to be warm and use active configuration from the subscription
  // instead of the default config.
  listener_config_.set_traffic_direction(envoy::config::core::v3::TrafficDirection::OUTBOUND);
  sendLdsResponse("updated");
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_warming", 0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterEq("extension_config_discovery.http_filter.foo.config_conflict", 0);
}

// Validate that a listener update falls back to the default extension configuration
// if the subscribed extension configuration fails to satisfy the type URL constraint.
TEST_P(ExtensionDiscoveryIntegrationTest, ReuseExtensionConfigInvalid) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendHttpFilterEcdsResponseWithFullYaml("foo", "1", denyPrivateConfigWithMatcher());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);

  // Remove matcher filter type URL, invalidating the subscription last config.
  auto hcm_config = MessageUtil::anyConvert<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
      listener_config_.filter_chains(0).filters(0).typed_config());
  hcm_config.mutable_http_filters(0)->mutable_config_discovery()->mutable_type_urls()->Clear();
  hcm_config.mutable_http_filters(0)->mutable_config_discovery()->add_type_urls(
      "type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig");
  listener_config_.mutable_filter_chains(0)->mutable_filters(0)->mutable_typed_config()->PackFrom(
      hcm_config);

  sendLdsResponse("updated");
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_warming", 0);

  // Should be using the default config (403)
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_conflict", 1);
}

TEST_P(ExtensionDiscoveryIntegrationTest, BasicFailWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendHttpFilterEcdsResponse("foo", "1", invalidConfig());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_fail", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
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
  sendHttpFilterEcdsResponse("foo", "1", invalidConfig());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_fail", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
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
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }

  // Update should cause a different response.
  sendHttpFilterEcdsResponse("bar", "1", denyPrivateConfig());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.bar.config_reload", 1);
  {
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
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
  sendHttpFilterEcdsResponse("bar", "1", invalidConfig());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.bar.config_fail", 1);
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
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
  sendHttpFilterEcdsResponse("baz", "1", denyPrivateConfig());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.baz.config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
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

// Validate that a listener update should fail if the subscribed extension configuration make filter
// terminal but the filter position is not at the last position at filter chain.
TEST_P(ExtensionDiscoveryIntegrationTest, BasicFailTerminalFilterNotAtEndOfFilterChain) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false, false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendHttpFilterEcdsResponse("foo", "1", terminalFilterConfig(), false, false);
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_fail", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Validate that deleting listeners does not break active ECDS subscription.
// This test also verifies clean deletion of the filter config on the main thread.
TEST_P(ExtensionDiscoveryIntegrationTest, ReloadBoth) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
  sendHttpFilterEcdsResponse("foo", "1", denyPrivateConfig());
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  Http::TestRequestHeaderMapImpl banned_request_headers{
      {":method", "GET"}, {":path", "/private/key"}, {":scheme", "http"}, {":authority", "host"}};
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  {
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }
  codec_client_->close();

  // Rename the listener to force delete the first listener and wait for the deletion.
  listener_config_.set_name("updated");
  sendLdsResponse("updated");
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_warming", 0);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", 0);

  // Verify ECDS is still applied on the new listener.
  registerTestServerPorts({"http"});
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  {
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
  }

  // Update ECDS but keep the connection.
  {
    sendHttpFilterEcdsResponse("foo", "2", allowAllConfig());
    test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 2);
    auto response = codec_client_->makeHeaderOnlyRequest(banned_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
  codec_client_->close();
}

// ECDS config dump test with one listener ECDS filter and one HTTP ECDS filter.
TEST_P(ExtensionDiscoveryIntegrationTest, ConfigDumpWithTwoSubscriptionTypes) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  two_ecds_filters_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  // HTTP ECDS filter
  addDynamicFilter("foo", false);
  // Listener ECDS filter
  addDynamicListenerFilter("bar");
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});

  // Send configuration update for HTTP ECDS filter.
  sendHttpFilterEcdsResponse("foo", "1", denyPrivateConfig());
  // Send configuration update for listener ECDS filter.
  sendListenerFilterEcdsResponse("bar", "2", 7);
  test_server_->waitForCounterGe("extension_config_discovery.http_filter.foo.config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  // Get config_dump and verify HTTP and Listener ECDS filters are dumped correctly.
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump", response));
  EXPECT_EQ("application/json", contentType(response));
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  size_t index = 0;
  const std::string expected_types[] = {
      "type.googleapis.com/envoy.admin.v3.BootstrapConfigDump",
      "type.googleapis.com/envoy.admin.v3.ClustersConfigDump",
      "type.googleapis.com/envoy.admin.v3.EcdsConfigDump", // HTTP
      "type.googleapis.com/envoy.admin.v3.EcdsConfigDump", // TCP Listener
      "type.googleapis.com/envoy.admin.v3.ListenersConfigDump",
      "type.googleapis.com/envoy.admin.v3.ScopedRoutesConfigDump",
      "type.googleapis.com/envoy.admin.v3.RoutesConfigDump",
      "type.googleapis.com/envoy.admin.v3.SecretsConfigDump"};

  for (const Json::ObjectSharedPtr& obj_ptr : json->getObjectArray("configs")) {
    EXPECT_TRUE(expected_types[index].compare(obj_ptr->getString("@type")) == 0);
    index++;
  }

  // Validate we can parse as proto.
  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(8, config_dump.configs_size());

  // Unpack the HTTP filter.
  envoy::admin::v3::EcdsConfigDump ecds_config_dump_http;
  config_dump.configs(2).UnpackTo(&ecds_config_dump_http);
  EXPECT_EQ("1", ecds_config_dump_http.ecds_filters(0).version_info());
  envoy::config::core::v3::TypedExtensionConfig http_filter_config;
  EXPECT_TRUE(ecds_config_dump_http.ecds_filters(0).ecds_filter().UnpackTo(&http_filter_config));
  EXPECT_EQ("foo", http_filter_config.name());
  test::integration::filters::SetResponseCodeFilterConfig http_config;
  http_filter_config.typed_config().UnpackTo(&http_config);
  EXPECT_EQ("/private", http_config.prefix());
  EXPECT_EQ(403, http_config.code());

  // Unpack the listener filter.
  envoy::admin::v3::EcdsConfigDump ecds_config_dump_listener;
  config_dump.configs(3).UnpackTo(&ecds_config_dump_listener);
  EXPECT_EQ("2", ecds_config_dump_listener.ecds_filters(0).version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_config_dump_listener.ecds_filters(0).ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ("bar", filter_config.name());
  test::integration::filters::TestTcpListenerFilterConfig listener_config;
  filter_config.typed_config().UnpackTo(&listener_config);
  EXPECT_EQ(7, listener_config.drain_bytes());
}

} // namespace
} // namespace Envoy
