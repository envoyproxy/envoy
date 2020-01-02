#include "envoy/api/v2/core/config_source.pb.h"

#include "common/common/assert.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

using Params =
    std::tuple<Network::Address::IpVersion, envoy::api::v2::core::ApiConfigSource::ApiType,
               envoy::api::v2::core::ApiVersion, envoy::api::v2::core::ApiVersion>;

class ApiVersionIntegrationTest : public testing::TestWithParam<Params>,
                                  public HttpIntegrationTest {
public:
  ApiVersionIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion()) {
    use_lds_ = false;
    create_xds_upstream_ = true;
    tls_xds_upstream_ = false;
    skipPortUsageValidation();
  }

  static std::string paramsToString(const testing::TestParamInfo<Params>& p) {
    return fmt::format("{}_{}_Resource_{}_Transport_{}",
                       std::get<0>(p.param) == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6",
                       envoy::api::v2::core::ApiConfigSource::ApiType_Name(std::get<1>(p.param)),
                       envoy::api::v2::core::ApiVersion_Name(std::get<2>(p.param)),
                       envoy::api::v2::core::ApiVersion_Name(std::get<3>(p.param)));
  }

  Network::Address::IpVersion ipVersion() const { return std::get<0>(GetParam()); }
  envoy::api::v2::core::ApiConfigSource::ApiType apiType() const { return std::get<1>(GetParam()); }
  envoy::api::v2::core::ApiVersion resourceApiVersion() const { return std::get<2>(GetParam()); }
  envoy::api::v2::core::ApiVersion transportApiVersion() const { return std::get<3>(GetParam()); }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()->clear_listeners();
      auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      xds_cluster->set_name("xds_cluster");
      xds_cluster->mutable_http2_protocol_options();
    });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    HttpIntegrationTest::initialize();
    if (xds_stream_ == nullptr) {
      createXdsConnection();
      AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      result = xds_stream_->waitForHeadersComplete();
      RELEASE_ASSERT(result, result.message());
      endpoint_ = std::string(xds_stream_->headers().Path()->value().getStringView());
      ENVOY_LOG_MISC(debug, "xDS endpoint {}", endpoint_);
    }
  }

  void setupConfigSource(envoy::api::v2::core::ConfigSource& config_source) {
    config_source.set_resource_api_version(resourceApiVersion());
    auto* api_config_source = config_source.mutable_api_config_source();
    api_config_source->set_transport_api_version(transportApiVersion());
    api_config_source->set_api_type(apiType());
    if (apiType() == envoy::api::v2::core::ApiConfigSource::REST) {
      api_config_source->add_cluster_names("xds_cluster");
      api_config_source->mutable_refresh_delay()->set_seconds(1);
    } else {
      auto* grpc_service = api_config_source->add_grpc_services();
      grpc_service->mutable_envoy_grpc()->set_cluster_name("xds_cluster");
    }
  }

  AssertionResult validateDiscoveryRequest(const std::string& expected_v2_sotw_endpoint,
                                           const std::string& expected_v2_delta_endpoint,
                                           const std::string& expected_v2_rest_endpoint,
                                           const std::string& expected_v3alpha_sotw_endpoint,
                                           const std::string& expected_v3alpha_delta_endpoint,
                                           const std::string& expected_v3alpha_rest_endpoint,
                                           const std::string& expected_v2_type_url,
                                           const std::string& expected_v3alpha_type_url) {
    std::string expected_endpoint;
    std::string expected_type_url;
    std::string actual_type_url;
    switch (transportApiVersion()) {
    case envoy::api::v2::core::ApiVersion::AUTO:
    case envoy::api::v2::core::ApiVersion::V2: {
      switch (apiType()) {
      case envoy::api::v2::core::ApiConfigSource::GRPC: {
        API_NO_BOOST(envoy::api::v2::DiscoveryRequest) discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request));
        xds_stream_->startGrpcStream();
        actual_type_url = discovery_request.type_url();
        expected_endpoint = expected_v2_sotw_endpoint;
        break;
      }
      case envoy::api::v2::core::ApiConfigSource::DELTA_GRPC: {
        API_NO_BOOST(envoy::api::v2::DeltaDiscoveryRequest) delta_discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, delta_discovery_request));
        xds_stream_->startGrpcStream();
        actual_type_url = delta_discovery_request.type_url();
        expected_endpoint = expected_v2_delta_endpoint;
        break;
      }
      case envoy::api::v2::core::ApiConfigSource::REST: {
        API_NO_BOOST(envoy::api::v2::DiscoveryRequest) discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForEndStream(*dispatcher_));
        MessageUtil::loadFromJson(xds_stream_->body().toString(), discovery_request,
                                  ProtobufMessage::getStrictValidationVisitor());
        actual_type_url = discovery_request.type_url();
        expected_endpoint = expected_v2_rest_endpoint;
        break;
      }
      default:
        NOT_REACHED_GCOVR_EXCL_LINE;
        break;
      }
      break;
    }
    case envoy::api::v2::core::ApiVersion::V3ALPHA: {
      switch (apiType()) {
      case envoy::api::v2::core::ApiConfigSource::GRPC: {
        API_NO_BOOST(envoy::api::v3alpha::DiscoveryRequest) discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request));
        actual_type_url = discovery_request.type_url();
        expected_endpoint = expected_v3alpha_sotw_endpoint;
        break;
      }
      case envoy::api::v2::core::ApiConfigSource::DELTA_GRPC: {
        API_NO_BOOST(envoy::api::v3alpha::DeltaDiscoveryRequest) delta_discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, delta_discovery_request));
        actual_type_url = delta_discovery_request.type_url();
        expected_endpoint = expected_v3alpha_delta_endpoint;
        break;
      }
      case envoy::api::v2::core::ApiConfigSource::REST: {
        API_NO_BOOST(envoy::api::v3alpha::DiscoveryRequest) discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForEndStream(*dispatcher_));
        MessageUtil::loadFromJson(xds_stream_->body().toString(), discovery_request,
                                  ProtobufMessage::getStrictValidationVisitor());
        actual_type_url = discovery_request.type_url();
        expected_endpoint = expected_v3alpha_rest_endpoint;
        break;
      }
      default:
        NOT_REACHED_GCOVR_EXCL_LINE;
        break;
      }
      break;
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    switch (resourceApiVersion()) {
    case envoy::api::v2::core::ApiVersion::AUTO:
    case envoy::api::v2::core::ApiVersion::V2:
      expected_type_url = expected_v2_type_url;
      break;
    case envoy::api::v2::core::ApiVersion::V3ALPHA:
      expected_type_url = expected_v3alpha_type_url;
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    if (endpoint_ != expected_endpoint) {
      return AssertionFailure() << "Expected endpoint " << expected_endpoint << ", got "
                                << endpoint_;
    }
    if (expected_type_url != actual_type_url) {
      return AssertionFailure() << "Expected type URL " << expected_type_url << ", got "
                                << actual_type_url;
    }
    return AssertionSuccess();
  }

  void TearDown() override {
    cleanUpXdsConnection();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  std::string endpoint_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsApiConfigSourcesApiVersions, ApiVersionIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(envoy::api::v2::core::ApiConfigSource::REST,
                                     envoy::api::v2::core::ApiConfigSource::GRPC,
                                     envoy::api::v2::core::ApiConfigSource::DELTA_GRPC),
                     testing::Values(envoy::api::v2::core::ApiVersion::AUTO,
                                     envoy::api::v2::core::ApiVersion::V2,
                                     envoy::api::v2::core::ApiVersion::V3ALPHA),
                     testing::Values(envoy::api::v2::core::ApiVersion::AUTO,
                                     envoy::api::v2::core::ApiVersion::V2,
                                     envoy::api::v2::core::ApiVersion::V3ALPHA)),
    ApiVersionIntegrationTest::paramsToString);

