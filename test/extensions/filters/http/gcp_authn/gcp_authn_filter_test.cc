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
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

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

class MockGcpAuthnClient : public GcpAuthnClient {
public:
  MockGcpAuthnClient() = default;
  ~MockGcpAuthnClient() override = default;

  MOCK_METHOD(void, fetchUnboundJwt,
              (const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
               Callbacks& callbacks),
              (override));
  MOCK_METHOD(void, fetchUnboundAccessToken,
              (const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
               Callbacks& callbacks),
              (override));
  MOCK_METHOD(void, fetchBoundJwt,
              (const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
               const std::string& fingerprint, Callbacks& callbacks),
              (override));
  MOCK_METHOD(void, fetchBoundAccessToken,
              (const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
               const std::string& fingerprint, Callbacks& callbacks),
              (override));
  MOCK_METHOD(void, fetchIamAccessToken,
              (const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
               const std::string& authorization, Callbacks& callbacks),
              (override));
  MOCK_METHOD(void, cancel, (), (override));
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
    absl::Status status;
    filter_config_ = std::make_shared<FilterConfig>(config_, context_.server_factory_context_,
                                                    "stats", context_.scope_, status);
    EXPECT_OK(status);
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

  void setupFilterAndCallback() {
    filter_ = std::make_unique<GcpAuthnFilter>(filter_config_, fingerprinter_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  std::optional<std::string> getClientCertFingerprint(Upstream::ThreadLocalCluster* cluster) {
    return filter_->getClientCertFingerprint(cluster);
  }

  void setupMockFilterMetadata(bool valid, const std::string& audience_url = "test") {
    // Set up mock filter metadata.
    cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));
    if (valid) {
      envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
      audience.set_url(audience_url);
      std::ignore = (*metadata_.mutable_typed_filter_metadata())
                        [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
                            .PackFrom(audience);
    }
    ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));
  }

  void refreshConfig() {
    absl::Status status;
    filter_config_ = std::make_shared<FilterConfig>(config_, context_.server_factory_context_,
                                                    "stats", context_.scope_, status);
    ASSERT_OK(status);
  }

  void setClient(std::unique_ptr<GcpAuthnClient> client) { filter_->client_ = std::move(client); }

  void
  testAudiencePrecedence(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
                         std::function<void(MockGcpAuthnClient&)> configure_client_expectations,
                         bool is_bound) {
    setupMockObjects();

    std::string dummy_pem;
    std::string expected_fingerprint;

    std::unique_ptr<NiceMock<Network::MockTransportSocketFactory>> socket_factory;
    std::unique_ptr<NiceMock<Ssl::MockClientContextConfig>> client_context_config;
    std::unique_ptr<NiceMock<Ssl::MockTlsCertificateConfig>> tls_cert_config;
    std::unique_ptr<NiceMock<Upstream::MockTransportSocketMatcher>> transport_socket_matcher;

    if (is_bound) {
      dummy_pem = "dummy cert PEM";
      expected_fingerprint = "mock_fingerprint_base64";

      socket_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
      client_context_config = std::make_unique<NiceMock<Ssl::MockClientContextConfig>>();
      tls_cert_config = std::make_unique<NiceMock<Ssl::MockTlsCertificateConfig>>();
      ON_CALL(*tls_cert_config, certificateChain()).WillByDefault(testing::ReturnRef(dummy_pem));
      std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>> tls_certs;
      tls_certs.push_back(*tls_cert_config);
      ON_CALL(*client_context_config, tlsCertificates()).WillByDefault(testing::Return(tls_certs));
      ON_CALL(*socket_factory, clientContextConfig())
          .WillByDefault(
              testing::Return(OptRef<const Ssl::ClientContextConfig>(*client_context_config)));
      transport_socket_matcher = std::make_unique<NiceMock<Upstream::MockTransportSocketMatcher>>(
          std::move(socket_factory));

      EXPECT_CALL(*fingerprinter_, getFingerprintFromPem(dummy_pem))
          .WillRepeatedly(testing::Return(expected_fingerprint));

      cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
      EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));
      EXPECT_CALL(*cluster_info_, transportSocketMatcher())
          .WillRepeatedly(testing::ReturnRef(*transport_socket_matcher));
    } else {
      cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
      EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));
    }

    setupFilterAndCallback();
    auto mock_client = std::make_unique<MockGcpAuthnClient>();
    MockGcpAuthnClient* mock_client_ptr = mock_client.get();
    setClient(std::move(mock_client));

    metadata_.clear_typed_filter_metadata();
    std::ignore = (*metadata_.mutable_typed_filter_metadata())
                      [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
                          .PackFrom(audience);
    ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

    configure_client_expectations(*mock_client_ptr);

    EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
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
  EXPECT_EQ(filter_config_->stats().retrieve_audience_failed_.value(), 1);
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
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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

TEST_F(GcpAuthnFilterTest, ResumeFilterChainIterationWithBoundAccessToken) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_access_token();
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
                        .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  const std::string dummy_pem = "dummy cert PEM";
  const std::string expected_fingerprint = "mock+fingerprint/base64=";

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

  EXPECT_EQ(message_->headers().Method()->value().getStringView(), "GET");
  EXPECT_EQ(message_->headers().Path()->value().getStringView(),
            "/computeMetadata/v1/instance/service-accounts/default/"
            "token?bindCertificateFingerprint=mock%252Bfingerprint%252Fbase64%253D");

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(
      R"({"access_token": "mock_access_token", "expires_in": 3600, "token_type": "Bearer"})");

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  client_callback_->onSuccess(client_request_, std::move(response));

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
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
                        .PackFrom(invalid_proto);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  // The filter should fail to unpack, return nullopt, fail open, and return Continue.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_config_->stats().retrieve_audience_failed_.value(), 1);
}

