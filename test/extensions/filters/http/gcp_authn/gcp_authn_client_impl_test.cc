#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"

#include "source/extensions/filters/http/gcp_authn/gcp_authn_client_impl.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/gcp_authn/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

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
const uint64_t ExpTime = 2001001001;

class GcpAuthnClientImplTest : public testing::Test {
public:
  GcpAuthnClientImplTest() {
    // Initialize the default configuration.
    TestUtility::loadFromYaml(DefaultConfig, config_);
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

  void createClient() { client_ = std::make_unique<GcpAuthnClientImpl>(config_, context_); }

  NiceMock<MockFactoryContext> context_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  NiceMock<Envoy::Http::MockAsyncClientRequest> client_request_{
      &thread_local_cluster_.async_client_};
  NiceMock<MockGcpAuthnClientCallbacks> request_callbacks_;

  // Mocks for http request.
  Envoy::Http::AsyncClient::Callbacks* client_callback_;
  Envoy::Http::RequestMessagePtr message_;
  Envoy::Http::AsyncClient::RequestOptions options_;

  std::unique_ptr<GcpAuthnClientImpl> client_;
  GcpAuthnFilterConfig config_;
};

TEST_F(GcpAuthnClientImplTest, Success) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchUnboundJwt(audience, request_callbacks_);
  EXPECT_EQ(message_->headers().Method()->value().getStringView(), "GET");
  EXPECT_EQ(message_->headers().Path()->value().getStringView(),
            "/computeMetadata/v1/instance/service-accounts/default/identity?audience=http://"
            "test_audience");

  EXPECT_EQ(options_.retry_policy->num_retries().value(), 5);
  EXPECT_EQ(options_.retry_policy->retry_back_off().base_interval().seconds(), 1);
  EXPECT_EQ(options_.retry_policy->retry_back_off().max_interval().seconds(), 10);
  EXPECT_EQ(options_.retry_policy->retry_on(), "5xx,gateway-error,connect-failure,reset");

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(std::string(GoodTokenStr));

