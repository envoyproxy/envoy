#include <memory>
#include <string>

#include "envoy/extensions/filters/http/oauth2/v3alpha/oauth.pb.h"

#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"
#include "common/secret/secret_provider_impl.h"

#include "extensions/filters/http/oauth2/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
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
                               const std::string& exception_message) {
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
  redirect_uri: "%REQ(:x-forwarded-proto)%://%REQ(:authority)%/callback"
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

  auto& secret_manager = context.cluster_manager_.cluster_manager_factory_.secretManager();
  ON_CALL(secret_manager,
          findStaticGenericSecretProvider(failed_secret_name == "token" ? "hmac" : "token"))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(*proto_config, "stats", context),
                            EnvoyException, exception_message);
}

} // namespace

TEST(ConfigTest, CreateFilter) {
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
  redirect_uri: "%REQ(:x-forwarded-proto)%://%REQ(:authority)%/callback"
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
  Server::Configuration::MockFactoryContext context;

  // This returns non-nullptr for token_secret and hmac_secret.
  auto& secret_manager = context.cluster_manager_.cluster_manager_factory_.secretManager();
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  EXPECT_CALL(context, messageValidationVisitor());
  EXPECT_CALL(context, clusterManager());
  EXPECT_CALL(context, scope());
  EXPECT_CALL(context, timeSource());
  EXPECT_CALL(context, api());
  EXPECT_CALL(context, getTransportSocketFactoryContext());
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
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

  envoy::extensions::filters::http::oauth2::v3alpha::OAuth2 proto_config;

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW_WITH_MESSAGE(
      config.createFilterFactoryFromProtoTyped(proto_config, "whatever", factory_context),
      EnvoyException, "config must be present for global config");
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
