#include <memory>
#include <string>

#include "envoy/extensions/filters/http/oauth2/v3/oauth.pb.h"
#include "envoy/extensions/filters/http/oauth2/v3/oauth.pb.validate.h"
#include "envoy/http/async_client.h"
#include "envoy/http/message.h"

#include "source/common/common/macros.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/common/secret/secret_manager_impl.h"
#include "source/extensions/filters/http/oauth2/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/test_runtime.h"
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
  MOCK_METHOD(std::string&, refreshToken, (), (const));

  MOCK_METHOD(bool, canUpdateTokenByRefreshToken, (), (const));
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
              (const std::string&, const std::string&, const std::string&, const std::string&,
               Envoy::Extensions::HttpFilters::Oauth2::AuthType));

  MOCK_METHOD(void, asyncRefreshAccessToken,
              (const std::string&, const std::string&, const std::string&,
               Envoy::Extensions::HttpFilters::Oauth2::AuthType));
};

class OAuth2Test : public testing::TestWithParam<int> {
public:
  OAuth2Test() : request_(&cm_.thread_local_cluster_.async_client_) {
    factory_context_.cluster_manager_.initializeClusters({"auth.example.com"}, {});
    init();
  }

  void init() { init(getConfig()); }

  void init(FilterConfigSharedPtr config) {
    // Set up the OAuth client.
    oauth_client_ = new MockOAuth2Client();
    std::unique_ptr<OAuth2Client> oauth_client_ptr{oauth_client_};

    config_ = config;
    filter_ = std::make_shared<OAuth2Filter>(config_, std::move(oauth_client_ptr), test_time_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    validator_ = std::make_shared<MockOAuth2CookieValidator>();
    filter_->validator_ = validator_;
  }

  // Set up proto fields with standard config.
  FilterConfigSharedPtr
  getConfig(bool forward_bearer_token = true, bool use_refresh_token = false,
            ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType auth_type =
                ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                    OAuth2Config_AuthType_URL_ENCODED_BODY) {
    envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
    auto* endpoint = p.mutable_token_endpoint();
    endpoint->set_cluster("auth.example.com");
    endpoint->set_uri("auth.example.com/_oauth");
    endpoint->mutable_timeout()->set_seconds(1);
    p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
    p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
    p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
    p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
    p.set_forward_bearer_token(forward_bearer_token);

    auto* useRefreshToken = p.mutable_use_refresh_token();
    useRefreshToken->set_value(use_refresh_token);
    p.set_auth_type(auth_type);
    p.add_auth_scopes("user");
    p.add_auth_scopes("openid");
    p.add_auth_scopes("email");
    p.add_resources("oauth2-resource");
    p.add_resources("http://example.com");
    p.add_resources("https://example.com/some/path%2F..%2F/utf8\xc3\x83;foo=bar?var1=1&var2=2");
    auto* matcher = p.add_pass_through_matcher();
    matcher->set_name(":method");
    matcher->mutable_string_match()->set_exact("OPTIONS");
    auto credentials = p.mutable_credentials();
    credentials->set_client_id(TEST_CLIENT_ID);
    credentials->mutable_token_secret()->set_name("secret");
    credentials->mutable_hmac_secret()->set_name("hmac");
    // Skipping setting credentials.cookie_names field should give default cookie names:
    // BearerToken, OauthHMAC, and OauthExpires.

    MessageUtil::validate(p, ProtobufMessage::getStrictValidationVisitor());

    // Create filter config.
    auto secret_reader = std::make_shared<MockSecretReader>();
    FilterConfigSharedPtr c = std::make_shared<FilterConfig>(p, factory_context_.cluster_manager_,
                                                             secret_reader, scope_, "test.");

    return c;
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

  // Validates the behavior of the cookie validator.
  void expectValidCookies(const CookieNames& cookie_names) {
    // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
    test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));

    const auto expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) + 10;

    Http::TestRequestHeaderMapImpl request_headers{
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Path.get(), "/anypath"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Cookie.get(),
         fmt::format("{}={};version=test", cookie_names.oauth_expires_, expires_at_s)},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.bearer_token_, "=xyztoken;version=test")},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.oauth_hmac_, "="
                                                "dCu0otMcLoaGF73jrT+R8rGA0pnWyMgNf4+GivGrHEI="
                                                ";version=test")},
    };

    auto cookie_validator = std::make_shared<OAuth2CookieValidator>(test_time_, cookie_names);
    EXPECT_EQ(cookie_validator->token(), "");
    EXPECT_EQ(cookie_validator->refreshToken(), "");
    cookie_validator->setParams(request_headers, "mock-secret");

    EXPECT_TRUE(cookie_validator->hmacIsValid());
    EXPECT_TRUE(cookie_validator->timestampIsValid());
    EXPECT_TRUE(cookie_validator->isValid());
    EXPECT_FALSE(cookie_validator->canUpdateTokenByRefreshToken());

    // If we advance time beyond 10s the timestamp should no longer be valid.
    test_time_.advanceTimeWait(std::chrono::seconds(11));

    EXPECT_FALSE(cookie_validator->timestampIsValid());
    EXPECT_FALSE(cookie_validator->isValid());
  }

  NiceMock<Event::MockTimer>* attachmentTimeout_timer_{};
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::shared_ptr<MockOAuth2CookieValidator> validator_;
  std::shared_ptr<OAuth2Filter> filter_;
  MockOAuth2Client* oauth_client_;
  FilterConfigSharedPtr config_;
  Http::MockAsyncClientRequest request_;
  std::deque<Http::AsyncClient::Callbacks*> callbacks_;
  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  Event::SimulatedTimeSystem test_time_;
};