  GcpToken expected_token{std::string(GoodTokenStr), ExpTime, audience};
  EXPECT_CALL(request_callbacks_, onComplete(absl::StatusOr<GcpToken>(expected_token)));
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, SuccessAccessToken) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_access_token();
  client_->fetchUnboundAccessToken(audience, request_callbacks_);
  EXPECT_EQ(message_->headers().Method()->value().getStringView(), "GET");
  EXPECT_EQ(message_->headers().Path()->value().getStringView(),
            "/computeMetadata/v1/instance/service-accounts/default/token");

  EXPECT_EQ(options_.retry_policy->num_retries().value(), 5);
  EXPECT_EQ(options_.retry_policy->retry_back_off().base_interval().seconds(), 1);
  EXPECT_EQ(options_.retry_policy->retry_back_off().max_interval().seconds(), 10);
  EXPECT_EQ(options_.retry_policy->retry_on(), "5xx,gateway-error,connect-failure,reset");

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(
      R"({"access_token": "mock_access_token", "expires_in": 3600, "token_type": "Bearer"})");

  uint64_t current_time = DateUtil::nowToSeconds(context_.server_factory_context_.timeSource());
  uint64_t expected_exp_time = current_time + 3600;
  GcpToken expected_token{"mock_access_token", expected_exp_time, audience};
  EXPECT_CALL(request_callbacks_, onComplete(absl::StatusOr<GcpToken>(expected_token)));
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, AccessTokenParsingFailure) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_access_token();
  client_->fetchUnboundAccessToken(audience, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  // Set invalid JSON body
  response->body().add("invalid_json_body");

  // Assert that callbacks are notified with an error since JSON parsing failed.
  EXPECT_CALL(request_callbacks_, onComplete(testing::Matcher<absl::StatusOr<GcpToken>>(_)))
      .WillOnce(Invoke([](absl::StatusOr<GcpToken> token) {
        EXPECT_FALSE(token.ok());
        EXPECT_EQ(token.status().message(), "Failed to parse access token response as JSON.");
      }));

  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, AccessTokenMissing) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_access_token();
  client_->fetchUnboundAccessToken(audience, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  // Missing access_token field
  response->body().add(R"({"expires_in": 3600, "token_type": "Bearer"})");

  // Assert that callbacks are notified with an error.
  EXPECT_CALL(request_callbacks_, onComplete(testing::Matcher<absl::StatusOr<GcpToken>>(_)))
      .WillOnce(Invoke([](absl::StatusOr<GcpToken> token) {
        EXPECT_FALSE(token.ok());
        EXPECT_EQ(token.status().message(),
                  "Failed to extract access_token or expires_in from response.");
      }));

  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, AccessTokenExpiresInMissing) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_access_token();
  client_->fetchUnboundAccessToken(audience, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  // Missing expires_in field
  response->body().add(R"({"access_token": "mock_access_token", "token_type": "Bearer"})");

  // Assert that callbacks are notified with an error.
  EXPECT_CALL(request_callbacks_, onComplete(testing::Matcher<absl::StatusOr<GcpToken>>(_)))
      .WillOnce(Invoke([](absl::StatusOr<GcpToken> token) {
        EXPECT_FALSE(token.ok());
        EXPECT_EQ(token.status().message(),
                  "Failed to extract access_token or expires_in from response.");
      }));

  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, AccessTokenEmptyInResponse) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_access_token();
  client_->fetchUnboundAccessToken(audience, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  // Empty access_token value
  response->body().add(R"({"access_token": "", "expires_in": 3600, "token_type": "Bearer"})");

  // Assert that callbacks are notified with an error.
  EXPECT_CALL(request_callbacks_, onComplete(testing::Matcher<absl::StatusOr<GcpToken>>(_)))
      .WillOnce(Invoke([](absl::StatusOr<GcpToken> token) {
        EXPECT_FALSE(token.ok());
        EXPECT_EQ(token.status().message(), "Extracted access_token is empty.");
      }));

  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, AccessTokenExpiresInNonPositive) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_access_token();
  client_->fetchUnboundAccessToken(audience, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  // expires_in is 0
  response->body().add(
      R"({"access_token": "mock_access_token", "expires_in": 0, "token_type": "Bearer"})");

  // Assert that callbacks are notified with an error.
  EXPECT_CALL(request_callbacks_, onComplete(testing::Matcher<absl::StatusOr<GcpToken>>(_)))
      .WillOnce(Invoke([](absl::StatusOr<GcpToken> token) {
        EXPECT_FALSE(token.ok());
        EXPECT_EQ(token.status().message(), "Extracted expires_in is non-positive.");
      }));

  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, NoCluster) {
  std::string no_cluster_config = R"EOF(
    http_uri:
      uri: http://testhost/path/test
      timeout:
        seconds: 5
    retry_policy:
      retry_back_off:
        base_interval: 1s
        max_interval: 10s
      num_retries: 5
  )EOF";

  EXPECT_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              httpAsyncClient())
      .Times(0);

  EXPECT_CALL(request_callbacks_, onComplete(_));
  GcpAuthnFilterConfig config;
  TestUtility::loadFromYaml(no_cluster_config, config);
  config_ = config;
  createClient();
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchUnboundJwt(audience, request_callbacks_);
}

TEST_F(GcpAuthnClientImplTest, Failure) {
  setupMockObjects();
  createClient();
  EXPECT_CALL(request_callbacks_, onComplete(_));
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchUnboundJwt(audience, request_callbacks_);
  client_callback_->onFailure(client_request_, Http::AsyncClient::FailureReason::Reset);
}

TEST_F(GcpAuthnClientImplTest, NotOkResponse) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchUnboundJwt(audience, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "504"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  EXPECT_CALL(request_callbacks_, onComplete(_));
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, EmptyResponseHeader) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchUnboundJwt(audience, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr empty_resp_headers(
      new Envoy::Http::TestResponseHeaderMapImpl({}));
  Envoy::Http::ResponseMessagePtr empty_response(
      new Envoy::Http::ResponseMessageImpl(std::move(empty_resp_headers)));
  EXPECT_CALL(request_callbacks_, onComplete(_));
  client_callback_->onSuccess(client_request_, std::move(empty_response));
}

