#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_client_impl.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/gcp_authn/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

class MockCertFingerprinter : public CertFingerprinter {
public:
  MOCK_METHOD(absl::StatusOr<std::string>, getFingerprintFromPem, (const std::string& pem),
              (const, override));
};

using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;
using Server::Configuration::MockFactoryContext;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using testing::Return;
using Upstream::MockThreadLocalCluster;

constexpr char DefaultConfig[] = R"EOF(
    http_uri:
      uri: http://testhost/path/test
      cluster: test_cluster
      timeout:
        seconds: 5
    retry_policy:
      retry_back_off:
        base_interval: 1s
        max_interval: 10s
      num_retries: 5
  )EOF";

// A mock GCE Identity Token (JWT) originally from token_cache_test.cc.
// Payload: {"iss":"https://example.com","sub":"test@example.com", "aud":"example_service",
// "exp":2001001001} Expiration corresponds to Sun May 29 2033 13:36:41 GMT.
constexpr absl::string_view GoodTokenStr =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
    "XVkIjoiZXhhbXBsZV9zZXJ2aWNlIn0.cuui_Syud76B0tqvjESE8IZbX7vzG6xA-M"
    "Daof1qEFNIoCFT_YQPkseLSUSR2Od3TJcNKk-dKjvUEL1JW3kGnyC1dBx4f3-Xxro"
    "yL23UbR2eS8TuxO9ZcNCGkjfvH5O4mDb6cVkFHRDEolGhA7XwNiuVgkGJ5Wkrvshi"
    "h6nqKXcPNaRx9lOaRWg2PkE6ySNoyju7rNfunXYtVxPuUIkl0KMq3WXWRb_cb8a_Z"
    "EprqSZUzi_ZzzYzqBNVhIJujcNWij7JRra2sXXiSAfKjtxHQoxrX8n4V1ySWJ3_1T"
    "H_cJcdfS_RKP7YgXRWC0L16PNF5K7iqRqmjKALNe83ZFnFIw";
} // namespace

class GcpAuthnFilterTest : public testing::Test {
public:
  GcpAuthnFilterTest() {
    // Initialize the default configuration.
    TestUtility::loadFromYaml(DefaultConfig, config_);
    filter_config_ =
        std::make_shared<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>(
            config_);
    fingerprinter_ = std::make_shared<NiceMock<MockCertFingerprinter>>();
  }

  void setupMockObjects() {
    EXPECT_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
        .WillRepeatedly(Return(&thread_local_cluster_));
    EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _))
        .WillRepeatedly(Invoke([&](Envoy::Http::RequestMessagePtr& message,
                                   Envoy::Http::AsyncClient::Callbacks& callback,
                                   const Envoy::Http::AsyncClient::RequestOptions& options)
                                   -> Http::AsyncClient::Request* {
          message_.swap(message);
          client_callback_ = &callback;
          options_ = options;
          return &client_request_;
        }));
  }

  void setupFilterAndCallback(TokenCacheImpl* cache = nullptr) {
    filter_ =
        std::make_unique<GcpAuthnFilter>(filter_config_, context_, "stats", cache, fingerprinter_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  absl::optional<std::string> getClientCertFingerprint(Upstream::ThreadLocalCluster* cluster) {
    return filter_->getClientCertFingerprint(cluster);
  }

  void setupMockFilterMetadata(bool valid, const std::string& audience_url = "test") {
    // Set up mock filter metadata.
    cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));
    if (valid) {
      envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
      audience.set_url(audience_url);

      (*metadata_.mutable_typed_filter_metadata())
          [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
              .PackFrom(audience);
    }
    ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));
  }

  void overrideConfig(const GcpAuthnFilterConfig& config) {
    config_ = config;
    filter_config_ =
        std::make_shared<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>(
            config);
  }

  NiceMock<MockFactoryContext> context_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Envoy::Http::MockAsyncClientRequest> client_request_{
      &thread_local_cluster_.async_client_};
  NiceMock<MockGcpAuthnClientCallbacks> request_callbacks_;

  // Mocks for http request.
  Envoy::Http::AsyncClient::Callbacks* client_callback_;
  Envoy::Http::RequestMessagePtr message_;
  Envoy::Http::AsyncClient::RequestOptions options_;

  std::unique_ptr<GcpAuthnFilter> filter_;
  GcpAuthnFilterConfig config_;
  FilterConfigSharedPtr filter_config_;
  std::shared_ptr<MockCertFingerprinter> fingerprinter_;
  Http::TestRequestHeaderMapImpl default_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  envoy::config::core::v3::Metadata metadata_;
};

