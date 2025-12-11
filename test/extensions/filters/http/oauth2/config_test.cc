#include "envoy/extensions/filters/http/oauth2/v3/oauth.pb.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/secret/secret_provider_impl.h"
#include "source/extensions/filters/http/oauth2/config.h"

#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

using testing::NiceMock;
using testing::Return;

namespace {

// This loads one of the secrets in credentials, and fails the other one.
void expectInvalidSecretConfig(const std::string& failed_secret_name,
                               const std::string& status_message) {
  const std::string yaml = R"EOF(
config:
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  retry_policy:
    retry_back_off:
      base_interval: 1s
      max_interval: 10s
    num_retries: 5
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  auth_scopes:
  - user
  - openid
  - email
  resources:
  - oauth2-resource
  - http://example.com
  - https://example.com
  auth_type: "BASIC_AUTH"
    )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;

  NiceMock<Secret::MockSecretManager> secret_manager;
  ON_CALL(context.server_factory_context_, secretManager())
      .WillByDefault(ReturnRef(secret_manager));
  ON_CALL(secret_manager,
          findStaticGenericSecretProvider(failed_secret_name == "token" ? "hmac" : "token"))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  const auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(), status_message);
}

} // namespace

TEST(ConfigTest, CreateFilter) {
  const std::string yaml = R"EOF(
config:
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  retry_policy:
    retry_back_off:
      base_interval: 1s
      max_interval: 10s
    num_retries: 5
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
    cookie_names:
      bearer_token: BearerToken
      oauth_hmac: OauthHMAC
      oauth_expires: OauthExpires
      id_token: IdToken
      refresh_token: RefreshToken
    cookie_domain: example.com
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  auth_scopes:
  - user
  - openid
  - email
  resources:
  - oauth2-resource
  - http://example.com
  - https://example.com
    )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  // This returns non-nullptr for token_secret and hmac_secret.
  NiceMock<Secret::MockSecretManager> secret_manager;
  ON_CALL(context.server_factory_context_, secretManager())
      .WillByDefault(ReturnRef(secret_manager));
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  EXPECT_CALL(context, messageValidationVisitor());
  EXPECT_CALL(context.server_factory_context_, clusterManager()).Times(2);
  EXPECT_CALL(context, scope());
  EXPECT_CALL(context.server_factory_context_, timeSource());
  EXPECT_CALL(context, initManager()).Times(2);
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(ConfigTest, InvalidTokenSecret) {
  expectInvalidSecretConfig("token", "invalid token secret configuration");
}

TEST(ConfigTest, InvalidHmacSecret) {
  expectInvalidSecretConfig("hmac", "invalid HMAC secret configuration");
}

TEST(ConfigTest, CreateFilterMissingConfig) {
  OAuth2Config config;

  envoy::extensions::filters::http::oauth2::v3::OAuth2 proto_config;

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  const auto result =
      config.createFilterFactoryFromProtoTyped(proto_config, "whatever", factory_context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(), "config must be present for global config");
}

TEST(ConfigTest, WrongCookieName) {
  const std::string yaml = R"EOF(
config:
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  retry_policy:
    retry_back_off:
      base_interval: 1s
      max_interval: 10s
    num_retries: 5
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
    cookie_names:
      bearer_token: "?"
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  auth_scopes:
  - user
  - openid
  - email
  resources:
  - oauth2-resource
  - http://example.com
  - https://example.com
    )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "value does not match regex pattern");
}

TEST(ConfigTest, WrongCombinationOfPreserveAuthorizationAndForwardBearer) {
  const std::string yaml = R"EOF(
config:
  forward_bearer_token: true
  preserve_authorization_header: true
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  retry_policy:
    retry_back_off:
      base_interval: 1s
      max_interval: 10s
    num_retries: 5
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
    cookie_names:
      bearer_token: BearerToken
      oauth_hmac: OauthHMAC
      oauth_expires: OauthExpires
      id_token: IdToken
      refresh_token: RefreshToken
    cookie_domain: example.com
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  auth_scopes:
  - user
  - openid
  - email
  resources:
  - oauth2-resource
  - http://example.com
  - https://example.com
    )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  // This returns non-nullptr for token_secret and hmac_secret.
  NiceMock<Secret::MockSecretManager> secret_manager;
  ON_CALL(context.server_factory_context_, secretManager())
      .WillByDefault(ReturnRef(secret_manager));
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  const auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(),
            "invalid combination of forward_bearer_token and preserve_authorization_header "
            "configuration. If forward_bearer_token is set to true, then "
            "preserve_authorization_header must be false");
}

TEST(ConfigTest, ValidSameSiteConfigs) {
  const std::string yaml = R"EOF(
config:
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  cookie_configs:
    bearer_token_cookie_config:
      same_site: LAX
    oauth_hmac_cookie_config:
      same_site: STRICT
    oauth_expires_cookie_config:
      same_site: NONE
    id_token_cookie_config:
      same_site: STRICT
    refresh_token_cookie_config:
      same_site: LAX
    oauth_nonce_cookie_config:
      same_site: NONE
  )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  NiceMock<Secret::MockSecretManager> secret_manager;
  ON_CALL(context.server_factory_context_, secretManager())
      .WillByDefault(ReturnRef(secret_manager));
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  const auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_TRUE(result.ok());
}

