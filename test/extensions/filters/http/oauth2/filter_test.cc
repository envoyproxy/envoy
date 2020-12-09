#include <memory>
#include <string>

#include "envoy/extensions/filters/http/oauth2/v3alpha/oauth.pb.h"
#include "envoy/extensions/filters/http/oauth2/v3alpha/oauth.pb.validate.h"
#include "envoy/http/async_client.h"
#include "envoy/http/message.h"

#include "common/common/macros.h"
#include "common/http/message_impl.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/oauth2/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

static const std::string TEST_CALLBACK = "/_oauth";
static const std::string TEST_CLIENT_ID = "1";
static const std::string TEST_CLIENT_SECRET_ID = "MyClientSecretKnoxID";
static const std::string TEST_TOKEN_SECRET_ID = "MyTokenSecretKnoxID";
static const std::string TEST_DEFAULT_SCOPE = "user";
static const std::string TEST_ENCODED_AUTH_SCOPES = "user%20openid%20email";

namespace {
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);
}

class MockSecretReader : public SecretReader {
public:
  const std::string& clientSecret() const override {
    CONSTRUCT_ON_FIRST_USE(std::string, "asdf_client_secret_fdsa");
  }
  const std::string& tokenSecret() const override {
    CONSTRUCT_ON_FIRST_USE(std::string, "asdf_token_secret_fdsa");
  }
};

class MockOAuth2CookieValidator : public CookieValidator {
public:
  MOCK_METHOD(std::string&, username, (), (const));
  MOCK_METHOD(std::string&, token, (), (const));
  MOCK_METHOD(bool, isValid, (), (const));
  MOCK_METHOD(void, setParams, (const Http::RequestHeaderMap& headers, const std::string& secret));
};

class MockOAuth2Client : public OAuth2Client {
public:
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {}
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {}
  void setCallbacks(FilterCallbacks&) override {}
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

  MOCK_METHOD(void, asyncGetAccessToken,
              (const std::string&, const std::string&, const std::string&, const std::string&));
};

class OAuth2Test : public testing::Test {
public:
  OAuth2Test() : request_(&cm_.async_client_) { init(); }