TEST_F(GcpAuthnFilterTest, NoRoute) {
  setupFilterAndCallback();

  // route() call return nullptr
  EXPECT_CALL(decoder_callbacks_, route()).WillOnce(Return(OptRef<const Router::Route>()));
  // decodeHeaders() is expected to return `Continue` because nothing can really be done without
  // route.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
}

TEST_F(GcpAuthnFilterTest, NoFilterMetadata) {
  setupMockObjects();
  setupFilterAndCallback();
  // Set up mock filter metadata.
  setupMockFilterMetadata(/*valid=*/false);
  // decodeHeaders() is expected to return `Continue` because no filter metadata is specified
  // in configuration.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->stats().retrieve_audience_failed_.value(), 1);
}

TEST_F(GcpAuthnFilterTest, ResumeFilterChainIteration) {
  setupMockObjects();
  setupFilterAndCallback();
  // Set up mock filter metadata.
  setupMockFilterMetadata(/*valid=*/true);

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(std::string(GoodTokenStr));
  // continueDecoding() is expected to be called to resume the filter chain iteration after
  // onSuccess().
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnFilterTest, ResumeFilterChainIterationWithAccessToken) {
  setupMockObjects();
  setupFilterAndCallback();

  // Set up mock filter metadata with AccessToken instead of url.
  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_access_token();

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(
      R"({"access_token": "mock_access_token", "expires_in": 3600, "token_type": "Bearer"})");

  // continueDecoding() is expected to be called to resume the filter chain iteration after
  // onSuccess().
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  client_callback_->onSuccess(client_request_, std::move(response));

  // Also check that the authorization header has been added correctly.
  EXPECT_EQ(default_headers_.get_("Authorization"), "Bearer mock_access_token");
}

TEST_F(GcpAuthnFilterTest, DestroyFilter) {
  setupMockObjects();
  setupFilterAndCallback();
  // Set up mock filter metadata.
  setupMockFilterMetadata(/*valid=*/true);

  // decodeHeaders() is expected to return `StopAllIterationAndWatermark` and state is expected to
  // be in `Calling` state because none of complete functions(i.e., onSuccess, onFailure, onDestroy,
  // etc) has been called.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  EXPECT_EQ(filter_->state(), GcpAuthnFilter::State::Calling);
  filter_->onDestroy();
  // onDestroy() call is expected to update the state from `Calling` to `Complete`.
  EXPECT_EQ(filter_->state(), GcpAuthnFilter::State::Complete);
}

TEST_F(GcpAuthnFilterTest, AudienceInvalidType) {
  setupMockObjects();
  setupFilterAndCallback();

  // Set up mock filter metadata using a completely different proto type (Duration).
  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  Protobuf::Duration invalid_proto;
  invalid_proto.set_seconds(5);

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(invalid_proto);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  // The filter should fail to unpack, return nullopt, fail open, and return Continue.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->stats().retrieve_audience_failed_.value(), 1);
}

TEST_F(GcpAuthnFilterTest, ClusterNotFound) {
  setupMockObjects();
  setupFilterAndCallback();

  // getThreadLocalCluster returns nullptr when the cluster is completely missing.
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(nullptr));

  // decodeHeaders should return Continue directly, increment stats, and fail open.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->stats().retrieve_audience_failed_.value(), 1);
}

TEST_F(GcpAuthnFilterTest, CacheHit) {
  setupMockObjects();

  // Set up metadata using Audience.
  setupMockFilterMetadata(/*valid=*/true);

  // Instantiate real TokenCacheImpl.
  envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig cache_config;
  cache_config.mutable_cache_size()->set_value(100);
  TokenCacheImpl cache(cache_config, context_.serverFactoryContext().timeSource());

  // Populate the cache directly with a valid token.
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("test");

  // Expiration in the future so the cache hit is valid.
  uint64_t far_future_exp =
      DateUtil::nowToSeconds(context_.serverFactoryContext().timeSource()) + 1000;
  auto token = std::make_unique<GcpToken>();
  token->token = "cached_token";
  token->expires_at = far_future_exp;
  token->audience = audience;
  cache.insert(std::move(token));

  setupFilterAndCallback(&cache);

  // The filter should inject the cached token and return Continue directly.
  // The async HTTP client should NOT be called (Times(0)).
  EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(default_headers_.get_("Authorization"), "Bearer cached_token");
}

