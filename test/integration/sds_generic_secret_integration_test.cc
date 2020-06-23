#include <string>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/http/filter.h"
#include "envoy/secret/secret_provider.h"

#include "common/config/datasource.h"
#include "common/grpc/common.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

namespace Envoy {

// The filter fetches a generic secret from secret manager and attaches it to a header for
// validation.
class SdsGenericSecretTestFilter : public Http::StreamDecoderFilter {
public:
  SdsGenericSecretTestFilter(Api::Api& api,
                             Secret::GenericSecretConfigProviderSharedPtr config_provider)
      : api_(api), config_provider_(config_provider) {}

  // Http::StreamFilterBase
  void onDestroy() override{};

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    headers.addCopy(Http::LowerCaseString("secret"),
                    Config::DataSource::read(config_provider_->secret()->secret(), true, api_));
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

private:
  Api::Api& api_;
  Secret::GenericSecretConfigProviderSharedPtr config_provider_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
};

class SdsGenericSecretTestFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  SdsGenericSecretTestFilterConfig()
      : Extensions::HttpFilters::Common::EmptyHttpFilterConfig("sds-generic-secret-test") {
    auto* api_config_source = config_source_.mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    auto* grpc_service = api_config_source->add_grpc_services();
    grpc_service->mutable_envoy_grpc()->set_cluster_name("sds_cluster");
  }

  Http::FilterFactoryCb
  createFilter(const std::string&,
               Server::Configuration::FactoryContext& factory_context) override {
    auto secret_provider =
        factory_context.clusterManager()
            .clusterManagerFactory()
            .secretManager()
            .findOrCreateGenericSecretProvider(config_source_, "encryption_key",
                                               factory_context.getTransportSocketFactoryContext());
    return
        [&factory_context, secret_provider](Http::FilterChainFactoryCallbacks& callbacks) -> void {
          callbacks.addStreamDecoderFilter(std::make_shared<::Envoy::SdsGenericSecretTestFilter>(
              factory_context.api(), secret_provider));
        };
  }

private:
  envoy::config::core::v3::ConfigSource config_source_;
};

class SdsGenericSecretIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                        public HttpIntegrationTest {
public:
  SdsGenericSecretIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()), registration_(factory_) {}

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* sds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      sds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      sds_cluster->set_name("sds_cluster");
      sds_cluster->mutable_http2_protocol_options();
    });

    config_helper_.addFilter("{ name: sds-generic-secret-test }");

    create_xds_upstream_ = true;
    HttpIntegrationTest::initialize();
  }

  void TearDown() override { cleanUpXdsConnection(); }

  void createSdsStream() {
    createXdsConnection();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  void sendSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name("encryption_key");
    auto* generic_secret = secret.mutable_generic_secret();
    generic_secret->mutable_secret()->set_inline_string("DUMMY_AES_128_KEY");
    API_NO_BOOST(envoy::api::v2::DiscoveryResponse) discovery_response;
    discovery_response.set_version_info("0");
    discovery_response.set_type_url(Config::TypeUrl::get().Secret);
    discovery_response.add_resources()->PackFrom(API_DOWNGRADE(secret));
    xds_stream_->sendGrpcMessage(discovery_response);
  }

  SdsGenericSecretTestFilterConfig factory_;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registration_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SdsGenericSecretIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// A test that an SDS generic secret can be successfully fetched by a filter.
TEST_P(SdsGenericSecretIntegrationTest, FilterFetchSuccess) {
  on_server_init_function_ = [this]() {
    createSdsStream();
    sendSecret();
  };
  initialize();

  codec_client_ = makeHttpConnection((lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_EQ(
      "DUMMY_AES_128_KEY",
      upstream_request_->headers().get(Http::LowerCaseString("secret"))->value().getStringView());
}

} // namespace Envoy
