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
constexpr absl::string_view expected_types[] = {
    "type.googleapis.com/envoy.admin.v3.BootstrapConfigDump",
    "type.googleapis.com/envoy.admin.v3.ClustersConfigDump",
    "type.googleapis.com/envoy.admin.v3.EcdsConfigDump",
    "type.googleapis.com/envoy.admin.v3.ListenersConfigDump",
    "type.googleapis.com/envoy.admin.v3.ScopedRoutesConfigDump",
    "type.googleapis.com/envoy.admin.v3.RoutesConfigDump",
    "type.googleapis.com/envoy.admin.v3.SecretsConfigDump"};

using HttpFilterProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;

class UpstreamHttpFilterIntegrationTestBase : public HttpIntegrationTest {
public:
  UpstreamHttpFilterIntegrationTestBase(Network::Address::IpVersion version,
                                        bool use_router_filters)
      : HttpIntegrationTest(Http::CodecType::HTTP2, version),
        use_router_filters_(use_router_filters) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    skip_tag_extraction_rule_check_ = true;
    autonomous_upstream_ = true;
  }

  void addStaticClusterFilter(const HttpFilterProto& config) {
    config_helper_.addConfigModifier([config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      ConfigHelper::HttpProtocolOptions protocol_options =
          MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
              (*cluster->mutable_typed_extension_protocol_options())
                  ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
      *protocol_options.add_http_filters() = config;
      (*cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
              .PackFrom(protocol_options);
    });
  }

  void addStaticRouterFilter(const HttpFilterProto& config) {
    config_helper_.addConfigModifier(
        [config](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          HttpFilterProto& router_filter = *hcm.mutable_http_filters(0);
          ASSERT_EQ(router_filter.name(), "envoy.filters.http.router");
          envoy::extensions::filters::http::router::v3::Router router;
          router_filter.typed_config().UnpackTo(&router);
          *router.add_upstream_http_filters() = config;
          router_filter.mutable_typed_config()->PackFrom(router);
        });
  }

  const HttpFilterProto getAddHeaderFilterConfig(const std::string& name, const std::string& key,
                                                 const std::string& value) {
    HttpFilterProto filter_config;
    filter_config.set_name(name);
    auto configuration = test::integration::filters::AddHeaderFilterConfig();
    configuration.set_header_key(key);
    configuration.set_header_value(value);
    filter_config.mutable_typed_config()->PackFrom(configuration);
    return filter_config;
  }

  const HttpFilterProto getCodecFilterConfig() {
    HttpFilterProto filter_config;
    filter_config.set_name("envoy.filters.http.upstream_codec");
    auto configuration = envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec();
    filter_config.mutable_typed_config()->PackFrom(configuration);
    return filter_config;
  }

  void addStaticFilter(const std::string& name, const std::string& key, const std::string& value) {
    if (useRouterFilters()) {
      addStaticRouterFilter(getAddHeaderFilterConfig(name, key, value));
    } else {
      addStaticClusterFilter(getAddHeaderFilterConfig(name, key, value));
    }
  }

  void addCodecRouterFilter() { addStaticRouterFilter(getCodecFilterConfig()); }

  void addCodecClusterFilter() { addStaticClusterFilter(getCodecFilterConfig()); }

  void addCodecFilter() {
    if (useRouterFilters()) {
      addCodecRouterFilter();
    } else {
      addCodecClusterFilter();
    }
  }

  bool useRouterFilters() const { return use_router_filters_; }

  void expectHeaderKeyAndValue(std::unique_ptr<Http::RequestHeaderMap>& headers,
                               const std::string value) {
    expectHeaderKeyAndValue(headers, default_header_key_, value);
  }

  void expectHeaderKeyAndValue(std::unique_ptr<Http::RequestHeaderMap>& headers,
                               const std::string key, const std::string value) {
    auto header = headers->get(Http::LowerCaseString(key));
    ASSERT_FALSE(header.empty());
    EXPECT_EQ(value, header[0]->value().getStringView());
  }

  std::unique_ptr<Http::RequestHeaderMap> sendRequestAndGetHeaders() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    auto upstream_headers =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
    EXPECT_TRUE(upstream_headers != nullptr);
    cleanupUpstreamAndDownstream();
    return upstream_headers;
  }

  bool use_router_filters_{false};
  const std::string default_header_key_ = "header-key";
  const std::string default_header_value_ = "default-value";
};

class StaticRouterOrClusterFiltersIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public UpstreamHttpFilterIntegrationTestBase {
public:
  StaticRouterOrClusterFiltersIntegrationTest()
      : UpstreamHttpFilterIntegrationTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}
};

TEST_P(StaticRouterOrClusterFiltersIntegrationTest, BasicSuccess) {
  addStaticFilter("envoy.test.add_header_upstream", default_header_key_, default_header_value_);
  addCodecFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, default_header_value_);
}

TEST_P(StaticRouterOrClusterFiltersIntegrationTest, TwoFilters) {
  addStaticFilter("foo", default_header_key_, "value1");
  addStaticFilter("bar", default_header_key_, "value2");
  addCodecFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, "value1,value2");
}

INSTANTIATE_TEST_SUITE_P(
    IpVersions, StaticRouterOrClusterFiltersIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()));

class StaticRouterAndClusterFiltersIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public UpstreamHttpFilterIntegrationTestBase {
public:
  StaticRouterAndClusterFiltersIntegrationTest()
      : UpstreamHttpFilterIntegrationTestBase(GetParam(), false) {}
};

// Only cluster-specified filters should be applied.
TEST_P(StaticRouterAndClusterFiltersIntegrationTest, StaticRouterAndClusterFilters) {
  addStaticClusterFilter(
      getAddHeaderFilterConfig("foo", default_header_key_, "value-from-cluster"));
  addStaticRouterFilter(getAddHeaderFilterConfig("bar", default_header_key_, "value-from-router"));
  addCodecClusterFilter();
  addCodecRouterFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, "value-from-cluster");
}

INSTANTIATE_TEST_SUITE_P(IpVersions, StaticRouterAndClusterFiltersIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

class GrpcClientIntegrationParamTestWithVariantUpstreamConfig
    : public testing::TestWithParam<
          std::tuple<std::tuple<Network::Address::IpVersion, Grpc::ClientType>, bool>> {
public:
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<
          std::tuple<std::tuple<Network::Address::IpVersion, Grpc::ClientType>, bool>>& p) {
    return fmt::format(
        "{}_{}_{}", TestUtility::ipVersionToString(std::get<0>(std::get<0>(p.param))),
        std::get<1>(std::get<0>(p.param)) == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc"
                                                                          : "EnvoyGrpc",
        std::get<1>(p.param) ? "WithRouterFilters" : "WithClusterFilters");
  }
};

