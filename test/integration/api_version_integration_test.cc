#include "envoy/api/v2/core/config_source.pb.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

class ApiVersionIntegrationTest
    : public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, envoy::api::v2::core::ApiConfigSource::ApiType,
                     envoy::api::v2::core::ApiVersion, envoy::api::v2::core::ApiVersion>>,
      public HttpIntegrationTest {
public:
  ApiVersionIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion()) {
    use_lds_ = false;
    create_xds_upstream_ = true;
    tls_xds_upstream_ = false;
    skipPortUsageValidation();
  }

  Network::Address::IpVersion ipVersion() const { return std::get<0>(GetParam()); }
  envoy::api::v2::core::ApiConfigSource::ApiType apiType() const { return std::get<1>(GetParam()); }
  envoy::api::v2::core::ApiVersion resourceApiVersion() const { return std::get<2>(GetParam()); }
  envoy::api::v2::core::ApiVersion transportApiVersion() const { return std::get<3>(GetParam()); }

  void initialize() override {
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
      xds_stream_->startGrpcStream();
    }
  }

  AssertionResult validateDiscoveryRequest(const std::string& expected_v2_sotw_endpoint,
                                           const std::string& expected_v2_delta_endpoint,
                                           const std::string& expected_type_url) {
    std::string expected_endpoint;
    std::string actual_type_url;
    if (sotw_or_delta_ == Grpc::SotwOrDelta::Sotw) {
      API_NO_BOOST(envoy::api::v2::DiscoveryRequest) discovery_request;
      VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request));
      expected_endpoint = expected_v2_sotw_endpoint;
      actual_type_url = discovery_request.type_url();
    } else {
      API_NO_BOOST(envoy::api::v2::DeltaDiscoveryRequest) delta_discovery_request;
      VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, delta_discovery_request));
      expected_endpoint = expected_v2_delta_endpoint;
      actual_type_url = delta_discovery_request.type_url();
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
                                     envoy::api::v2::core::ApiVersion::V3ALPHA)));

TEST_P(ApiVersionIntegrationTest, Lds) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()->clear_listeners();
    auto* lds_config = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
    lds_config->set_resource_api_version(resourceApiVersion());
    auto* api_config_source = lds_config->mutable_api_config_source();
    api_config_source->set_transport_api_version(transportApiVersion());
    api_config_source->set_api_type(apiType());
    auto* grpc_service = api_config_source->add_grpc_services();
    grpc_service->mutable_envoy_grpc()->set_cluster_name("lds_cluster");
    auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
    lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    lds_cluster->set_name("lds_cluster");
    lds_cluster->mutable_http2_protocol_options();
  });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest("/envoy.api.v2.ListenerDiscoveryService/StreamListeners",
                                       "/envoy.api.v2.ListenerDiscoveryService/DeltaListeners",
                                       "type.googleapis.com/envoy.api.v2.Listener"));
}

} // namespace
} // namespace Envoy