TEST_F(GcpAuthnFilterTest, CacheMissAndInsert) {
  setupMockObjects();
  setupMockFilterMetadata(/*valid=*/true);

  // Instantiate real TokenCacheImpl.
  envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig cache_config;
  cache_config.mutable_cache_size()->set_value(100);
  TokenCacheImpl cache(cache_config, context_.serverFactoryContext().timeSource());

  setupFilterAndCallback(&cache);

  // The filter should fall back to calling the async client because of cache miss.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Mock successful async HTTP client response.
  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(std::string(GoodTokenStr));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  client_callback_->onSuccess(client_request_, std::move(response));

  // After fetch completes, the token must be automatically inserted into the cache!
  // Verify by performing a lookup in the cache and asserting it is found!
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("test");
  auto cached_val = cache.lookUp(audience, absl::nullopt);
  EXPECT_TRUE(cached_val.has_value());
  EXPECT_EQ(cached_val.value(), std::string(GoodTokenStr));
}

TEST_F(GcpAuthnFilterTest, BoundJwtCacheMissAndInsert) {
  setupMockObjects();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("test");

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  const std::string dummy_pem = "dummy cert PEM";
  const std::string expected_fingerprint = "mock_fingerprint_base64";

  auto socket_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
  auto client_context_config = std::make_unique<NiceMock<Ssl::MockClientContextConfig>>();
  auto tls_cert_config = std::make_unique<NiceMock<Ssl::MockTlsCertificateConfig>>();

  ON_CALL(*tls_cert_config, certificateChain()).WillByDefault(testing::ReturnRef(dummy_pem));

  std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>> tls_certs;
  tls_certs.push_back(*tls_cert_config);
  ON_CALL(*client_context_config, tlsCertificates()).WillByDefault(testing::Return(tls_certs));

  ON_CALL(*socket_factory, clientContextConfig())
      .WillByDefault(
          testing::Return(OptRef<const Ssl::ClientContextConfig>(*client_context_config)));

  auto transport_socket_matcher =
      std::make_unique<NiceMock<Upstream::MockTransportSocketMatcher>>(std::move(socket_factory));
  EXPECT_CALL(*cluster_info_, transportSocketMatcher())
      .WillRepeatedly(testing::ReturnRef(*transport_socket_matcher));

  EXPECT_CALL(*fingerprinter_, getFingerprintFromPem(dummy_pem))
      .WillOnce(testing::Return(expected_fingerprint));

  // Instantiate real TokenCacheImpl.
  envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig cache_config;
  cache_config.mutable_cache_size()->set_value(100);
  TokenCacheImpl cache(cache_config, context_.serverFactoryContext().timeSource());

  setupFilterAndCallback(&cache);

  // The filter should fall back to calling the async client because of cache miss.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Mock successful async HTTP client response.
  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(std::string(GoodTokenStr));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  client_callback_->onSuccess(client_request_, std::move(response));

  // After fetch completes, the token must be automatically inserted into the cache with the
  // fingerprint!
  auto cached_val = cache.lookUp(audience, expected_fingerprint);
  EXPECT_TRUE(cached_val.has_value());
  EXPECT_EQ(cached_val.value(), std::string(GoodTokenStr));
}