TEST_P(ApiVersionIntegrationTest, Lds) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    setupConfigSource(*bootstrap.mutable_dynamic_resources()->mutable_lds_config());
  });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.api.v2.ListenerDiscoveryService/StreamListeners",
      "/envoy.api.v2.ListenerDiscoveryService/DeltaListeners", "/v2/discovery:listeners",
      "/envoy.api.v3alpha.ListenerDiscoveryService/StreamListeners",
      "/envoy.api.v3alpha.ListenerDiscoveryService/DeltaListeners", "/v3alpha/discovery:listeners",
      "type.googleapis.com/envoy.api.v2.Listener",
      "type.googleapis.com/envoy.api.v3alpha.Listener"));
}

TEST_P(ApiVersionIntegrationTest, Cds) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    setupConfigSource(*bootstrap.mutable_dynamic_resources()->mutable_cds_config());
  });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.api.v2.ClusterDiscoveryService/StreamClusters",
      "/envoy.api.v2.ClusterDiscoveryService/DeltaClusters", "/v2/discovery:clusters",
      "/envoy.api.v3alpha.ClusterDiscoveryService/StreamClusters",
      "/envoy.api.v3alpha.ClusterDiscoveryService/DeltaClusters", "/v3alpha/discovery:clusters",
      "type.googleapis.com/envoy.api.v2.Cluster", "type.googleapis.com/envoy.api.v3alpha.Cluster"));
}

TEST_P(ApiVersionIntegrationTest, Rtds) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto* admin_layer = bootstrap.mutable_layered_runtime()->add_layers();
    admin_layer->set_name("admin layer");
    admin_layer->mutable_admin_layer();
    auto* rtds_layer = bootstrap.mutable_layered_runtime()->add_layers();
    rtds_layer->set_name("rtds_layer");
    setupConfigSource(*rtds_layer->mutable_rtds_layer()->mutable_rtds_config());
  });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.service.discovery.v2.RuntimeDiscoveryService/StreamRuntime",
      "/envoy.service.discovery.v2.RuntimeDiscoveryService/DeltaRuntime", "/v2/discovery:runtime",
      "/envoy.service.discovery.v3alpha.RuntimeDiscoveryService/StreamRuntime",
      "/envoy.service.discovery.v3alpha.RuntimeDiscoveryService/DeltaRuntime",
      "/v3alpha/discovery:runtime", "type.googleapis.com/envoy.service.discovery.v2.Runtime",
      "type.googleapis.com/envoy.service.discovery.v3alpha.Runtime"));
}

} // namespace
} // namespace Envoy