// Verifies that the OAuth SDSSecretReader correctly updates dynamic generic secret.
TEST_F(OAuth2Test, SdsDynamicGenericSecret) {
  NiceMock<Server::MockConfigTracker> config_tracker;
  Secret::SecretManagerImpl secret_manager{config_tracker};
  envoy::config::core::v3::ConfigSource config_source;

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Init::MockManager> init_manager;
  Init::TargetHandlePtr init_handle;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(secret_context.server_context_, localInfo()).WillRepeatedly(ReturnRef(local_info));
  EXPECT_CALL(secret_context.server_context_, api()).WillRepeatedly(ReturnRef(*api));
  EXPECT_CALL(secret_context.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_handle](const Init::Target& target) {
        init_handle = target.createHandle("test");
      }));

  auto client_secret_provider = secret_manager.findOrCreateGenericSecretProvider(
      config_source, "client", secret_context, init_manager);
  auto client_callback = secret_context.cluster_manager_.subscription_factory_.callbacks_;
  auto token_secret_provider = secret_manager.findOrCreateGenericSecretProvider(
      config_source, "token", secret_context, init_manager);
  auto token_callback = secret_context.cluster_manager_.subscription_factory_.callbacks_;

  SDSSecretReader secret_reader(client_secret_provider, token_secret_provider, *api);
  EXPECT_TRUE(secret_reader.clientSecret().empty());
  EXPECT_TRUE(secret_reader.tokenSecret().empty());

  const std::string yaml_client = R"EOF(
name: client
generic_secret:
  secret:
    inline_string: "client_test"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(yaml_client, typed_secret);
  const auto decoded_resources_client = TestUtility::decodeResources({typed_secret});

  EXPECT_TRUE(client_callback->onConfigUpdate(decoded_resources_client.refvec_, "").ok());
  EXPECT_EQ(secret_reader.clientSecret(), "client_test");
  EXPECT_EQ(secret_reader.tokenSecret(), "");

  const std::string yaml_token = R"EOF(
name: token
generic_secret:
  secret:
    inline_string: "token_test"
)EOF";
  TestUtility::loadFromYaml(yaml_token, typed_secret);
  const auto decoded_resources_token = TestUtility::decodeResources({typed_secret});

  EXPECT_TRUE(token_callback->onConfigUpdate(decoded_resources_token.refvec_, "").ok());
  EXPECT_EQ(secret_reader.clientSecret(), "client_test");
  EXPECT_EQ(secret_reader.tokenSecret(), "token_test");

  const std::string yaml_client_recheck = R"EOF(
name: client
generic_secret:
  secret:
    inline_string: "client_test_recheck"
)EOF";
  TestUtility::loadFromYaml(yaml_client_recheck, typed_secret);
  const auto decoded_resources_client_recheck = TestUtility::decodeResources({typed_secret});

  EXPECT_TRUE(client_callback->onConfigUpdate(decoded_resources_client_recheck.refvec_, "").ok());
  EXPECT_EQ(secret_reader.clientSecret(), "client_test_recheck");
  EXPECT_EQ(secret_reader.tokenSecret(), "token_test");
}
// Verifies that we fail constructing the filter if the configured cluster doesn't exist.
TEST_F(OAuth2Test, InvalidCluster) {
  ON_CALL(factory_context_.cluster_manager_, clusters())
      .WillByDefault(Return(Upstream::ClusterManager::ClusterInfoMaps()));

  EXPECT_THROW_WITH_MESSAGE(init(), EnvoyException,
                            "OAuth2 filter: unknown cluster 'auth.example.com' in config. Please "
                            "specify which cluster to direct OAuth requests to.");
}

// Verifies that we fail constructing the filter if the authorization endpoint isn't a valid URL.
TEST_F(OAuth2Test, InvalidAuthorizationEndpoint) {
  // Create a filter config with an invalid authorization_endpoint URL.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  p.set_authorization_endpoint("INVALID_URL");
  auto secret_reader = std::make_shared<MockSecretReader>();

  EXPECT_THROW_WITH_MESSAGE(
      std::make_shared<FilterConfig>(p, factory_context_.cluster_manager_, secret_reader, scope_,
                                     "test."),
      EnvoyException, "OAuth2 filter: invalid authorization endpoint URL 'INVALID_URL' in config.");
}

// Verifies that the OAuth config is created with a default value for auth_scopes field when it is
// not set in proto/yaml.
TEST_F(OAuth2Test, DefaultAuthScope) {
  // Set up proto fields with no auth scope set.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  p.set_forward_bearer_token(true);
  auto* matcher = p.add_pass_through_matcher();
  matcher->set_name(":method");
  matcher->mutable_string_match()->set_exact("OPTIONS");

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

  // resource is optional
  EXPECT_EQ(test_config_->encodedResourceQueryParams(), "");

  // Recreate the filter with current config and test if the scope was added
  // as a query parameter in response headers.
  init(test_config_);
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/not/_oauth"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "http"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&redirect_uri=http%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_DEFAULT_SCOPE + "&state=http%3A%2F%2Ftraffic.example.com%2Fnot%2F_oauth"},
  };

  // explicitly tell the validator to fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

