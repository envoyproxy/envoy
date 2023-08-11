#include <ostream>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/config/utility.h"
#include "test/integration/filters/add_header_filter.pb.h"
#include "test/integration/filters/repick_cluster_filter.h"
#include "test/integration/http_integration.h"

#include "http_integration.h"

namespace Envoy {
namespace {

constexpr absl::string_view EcdsClusterName = "ecds_cluster";
constexpr absl::string_view Ecds2ClusterName = "ecds2_cluster";

using HttpFilterProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;

class UpstreamHttpFilterIntegrationTestBase : public HttpIntegrationTest {
public:
  UpstreamHttpFilterIntegrationTestBase(Network::Address::IpVersion version)
      : HttpIntegrationTest(Http::CodecType::HTTP2, version) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    skip_tag_extraction_rule_check_ = true;
    autonomous_upstream_ = true;
  }

  void addStaticFilters(const std::vector<HttpFilterProto>& cluster_upstream_filters,
                        const std::vector<HttpFilterProto>& router_upstream_filters) {
    if (!cluster_upstream_filters.empty()) {
      config_helper_.addConfigModifier(
          [cluster_upstream_filters](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
            auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
            ConfigHelper::HttpProtocolOptions protocol_options =
                MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
                    (*cluster->mutable_typed_extension_protocol_options())
                        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
            for (const auto& filter : cluster_upstream_filters) {
              *protocol_options.add_http_filters() = filter;
            }
            (*cluster->mutable_typed_extension_protocol_options())
                ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                    .PackFrom(protocol_options);
            // std::cout << cluster->DebugString() << std::endl;
            // std::cout << bootstrap.DebugString() << std::endl;
          });
    }

    if (!router_upstream_filters.empty()) {
      config_helper_.addConfigModifier(
          [router_upstream_filters](envoy::extensions::filters::network::http_connection_manager::
                                        v3::HttpConnectionManager& hcm) -> void {
            HttpFilterProto& router_filter = *hcm.mutable_http_filters(0);
            ASSERT_EQ(router_filter.name(), "envoy.filters.http.router");
            envoy::extensions::filters::http::router::v3::Router router;
            router_filter.typed_config().UnpackTo(&router);
            for (const auto& filter : router_upstream_filters) {
              *router.add_upstream_http_filters() = filter;
            }
            router_filter.mutable_typed_config()->PackFrom(router);
            std::cout << hcm.DebugString() << std::endl;
          });
    }
  }
};

class StaticUpstreamHttpFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public UpstreamHttpFilterIntegrationTestBase {
public:
  StaticUpstreamHttpFilterIntegrationTest() : UpstreamHttpFilterIntegrationTestBase(GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, StaticUpstreamHttpFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StaticUpstreamHttpFilterIntegrationTest, ClusterOnlyFilters) {
  HttpFilterProto add_header_filter;
  add_header_filter.set_name("add-header-filter");
  HttpFilterProto codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  addStaticFilters({add_header_filter, codec_filter}, {});
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  std::cout << *upstream_headers;
  ASSERT_TRUE(upstream_headers != nullptr);
  cleanupUpstreamAndDownstream();
  EXPECT_FALSE(upstream_headers->get(Http::LowerCaseString("x-header-to-add")).empty());
}

TEST_P(StaticUpstreamHttpFilterIntegrationTest, RouterOnlyFilters) {
  HttpFilterProto add_header_filter;
  add_header_filter.set_name("add-header-filter");
  HttpFilterProto codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  addStaticFilters({}, {add_header_filter, codec_filter});
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  cleanupUpstreamAndDownstream();
  EXPECT_FALSE(upstream_headers->get(Http::LowerCaseString("x-header-to-add")).empty());
}

// Only cluster-specified filters should be applied.
TEST_P(StaticUpstreamHttpFilterIntegrationTest, RouterAndClusterFilters) {
  HttpFilterProto add_header_filter;
  add_header_filter.set_name("add-header-filter");
  HttpFilterProto add_body_filter;
  add_body_filter.set_name("add-body-filter");
  HttpFilterProto codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  addStaticFilters({add_header_filter, codec_filter}, {add_body_filter, codec_filter});
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  cleanupUpstreamAndDownstream();
  EXPECT_FALSE(upstream_headers->get(Http::LowerCaseString("x-header-to-add")).empty());
}

class UpstreamHttpExtensionDiscoveryIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                                      public UpstreamHttpFilterIntegrationTestBase {
public:
  UpstreamHttpExtensionDiscoveryIntegrationTest()
      : UpstreamHttpFilterIntegrationTestBase(ipVersion()) {}

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        bool second_connection = false) {
    config_helper_.addConfigModifier([name, apply_without_warming, set_default_config, rate_limit,
                                      second_connection,
                                      this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      ConfigHelper::HttpProtocolOptions protocol_options =
          MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
              (*cluster->mutable_typed_extension_protocol_options())
                  ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);

      auto* filter = protocol_options.add_http_filters();
      filter->set_name(name);

      auto* discovery = filter->mutable_config_discovery();
      discovery->add_type_urls(
          "type.googleapis.com/test.integration.filters.AddHeaderFilterConfig");
      if (set_default_config) {
        auto default_configuration = test::integration::filters::AddHeaderFilterConfig();
        default_configuration.set_header_key(header_key_);
        default_configuration.set_header_value(header_default_value_);
        discovery->mutable_default_config()->PackFrom(default_configuration);
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
      if (!second_connection) {
        setGrpcService(*grpc_service, std::string(EcdsClusterName),
                       getEcdsFakeUpstream().localAddress());
      } else {
        setGrpcService(*grpc_service, std::string(Ecds2ClusterName),
                       getEcds2FakeUpstream().localAddress());
      }

      (*cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
              .PackFrom(protocol_options);
    });
  }

  void addCodecFilter() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      HttpFilterProto codec_filter;
      codec_filter.set_name("envoy.filters.http.upstream_codec");
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      ConfigHelper::HttpProtocolOptions protocol_options =
          MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
              (*cluster->mutable_typed_extension_protocol_options())
                  ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
      *protocol_options.add_http_filters() = codec_filter;
      (*cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
              .PackFrom(protocol_options);
    });
  }