TEST_F(GcpAuthnFilterTest, ClusterNotFound) {
  setupMockObjects();
  setupFilterAndCallback();

  // getThreadLocalCluster returns nullptr when the cluster is completely missing.
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(nullptr));

  // decodeHeaders should return Continue directly, increment stats, and fail open.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_config_->stats().retrieve_audience_failed_.value(), 1);
}

TEST_F(GcpAuthnFilterTest, CacheHit) {
  setupMockObjects();

  // Set up metadata using Audience.
  setupMockFilterMetadata(/*valid=*/true);

  // Instantiate real TokenCacheImpl.
  envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig cache_config;
  cache_config.mutable_cache_size()->set_value(100);
  config_.mutable_cache_config()->CopyFrom(cache_config);
  refreshConfig();
  setupFilterAndCallback();

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
  filter_config_->tokenCache()->insert(std::move(token));

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
  config_.mutable_cache_config()->CopyFrom(cache_config);
  refreshConfig();
  setupFilterAndCallback();

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
  auto cached_val = filter_config_->tokenCache()->lookUp(audience, std::nullopt);
  EXPECT_TRUE(cached_val.has_value());
  EXPECT_EQ(cached_val.value(), std::string(GoodTokenStr));
}

TEST_F(GcpAuthnFilterTest, BoundJwtCacheMissAndInsert) {
  setupMockObjects();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("test");
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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
  config_.mutable_cache_config()->CopyFrom(cache_config);
  refreshConfig();
  setupFilterAndCallback();

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
  auto cached_val = filter_config_->tokenCache()->lookUp(audience, expected_fingerprint);
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
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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
  EXPECT_EQ(getClientCertFingerprint(nullptr), std::nullopt);
}

TEST_F(GcpAuthnFilterTest, BoundJwtWithEmptyTlsCertificatesFails) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("http://bound_audience");
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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
  config_.mutable_cache_config()->CopyFrom(cache_config);
  refreshConfig();
  setupFilterAndCallback();

  uint64_t far_future_exp =
      DateUtil::nowToSeconds(context_.serverFactoryContext().timeSource()) + 1000;
  auto token = std::make_unique<GcpToken>("cached_bound_token", far_future_exp, audience,
                                          expected_fingerprint);
  filter_config_->tokenCache()->insert(std::move(token));

  EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(default_headers_.get_("Authorization"), "Bearer cached_bound_token");
}

TEST_F(GcpAuthnFilterTest, BoundAccessTokenWithoutFingerprintFails) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_access_token();
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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

