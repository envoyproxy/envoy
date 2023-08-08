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

  void addDynamicFilter(const std::string& name, bool apply_without_warming) {
    config_helper_.addConfigModifier(
        [this, name, apply_without_warming](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          HttpFilterProto codec_filter;
          codec_filter.set_name("envoy.filters.http.upstream_codec");
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

          discovery->set_apply_default_config_without_warming(apply_without_warming);
          discovery->mutable_config_source()->set_resource_api_version(
              envoy::config::core::v3::ApiVersion::V3);
          auto* api_config_source = discovery->mutable_config_source()->mutable_api_config_source();
          api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
          api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);

          auto* grpc_service = api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "ecds_cluster", getEcdsFakeUpstream().localAddress());

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

    addEcdsCluster("ecds_cluster");

    // In case to configure both HTTP and Listener ECDS filters, adding the 2nd ECDS cluster.
    if (two_ecds_filters_) {
      addEcdsCluster("ecds2_cluster");
    }

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
    resetConnection(ecds2_connection_);
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the extension config discovery upstream (fake_upstreams_[1]).
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
    waitForEcdsStream(getEcdsFakeUpstream(), ecds_connection_, ecds_stream_);
    if (two_ecds_filters_) {
      // Wait for 2nd ECDS stream.
      waitForEcdsStream(getEcds2FakeUpstream(), ecds2_connection_, ecds2_stream_);
    }
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
                                  const std::string& key, const std::string& val) {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);

    auto configuration = test::integration::filters::AddHeaderFilterConfig();
    configuration.set_header_key(key);
    configuration.set_header_value(val);
    typed_config.mutable_typed_config()->PackFrom(configuration);

    sendEcdsResponse(typed_config, name, version, false, ecds_stream_);
  }

  bool two_ecds_filters_{false};
  FakeUpstream& getEcdsFakeUpstream() const { return *fake_upstreams_[1]; }
  FakeUpstream& getEcds2FakeUpstream() const { return *fake_upstreams_[2]; }

  // gRPC ECDS set-up
  FakeHttpConnectionPtr ecds_connection_{nullptr};
  FakeStreamPtr ecds_stream_{nullptr};
  FakeHttpConnectionPtr ecds2_connection_{nullptr};
  FakeStreamPtr ecds2_stream_{nullptr};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, UpstreamHttpExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(UpstreamHttpExtensionDiscoveryIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", false);
  addCodecFilter();
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});
};

} // namespace
} // namespace Envoy