TEST_F(GcpAuthnFilterTest, MtlsClusterFingerprint) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("test");

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  const std::string dummy_pem = "dummy cert PEM";
  const std::string expected_fingerprint = "mock_fingerprint_base64";

  auto socket_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
  auto client_context_config = std::make_unique<NiceMock<Ssl::MockClientContextConfig>>();
  auto tls_cert_config = std::make_unique<NiceMock<Ssl::MockTlsCertificateConfig>>();

  ON_CALL(*tls_cert_config, certificateChain()).WillByDefault(testing::ReturnRef(dummy_pem));

  std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>> tls_certs;
  tls_certs.push_back(*tls_cert_config);
  ON_CALL(*client_context_config, tlsCertificates()).WillByDefault(testing::Return(tls_certs));

  ON_CALL(*socket_factory, clientContextConfig())
      .WillByDefault(
          testing::Return(OptRef<const Ssl::ClientContextConfig>(*client_context_config)));

  auto transport_socket_matcher =
      std::make_unique<NiceMock<Upstream::MockTransportSocketMatcher>>(std::move(socket_factory));
  EXPECT_CALL(*cluster_info_, transportSocketMatcher())
      .WillRepeatedly(testing::ReturnRef(*transport_socket_matcher));

  EXPECT_CALL(*fingerprinter_, getFingerprintFromPem(dummy_pem))
      .WillOnce(testing::Return(expected_fingerprint));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  EXPECT_TRUE(filter_->fingerprint().has_value());
  EXPECT_EQ(filter_->fingerprint().value(), expected_fingerprint);

  filter_.reset();
}

TEST_F(GcpAuthnFilterTest, BoundJwtWithoutFingerprintFails) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("http://bound_audience");

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  auto socket_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
  ON_CALL(*socket_factory, clientContextConfig())
      .WillByDefault(testing::Return(OptRef<const Ssl::ClientContextConfig>{}));

  auto transport_socket_matcher =
      std::make_unique<NiceMock<Upstream::MockTransportSocketMatcher>>(std::move(socket_factory));
  EXPECT_CALL(*cluster_info_, transportSocketMatcher())
      .WillRepeatedly(testing::ReturnRef(*transport_socket_matcher));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  EXPECT_FALSE(filter_->fingerprint().has_value());
}

TEST_F(GcpAuthnFilterTest, GetClientCertFingerprintWithNullClusterReturnsNullopt) {
  setupFilterAndCallback();
  EXPECT_EQ(getClientCertFingerprint(nullptr), absl::nullopt);
}

TEST_F(GcpAuthnFilterTest, BoundJwtWithEmptyTlsCertificatesFails) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("http://bound_audience");

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  auto socket_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
  auto client_context_config = std::make_unique<NiceMock<Ssl::MockClientContextConfig>>();

  std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>> empty_tls_certs;
  ON_CALL(*client_context_config, tlsCertificates())
      .WillByDefault(testing::Return(empty_tls_certs));

  ON_CALL(*socket_factory, clientContextConfig())
      .WillByDefault(
          testing::Return(OptRef<const Ssl::ClientContextConfig>(*client_context_config)));

  auto transport_socket_matcher =
      std::make_unique<NiceMock<Upstream::MockTransportSocketMatcher>>(std::move(socket_factory));
  EXPECT_CALL(*cluster_info_, transportSocketMatcher())
      .WillRepeatedly(testing::ReturnRef(*transport_socket_matcher));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  EXPECT_FALSE(filter_->fingerprint().has_value());
}

TEST_F(GcpAuthnFilterTest, BoundJwtWithEmptyCertChainFails) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("http://bound_audience");

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  auto socket_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
  auto client_context_config = std::make_unique<NiceMock<Ssl::MockClientContextConfig>>();
  auto tls_cert_config = std::make_unique<NiceMock<Ssl::MockTlsCertificateConfig>>();

  const std::string empty_pem = "";
  ON_CALL(*tls_cert_config, certificateChain()).WillByDefault(testing::ReturnRef(empty_pem));

  std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>> tls_certs;
  tls_certs.push_back(*tls_cert_config);
  ON_CALL(*client_context_config, tlsCertificates()).WillByDefault(testing::Return(tls_certs));

  ON_CALL(*socket_factory, clientContextConfig())
      .WillByDefault(
          testing::Return(OptRef<const Ssl::ClientContextConfig>(*client_context_config)));

  auto transport_socket_matcher =
      std::make_unique<NiceMock<Upstream::MockTransportSocketMatcher>>(std::move(socket_factory));
  EXPECT_CALL(*cluster_info_, transportSocketMatcher())
      .WillRepeatedly(testing::ReturnRef(*transport_socket_matcher));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  EXPECT_FALSE(filter_->fingerprint().has_value());
}