// Verifies that query parameters in the authorization_endpoint URL are preserved.
TEST_F(OAuth2Test, PreservesQueryParametersInAuthorizationEndpoint) {
  // Create a filter config with an authorization_endpoint URL with query parameters.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/?foo=bar");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");

  // Create the OAuth config.
  auto secret_reader = std::make_shared<MockSecretReader>();
  FilterConfigSharedPtr test_config_;
  test_config_ = std::make_shared<FilterConfig>(p, factory_context_.cluster_manager_, secret_reader,
                                                scope_, "test.");
  init(test_config_);
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/not/_oauth"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "http"},
  };

  // Explicitly tell the validator to fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  // Verify that the foo=bar query parameter is preserved in the redirect.
  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&foo=bar"
           "&redirect_uri=http%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_DEFAULT_SCOPE + "&state=http%3A%2F%2Ftraffic.example.com%2Fnot%2F_oauth"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(OAuth2Test, PreservesQueryParametersInAuthorizationEndpointWithUrlEncoding) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({
      {"envoy.reloadable_features.oauth_use_url_encoding", "true"},
  });
  // Create a filter config with an authorization_endpoint URL with query parameters.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/?foo=bar");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");

  // Create the OAuth config.
  auto secret_reader = std::make_shared<MockSecretReader>();
  FilterConfigSharedPtr test_config_;
  test_config_ = std::make_shared<FilterConfig>(p, factory_context_.cluster_manager_, secret_reader,
                                                scope_, "test.");
  init(test_config_);
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/not/_oauth"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "http"},
  };

  // Explicitly tell the validator to fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  // Verify that the foo=bar query parameter is preserved in the redirect.
  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&foo=bar"
           "&redirect_uri=http%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_DEFAULT_SCOPE + "&state=http%3A%2F%2Ftraffic.example.com%2Fnot%2F_oauth"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
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
      {Http::Headers::get().Scheme.get(), "https"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().Location.get(), "https://traffic.example.com/"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
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
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer injected_malice!"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer legit_token"},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _));
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
 * Scenario: The OAuth filter receives a request to an arbitrary path with valid OAuth cookies
 * (cookie values and validation are mocked out), but with an invalid token in the Authorization
 * header and forwarding bearer token is disabled.
 *
 * Expected behavior: the filter should sanitize the Authorization header and let the request
 * proceed.
 */
TEST_F(OAuth2Test, OAuthOkPassButInvalidToken) {
  init(getConfig(false /* forward_bearer_token */));

  Http::TestRequestHeaderMapImpl mock_request_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer injected_malice!"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _));
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
TEST_F(OAuth2Test, OAuthErrorNonOAuthHttpCallbackLegacyEncoding) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({
      {"envoy.reloadable_features.oauth_use_url_encoding", "false"},
  });
  init();
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/not/_oauth"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "http"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&redirect_uri=http%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES +
           "&state=http%3A%2F%2Ftraffic.example.com%2Fnot%2F_oauth"
           "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%2F..%2F%2Futf8%C3%83;foo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // explicitly tell the validator to fail the validation
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(OAuth2Test, OAuthErrorNonOAuthHttpCallback) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({
      {"envoy.reloadable_features.oauth_use_url_encoding", "true"},
  });
  init();
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/not/_oauth"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "http"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&redirect_uri=http%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES +
           "&state=http%3A%2F%2Ftraffic.example.com%2Fnot%2F_oauth"
           "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // explicitly tell the validator to fail the validation
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
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

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
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
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  AuthType::UrlEncodedBody));

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
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"}};

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Options},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(request_headers, expected_headers);
  EXPECT_EQ(scope_.counterFromString("test.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.oauth_passthrough").value(), 1);
  EXPECT_EQ(scope_.counterFromString("test.oauth_success").value(), 0);
}

// Validates the behavior of the cookie validator.
TEST_F(OAuth2Test, CookieValidator) {
  expectValidCookies(
      CookieNames{"BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken"});
}

// Validates the behavior of the cookie validator with custom cookie names.
TEST_F(OAuth2Test, CookieValidatorWithCustomNames) {
  expectValidCookies(CookieNames{"CustomBearerToken", "CustomOauthHMAC", "CustomOauthExpires",
                                 "CustomIdToken", "CustomRefreshToken"});
}