  void init() {
    // Set up the OAuth client
    oauth_client_ = new MockOAuth2Client();
    std::unique_ptr<OAuth2Client> oauth_client_ptr{oauth_client_};

    // Set up proto fields
    envoy::extensions::filters::http::oauth2::v3alpha::OAuth2Config p;
    auto* endpoint = p.mutable_token_endpoint();
    endpoint->set_cluster("auth.example.com");
    endpoint->set_uri("auth.example.com/_oauth");
    endpoint->mutable_timeout()->set_seconds(1);
    p.set_redirect_uri("%REQ(x-forwarded-proto)%://%REQ(:authority)%" + TEST_CALLBACK);
    p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
    p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
    p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
    p.set_forward_bearer_token(true);
    p.add_auth_scopes("user");
    p.add_auth_scopes("openid");
    p.add_auth_scopes("email");
    auto* matcher = p.add_pass_through_matcher();
    matcher->set_name(":method");
    matcher->set_exact_match("OPTIONS");

    auto credentials = p.mutable_credentials();
    credentials->set_client_id(TEST_CLIENT_ID);
    credentials->mutable_token_secret()->set_name("secret");
    credentials->mutable_hmac_secret()->set_name("hmac");

    MessageUtil::validate(p, ProtobufMessage::getStrictValidationVisitor());

    // Create the OAuth config.
    auto secret_reader = std::make_shared<MockSecretReader>();
    config_ = std::make_shared<FilterConfig>(p, factory_context_.cluster_manager_, secret_reader,
                                             scope_, "test.");

    filter_ = std::make_shared<OAuth2Filter>(config_, std::move(oauth_client_ptr), test_time_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    validator_ = std::make_shared<MockOAuth2CookieValidator>();
    filter_->validator_ = validator_;
  }

  Http::AsyncClient::Callbacks* popPendingCallback() {
    if (callbacks_.empty()) {
      // Can't use ASSERT_* as this is not a test function
      throw std::underflow_error("empty deque");
    }

    auto callbacks = callbacks_.front();
    callbacks_.pop_front();
    return callbacks;
  }

  NiceMock<Event::MockTimer>* attachmentTimeout_timer_{};
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::shared_ptr<MockOAuth2CookieValidator> validator_;
  std::shared_ptr<OAuth2Filter> filter_;
  MockOAuth2Client* oauth_client_;
  FilterConfigSharedPtr config_;
  Http::MockAsyncClientRequest request_;
  std::deque<Http::AsyncClient::Callbacks*> callbacks_;
  Stats::IsolatedStoreImpl scope_;
  Event::SimulatedTimeSystem test_time_;
};

// Verifies that we fail constructing the filter if the configured cluster doesn't exist.
TEST_F(OAuth2Test, InvalidCluster) {
  ON_CALL(factory_context_.cluster_manager_, get(_)).WillByDefault(Return(nullptr));

  EXPECT_THROW_WITH_MESSAGE(init(), EnvoyException,
                            "OAuth2 filter: unknown cluster 'auth.example.com' in config. Please "
                            "specify which cluster to direct OAuth requests to.");
}

// Verifies that the OAuth config is created with a default value for auth_scopes field when it is
// not set in proto/yaml.
TEST_F(OAuth2Test, DefaultAuthScope) {

  // Set up proto fields
  envoy::extensions::filters::http::oauth2::v3alpha::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(x-forwarded-proto)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  p.set_forward_bearer_token(true);
  auto* matcher = p.add_pass_through_matcher();
  matcher->set_name(":method");
  matcher->set_exact_match("OPTIONS");

  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");

  MessageUtil::validate(p, ProtobufMessage::getStrictValidationVisitor());

  // Create the OAuth config.
  auto secret_reader = std::make_shared<MockSecretReader>();
  FilterConfigSharedPtr test_config_;
  test_config_ = std::make_shared<FilterConfig>(p, factory_context_.cluster_manager_, secret_reader,
                                                scope_, "test.");

  // Auth_scopes was not set, should return default value.
  std::vector<std::string> default_scope = {TEST_DEFAULT_SCOPE};
  EXPECT_EQ(test_config_->authScopes(), default_scope);
}

/**
 * Scenario: The OAuth filter receives a sign out request.
 *
 * Expected behavior: the filter should redirect to the server name with cleared OAuth cookies.
 */
TEST_F(OAuth2Test, RequestSignout) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().Location.get(), "https://traffic.example.com/"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a request to an arbitrary path with valid OAuth cookies
 * (cookie values and validation are mocked out)
 * In a real flow, the injected OAuth headers should be sanitized and replaced with legitimate
 * values.
 *
 * Expected behavior: the filter should let the request proceed, and sanitize the injected headers.
 */
TEST_F(OAuth2Test, OAuthOkPass) {
  Http::TestRequestHeaderMapImpl mock_request_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer injected_malice!"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer legit_token"},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  // Sanitized return reference mocking
  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(mock_request_headers, false));

  // Ensure that existing OAuth forwarded headers got sanitized.
  EXPECT_EQ(mock_request_headers, expected_headers);

  EXPECT_EQ(scope_.counterFromString("test.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.oauth_success").value(), 1);
}

/**
 * Scenario: The OAuth filter receives a request without valid OAuth cookies to a non-callback URL
 * (indicating that the user needs to re-validate cookies or get 401'd).
 * This also tests both a forwarded http protocol from upstream and a plaintext connection.
 *
 * Expected behavior: the filter should redirect the user to the OAuth server with the credentials
 * in the query parameters.
 */
TEST_F(OAuth2Test, OAuthErrorNonOAuthHttpCallback) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/not/_oauth"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "http"},
      {Http::Headers::get().ForwardedProto.get(), "http"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&response_type=code&"
           "redirect_uri=http%3A%2F%2Ftraffic.example.com%2F"
           "_oauth&state=http%3A%2F%2Ftraffic.example.com%2Fnot%2F_oauth"},
  };

  // explicitly tell the validator to fail the validation
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a callback request with an error code
 */