TEST_F(GcpAuthnClientImplTest, Cancel) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchUnboundJwt(audience, request_callbacks_);

  EXPECT_CALL(client_request_, cancel());
  client_->cancel();
}

TEST_F(GcpAuthnClientImplTest, NoRetryPolicy) {
  std::string no_retry_config = R"EOF(
    http_uri:
      uri: http://testhost/path/test
      cluster: test_cluster
      timeout:
        seconds: 5
  )EOF";

  GcpAuthnFilterConfig config;
  TestUtility::loadFromYaml(no_retry_config, config);
  config_ = config;

  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchUnboundJwt(audience, request_callbacks_);

  EXPECT_FALSE(options_.retry_policy.has_value());
}

TEST_F(GcpAuthnClientImplTest, TimeoutAtRootConfig) {
  std::string root_timeout_config = R"EOF(
    http_uri:
      uri: http://testhost/path/test
      cluster: test_cluster
      timeout:
        seconds: 5
    timeout:
      seconds: 15
  )EOF";

  GcpAuthnFilterConfig config;
  TestUtility::loadFromYaml(root_timeout_config, config);
  config_ = config;

  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchUnboundJwt(audience, request_callbacks_);

  // Verify that root-level timeout (15s) takes precedence over http_uri level timeout (5s).
  EXPECT_EQ(options_.timeout->count(), 15000);
}

TEST_F(GcpAuthnClientImplTest, JwtParsingFailure) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchUnboundJwt(audience, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  // Set invalid payload body
  response->body().add("invalid_jwt_token_payload");

  // Assert that callbacks are notified with an error since JWT parsing failed.
  EXPECT_CALL(request_callbacks_, onComplete(testing::Matcher<absl::StatusOr<GcpToken>>(_)))
      .WillOnce(Invoke([](absl::StatusOr<GcpToken> token) {
        EXPECT_FALSE(token.ok());
        EXPECT_EQ(token.status().message(), "Failed to parse identity token/JWT.");
      }));

  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, SuccessBoundJwt) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("http://test_audience");
  const std::string fingerprint = "abc+def/ghi=";
  client_->fetchBoundJwt(audience, fingerprint, request_callbacks_);
  EXPECT_EQ(message_->headers().Method()->value().getStringView(), "GET");
  EXPECT_EQ(message_->headers().Path()->value().getStringView(),
            "/computeMetadata/v1/instance/service-accounts/default/identity?audience=http://"
            "test_audience&bindCertificateFingerprint=abc%252Bdef%252Fghi%253D");

  EXPECT_EQ(options_.retry_policy->num_retries().value(), 5);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(std::string(GoodTokenStr));

  GcpToken expected_token{std::string(GoodTokenStr), ExpTime, audience, fingerprint};
  EXPECT_CALL(request_callbacks_, onComplete(absl::StatusOr<GcpToken>(expected_token)));
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnClientImplTest, SuccessBoundAccessToken) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_access_token();
  const std::string fingerprint = "abc+def/ghi=";
  client_->fetchBoundAccessToken(audience, fingerprint, request_callbacks_);
  EXPECT_EQ(message_->headers().Method()->value().getStringView(), "GET");
  EXPECT_EQ(message_->headers().Path()->value().getStringView(),
            "/computeMetadata/v1/instance/service-accounts/default/"
            "token?bindCertificateFingerprint=abc%252Bdef%252Fghi%253D");

  EXPECT_EQ(options_.retry_policy->num_retries().value(), 5);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add(
      R"({"access_token": "mock_access_token", "expires_in": 3600, "token_type": "Bearer"})");

  uint64_t current_time = DateUtil::nowToSeconds(context_.server_factory_context_.timeSource());
  uint64_t expected_exp_time = current_time + 3600;
  GcpToken expected_token{"mock_access_token", expected_exp_time, audience, fingerprint};
  EXPECT_CALL(request_callbacks_, onComplete(absl::StatusOr<GcpToken>(expected_token)));
  client_callback_->onSuccess(client_request_, std::move(response));
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