// Validates the behavior of the cookie validator when the combination of some fields could be same.
TEST_F(OAuth2Test, CookieValidatorSame) {
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "true"},
    });
    test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
    auto cookie_names =
        CookieNames{"BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken"};
    const auto expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) + 5;

    // Host name is `traffic.example.com:101` and the expire time is 5.
    Http::TestRequestHeaderMapImpl request_headers{
        {Http::Headers::get().Host.get(), "traffic.example.com:101"},
        {Http::Headers::get().Path.get(), "/anypath"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Cookie.get(),
         fmt::format("{}={};version=test", cookie_names.oauth_expires_, expires_at_s)},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.bearer_token_, "=xyztoken;version=test")},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.oauth_hmac_, "="
                                                "MSq8mkNQGdXx2LKGlLHMwSIj8rLZRnrHE6EWvvTUFx0="
                                                ";version=test")},
    };

    auto cookie_validator = std::make_shared<OAuth2CookieValidator>(test_time_, cookie_names);
    EXPECT_EQ(cookie_validator->token(), "");
    cookie_validator->setParams(request_headers, "mock-secret");

    EXPECT_TRUE(cookie_validator->hmacIsValid());
    EXPECT_TRUE(cookie_validator->timestampIsValid());
    EXPECT_TRUE(cookie_validator->isValid());

    // If we advance time beyond 5s the timestamp should no longer be valid.
    test_time_.advanceTimeWait(std::chrono::seconds(6));

    EXPECT_FALSE(cookie_validator->timestampIsValid());
    EXPECT_FALSE(cookie_validator->isValid());

    test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
    const auto new_expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) + 15;

    // Host name is `traffic.example.com:10` and the expire time is 15.
    // HMAC should be different from the above one with the separator fix.
    Http::TestRequestHeaderMapImpl request_headers_second{
        {Http::Headers::get().Host.get(), "traffic.example.com:10"},
        {Http::Headers::get().Path.get(), "/anypath"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Cookie.get(),
         fmt::format("{}={};version=test", cookie_names.oauth_expires_, new_expires_at_s)},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.bearer_token_, "=xyztoken;version=test")},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.oauth_hmac_, "="
                                                "dbl04CSr6eWF52wdNDCRt/Uw6A4y41wbpmtUWRyD2Fo="
                                                ";version=test")},
    };

    cookie_validator->setParams(request_headers_second, "mock-secret");

    EXPECT_TRUE(cookie_validator->hmacIsValid());
    EXPECT_TRUE(cookie_validator->timestampIsValid());
    EXPECT_TRUE(cookie_validator->isValid());

    // If we advance time beyond 15s the timestamp should no longer be valid.
    test_time_.advanceTimeWait(std::chrono::seconds(16));

    EXPECT_FALSE(cookie_validator->timestampIsValid());
    EXPECT_FALSE(cookie_validator->isValid());
  }
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "false"},
    });
    test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
    auto cookie_names =
        CookieNames{"BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken"};
    const auto expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) + 5;

    // Host name is `traffic.example.com:101` and the expire time is 5.
    Http::TestRequestHeaderMapImpl request_headers{
        {Http::Headers::get().Host.get(), "traffic.example.com:101"},
        {Http::Headers::get().Path.get(), "/anypath"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Cookie.get(),
         fmt::format("{}={};version=test", cookie_names.oauth_expires_, expires_at_s)},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.bearer_token_, "=xyztoken;version=test")},
        {Http::Headers::get().Cookie.get(),

         absl::StrCat(cookie_names.oauth_hmac_, "="
                                                "MzEyYWJjOWE0MzUwMTlkNWYxZDhiMjg2OTRiMWNjYzEyMjIzZj"
                                                "JiMmQ5NDY3YWM3MTNhMTE2YmVmNGQ0MTcxZA=="
                                                ";version=test")},
    };

    auto cookie_validator = std::make_shared<OAuth2CookieValidator>(test_time_, cookie_names);
    EXPECT_EQ(cookie_validator->token(), "");
    cookie_validator->setParams(request_headers, "mock-secret");

    EXPECT_TRUE(cookie_validator->hmacIsValid());
    EXPECT_TRUE(cookie_validator->timestampIsValid());
    EXPECT_TRUE(cookie_validator->isValid());

    // If we advance time beyond 5s the timestamp should no longer be valid.
    test_time_.advanceTimeWait(std::chrono::seconds(6));

    EXPECT_FALSE(cookie_validator->timestampIsValid());
    EXPECT_FALSE(cookie_validator->isValid());

    test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
    const auto new_expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) + 15;

    // Host name is `traffic.example.com:10` and the expire time is 15.
    // HMAC should be different from the above one with the separator fix.
    Http::TestRequestHeaderMapImpl request_headers_second{
        {Http::Headers::get().Host.get(), "traffic.example.com:10"},
        {Http::Headers::get().Path.get(), "/anypath"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Cookie.get(),
         fmt::format("{}={};version=test", cookie_names.oauth_expires_, new_expires_at_s)},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.bearer_token_, "=xyztoken;version=test")},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.oauth_hmac_, "="
                                                "NzViOTc0ZTAyNGFiZTllNTg1ZTc2YzFkMzQzMDkxYjdmNTMwZT"
                                                "gwZTMyZTM1YzFiYTY2YjU0NTkxYzgzZDg1YQ=="
                                                ";version=test")},
    };

    cookie_validator->setParams(request_headers_second, "mock-secret");

    EXPECT_TRUE(cookie_validator->hmacIsValid());
    EXPECT_TRUE(cookie_validator->timestampIsValid());
    EXPECT_TRUE(cookie_validator->isValid());

    // If we advance time beyond 15s the timestamp should no longer be valid.
    test_time_.advanceTimeWait(std::chrono::seconds(16));

    EXPECT_FALSE(cookie_validator->timestampIsValid());
    EXPECT_FALSE(cookie_validator->isValid());
  }
}

// Validates the behavior of the cookie validator when the expires_at value is not a valid integer.
TEST_F(OAuth2Test, CookieValidatorInvalidExpiresAt) {
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "true"},
    });
    Http::TestRequestHeaderMapImpl request_headers{
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Path.get(), "/anypath"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Cookie.get(), "OauthExpires=notanumber;version=test"},
        {Http::Headers::get().Cookie.get(), "BearerToken=xyztoken;version=test"},
        {Http::Headers::get().Cookie.get(), "OauthHMAC="
                                            "c+1qzyrMmqG8+O4dn7b28OvNNDWcb04yJfNbZCE1zYE="
                                            ";version=test"},
    };

    auto cookie_validator = std::make_shared<OAuth2CookieValidator>(
        test_time_,
        CookieNames{"BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken"});
    cookie_validator->setParams(request_headers, "mock-secret");

    EXPECT_TRUE(cookie_validator->hmacIsValid());
    EXPECT_FALSE(cookie_validator->timestampIsValid());
    EXPECT_FALSE(cookie_validator->isValid());
  }
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "false"},
    });
    Http::TestRequestHeaderMapImpl request_headers{
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Path.get(), "/anypath"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Cookie.get(), "OauthExpires=notanumber;version=test"},
        {Http::Headers::get().Cookie.get(), "BearerToken=xyztoken;version=test"},
        {Http::Headers::get().Cookie.get(),
         "OauthHMAC="
         "NzNlZDZhY2YyYWNjOWFhMWJjZjhlZTFkOWZiNmY2ZjBlYmNkMzQzNTljNmY0ZTMyMjVmMzViNjQyMTM1Y2Q4MQ=="
         ";version=test"},
    };

    auto cookie_validator = std::make_shared<OAuth2CookieValidator>(
        test_time_,
        CookieNames{"BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken"});
    cookie_validator->setParams(request_headers, "mock-secret");

    EXPECT_TRUE(cookie_validator->hmacIsValid());
    EXPECT_FALSE(cookie_validator->timestampIsValid());
    EXPECT_FALSE(cookie_validator->isValid());
  }
}