TEST_F(OAuth2Test, OAuthErrorQueryString) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?error=someerrorcode"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "401"},
      {Http::Headers::get().ContentLength.get(), "18"}, // unauthorizedBodyMessage()
      {Http::Headers::get().ContentType.get(), "text/plain"},
  };

  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_EQ(scope_.counterFromString("test.oauth_failure").value(), 1);
  EXPECT_EQ(scope_.counterFromString("test.oauth_success").value(), 0);
}

/**
 * Scenario: The OAuth filter requests credentials from auth.example.com which returns a
 * response without expires_in (JSON response is mocked out)
 *
 * Expected behavior: the filter should return a 401 directly to the user.
 */
TEST_F(OAuth2Test, OAuthCallbackStartsAuthentication) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=https://asdf&method=GET"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: Protoc in opted-in to allow OPTIONS requests to pass-through. This is important as
 * POST requests initiate an OPTIONS request first in order to ensure POST is supported. During a
 * preflight request where the client Javascript initiates a remote call to a different endpoint,
 * we don't want to fail the call immediately due to browser restrictions, and use existing
 * cookies instead (OPTIONS requests do not send OAuth cookies.)
 */
TEST_F(OAuth2Test, OAuthOptionsRequestAndContinue) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Options},
  };

  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
}

// Validates the behavior of the cookie validator.
TEST_F(OAuth2Test, CookieValidator) {
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));

  const auto expires_at_s =
      std::chrono::duration_cast<std::chrono::seconds>(
          test_time_.timeSystem().systemTime().time_since_epoch() + std::chrono::seconds(10))
          .count();

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(),
       fmt::format("OauthExpires={};version=test", expires_at_s)},
      {Http::Headers::get().Cookie.get(), "BearerToken=xyztoken;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "NGQ3MzVjZGExNGM5NTFiZGJjODBkMjBmYjAyYjNiOTFjMmNjYjIxMTUzNmNiNWU0NjQzMmMxMWUzZmE2ZWJjYg=="
       ";version=test"},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(test_time_);
  cookie_validator->setParams(request_headers, "mock-secret");

  EXPECT_TRUE(cookie_validator->hmacIsValid());
  EXPECT_TRUE(cookie_validator->timestampIsValid());
  EXPECT_TRUE(cookie_validator->isValid());

  // If we advance time beyond 10s the timestamp should no longer be valid.
  test_time_.advanceTimeWait(std::chrono::seconds(11));

  EXPECT_FALSE(cookie_validator->timestampIsValid());
  EXPECT_FALSE(cookie_validator->isValid());
}

// Validates the behavior of the cookie validator when the expires_at value is not a valid integer.
TEST_F(OAuth2Test, CookieValidatorInvalidExpiresAt) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=notanumber;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=xyztoken;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "M2NjZmIxYWE0NzQzOGZlZTJjMjQwMzBiZTU5OTdkN2Y0NDRhZjE5MjZiOWNhY2YzNjM0MWRmMTNkMDVmZWFlOQ=="
       ";version=test"},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(test_time_);
  cookie_validator->setParams(request_headers, "mock-secret");

  EXPECT_TRUE(cookie_validator->hmacIsValid());
  EXPECT_FALSE(cookie_validator->timestampIsValid());
  EXPECT_FALSE(cookie_validator->isValid());
}

// Verify that we 401 the request if the state query param doesn't contain a valid URL.
TEST_F(OAuth2Test, OAuthTestInvalidUrlInStateQueryParam) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES + "&state=blah"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=legit_token;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ====;version=test"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "401"},
      {Http::Headers::get().ContentLength.get(), "18"},
      {Http::Headers::get().ContentType.get(), "text/plain"},
      // Invalid URL: we inject a few : in the middle of the URL.
  };

  // Succeed the HMAC validation.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));
}

// Verify that we 401 the request if the state query param contains the callback URL.
TEST_F(OAuth2Test, OAuthTestCallbackUrlInStateQueryParam) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&state=https%3A%2F%2Ftraffic.example.com%2F_oauth"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=legit_token;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ====;version=test"},
  };

  Http::TestRequestHeaderMapImpl expected_response_headers{
      {Http::Headers::get().Status.get(), "401"},
      {Http::Headers::get().ContentLength.get(), "18"},
      {Http::Headers::get().ContentType.get(), "text/plain"},
  };

  // Succeed the HMAC validation.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));

  Http::TestRequestHeaderMapImpl final_request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&state=https%3A%2F%2Ftraffic.example.com%2F_oauth"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=legit_token;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ====;version=test"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer legit_token"},
  };

  EXPECT_EQ(request_headers, final_request_headers);
}