TEST(ConfigTest, MissingSameSiteConfigs) {
  const std::string yaml = R"EOF(
config:
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  cookie_configs:
    bearer_token_cookie_config:
      same_site: LAX
    id_token_cookie_config:
      same_site: STRICT
    refresh_token_cookie_config:
      same_site: LAX
  )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  NiceMock<Secret::MockSecretManager> secret_manager;
  ON_CALL(context.server_factory_context_, secretManager())
      .WillByDefault(ReturnRef(secret_manager));
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  const auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_TRUE(result.ok());
}

TEST(ConfigTest, NoCookieConfigs) {
  const std::string yaml = R"EOF(
config:
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  NiceMock<Secret::MockSecretManager> secret_manager;
  ON_CALL(context.server_factory_context_, secretManager())
      .WillByDefault(ReturnRef(secret_manager));
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  const auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_TRUE(result.ok());
}

TEST(ConfigTest, EndSessionEndpointWithOpenId) {
  const std::string yaml = R"EOF(
    config:
      token_endpoint:
        cluster: foo
        uri: oauth.com/token
        timeout: 3s
      credentials:
        client_id: "secret"
        token_secret:
          name: token
        hmac_secret:
          name: hmac
      authorization_endpoint: https://oauth.com/oauth/authorize/
      end_session_endpoint: https://oauth.com/oauth/logout
      auth_scopes: openid
      redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
      redirect_path_matcher:
        path:
          exact: /callback
      signout_path:
        path:
          exact: /signout
      )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  NiceMock<Secret::MockSecretManager> secret_manager;
  ON_CALL(context.server_factory_context_, secretManager())
      .WillByDefault(ReturnRef(secret_manager));
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  const auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_TRUE(result.ok());
}

TEST(ConfigTest, EndSessionEndpointWithoutOpenId) {
  const std::string yaml = R"EOF(
    config:
      token_endpoint:
        cluster: foo
        uri: oauth.com/token
        timeout: 3s
      credentials:
        client_id: "secret"
        token_secret:
          name: token
        hmac_secret:
          name: hmac
      authorization_endpoint: https://oauth.com/oauth/authorize/
      end_session_endpoint: https://oauth.com/oauth/logout
      redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
      redirect_path_matcher:
        path:
          exact: /callback
      signout_path:
        path:
          exact: /signout
      )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  NiceMock<Secret::MockSecretManager> secret_manager;
  ON_CALL(context.server_factory_context_, secretManager())
      .WillByDefault(ReturnRef(secret_manager));
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).value(), EnvoyException,
      "OAuth2 filter: end session endpoint is only supported for OpenID Connect.");
}

TEST(ConfigTest, ValidCookieDomainAndPath) {
  const std::string yaml = R"EOF(
config:
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
    cookie_domain: example.com
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  cookie_configs:
    bearer_token_cookie_config:
      path: /api/v1
    oauth_hmac_cookie_config:
      path: /oauth
  )EOF";

  OAuth2Config factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  NiceMock<Secret::MockSecretManager> secret_manager;
  ON_CALL(context.server_factory_context_, secretManager())
      .WillByDefault(ReturnRef(secret_manager));
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  const auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_TRUE(result.ok());
}

TEST(ConfigTest, InvalidCookieDomain) {
  // Test domains with space, semicolon, and comma.
  const std::vector<std::string> invalid_domains = {"example .com", "example;.com", "example,com"};

  for (const auto& domain : invalid_domains) {
    const std::string yaml = fmt::format(R"EOF(
config:
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
    cookie_domain: "{}"
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  )EOF",
                                         domain);

    OAuth2Config factory;
    ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
    TestUtility::loadFromYaml(yaml, *proto_config);
    NiceMock<Server::Configuration::MockFactoryContext> context;

    EXPECT_THROW_WITH_REGEX(factory.createFilterFactoryFromProto(*proto_config, "stats", context)
                                .status()
                                .IgnoreError(),
                            EnvoyException, "value does not match regex pattern");
  }
}

TEST(ConfigTest, InvalidCookiePath) {
  // Test paths that don't start with slash, or contain space, semicolon, or comma.
  const std::vector<std::string> invalid_paths = {"api/v1", "/api /v1", "/api;v1", "/api,v1"};

  for (const auto& path : invalid_paths) {
    const std::string yaml = fmt::format(R"EOF(
config:
  token_endpoint:
    cluster: foo
    uri: oauth.com/token
    timeout: 3s
  credentials:
    client_id: "secret"
    token_secret:
      name: token
    hmac_secret:
      name: hmac
  authorization_endpoint: https://oauth.com/oauth/authorize/
  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
  redirect_path_matcher:
    path:
      exact: /callback
  signout_path:
    path:
      exact: /signout
  cookie_configs:
    bearer_token_cookie_config:
      path: "{}"
  )EOF",
                                         path);

    OAuth2Config factory;
    ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
    TestUtility::loadFromYaml(yaml, *proto_config);
    NiceMock<Server::Configuration::MockFactoryContext> context;

    EXPECT_THROW_WITH_REGEX(factory.createFilterFactoryFromProto(*proto_config, "stats", context)
                                .status()
                                .IgnoreError(),
                            EnvoyException, "value does not match regex pattern");
  }
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