class UpstreamHttpExtensionDiscoveryIntegrationTestBase
    : public Grpc::BaseGrpcClientIntegrationParamTest,
      public UpstreamHttpFilterIntegrationTestBase {
public:
  UpstreamHttpExtensionDiscoveryIntegrationTestBase(Network::Address::IpVersion version,
                                                    bool use_router_filters)
      : UpstreamHttpFilterIntegrationTestBase(version, use_router_filters) {}

  void setDynamicFilterConfig(HttpFilterProto* filter, const std::string& name,
                              bool apply_without_warming, bool set_default_config = true,
                              bool rate_limit = false, bool second_connection = false) {
    filter->set_name(name);

    auto* discovery = filter->mutable_config_discovery();
    discovery->add_type_urls("type.googleapis.com/test.integration.filters.AddHeaderFilterConfig");
    if (set_default_config) {
      auto default_configuration = test::integration::filters::AddHeaderFilterConfig();
      default_configuration.set_header_key(default_header_key_);
      default_configuration.set_header_value(default_header_value_);
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
  }

  void addDynamicClusterFilter(const std::string& name, bool apply_without_warming,
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
      setDynamicFilterConfig(filter, name, apply_without_warming, set_default_config, rate_limit,
                             second_connection);
      (*cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
              .PackFrom(protocol_options);
    });
  }

  void addDynamicRouterFilter(const std::string& name, bool apply_without_warming,
                              bool set_default_config = true, bool rate_limit = false,
                              bool second_connection = false) {
    config_helper_.addConfigModifier(
        [name, apply_without_warming, set_default_config, rate_limit, second_connection, this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          HttpFilterProto& router_filter = *hcm.mutable_http_filters(0);
          ASSERT_EQ(router_filter.name(), "envoy.filters.http.router");
          envoy::extensions::filters::http::router::v3::Router router;
          router_filter.typed_config().UnpackTo(&router);
          auto* filter = router.add_upstream_http_filters();
          setDynamicFilterConfig(filter, name, apply_without_warming, set_default_config,
                                 rate_limit, second_connection);
          router_filter.mutable_typed_config()->PackFrom(router);
        });
  }

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        bool second_connection = false) {
    if (useRouterFilters()) {
      addDynamicRouterFilter(name, apply_without_warming, set_default_config, rate_limit,
                             second_connection);
    } else {
      addDynamicClusterFilter(name, apply_without_warming, set_default_config, rate_limit,
                              second_connection);
    }
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
    test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
    registerTestServerPorts({"http"});
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

  ~UpstreamHttpExtensionDiscoveryIntegrationTestBase() override {
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

  // Utilities used for config dump.
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

class DynamicRouterOrClusterFiltersIntegrationTest
    : public GrpcClientIntegrationParamTestWithVariantUpstreamConfig,
      public UpstreamHttpExtensionDiscoveryIntegrationTestBase {
public:
  DynamicRouterOrClusterFiltersIntegrationTest()
      : UpstreamHttpExtensionDiscoveryIntegrationTestBase(
            DynamicRouterOrClusterFiltersIntegrationTest::ipVersion(), std::get<1>(GetParam())) {}

  Network::Address::IpVersion ipVersion() const override {
    return std::get<0>(std::get<0>(GetParam()));
  }
  Grpc::ClientType clientType() const override { return std::get<1>(std::get<0>(GetParam())); }
};

// All upstream HTTP filters that are applied to cluster config must apply default config
// without warming. Otherwise, clusters initialization enters deadlock.
TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  // Expect default headers before config is updated
  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, default_header_value_);

  // Send 1st config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val1");
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, "test-val1");

  // Send 2nd config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val2");
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 2);
  auto headers3 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers3, "test-val2");
};

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, BasicSuccessWithTtl) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  // Send 1st config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val1", true);
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);
  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, "test-val1");

  // Wait for configuration expiry, the default configuration should be applied.
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 2);
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, default_header_value_);
};

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, BasicWithConfigFail) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  // Send config update with invalid config (header value length has to >=2).
  sendXdsResponse(filter_name_, "1", default_header_key_, "x");
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_fail", 1);
  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_value_);
};

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, TwoSubscriptionsSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val");
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);
  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, "test-val,test-val");
}

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, TwoSubscriptionsDifferentName) {
  two_ecds_filters_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", true, true, false, true);
  addCodecFilter();
  initialize();

  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, default_header_value_ + "," + default_header_value_);

  // Send 1st config update.
  sendXdsResponse("foo", "1", "header-key1", "test-val1");
  sendXdsResponse("bar", "1", "header-key2", "test-val1", false, true);
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter.foo.config_reload", 1);
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter.bar.config_reload", 1);
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, "header-key1", "test-val1");
  expectHeaderKeyAndValue(headers2, "header-key2", "test-val1");

  // Send 2nd config update.
  sendXdsResponse("foo", "2", "header-key1", "test-val2");
  sendXdsResponse("bar", "2", "header-key2", "test-val2", false, true);
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter.foo.config_reload", 2);
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter.bar.config_reload", 2);
  auto headers3 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers3, "header-key1", "test-val2");
  expectHeaderKeyAndValue(headers3, "header-key2", "test-val2");
}

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, TwoDynamicTwoStaticFilterMixed) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addStaticFilter("bar", default_header_key_, "static-val1");
  addDynamicFilter(filter_name_, true);
  addStaticFilter("foobar", "header2", "static-val2");
  addCodecFilter();
  initialize();

  sendXdsResponse(filter_name_, "1", default_header_key_, "xds-val");
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);
  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, "xds-val,static-val1,xds-val");
  expectHeaderKeyAndValue(headers, "header2", "static-val2");
}

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, DynamicStaticFilterMixedDifferentOrder) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addStaticFilter("bar", default_header_key_, "static-val1");
  addStaticFilter("foobar", "header2", "static-val2");
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  sendXdsResponse(filter_name_, "1", default_header_key_, "xds-val");
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);
  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, "static-val1,xds-val,xds-val");
  expectHeaderKeyAndValue(headers, "header2", "static-val2");
}

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, UpdateDuringConnection) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response1 =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(response1->complete());
  std::unique_ptr<Http::RequestHeaderMap> headers1 =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  EXPECT_TRUE(headers1 != nullptr);

  // Expect default headers before config is updated
  expectHeaderKeyAndValue(headers1, default_header_value_);

  // Send config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "xds-val");
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);

  IntegrationStreamDecoderPtr response2 =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  std::unique_ptr<Http::RequestHeaderMap> headers2 =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  EXPECT_TRUE(headers2 != nullptr);

  // Expect default headers before config is updated
  expectHeaderKeyAndValue(headers2, "xds-val");

  cleanupUpstreamAndDownstream();
};