TEST_F(GcpAuthnFilterTest, BoundJwtWithFingerprinterErrorFails) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("http://bound_audience");

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  auto socket_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
  auto client_context_config = std::make_unique<NiceMock<Ssl::MockClientContextConfig>>();
  auto tls_cert_config = std::make_unique<NiceMock<Ssl::MockTlsCertificateConfig>>();

  const std::string dummy_pem = "dummy PEM";
  ON_CALL(*tls_cert_config, certificateChain()).WillByDefault(testing::ReturnRef(dummy_pem));

  std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>> tls_certs;
  tls_certs.push_back(*tls_cert_config);
  ON_CALL(*client_context_config, tlsCertificates()).WillByDefault(testing::Return(tls_certs));

  ON_CALL(*socket_factory, clientContextConfig())
      .WillByDefault(
          testing::Return(OptRef<const Ssl::ClientContextConfig>(*client_context_config)));

  auto transport_socket_matcher =
      std::make_unique<NiceMock<Upstream::MockTransportSocketMatcher>>(std::move(socket_factory));
  EXPECT_CALL(*cluster_info_, transportSocketMatcher())
      .WillRepeatedly(testing::ReturnRef(*transport_socket_matcher));

  EXPECT_CALL(*fingerprinter_, getFingerprintFromPem(dummy_pem))
      .WillOnce(testing::Return(absl::InternalError("fingerprint failure")));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  EXPECT_FALSE(filter_->fingerprint().has_value());
}

TEST_F(GcpAuthnFilterTest, BoundJwtCacheHit) {
  setupMockObjects();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("test");

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  const std::string dummy_pem = "dummy cert PEM";
  const std::string expected_fingerprint = "mock_fingerprint_base64";

  auto socket_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
  auto client_context_config = std::make_unique<NiceMock<Ssl::MockClientContextConfig>>();
  auto tls_cert_config = std::make_unique<NiceMock<Ssl::MockTlsCertificateConfig>>();

  ON_CALL(*tls_cert_config, certificateChain()).WillByDefault(testing::ReturnRef(dummy_pem));

  std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>> tls_certs;
  tls_certs.push_back(*tls_cert_config);
  ON_CALL(*client_context_config, tlsCertificates()).WillByDefault(testing::Return(tls_certs));

  ON_CALL(*socket_factory, clientContextConfig())
      .WillByDefault(
          testing::Return(OptRef<const Ssl::ClientContextConfig>(*client_context_config)));

  auto transport_socket_matcher =
      std::make_unique<NiceMock<Upstream::MockTransportSocketMatcher>>(std::move(socket_factory));
  EXPECT_CALL(*cluster_info_, transportSocketMatcher())
      .WillRepeatedly(testing::ReturnRef(*transport_socket_matcher));

  EXPECT_CALL(*fingerprinter_, getFingerprintFromPem(dummy_pem))
      .WillOnce(testing::Return(expected_fingerprint));

  envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig cache_config;
  cache_config.mutable_cache_size()->set_value(100);
  TokenCacheImpl cache(cache_config, context_.serverFactoryContext().timeSource());

  uint64_t far_future_exp =
      DateUtil::nowToSeconds(context_.serverFactoryContext().timeSource()) + 1000;
  auto token = std::make_unique<GcpToken>("cached_bound_token", far_future_exp, audience,
                                          expected_fingerprint);
  cache.insert(std::move(token));

  setupFilterAndCallback(&cache);

  EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(default_headers_.get_("Authorization"), "Bearer cached_bound_token");
}

TEST_F(GcpAuthnFilterTest, EmptyAudienceProto) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience empty_audience;

  (*metadata_
        .mutable_typed_filter_metadata())[std::string(
                                              Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
      .PackFrom(empty_audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->state(), GcpAuthnFilter::State::Complete);
  EXPECT_EQ(filter_->stats().empty_audience_.value(), 1);
}

TEST_F(GcpAuthnFilterTest, CompleteWithNullRequestHeaderMap) {
  setupFilterAndCallback();

  GcpToken token;
  token.token = "dummy_token";
  token.expires_at = 3600;

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->onComplete(token);

  EXPECT_EQ(filter_->state(), GcpAuthnFilter::State::Complete);
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