TEST_F(GcpAuthnFilterTest, BoundAccessTokenCacheHit) {
  setupMockObjects();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_access_token();
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
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
  config_.mutable_cache_config()->CopyFrom(cache_config);
  refreshConfig();
  setupFilterAndCallback();

  uint64_t far_future_exp =
      DateUtil::nowToSeconds(context_.serverFactoryContext().timeSource()) + 1000;
  auto token = std::make_unique<GcpToken>("cached_bound_access_token", far_future_exp, audience,
                                          expected_fingerprint);
  filter_config_->tokenCache()->insert(std::move(token));

  EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(default_headers_.get_("Authorization"), "Bearer cached_bound_access_token");
}

TEST_F(GcpAuthnFilterTest, EmptyAudienceProto) {
  setupMockObjects();
  setupFilterAndCallback();

  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));

  envoy::extensions::filters::http::gcp_authn::v3::Audience empty_audience;
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
                        .PackFrom(empty_audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->state(), GcpAuthnFilter::State::Complete);
  EXPECT_EQ(filter_config_->stats().empty_audience_.value(), 1);
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

TEST_F(GcpAuthnFilterTest, AudiencePrecedenceBoundAccessToken) {
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://unbound_jwt");
  audience.mutable_access_token();
  audience.mutable_bound_jwt()->set_url("http://bound_jwt");
  audience.mutable_bound_access_token();

  testAudiencePrecedence(
      audience,
      [](MockGcpAuthnClient& client) {
        EXPECT_CALL(client, fetchBoundAccessToken(_, "mock_fingerprint_base64", _));
        EXPECT_CALL(client, fetchBoundJwt(_, _, _)).Times(0);
        EXPECT_CALL(client, fetchUnboundAccessToken(_, _)).Times(0);
        EXPECT_CALL(client, fetchUnboundJwt(_, _)).Times(0);
      },
      /*is_bound=*/true);
}

TEST_F(GcpAuthnFilterTest, AudiencePrecedenceBoundJwt) {
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://unbound_jwt");
  audience.mutable_access_token();
  audience.mutable_bound_jwt()->set_url("http://bound_jwt");

  testAudiencePrecedence(
      audience,
      [](MockGcpAuthnClient& client) {
        EXPECT_CALL(client, fetchBoundJwt(_, "mock_fingerprint_base64", _));
        EXPECT_CALL(client, fetchBoundAccessToken(_, _, _)).Times(0);
        EXPECT_CALL(client, fetchUnboundAccessToken(_, _)).Times(0);
        EXPECT_CALL(client, fetchUnboundJwt(_, _)).Times(0);
      },
      /*is_bound=*/true);
}

TEST_F(GcpAuthnFilterTest, AudiencePrecedenceUnboundAccessToken) {
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://unbound_jwt");
  audience.mutable_access_token();

  testAudiencePrecedence(
      audience,
      [](MockGcpAuthnClient& client) {
        EXPECT_CALL(client, fetchUnboundAccessToken(_, _));
        EXPECT_CALL(client, fetchBoundAccessToken(_, _, _)).Times(0);
        EXPECT_CALL(client, fetchBoundJwt(_, _, _)).Times(0);
        EXPECT_CALL(client, fetchUnboundJwt(_, _)).Times(0);
      },
      /*is_bound=*/false);
}

TEST_F(GcpAuthnFilterTest, SaveTokenToDynamicMetadata) {
  config_.set_token_metadata_key("custom_token_key");
  refreshConfig();

  setupMockObjects();
  setupFilterAndCallback();
  EXPECT_CALL(decoder_callbacks_, filterConfigName())
      .WillRepeatedly(Return("envoy.filters.http.gcp_authn"));
  setupMockFilterMetadata(/*valid=*/true);

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  Protobuf::Struct expected_metadata;
  (*expected_metadata.mutable_fields())["custom_token_key"].set_string_value(
      std::string(GoodTokenStr));
  EXPECT_CALL(decoder_callbacks_.stream_info_,
              setDynamicMetadata("envoy.filters.http.gcp_authn", ProtoEq(expected_metadata)));

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(std::string(GoodTokenStr));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  client_callback_->onSuccess(client_request_, std::move(response));

  EXPECT_EQ(default_headers_.get_("Authorization"), "");
}