  void addEcdsCluster(const std::string& cluster_name) {
    // Add an xDS cluster for extension config discovery.
    config_helper_.addConfigModifier(
        [cluster_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ecds_cluster->set_name(cluster_name);
          ecds_cluster->mutable_typed_extension_protocol_options()->clear();
          ConfigHelper::setHttp2(*ecds_cluster);
        });
  }

  void initialize() override {
    defer_listener_finalization_ = true;
    setUpstreamCount(1);

    addEcdsCluster(std::string(EcdsClusterName));

    // Use gRPC LDS instead of default file LDS.
    use_lds_ = false;
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      lds_cluster->set_name("lds_cluster");
      lds_cluster->mutable_typed_extension_protocol_options()->clear();
      ConfigHelper::setHttp2(*lds_cluster);
    });

    // In case to configure both HTTP and Listener ECDS filters, adding the 2nd ECDS cluster.
    if (two_ecds_filters_) {
      addEcdsCluster(std::string(Ecds2ClusterName));
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

  void resetConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  ~UpstreamHttpExtensionDiscoveryIntegrationTest() override {
    resetConnection(ecds_connection_);
    resetConnection(lds_connection_);
    resetConnection(ecds2_connection_);
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

  void sendXdsResponse(const std::string& name, const std::string& version, const std::string& key,
                       const std::string& val, bool ttl = false, bool second_connection = false) {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);

    auto configuration = test::integration::filters::AddHeaderFilterConfig();
    configuration.set_header_key(key);
    configuration.set_header_value(val);
    typed_config.mutable_typed_config()->PackFrom(configuration);

    envoy::service::discovery::v3::Resource resource;
    resource.set_name(name);
    resource.mutable_resource()->PackFrom(typed_config);
    if (ttl) {
      resource.mutable_ttl()->set_seconds(1);
    }

    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    response.add_resources()->PackFrom(resource);

    if (!second_connection) {
      ecds_stream_->sendGrpcMessage(response);
    } else {
      ecds2_stream_->sendGrpcMessage(response);
    }
  }

  void assertHeaders(std::unique_ptr<Http::TestRequestHeaderMapImpl>& headers) {
    std::cout << *headers;
    ASSERT_TRUE(headers != nullptr);
  }

  void assertResponse(IntegrationStreamDecoderPtr& response) {
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
  }

  std::unique_ptr<Http::RequestHeaderMap> sentRequestAndGetHeaders() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    assertResponse(response);
    auto upstream_headers =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
    assertHeaders(upstream_headers);
    cleanupUpstreamAndDownstream();
    return upstream_headers;
  }

  void expectHeaderKeyAndValue(std::unique_ptr<Http::RequestHeaderMap>& headers,
                               const std::string value) {
    expectHeaderKeyAndValue(headers, header_key_, value);
  }

  void expectHeaderKeyAndValue(std::unique_ptr<Http::RequestHeaderMap>& headers,
                               const std::string key, const std::string value) {
    auto header = headers->get(Http::LowerCaseString(key));
    ASSERT_FALSE(header.empty());
    EXPECT_EQ(value, header[0]->value().getStringView());
  }

  const std::string header_key_ = "header-key";
  const std::string header_default_value_ = "default-value";

  const std::string filter_name_ = "foo";
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

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, UpstreamHttpExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(UpstreamHttpExtensionDiscoveryIntegrationTest, BasicWithoutWarming) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  registerTestServerPorts({"http"});

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  std::cout << *upstream_headers;
  ASSERT_TRUE(upstream_headers != nullptr);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  auto header = upstream_headers->get(Http::LowerCaseString(header_key_));
  ASSERT_FALSE(header.empty());
  EXPECT_EQ(header_default_value_, header[0]->value().getStringView());

  // Send 1st config update.
  sendXdsResponse(filter_name_, "1", header_key_, "test-val1");
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);
  {
    auto headers = sentRequestAndGetHeaders();
    expectHeaderKeyAndValue(headers, "test-val1");
  }

  // Send 2nd config update.
  sendXdsResponse(filter_name_, "1", header_key_, "test-val2");
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 2);
  {
    auto headers = sentRequestAndGetHeaders();
    expectHeaderKeyAndValue(headers, "test-val2");
  }
};