// Validates the behavior of the cookie validator when the expires_at value is not a valid integer.
TEST_F(OAuth2Test, CookieValidatorCanUpdateToken) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=notanumber;version=test"},
      {Http::Headers::get().Cookie.get(),
       "BearerToken=xyztoken;version=test;RefreshToken=dsdtoken;"},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(
      test_time_,
      CookieNames("BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken"));
  cookie_validator->setParams(request_headers, "mock-secret");

  EXPECT_TRUE(cookie_validator->canUpdateTokenByRefreshToken());
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
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
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
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
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
TEST_F(OAuth2Test, OAuthTestUpdatePathAfterSuccessLegacyEncoding) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({
      {"envoy.reloadable_features.oauth_use_url_encoding", "false"},
  });
  init();
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
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

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

TEST_F(OAuth2Test, OAuthTestUpdatePathAfterSuccess) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({
      {"envoy.reloadable_features.oauth_use_url_encoding", "true"},
  });
  init();
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&state=https://traffic.example.com/original_path?var1=1%26var2=2"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123;version=test"},
      {Http::Headers::get().Cookie.get(), "BearerToken=legit_token;version=test"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ====;version=test"},
  };

  Http::TestRequestHeaderMapImpl expected_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/original_path?var1=1&var2=2"},
  };

  // Succeed the HMAC validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Http::TestRequestHeaderMapImpl final_request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&state=https://traffic.example.com/original_path?var1=1%26var2=2"},
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
TEST_F(OAuth2Test, OAuthTestFullFlowPostWithParametersLegacyEncoding) {
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.oauth_use_url_encoding", "false"},
        {"envoy.reloadable_features.hmac_base64_encoding_only", "true"},
    });
    init();
    // First construct the initial request to the oauth filter with URI parameters.
    Http::TestRequestHeaderMapImpl first_request_headers{
        {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
        {Http::Headers::get().Scheme.get(), "https"},
    };

    // This is the immediate response - a redirect to the auth cluster.
    Http::TestResponseHeaderMapImpl first_response_headers{
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().Location.get(),
         "https://auth.example.com/oauth/"
         "authorize/?client_id=" +
             TEST_CLIENT_ID +
             "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
             "&response_type=code"
             "&scope=" +
             TEST_ENCODED_AUTH_SCOPES +
             "&state=https%3A%2F%2Ftraffic.example.com%2Ftest%3Fname%3Dadmin%26level%3Dtrace"
             "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
             "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%2F..%2F%2Futf8%C3%83;foo%3Dbar%"
             "3Fvar1%3D1%26var2%3D2"},
    };

    // Fail the validation to trigger the OAuth flow.
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

    // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

    // This represents the beginning of the OAuth filter.
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(first_request_headers, false));

    // This represents the callback request from the authorization server.
    Http::TestRequestHeaderMapImpl second_request_headers{
        {Http::Headers::get().Path.get(),
         "/_oauth?code=123&state=https%3A%2F%2Ftraffic.example.com%"
         "2Ftest%3Fname%3Dadmin%26level%3Dtrace"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Scheme.get(), "https"},
    };

    // Deliberately fail the HMAC validation check.
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

    EXPECT_CALL(*oauth_client_,
                asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                    "https://traffic.example.com" + TEST_CALLBACK,
                                    AuthType::UrlEncodedBody));

    // Invoke the callback logic. As a side effect, state_ will be populated.
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
              filter_->decodeHeaders(second_request_headers, false));

    EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
    EXPECT_EQ(config_->clusterName(), "auth.example.com");

    // Expected response after the callback & validation is complete - verifying we kept the
    // state and method of the original request, including the query string parameters.
    Http::TestRequestHeaderMapImpl second_response_headers{
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                               "fV62OgLipChTQQC3UFgDp+l5sCiSb3zt7nCoJiVivWw=;"
                                               "version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "OauthExpires=;version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "BearerToken=;version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().Location.get(),
         "https://traffic.example.com/test?name=admin&level=trace"},
    };

    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

    filter_->finishGetAccessTokenFlow();
  }
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.oauth_use_url_encoding", "false"},
        {"envoy.reloadable_features.hmac_base64_encoding_only", "false"},
    });
    init();
    // First construct the initial request to the oauth filter with URI parameters.
    Http::TestRequestHeaderMapImpl first_request_headers{
        {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
        {Http::Headers::get().Scheme.get(), "https"},
    };

    // This is the immediate response - a redirect to the auth cluster.
    Http::TestResponseHeaderMapImpl first_response_headers{
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().Location.get(),
         "https://auth.example.com/oauth/"
         "authorize/?client_id=" +
             TEST_CLIENT_ID +
             "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
             "&response_type=code"
             "&scope=" +
             TEST_ENCODED_AUTH_SCOPES +
             "&state=https%3A%2F%2Ftraffic.example.com%2Ftest%3Fname%3Dadmin%26level%3Dtrace"
             "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
             "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%2F..%2F%2Futf8%C3%83;foo%3Dbar%"
             "3Fvar1%3D1%26var2%3D2"},
    };

    // Fail the validation to trigger the OAuth flow.
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

    // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

    // This represents the beginning of the OAuth filter.
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(first_request_headers, false));

    // This represents the callback request from the authorization server.
    Http::TestRequestHeaderMapImpl second_request_headers{
        {Http::Headers::get().Path.get(),
         "/_oauth?code=123&state=https%3A%2F%2Ftraffic.example.com%"
         "2Ftest%3Fname%3Dadmin%26level%3Dtrace"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Scheme.get(), "https"},
    };

    // Deliberately fail the HMAC validation check.
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

    EXPECT_CALL(*oauth_client_,
                asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                    "https://traffic.example.com" + TEST_CALLBACK,
                                    AuthType::UrlEncodedBody));

    // Invoke the callback logic. As a side effect, state_ will be populated.
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
              filter_->decodeHeaders(second_request_headers, false));

    EXPECT_EQ(2, config_->stats().oauth_unauthorized_rq_.value());
    EXPECT_EQ(config_->clusterName(), "auth.example.com");

    // Expected response after the callback & validation is complete - verifying we kept the
    // state and method of the original request, including the query string parameters.
    Http::TestRequestHeaderMapImpl second_response_headers{
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().SetCookie.get(),
         "OauthHMAC="
         "N2Q1ZWI2M2EwMmUyYTQyODUzNDEwMGI3NTA1ODAzYTdlOTc5YjAyODkyNmY3Y2VkZWU3MGE4MjYyNTYyYmQ2Yw==;"
         "version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "OauthExpires=;version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "BearerToken=;version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().Location.get(),
         "https://traffic.example.com/test?name=admin&level=trace"},
    };

    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

    filter_->finishGetAccessTokenFlow();
  }
}