TEST_F(GcpAuthnFilterTest, SaveTokenToDynamicMetadataCacheHit) {
  config_.set_token_metadata_key("custom_token_key");
  envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig cache_config;
  cache_config.mutable_cache_size()->set_value(100);
  config_.mutable_cache_config()->CopyFrom(cache_config);
  refreshConfig();

  setupMockObjects();
  setupMockFilterMetadata(/*valid=*/true);
  setupFilterAndCallback();
  EXPECT_CALL(decoder_callbacks_, filterConfigName())
      .WillRepeatedly(Return("envoy.filters.http.gcp_authn"));

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("test");

  uint64_t far_future_exp =
      DateUtil::nowToSeconds(context_.serverFactoryContext().timeSource()) + 1000;
  auto token = std::make_unique<GcpToken>();
  token->token = "cached_token";
  token->expires_at = far_future_exp;
  token->audience = audience;
  filter_config_->tokenCache()->insert(std::move(token));

  EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

  Protobuf::Struct expected_metadata;
  (*expected_metadata.mutable_fields())["custom_token_key"].set_string_value("cached_token");
  EXPECT_CALL(decoder_callbacks_.stream_info_,
              setDynamicMetadata("envoy.filters.http.gcp_authn", ProtoEq(expected_metadata)));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(default_headers_.get_("Authorization"), "");
}

TEST_F(GcpAuthnFilterTest, TokenMetadataKeyPrecedenceOverTokenHeader) {
  config_.set_token_metadata_key("custom_token_key");
  auto* token_header = config_.mutable_token_header();
  token_header->set_name("custom-header");
  token_header->set_value_prefix("Prefix ");
  refreshConfig();

  setupMockObjects();
  setupFilterAndCallback();
  EXPECT_CALL(decoder_callbacks_, filterConfigName())
      .WillRepeatedly(Return("envoy.filters.http.gcp_authn"));
  setupMockFilterMetadata(/*valid=*/true);

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  Protobuf::Struct expected_metadata;
  (*expected_metadata.mutable_fields())["custom_token_key"].set_string_value(
      std::string(GoodTokenStr));
  EXPECT_CALL(decoder_callbacks_.stream_info_,
              setDynamicMetadata("envoy.filters.http.gcp_authn", ProtoEq(expected_metadata)));

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(std::string(GoodTokenStr));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  client_callback_->onSuccess(client_request_, std::move(response));

  EXPECT_EQ(default_headers_.get_("custom-header"), "");
  EXPECT_EQ(default_headers_.get_("Authorization"), "");
}

TEST_F(GcpAuthnFilterTest, ConfigAudienceOverridesClusterMetadata) {
  config_.mutable_audience()->set_url("http://config_audience");
  refreshConfig();

  setupMockObjects();
  setupFilterAndCallback();
  auto mock_client = std::make_unique<MockGcpAuthnClient>();
  MockGcpAuthnClient* mock_client_ptr = mock_client.get();
  setClient(std::move(mock_client));

  // Set up cluster metadata with a different audience.
  setupMockFilterMetadata(/*valid=*/true, "http://cluster_audience");

  EXPECT_CALL(*mock_client_ptr,
              fetchUnboundJwt(
                  testing::Property(&envoy::extensions::filters::http::gcp_authn::v3::Audience::url,
                                    "http://config_audience"),
                  _));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
}

TEST_F(GcpAuthnFilterTest, ConfigAudienceWithoutClusterMetadata) {
  config_.mutable_audience()->set_url("http://config_audience");
  refreshConfig();

  setupMockObjects();
  setupFilterAndCallback();
  auto mock_client = std::make_unique<MockGcpAuthnClient>();
  MockGcpAuthnClient* mock_client_ptr = mock_client.get();
  setClient(std::move(mock_client));

  // Set up cluster metadata without audience filter metadata.
  setupMockFilterMetadata(/*valid=*/false);

  EXPECT_CALL(*mock_client_ptr,
              fetchUnboundJwt(
                  testing::Property(&envoy::extensions::filters::http::gcp_authn::v3::Audience::url,
                                    "http://config_audience"),
                  _));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
}