// Basic ECDS config dump test with one filter.
TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, BasicSuccessWithConfigDump) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  // Send 1st config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "xds-val");
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);

  // Verify ECDS config dump are working correctly.
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump", response));
  EXPECT_EQ("application/json", contentType(response));
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  size_t index = 0;
  for (const Json::ObjectSharedPtr& obj_ptr : json->getObjectArray("configs")) {
    EXPECT_TRUE(expected_types[index].compare(obj_ptr->getString("@type")) == 0);
    index++;
  }

  // Validate we can parse as proto.
  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(7, config_dump.configs_size());

  // With /config_dump, the response has the format: EcdsConfigDump.
  envoy::admin::v3::EcdsConfigDump ecds_config_dump;
  config_dump.configs(2).UnpackTo(&ecds_config_dump);
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(0).version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_config_dump.ecds_filters(0).ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ("foo", filter_config.name());
  test::integration::filters::AddHeaderFilterConfig http_filter_config;
  filter_config.typed_config().UnpackTo(&http_filter_config);
  EXPECT_EQ(default_header_key_, http_filter_config.header_key());
  EXPECT_EQ("xds-val", http_filter_config.header_value());
}

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientType, DynamicRouterOrClusterFiltersIntegrationTest,
    testing::Combine(GRPC_CLIENT_INTEGRATION_PARAMS, testing::Bool()),
    GrpcClientIntegrationParamTestWithVariantUpstreamConfig::protocolTestParamsToString);

class GrpcClientIntegrationParamTestParams
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, Grpc::ClientType>> {
public:
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, Grpc::ClientType>>&
          p) {
    return fmt::format("{}_{}", TestUtility::ipVersionToString(std::get<0>(p.param)),
                       std::get<1>(p.param) == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc"
                                                                            : "EnvoyGrpc");
  }
};

class DynamicRouterAndClusterFiltersIntegrationTest
    : public GrpcClientIntegrationParamTestParams,
      public UpstreamHttpExtensionDiscoveryIntegrationTestBase {
public:
  DynamicRouterAndClusterFiltersIntegrationTest()
      : UpstreamHttpExtensionDiscoveryIntegrationTestBase(
            DynamicRouterAndClusterFiltersIntegrationTest::ipVersion(), false) {}

  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  Grpc::ClientType clientType() const override { return std::get<1>(GetParam()); }
};

TEST_P(DynamicRouterAndClusterFiltersIntegrationTest, DynamicRouterAndClusterSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicClusterFilter(filter_name_, true);
  addDynamicRouterFilter(filter_name_, true);
  addCodecClusterFilter();
  addCodecRouterFilter();
  initialize();

  // Expect default headers before config is updated.
  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, default_header_value_);

  // Send 1st config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val1");
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", 1);
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, "test-val1");
};

TEST_P(DynamicRouterAndClusterFiltersIntegrationTest, DynamicRouterAndClusterDifferentName) {
  two_ecds_filters_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicClusterFilter("foo", true);
  addDynamicRouterFilter("bar", true, true, false, true);
  addCodecClusterFilter();
  addCodecRouterFilter();
  initialize();

  // Expect default headers before config is updated.
  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, default_header_value_);

  // Send 1st config update.
  sendXdsResponse("foo", "1", default_header_key_, "value-from-cluster");
  sendXdsResponse("bar", "1", default_header_key_, "value-from-router", false, true);
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter.foo.config_reload", 1);
  test_server_->waitForCounterEq(
      "extension_config_discovery.upstream_http_filter.bar.config_reload", 1);
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, "value-from-cluster");
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, DynamicRouterAndClusterFiltersIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         GrpcClientIntegrationParamTestParams::protocolTestParamsToString);

} // namespace
} // namespace Envoy