TEST_P(UpstreamHttpExtensionDiscoveryIntegrationTest, BasicWithoutWarmingWithConfigFail) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  registerTestServerPorts({"http"});

  // Send config update with invalid config (header value length has to >=2).
  sendXdsResponse(filter_name_, "1", header_key_, "x");
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_fail", 1);
  {
    auto headers = sentRequestAndGetHeaders();
    expectHeaderKeyAndValue(headers, header_default_value_);
  }
};

TEST_P(UpstreamHttpExtensionDiscoveryIntegrationTest, TwoSubscriptionsSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  registerTestServerPorts({"http"});

  sendXdsResponse(filter_name_, "1", header_key_, "test-val");
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);
  {
    auto headers = sentRequestAndGetHeaders();
    expectHeaderKeyAndValue(headers, "test-val,test-val");
  }
}

TEST_P(UpstreamHttpExtensionDiscoveryIntegrationTest, TwoSubscriptionsDifferentName) {
  two_ecds_filters_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", true, true, false, true);
  addCodecFilter();
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  registerTestServerPorts({"http"});
  {
    auto headers = sentRequestAndGetHeaders();
    expectHeaderKeyAndValue(headers, header_default_value_ + "," + header_default_value_);
  }

  // Send 1st config update.
  sendXdsResponse("foo", "1", "header-key1", "test-val1");
  sendXdsResponse("bar", "1", "header-key2", "test-val1", false, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_http_filter.foo.config_reload", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_http_filter.bar.config_reload", 1);
  {
    auto headers = sentRequestAndGetHeaders();
    expectHeaderKeyAndValue(headers, "header-key1", "test-val1");
    expectHeaderKeyAndValue(headers, "header-key2", "test-val1");
  }

  // Send 2nd config update.
  sendXdsResponse("foo", "2", "header-key1", "test-val2");
  sendXdsResponse("bar", "2", "header-key2", "test-val2", false, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_http_filter.foo.config_reload", 2);
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_http_filter.bar.config_reload", 2);
  {
    auto headers = sentRequestAndGetHeaders();
    expectHeaderKeyAndValue(headers, "header-key1", "test-val2");
    expectHeaderKeyAndValue(headers, "header-key2", "test-val2");
  }
}

} // namespace
} // namespace Envoy