TEST_F(OAuth2Test, OAuthTestFullFlowPostWithParametersFillRefreshAndIdToken) {
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // This is the immediate response - a redirect to the auth cluster.
  Http::TestResponseHeaderMapImpl first_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES +
           "&state=https%3A%2F%2Ftraffic.example.com%2Ftest%3Fname%3Dadmin%26level%3Dtrace"
           "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

  // This represents the beginning of the OAuth filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(first_request_headers, false));

  // This represents the callback request from the authorization server.
  Http::TestRequestHeaderMapImpl second_request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=https%3A%2F%2Ftraffic.example.com%"
                                        "2Ftest%3Fname%3Dadmin%26level%3Dtrace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Deliberately fail the HMAC validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  AuthType::UrlEncodedBody));

  // Invoke the callback logic. As a side effect, state_ will be populated.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(second_request_headers, false));

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(config_->clusterName(), "auth.example.com");

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  const std::chrono::seconds expiredTime(10);
  filter_->updateTokens("accessToken", "idToken", "refreshToken", expiredTime);

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl second_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "OYnODPsSGabEpZ2LAiPxyjAFgN/7/5Xg24G7jUoUbyI=;"
                                             "version=1;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=10;version=1;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=accessToken;version=1;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=idToken;version=1;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=refreshToken;version=1;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/test?name=admin&level=trace"},
  };

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

  filter_->finishGetAccessTokenFlow();
}

// This test adds %-encoded UTF-8 characters to the URL and shows that
// the new decoding correctly handles that case.
TEST_F(OAuth2Test, OAuthTestFullFlowPostWithParameters) {
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.oauth_use_url_encoding", "true"},
        {"envoy.reloadable_features.hmac_base64_encoding_only", "true"},
    });
    init();
    // First construct the initial request to the oauth filter with URI parameters.
    Http::TestRequestHeaderMapImpl first_request_headers{
        {Http::Headers::get().Path.get(), "/test/utf8%C3%83?name=admin&level=trace"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
        {Http::Headers::get().Scheme.get(), "https"},
    };

    // This is the immediate response - a redirect to the auth cluster.
    Http::TestResponseHeaderMapImpl first_response_headers{
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().Location.get(),
         "https://auth.example.com/oauth/"
         "authorize/?client_id=" +
             TEST_CLIENT_ID +
             "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
             "&response_type=code"
             "&scope=" +
             TEST_ENCODED_AUTH_SCOPES +
             "&state=https%3A%2F%2Ftraffic.example.com%2Ftest%2Futf8%25C3%2583%3Fname%3Dadmin%"
             "26level%3Dtrace"
             "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
             "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%"
             "3Dbar%"
             "3Fvar1%3D1%26var2%3D2"},
    };

    // Fail the validation to trigger the OAuth flow.
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

    // Check that the redirect includes the escaped parameter characters using URL encoding.
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

    // This represents the beginning of the OAuth filter.
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(first_request_headers, false));

    // This represents the callback request from the authorization server.
    Http::TestRequestHeaderMapImpl second_request_headers{
        {Http::Headers::get().Path.get(),
         "/_oauth?code=123&state=https%3A%2F%2Ftraffic.example.com%"
         "2Ftest%2Futf8%25C3%2583%3Fname%3Dadmin%26level%3Dtrace"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Scheme.get(), "https"},
    };

    // Deliberately fail the HMAC validation check.
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

    EXPECT_CALL(*oauth_client_,
                asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                    "https://traffic.example.com" + TEST_CALLBACK,
                                    AuthType::UrlEncodedBody));

    // Invoke the callback logic. As a side effect, state_ will be populated.
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
              filter_->decodeHeaders(second_request_headers, false));

    EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
    EXPECT_EQ(config_->clusterName(), "auth.example.com");

    // Expected response after the callback & validation is complete - verifying we kept the
    // state and method of the original request, including the query string parameters and UTF8
    // sequences.
    Http::TestRequestHeaderMapImpl second_response_headers{
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                               "fV62OgLipChTQQC3UFgDp+l5sCiSb3zt7nCoJiVivWw=;"
                                               "version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "OauthExpires=;version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "BearerToken=;version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().Location.get(),
         "https://traffic.example.com/test/utf8%C3%83?name=admin&level=trace"},
    };

    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

    filter_->finishGetAccessTokenFlow();
  }
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.oauth_use_url_encoding", "true"},
        {"envoy.reloadable_features.hmac_base64_encoding_only", "false"},
    });
    init();
    // First construct the initial request to the oauth filter with URI parameters.
    Http::TestRequestHeaderMapImpl first_request_headers{
        {Http::Headers::get().Path.get(), "/test/utf8%C3%83?name=admin&level=trace"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
        {Http::Headers::get().Scheme.get(), "https"},
    };

    // This is the immediate response - a redirect to the auth cluster.
    Http::TestResponseHeaderMapImpl first_response_headers{
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().Location.get(),
         "https://auth.example.com/oauth/"
         "authorize/?client_id=" +
             TEST_CLIENT_ID +
             "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
             "&response_type=code"
             "&scope=" +
             TEST_ENCODED_AUTH_SCOPES +
             "&state=https%3A%2F%2Ftraffic.example.com%2Ftest%2Futf8%25C3%2583%3Fname%3Dadmin%"
             "26level%3Dtrace"
             "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
             "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%"
             "3Dbar%"
             "3Fvar1%3D1%26var2%3D2"},
    };

    // Fail the validation to trigger the OAuth flow.
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

    // Check that the redirect includes the escaped parameter characters using URL encoding.
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

    // This represents the beginning of the OAuth filter.
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(first_request_headers, false));

    // This represents the callback request from the authorization server.
    Http::TestRequestHeaderMapImpl second_request_headers{
        {Http::Headers::get().Path.get(),
         "/_oauth?code=123&state=https%3A%2F%2Ftraffic.example.com%"
         "2Ftest%2Futf8%25C3%2583%3Fname%3Dadmin%26level%3Dtrace"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Scheme.get(), "https"},
    };

    // Deliberately fail the HMAC validation check.
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

    EXPECT_CALL(*oauth_client_,
                asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                    "https://traffic.example.com" + TEST_CALLBACK,
                                    AuthType::UrlEncodedBody));

    // Invoke the callback logic. As a side effect, state_ will be populated.
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
              filter_->decodeHeaders(second_request_headers, false));

    EXPECT_EQ(2, config_->stats().oauth_unauthorized_rq_.value());
    EXPECT_EQ(config_->clusterName(), "auth.example.com");

    // Expected response after the callback & validation is complete - verifying we kept the
    // state and method of the original request, including the query string parameters and UTF8
    // sequences.
    Http::TestRequestHeaderMapImpl second_response_headers{
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().SetCookie.get(),
         "OauthHMAC="
         "N2Q1ZWI2M2EwMmUyYTQyODUzNDEwMGI3NTA1ODAzYTdlOTc5YjAyODkyNmY3Y2VkZWU3MGE4MjYyNTYyYmQ2Yw==;"
         "version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "OauthExpires=;version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "BearerToken=;version=1;path=/;Max-Age=;secure;HttpOnly"},
        {Http::Headers::get().Location.get(),
         "https://traffic.example.com/test/utf8%C3%83?name=admin&level=trace"},
    };

    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

    filter_->finishGetAccessTokenFlow();
  }
}