/**
 * Testing the Path header replacement after an OAuth success.
 *
 * Expected behavior: the passed in HeaderMap should pass the OAuth flow, but since it's during
 * a callback from the authentication server, we should first parse out the state query string
 * parameter and set it to be the new path.
 */
TEST_F(OAuth2Test, OAuthTestUpdatePathAfterSuccess) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&state=https%3A%2F%2Ftraffic.example.com%2Foriginal_path"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=legit_token;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ====;version=test"},
  };

  Http::TestRequestHeaderMapImpl expected_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(), "https://traffic.example.com/original_path"},
  };

  // Succeed the HMAC validation.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Http::TestRequestHeaderMapImpl final_request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&state=https%3A%2F%2Ftraffic.example.com%2Foriginal_path"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=legit_token;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ====;version=test"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer legit_token"},
  };

  EXPECT_EQ(request_headers, final_request_headers);
}

/**
 * Testing oauth state with query string parameters.
 *
 * Expected behavior: HTTP Utility should not strip the parameters of the original request.
 */
TEST_F(OAuth2Test, OAuthTestFullFlowPostWithParameters) {
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().ForwardedProto.get(), "https"},
  };

  // This is the immediate response - a redirect to the auth cluster.
  Http::TestResponseHeaderMapImpl first_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&response_type=code&"
           "redirect_uri=https%3A%2F%2Ftraffic.example.com%2F"
           "_oauth&state=https%3A%2F%2Ftraffic.example.com%2Ftest%"
           "3Fname%3Dadmin%26level%3Dtrace"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

  // This represents the beginning of the OAuth filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(first_request_headers, false));

  // This represents the callback request from the authorization server.
  Http::TestRequestHeaderMapImpl second_request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=https%3A%2F%2Ftraffic.example.com%"
                                        "2Ftest%3Fname%3Dadmin%26level%3Dtrace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
  };

  // Deliberately fail the HMAC validation check.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK));

  // Invoke the callback logic. As a side effect, state_ will be populated.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(second_request_headers, false));

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl second_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC="
       "NWUzNzE5MWQwYTg0ZjA2NjIyMjVjMzk3MzY3MzMyZmE0NjZmMWI2MjI1NWFhNDhkYjQ4NDFlZmRiMTVmMTk0MQ==;"
       "version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=;version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(), "BearerToken=;version=1;path=/;Max-Age=;secure"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/test?name=admin&level=trace"},
  };

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  filter_->finishFlow();
}

TEST_F(OAuth2Test, OAuthBearerTokenFlowFromHeader) {
  Http::TestRequestHeaderMapImpl request_headers_before{
      {Http::Headers::get().Path.get(), "/test?role=bearer"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"},
  };
  // Expected decoded headers after the callback & validation of the bearer token is complete.
  Http::TestRequestHeaderMapImpl request_headers_after{
      {Http::Headers::get().Path.get(), "/test?role=bearer"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_before, false));

  // Finally, expect that the header map had OAuth information appended to it.
  EXPECT_EQ(request_headers_before, request_headers_after);
}

TEST_F(OAuth2Test, OAuthBearerTokenFlowFromQueryParameters) {
  Http::TestRequestHeaderMapImpl request_headers_before{
      {Http::Headers::get().Path.get(), "/test?role=bearer&token=xyz-queryparam-token"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
  };
  Http::TestRequestHeaderMapImpl request_headers_after{
      {Http::Headers::get().Path.get(), "/test?role=bearer&token=xyz-queryparam-token"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().ForwardedProto.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-queryparam-token"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _)).Times(1);
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_before, false));

  // Expected decoded headers after the callback & validation of the bearer token is complete.
  EXPECT_EQ(request_headers_before, request_headers_after);
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