TEST_F(GcpAuthnFilterTest, ConfigAudienceAccessToken) {
  config_.mutable_audience()->mutable_access_token();
  refreshConfig();

  setupMockObjects();
  setupFilterAndCallback();
  auto mock_client = std::make_unique<MockGcpAuthnClient>();
  MockGcpAuthnClient* mock_client_ptr = mock_client.get();
  setClient(std::move(mock_client));

  // Set up cluster metadata with a URL audience.
  setupMockFilterMetadata(/*valid=*/true, "http://cluster_audience");

  EXPECT_CALL(*mock_client_ptr, fetchUnboundAccessToken(_, _));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
}

TEST_F(GcpAuthnFilterTest, ResumeFilterChainIterationWithIamAccessToken) {
  auto* iam_access_token = config_.mutable_audience()->mutable_iam_access_token();
  iam_access_token->set_account("%DYNAMIC_METADATA(custom_filter:target_sa)%");
  iam_access_token->set_authorization("Bearer %DYNAMIC_METADATA(custom_filter:gce_token)%");
  refreshConfig();

  setupMockObjects();
  setupFilterAndCallback();

  // Set up dynamic metadata for target SA and GCE token.
  Protobuf::Struct custom_metadata;
  (*custom_metadata.mutable_fields())["target_sa"].set_string_value(
      "target-sa@proj.iam.gserviceaccount.com");
  (*custom_metadata.mutable_fields())["gce_token"].set_string_value("mock_gce_token_123");
  decoder_callbacks_.stream_info_.metadata_.mutable_filter_metadata()->insert(
      {"custom_filter", custom_metadata});

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  EXPECT_EQ(message_->headers().Method()->value().getStringView(), "POST");
  EXPECT_EQ(message_->headers().Host()->value().getStringView(), "iamcredentials.googleapis.com");
  EXPECT_EQ(
      message_->headers().Path()->value().getStringView(),
      "/v1/projects/-/serviceAccounts/target-sa@proj.iam.gserviceaccount.com:generateAccessToken");
  EXPECT_EQ(message_->headers()
                .get(Envoy::Http::CustomHeaders::get().Authorization)[0]
                ->value()
                .getStringView(),
            "Bearer mock_gce_token_123");
  EXPECT_EQ(message_->headers().ContentType()->value().getStringView(),
            "application/json; charset=utf-8");
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      message_->bodyAsString(),
      "{\"lifetime\":\"3600s\",\"scope\":[\"https://www.googleapis.com/auth/cloud-platform\"]}"));

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(
      R"({"accessToken": "mock_iam_access_token", "expireTime": "2033-05-29T17:36:41Z"})");

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  client_callback_->onSuccess(client_request_, std::move(response));

  EXPECT_EQ(default_headers_.get_("Authorization"), "Bearer mock_iam_access_token");
}

TEST_F(GcpAuthnFilterTest, IamAccessTokenInvalidConfigFormatters) {
  auto* iam_access_token = config_.mutable_audience()->mutable_iam_access_token();
  iam_access_token->set_account("%INVALID_FORMATTER(");
  iam_access_token->set_authorization("Bearer token");

  absl::Status create_status;
  FilterConfig filter_config(config_, context_.server_factory_context_, "stats", context_.scope_,
                             create_status);
  EXPECT_FALSE(create_status.ok());
  EXPECT_THAT(create_status,
              StatusHelpers::HasStatusMessage("Incorrect configuration: %INVALID_FORMATTER(. "
                                              "Couldn't find valid command at position 0"));
}

TEST_F(GcpAuthnFilterTest, IamAccessTokenResolutionFailed) {
  auto* iam_access_token = config_.mutable_audience()->mutable_iam_access_token();
  iam_access_token->set_account("%DYNAMIC_METADATA(custom_filter:target_sa)%");
  iam_access_token->set_authorization("Bearer %DYNAMIC_METADATA(custom_filter:gce_token)%");
  refreshConfig();

  setupMockObjects();
  setupFilterAndCallback();

  // Dynamic metadata is not populated.
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Failed to resolve IAM access token account or authorization.", _, _,
                             "iam_token_resolution_failed"));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
}