/**
 * Testing oauth response after tokens are set.
 *
 * Expected behavior: cookies are set.
 */

std::string oauthHMAC;

TEST_P(OAuth2Test, OAuthAccessTokenSucessWithTokens) {
  TestScopedRuntime scoped_runtime;
  if (GetParam() == 1) {
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "false"},
    });
    oauthHMAC =
        "ZTEzMmIyYzRmNTdmMTdiY2IyYmViZDE3ODA5ZDliOTE2MTRlNzNjYjc4MjBlMTVlOWY1OTM2ZjViZjM4MzAwNA==;";
  } else {
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "true"},
    });
    oauthHMAC = "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;";
  }
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=access_code;version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=some-id-token;version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=some-refresh-token;version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

INSTANTIATE_TEST_SUITE_P(EndcodingParams, OAuth2Test, testing::Values(1, 0));

TEST_P(OAuth2Test, OAuthAccessTokenSucessWithTokens_oauth_use_standard_max_age_value) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({
      {"envoy.reloadable_features.oauth_use_standard_max_age_value", "false"},
  });

  if (GetParam() == 1) {
    oauthHMAC =
        "ZmMzNzFkOWVkY2ZmNzc3M2NjYjk0ZTA0NDM4YTlkOWIxMTUxNmI3NDkyMGRkYjM1Mzg4YTBiMzc4NGRmOWU4Mw==;";
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "false"},
    });
  } else {
    oauthHMAC = "/Dcdntz/d3PMuU4EQ4qdmxFRa3SSDds1OIoLN4TfnoM=;";
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "true"},
    });
  }

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=600;version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=access_code;version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=some-id-token;version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=some-refresh-token;version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

TEST_P(OAuth2Test, OAuthAccessTokenSucessWithTokens_oauth_make_token_cookie_httponly) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({
      {"envoy.reloadable_features.oauth_make_token_cookie_httponly", "false"},
  });
  if (GetParam() == 1) {
    oauthHMAC =
        "ZTEzMmIyYzRmNTdmMTdiY2IyYmViZDE3ODA5ZDliOTE2MTRlNzNjYjc4MjBlMTVlOWY1OTM2ZjViZjM4MzAwNA==;";
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "false"},
    });
  } else {
    oauthHMAC = "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;";
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "true"},
    });
  }

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;version=1;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=access_code;version=1;path=/;Max-Age=600;secure"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=some-id-token;version=1;path=/;Max-Age=600;secure"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=some-refresh-token;version=1;path=/;Max-Age=600;secure"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

TEST_F(OAuth2Test, OAuthBearerTokenFlowFromHeader) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/test?role=bearer"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"},
  };

  // Fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(OAuth2Test, OAuthBearerTokenFlowFromQueryParameters) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/test?role=bearer&token=xyz-queryparam-token"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_P(OAuth2Test, CookieValidatorInTransition) {
  TestScopedRuntime scoped_runtime;
  if (GetParam() == 0) {
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "true"},
    });
  } else {
    scoped_runtime.mergeValues({
        {"envoy.reloadable_features.hmac_base64_encoding_only", "false"},
    });
  }

  Http::TestRequestHeaderMapImpl request_headers_base64only{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=1600;version=1"},
      {Http::Headers::get().Cookie.get(), "BearerToken=access_code;version=1"},
      {Http::Headers::get().Cookie.get(), "IdToken=some-id-token;version=1"},
      {Http::Headers::get().Cookie.get(), "RefreshToken=some-refresh-token;version=1"},
      {Http::Headers::get().Cookie.get(), "OauthHMAC="
                                          "Y9gCpVnhyaY+ecSxt/ZLZc/OMb8ZNivrVH1RByJxEbs="
                                          ";version=test"},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(
      test_time_,
      CookieNames{"BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken"});
  cookie_validator->setParams(request_headers_base64only, "mock-secret");
  std::cout << "before first valid() " << GetParam() << std::endl;
  EXPECT_TRUE(cookie_validator->hmacIsValid());
  std::cout << "after first valid() " << GetParam() << std::endl;

  Http::TestRequestHeaderMapImpl request_headers_hexbase64{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=1600;version=1"},
      {Http::Headers::get().Cookie.get(), "BearerToken=access_code;version=1"},
      {Http::Headers::get().Cookie.get(), "IdToken=some-id-token;version=1"},
      {Http::Headers::get().Cookie.get(), "RefreshToken=some-refresh-token;version=1"},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "NjNkODAyYTU1OWUxYzlhNjNlNzljNGIxYjdmNjRiNjVjZmNlMzFiZjE5MzYyYmViNTQ3ZDUxMDcyMjcxMTFiYg=="
       ";version=test"},
  };
  cookie_validator->setParams(request_headers_hexbase64, "mock-secret");

  EXPECT_TRUE(cookie_validator->hmacIsValid());
}

