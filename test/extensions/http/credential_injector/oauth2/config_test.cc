#include "source/extensions/http/injected_credentials/oauth2/config.h"

#include "test/mocks/server/server_factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

using testing::NiceMock;

TEST(Config, OAuth2FlowTypeUnset) {
  envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2 proto_config;
  proto_config.mutable_token_fetch_retry_interval()->set_seconds(1);
  OAuth2CredentialInjectorFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Init::MockManager> init_manager;

  EXPECT_THROW_WITH_REGEX(
      factory.createCredentialInjectorFromProtoTyped(proto_config, "stats", context, init_manager),
      EnvoyException, "OAuth2 flow type not set");
}

TEST(Config, NullClientSecret) {
  const std::string yaml_string = R"EOF(
      token_fetch_retry_interval: 1s
      token_endpoint:
        cluster: non-existing-cluster
        timeout: 0.5s
        uri: "oauth.com/token"
      client_credentials:
        client_id: "client-id"
        client_secret: {}
  )EOF";

  envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2 proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  OAuth2CredentialInjectorFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      transport_socket_factory_context;
  NiceMock<Init::MockManager> init_manager;
  ON_CALL(server_factory_context, getTransportSocketFactoryContext())
      .WillByDefault(ReturnRef(transport_socket_factory_context));

  EXPECT_THROW_WITH_REGEX(factory.createOauth2ClientCredentialInjector(
                              proto_config, "stats", server_factory_context, init_manager),
                          EnvoyException, "Invalid oauth2 client secret configuration");
}

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