TEST_F(GcpAuthnFilterTest, IamAccessTokenInClusterMetadataRejected) {
  setupMockObjects();
  setupFilterAndCallback();

  // Cluster metadata has IAMAccessToken, but filter config has no audience.
  cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  auto* iam_token = audience.mutable_iam_access_token();
  iam_token->set_account("sa@proj.iam.gserviceaccount.com");
  iam_token->set_authorization("Bearer token");
  std::ignore = (*metadata_.mutable_typed_filter_metadata())
                    [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
                        .PackFrom(audience);
  ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "IAM access token requires audience configured in filter config.", _,
                             _, "iam_token_config_error"));

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
}

TEST_F(GcpAuthnFilterTest, IamAccessTokenCacheMissAndHit) {
  auto* iam_access_token = config_.mutable_audience()->mutable_iam_access_token();
  iam_access_token->set_account("%DYNAMIC_METADATA(custom_filter:target_sa)%");
  iam_access_token->set_authorization("Bearer %DYNAMIC_METADATA(custom_filter:gce_token)%");
  envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig cache_config;
  cache_config.mutable_cache_size()->set_value(100);
  config_.mutable_cache_config()->CopyFrom(cache_config);
  refreshConfig();

  setupMockObjects();
  setupFilterAndCallback();

  Protobuf::Struct custom_metadata;
  (*custom_metadata.mutable_fields())["target_sa"].set_string_value(
      "target-sa@proj.iam.gserviceaccount.com");
  (*custom_metadata.mutable_fields())["gce_token"].set_string_value("mock_gce_token_123");
  decoder_callbacks_.stream_info_.metadata_.mutable_filter_metadata()->insert(
      {"custom_filter", custom_metadata});

  // First request: cache miss.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(
      R"({"accessToken": "mock_iam_access_token", "expireTime": "2033-05-29T17:36:41Z"})");

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  client_callback_->onSuccess(client_request_, std::move(response));
  EXPECT_EQ(default_headers_.get_("Authorization"), "Bearer mock_iam_access_token");

  // Second request: cache hit.
  Http::TestRequestHeaderMapImpl second_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

  // Rotate the authentication token, this should not impact caching.
  (*custom_metadata.mutable_fields())["gce_token"].set_string_value("mock_gce_token_456");
  decoder_callbacks_.stream_info_.metadata_.mutable_filter_metadata()->insert(
      {"custom_filter", custom_metadata});
  setupFilterAndCallback();
  EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

  EXPECT_EQ(filter_->decodeHeaders(second_headers, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(second_headers.get_("Authorization"), "Bearer mock_iam_access_token");
}

TEST_F(GcpAuthnFilterTest, AudiencePrecedenceIamAccessToken) {
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://unbound_jwt");
  audience.mutable_access_token();
  audience.mutable_bound_jwt()->set_url("http://bound_jwt");
  audience.mutable_bound_access_token();
  auto* iam = audience.mutable_iam_access_token();
  iam->set_account("target-sa@proj.iam.gserviceaccount.com");
  iam->set_authorization("Bearer token");

  config_.mutable_audience()->CopyFrom(audience);
  refreshConfig();

  setupMockObjects();
  setupFilterAndCallback();
  auto mock_client = std::make_unique<MockGcpAuthnClient>();
  MockGcpAuthnClient* mock_client_ptr = mock_client.get();
  setClient(std::move(mock_client));

  EXPECT_CALL(*mock_client_ptr, fetchIamAccessToken(_, _, _));
  EXPECT_CALL(*mock_client_ptr, fetchBoundAccessToken(_, _, _)).Times(0);
  EXPECT_CALL(*mock_client_ptr, fetchBoundJwt(_, _, _)).Times(0);
  EXPECT_CALL(*mock_client_ptr, fetchUnboundAccessToken(_, _)).Times(0);
  EXPECT_CALL(*mock_client_ptr, fetchUnboundJwt(_, _)).Times(0);

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