// - The filter receives the initial request
// - The filter redirects a user to the authorization endpoint
// - The filter receives the callback request from the authorization endpoint
// - The filter gets a bearer and refresh tokens from the authorization endpoint
// - The filter redirects a user to the user agent with actual authorization data
// - The filter receives an other request when a bearer token is expired
// - The filter tries to update a bearer token via the refresh token instead of redirect user to the
// authorization endpoint
// - The filter gets a new bearer and refresh tokens via the current refresh token
// - The filter continues to handler the request without redirection to the user agent
TEST_F(OAuth2Test, OAuthTestFullFlowWithUseRefreshToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // This is the immediate response - a redirect to the auth cluster.
  Http::TestResponseHeaderMapImpl first_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES +
           "&state=https%3A%2F%2Ftraffic.example.com%2Ftest%3Fname%3Dadmin%26level%3Dtrace"
           "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // Fail the validation to trigger the OAuth flow.

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));

  // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

  // This represents the beginning of the OAuth filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(first_request_headers, false));

  // This represents the callback request from the authorization server.
  Http::TestRequestHeaderMapImpl second_request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=https%3A%2F%2Ftraffic.example.com%"
                                        "2Ftest%3Fname%3Dadmin%26level%3Dtrace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Deliberately fail the HMAC validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  AuthType::UrlEncodedBody));

  // Invoke the callback logic. As a side effect, state_ will be populated.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(second_request_headers, false));

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(config_->clusterName(), "auth.example.com");

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl second_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "fV62OgLipChTQQC3UFgDp+l5sCiSb3zt7nCoJiVivWw=;"
                                             "version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=;version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=;version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/test?name=admin&level=trace"},
  };

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

  filter_->finishGetAccessTokenFlow();

  // the third request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl third_request_headers{
      {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(third_request_headers, false));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  filter_->finishRefreshAccessTokenFlow();
  EXPECT_EQ(1, config_->stats().oauth_refreshtoken_success_.value());
  EXPECT_EQ(2, config_->stats().oauth_success_.value());
}

TEST_F(OAuth2Test, OAuthTestRefreshAccessTokenSuccess) {

  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  // Fail the validation to trigger the OAuth flow with trying to get the access token using by
  // refresh token.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(first_request_headers, false));

  Http::TestResponseHeaderMapImpl redirect_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES +
           "&state=https%3A%2F%2Ftraffic.example.com%2Ftest%3Fname%3Dadmin%26level%3Dtrace"
           "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com"},
  };

  // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  filter_->onRefreshAccessTokenSuccess("", "", "", std::chrono::seconds(10));

  EXPECT_EQ(1, config_->stats().oauth_refreshtoken_success_.value());
  EXPECT_EQ(1, config_->stats().oauth_success_.value());
}

TEST_F(OAuth2Test, OAuthTestRefreshAccessTokenFail) {

  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  // Fail the validation to trigger the OAuth flow with trying to get the access token using by
  // refresh token.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(first_request_headers, false));

  Http::TestResponseHeaderMapImpl redirect_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES +
           "&state=https%3A%2F%2Ftraffic.example.com%2Ftest%3Fname%3Dadmin%26level%3Dtrace"
           "&resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&redirect_response_headers), true));

  filter_->onRefreshAccessTokenFailure();

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(1, config_->stats().oauth_refreshtoken_failure_.value());
}

TEST_F(OAuth2Test, OAuthTestSetCookiesAfterRefreshAccessToken) {

  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));

  // the third request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  filter_->finishRefreshAccessTokenFlow();

  Http::TestResponseHeaderMapImpl response_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Http::TestResponseHeaderMapImpl expected_response_headers{
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "fV62OgLipChTQQC3UFgDp+l5sCiSb3zt7nCoJiVivWw=;"
                                             "version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=;version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=;version=1;path=/;Max-Age=;secure;HttpOnly"},
  };

  EXPECT_THAT(response_headers, HeaderMapEqualRef(&expected_response_headers));
}

TEST_F(OAuth2Test, OAuthTestSetCookiesAfterRefreshAccessTokenWithBasicAuth) {

  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_BASIC_AUTH
                 /* authType */));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/test?name=admin&level=trace"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::BasicAuth));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  filter_->finishRefreshAccessTokenFlow();

  Http::TestResponseHeaderMapImpl response_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Http::TestResponseHeaderMapImpl expected_response_headers{
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "fV62OgLipChTQQC3UFgDp+l5sCiSb3zt7nCoJiVivWw=;"
                                             "version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=;version=1;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=;version=1;path=/;Max-Age=;secure;HttpOnly"},
  };

  EXPECT_THAT(response_headers, HeaderMapEqualRef(&expected_response_headers));
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
