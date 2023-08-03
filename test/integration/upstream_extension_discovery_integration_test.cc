#include "envoy/extensions/common/matching/v3/extension_matcher.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/utility.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/filters/add_header_temp_filter_config.pb.h"
#include "test/integration/filters/repick_cluster_filter.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "http_integration.h"

namespace Envoy {
namespace {

using HttpFilterProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;

class ExtensionDiscoveryIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                          public HttpIntegrationTest {
public:
  ExtensionDiscoveryIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    autonomous_upstream_ = true;
  }

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
              "type.googleapis.com/test.integration.filters.AddHeaderTempFilterConfig");

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
          ConfigHelper::setHttp2(*ecds_cluster);
        });
  }

  void initialize() override {
    defer_listener_finalization_ = true;
    setUpstreamCount(1);

    addEcdsCluster("ecds_cluster");

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
                                  const std::string& key, const std::string& val) {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);

    auto configuration = test::integration::filters::AddHeaderTempFilterConfig();
    configuration.set_header_key(key);
    configuration.set_header_val(val);
    typed_config.mutable_typed_config()->PackFrom(configuration);

    sendEcdsResponse(typed_config, name, version, false, ecds_stream_);
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
  addCodecFilter();
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({"http"});

  sendHttpFilterEcdsResponse("foo", "1", "test-key", "test-val");
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
}

} // namespace
} // namespace Envoy
